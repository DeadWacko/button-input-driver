#include "buttonlib.h"
#include <string.h>

#define MS_TO_US(x) ((uint64_t)(x) * 1000ULL)
#define LONG_PRESS_ACTIVE 0xFF

/* -------------------------------------------------------------------------- */
/*  Internal helpers                                                          */
/* -------------------------------------------------------------------------- */

static int find_index(btn_context_t *ctx, uint8_t id) {
    if (!ctx) return -1;
    for (size_t i = 0; i < ctx->btn_count; i++) {
        if (ctx->buttons[i].config &&
            ctx->buttons[i].config->id == id) {
            return (int)i;
        }
    }
    return -1;
}

static void push_event(btn_context_t *ctx, btn_event_t evt) {
    if (!ctx || !ctx->queue || ctx->queue_size == 0) return;

    size_t next = (ctx->head + 1) % ctx->queue_size;

    // Overwrite-oldest strategy:
    // If the queue is full, drop the oldest event and increment diagnostics.
    if (next == ctx->tail) {
        ctx->tail = (ctx->tail + 1) % ctx->queue_size;
        ctx->dropped_events++;
    }

    ctx->queue[ctx->head] = evt;
    ctx->head = next;
}

static void emit(btn_context_t *ctx,
                 const btn_config_t *cfg,
                 btn_state_t *st,
                 btn_event_type_t type,
                 uint8_t clicks,
                 uint64_t timestamp) {

    if (st->suppressed) {
        // Suppressed buttons do not emit any events.
        return;
    }

    btn_event_t evt = {
        .btn_id = cfg->id,
        .type   = type,
        .clicks = clicks,
        .timestamp = timestamp
    };

    if (cfg->callback && cfg->callback(&evt, cfg->cb_user_data)) {
        // Callback consumed the event.
        return;
    }

    push_event(ctx, evt);
}

/* -------------------------------------------------------------------------- */
/*  Public API                                                                */
/* -------------------------------------------------------------------------- */

bool btn_init(btn_context_t *ctx,
              btn_instance_t *buttons,
              size_t count,
              btn_event_t *queue,
              size_t q_size) {
    if (!ctx || !buttons) return false;

    memset(ctx, 0, sizeof(btn_context_t));
    ctx->buttons   = buttons;
    ctx->btn_count = count;
    ctx->queue     = queue;
    ctx->queue_size = q_size;

    return true;
}

void btn_setup(btn_context_t *ctx,
               uint8_t index,
               const btn_config_t *cfg,
               btn_state_t *st) {
    if (!ctx || index >= ctx->btn_count || !st) return;

    memset(st, 0, sizeof(btn_state_t));

    // Validate configuration: read_fn must be non-null.
    if (!cfg || !cfg->read_fn) {
        ctx->buttons[index].config = NULL;
        ctx->buttons[index].state  = NULL;
        return;
    }

    ctx->buttons[index].config = cfg;
    ctx->buttons[index].state  = st;
}

