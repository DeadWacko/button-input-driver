#include "buttonlib.h"
#include <string.h>

#define MS_TO_US(x) ((uint64_t)(x) * 1000ULL)
#define LONG_PRESS_ACTIVE 0xFF

static int find_index(btn_context_t *ctx, uint8_t id) {
    for (size_t i = 0; i < ctx->btn_count; i++) {
        if (ctx->buttons[i].config && ctx->buttons[i].config->id == id) return (int)i;
    }
    return -1;
}

static void push_event(btn_context_t *ctx, btn_event_t evt) {
    if (!ctx->queue || ctx->queue_size == 0) return;
    size_t next = (ctx->head + 1) % ctx->queue_size;
    
    // Overwrite old events if full
    if (next == ctx->tail) {
        ctx->tail = (ctx->tail + 1) % ctx->queue_size; 
    }
    
    ctx->queue[ctx->head] = evt;
    ctx->head = next;
}

static void emit(btn_context_t *ctx, const btn_config_t *cfg, btn_state_t *st,
                 btn_event_type_t type, uint8_t clicks, uint64_t now) {
    
    if (st->suppressed) return;

    btn_event_t evt = {
        .btn_id = cfg->id,
        .type = type,
        .clicks = clicks,
        .timestamp = now
    };

    if (cfg->callback && cfg->callback(&evt, cfg->cb_user_data)) return;
    push_event(ctx, evt);
}

// --- API ---

bool btn_init(btn_context_t *ctx, btn_instance_t *buttons, size_t count, btn_event_t *queue, size_t q_size) {
    if (!ctx || !buttons) return false;
    memset(ctx, 0, sizeof(btn_context_t));
    ctx->buttons = buttons;
    ctx->btn_count = count;
    ctx->queue = queue;
    ctx->queue_size = q_size;
    return true;
}

void btn_setup(btn_context_t *ctx, uint8_t index, const btn_config_t *cfg, btn_state_t *st) {
    if (index >= ctx->btn_count) return;
    memset(st, 0, sizeof(btn_state_t));
    ctx->buttons[index].config = cfg;
    ctx->buttons[index].state = st;
}

void btn_update(btn_context_t *ctx, uint64_t now_us) {
    for (size_t i = 0; i < ctx->btn_count; i++) {
        const btn_config_t *cfg = ctx->buttons[i].config;
        btn_state_t *st = ctx->buttons[i].state;
        if (!cfg || !st) continue;

        // 1. Hardware Read
        bool raw = cfg->read_fn(cfg->hw_arg);
        if (cfg->active_low) raw = !raw;

        // 2. Debounce
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
                    // -> PRESSED
                    st->state_start_time = now_us;
                    st->last_repeat_time = now_us;
                    st->suppressed = false; // Reset suppression on new press
                    emit(ctx, cfg, st, BTN_EVT_DOWN, 0, now_us);
                } else {
                    // -> RELEASED
                    emit(ctx, cfg, st, BTN_EVT_UP, 0, now_us);
                    
                    if (!st->suppressed) {
                        uint64_t duration = now_us - st->state_start_time;
                        // Если это было короткое нажатие - засчитываем в серию
                        if (duration < MS_TO_US(cfg->long_press_ms)) {
                            st->click_count++;
                            st->last_release_time = now_us; // Таймер таймаута серии
                        } else {
                            st->click_count = 0; // Длинное нажатие обнуляет серию
                        }
                    }
                }
            }
        }

        // 3. Logic (Timeouts & Holds)
        if (stable) {
            // == HELD ==
            uint64_t hold_time = now_us - st->state_start_time;
            
            if (hold_time > MS_TO_US(cfg->long_press_ms)) {
                if (st->click_count != LONG_PRESS_ACTIVE) {
                    st->click_count = LONG_PRESS_ACTIVE; // Mark as handled
                    emit(ctx, cfg, st, BTN_EVT_LONG_START, 0, now_us);
                    st->last_repeat_time = now_us;
                }
                
                // Auto-Repeat
                if (cfg->repeat_period_ms > 0) {
                    if ((now_us - st->last_repeat_time) > MS_TO_US(cfg->repeat_period_ms)) {
                        st->hold_repeat_count++;
                        emit(ctx, cfg, st, BTN_EVT_LONG_HOLD, st->hold_repeat_count, now_us);
                        st->last_repeat_time = now_us;
                    }
                }
            }
        } else {
            // == IDLE ==
            // Check Click Timeout
            if (st->click_count > 0 && st->click_count != LONG_PRESS_ACTIVE) {
                if ((now_us - st->last_release_time) > MS_TO_US(cfg->click_timeout_ms)) {
                    if (!st->suppressed) {
                        emit(ctx, cfg, st, BTN_EVT_CLICK, st->click_count, now_us);
                    }
                    st->click_count = 0;
                    st->hold_repeat_count = 0;
                }
            }
            // Reset LongPress flag on release (cleanup)
            if (st->click_count == LONG_PRESS_ACTIVE) {
                st->click_count = 0;
                st->hold_repeat_count = 0;
            }
        }
    }
}

bool btn_pop_event(btn_context_t *ctx, btn_event_t *evt) {
    if (ctx->head == ctx->tail) return false;
    *evt = ctx->queue[ctx->tail];
    ctx->tail = (ctx->tail + 1) % ctx->queue_size;
    return true;
}

bool btn_is_pressed(btn_context_t *ctx, uint8_t btn_id) {
    int i = find_index(ctx, btn_id);
    return (i >= 0) ? ctx->buttons[i].state->logic_state : false;
}

uint64_t btn_get_duration(btn_context_t *ctx, uint8_t btn_id, uint64_t now_us) {
    int i = find_index(ctx, btn_id);
    if (i < 0) return 0;
    btn_state_t *st = ctx->buttons[i].state;
    if (!st->logic_state) return 0;
    
    // Защита от переполнения времени (хотя с 64 битами маловероятно)
    return (now_us >= st->state_start_time) ? (now_us - st->state_start_time) : 0;
}

void btn_suppress_events(btn_context_t *ctx, uint8_t btn_id) {
    int i = find_index(ctx, btn_id);
    if (i >= 0) {
        btn_state_t *st = ctx->buttons[i].state;
        st->suppressed = true;
        st->click_count = 0;
        st->hold_repeat_count = 0;
        // оставляем временные метки как есть — после нового BTN_EVT_DOWN suppression снимется автоматически
    }
}