void btn_update(btn_context_t *ctx, uint64_t now_us) {
    if (!ctx) return;

    for (size_t i = 0; i < ctx->btn_count; i++) {
        const btn_config_t *cfg = ctx->buttons[i].config;
        btn_state_t        *st  = ctx->buttons[i].state;
        if (!cfg || !st) continue;
        if (!cfg->read_fn) continue; // Safety

        /* 1. Read hardware (raw state) */
        bool raw = cfg->read_fn(cfg->hw_arg);
        if (cfg->active_low) {
            raw = !raw;
        }

        /* 2. Debounce on raw changes */
        if (raw != st->raw_state) {
            st->last_debounce_time = now_us;
            st->raw_state = raw;
        }

        bool stable = st->logic_state;

        if ((now_us - st->last_debounce_time) > MS_TO_US(cfg->debounce_ms)) {
            if (st->logic_state != raw) {
                stable = raw;
                st->logic_state = raw;

                if (stable) {
                    /* -> PRESSED (logical) */
                    st->state_start_time  = now_us;
                    st->last_repeat_time  = now_us;
                    st->hold_repeat_count = 0;
                    st->suppressed        = false; // New press cancels suppression

                    emit(ctx, cfg, st, BTN_EVT_DOWN, 0, now_us);
                } else {
                    /* -> RELEASED (logical) */
                    emit(ctx, cfg, st, BTN_EVT_UP, 0, now_us);

                    if (!st->suppressed) {
                        uint64_t duration = now_us - st->state_start_time;

                        // Short presses contribute to a click series
                        if (duration < MS_TO_US(cfg->long_press_ms)) {
                            st->click_count++;
                            st->last_release_time = now_us;
                        } else {
                            // Long press clears click series
                            st->click_count = 0;
                        }
                    }
                }
            }
        }

        /* 3. Long press / auto-repeat / click timeout */
        if (stable) {
            /* == HELD == */
            uint64_t hold_time = now_us - st->state_start_time;

            if (hold_time > MS_TO_US(cfg->long_press_ms)) {
                if (st->click_count != LONG_PRESS_ACTIVE) {
                    st->click_count       = LONG_PRESS_ACTIVE; // Mark as handled
                    st->hold_repeat_count = 0;                 // Reset per-hold counter

                    emit(ctx, cfg, st, BTN_EVT_LONG_START, 0, now_us);
                    st->last_repeat_time = now_us;
                }

                // Auto-repeat while held
                if (cfg->repeat_period_ms > 0) {
                    if ((now_us - st->last_repeat_time) >
                        MS_TO_US(cfg->repeat_period_ms)) {

                        // Increment with saturation at 0xFF to avoid wraparound
                        if (st->hold_repeat_count < 0xFF) {
                            st->hold_repeat_count++;
                        }

                        emit(ctx, cfg, st,
                             BTN_EVT_LONG_HOLD,
                             st->hold_repeat_count,
                             now_us);

                        st->last_repeat_time = now_us;
                    }
                }
            }
        } else {
            /* == IDLE (not logically pressed) == */

            // Check click timeout for accumulated short presses
            if (st->click_count > 0 &&
                st->click_count != LONG_PRESS_ACTIVE) {

                if ((now_us - st->last_release_time) >
                    MS_TO_US(cfg->click_timeout_ms)) {

                    if (!st->suppressed) {
                        // For CLICK, timestamp = last logical release in series
                        emit(ctx, cfg, st,
                             BTN_EVT_CLICK,
                             st->click_count,
                             st->last_release_time);
                    }

                    st->click_count       = 0;
                    st->hold_repeat_count = 0;
                }
            }

            // Reset long-press marker if still set (cleanup)
            if (st->click_count == LONG_PRESS_ACTIVE) {
                st->click_count       = 0;
                st->hold_repeat_count = 0;
            }
        }
    }
}

bool btn_pop_event(btn_context_t *ctx, btn_event_t *evt) {
    if (!ctx || !evt) return false;
    if (!ctx->queue || ctx->queue_size == 0) return false;
    if (ctx->head == ctx->tail) return false;

    *evt = ctx->queue[ctx->tail];
    ctx->tail = (ctx->tail + 1) % ctx->queue_size;

    return true;
}

size_t btn_get_dropped_events(const btn_context_t *ctx) {
    if (!ctx) return 0;
    return ctx->dropped_events;
}

bool btn_is_pressed(btn_context_t *ctx, uint8_t btn_id) {
    if (!ctx) return false;

    int i = find_index(ctx, btn_id);
    if (i < 0) return false;

    btn_state_t *st = ctx->buttons[i].state;
    if (!st) return false;

    // Suppressed buttons are treated as "not pressed" by the helper API.
    return (st->logic_state && !st->suppressed);
}

uint64_t btn_get_duration(btn_context_t *ctx,
                          uint8_t btn_id,
                          uint64_t now_us) {
    if (!ctx) return 0;

    int i = find_index(ctx, btn_id);
    if (i < 0) return 0;

    btn_state_t *st = ctx->buttons[i].state;
    if (!st) return 0;

    if (!st->logic_state || st->suppressed) {
        return 0;
    }

    // Monotonic difference; 64-bit wraparound is practically unreachable.
    if (now_us >= st->state_start_time) {
        return now_us - st->state_start_time;
    }

    return 0;
}

void btn_suppress_events(btn_context_t *ctx, uint8_t btn_id) {
    if (!ctx) return;

    int i = find_index(ctx, btn_id);
    if (i < 0) return;

    btn_state_t *st = ctx->buttons[i].state;
    if (!st) return;

    // Suppress all further events until the next logical press (DOWN).
    st->suppressed        = true;
    st->click_count       = 0;
    st->hold_repeat_count = 0;

    // We intentionally do not modify logic_state or timing fields here:
    //  - no CLICK/UP/LONG_* will be emitted while suppressed,
    //  - physical release after suppression does not produce UP,
    //  - a new logical press (DOWN) clears suppression and starts a new series.
}
