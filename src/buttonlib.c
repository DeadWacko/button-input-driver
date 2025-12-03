#include "buttonlib.h"
#include <string.h>

#define MS_TO_US(x) ((uint64_t)(x) * 1000ULL)

static void enqueue_event(button_context_t *ctx, button_event_t evt) {
    // 1. Сначала пробуем вызвать Callback, если он задан для этой кнопки
    // Находим конфиг кнопки по ID (немного неэффективно искать перебором, 
    // но надежнее. В button_t нет обратной ссылки на ctx).
    // Оптимизация: передаем указатель на config прямо в update loop, 
    // поэтому вызов callback будет там, а не здесь.
    // Здесь только очередь.
    
    if (ctx->queue == NULL || ctx->queue_size == 0) return;

    size_t next_head = (ctx->head + 1) % ctx->queue_size;

    // ПЕРЕЗАПИСЬ (Overwriting): Если голова догнала хвост, двигаем хвост
    if (next_head == ctx->tail) {
        ctx->tail = (ctx->tail + 1) % ctx->queue_size;
    }

    ctx->queue[ctx->head] = evt;
    ctx->head = next_head;
}

// Внутренняя функция отправки события
static void emit(button_context_t *ctx, const button_config_t *cfg, 
                 button_event_type_t type, uint8_t count, uint64_t now) {
    
    button_event_t evt = {
        .button_id = cfg->id,
        .type = type,
        .clicks_count = count,
        .timestamp = now
    };

    // Callback Priority
    if (cfg->callback) {
        // Если колбэк вернул true, значит событие обработано и не нужно в очередь
        if (cfg->callback(&evt, cfg->cb_user_data)) {
            return;
        }
    }

    enqueue_event(ctx, evt);
}

bool btn_init(button_context_t *ctx, button_t *buttons, size_t count, 
              button_event_t *queue, size_t q_size) {
    if (!ctx || !buttons) return false;
    
    ctx->buttons = buttons;
    ctx->button_count = count;
    ctx->queue = queue;
    ctx->queue_size = q_size;
    ctx->head = 0;
    ctx->tail = 0;
    return true;
}

void btn_setup(button_context_t *ctx, uint8_t index, 
               const button_config_t *cfg, button_state_t *st) {
    if (index >= ctx->button_count) return;
    memset(st, 0, sizeof(button_state_t));
    ctx->buttons[index].config = cfg;
    ctx->buttons[index].state = st;
}

void btn_update(button_context_t *ctx, uint64_t now_us) {
    for (size_t i = 0; i < ctx->button_count; i++) {
        const button_config_t *cfg = ctx->buttons[i].config;
        button_state_t *st = ctx->buttons[i].state;
        if (!cfg || !st) continue; // Skip unconfigured slots

        // --- 1. Debounce ---
        bool raw = cfg->read_fn(cfg->hw_arg);
        if (cfg->active_low) raw = !raw;

        if (raw != st->raw_state) {
            st->last_debounce_time = now_us;
            st->raw_state = raw;
        }

        bool stable = st->logic_state;
        // Если сигнал стабилен дольше debounce времени
        if ((now_us - st->last_debounce_time) > MS_TO_US(cfg->debounce_ms)) {
            if (st->logic_state != raw) {
                // Изменение состояния
                stable = raw;
                st->logic_state = raw;

                if (stable) {
                    // -> PRESSED
                    st->state_start_time = now_us;
                    st->last_repeat_time = now_us;
                    emit(ctx, cfg, BTN_EVT_DOWN, 0, now_us);
                } else {
                    // -> RELEASED
                    emit(ctx, cfg, BTN_EVT_UP, 0, now_us);
                    
                    // Логика мульти-клика:
                    // Если мы отпустили кнопку ДО long_press, это потенциальный клик
                    uint64_t press_dur = now_us - st->state_start_time;
                    if (press_dur < MS_TO_US(cfg->long_press_ms)) {
                        st->click_count++; 
                        // Таймер сбрасывается при каждом отпускании в серии
                        st->state_start_time = now_us; // Используем это поле как таймер таймаута клика
                    } else {
                        // Если было удержание, сбрасываем счетчик кликов
                        st->click_count = 0;
                    }
                }
            }
        }

        // --- 2. Обработка состояний (Timeouts & Holds) ---
        if (stable) {
            // == Кнопка НАЖАТА ==
            uint64_t hold_time = now_us - st->state_start_time;

            // Long Press
            if (hold_time > MS_TO_US(cfg->long_press_ms)) {
                // Если мы только что перешагнули порог (используем click_count как флаг "long press already sent" для экономии памяти)
                // Сброс click_count в 0xFF означает "Long Press активен"
                if (st->click_count != 0xFF) {
                    st->click_count = 0xFF; // Маркер: длинное нажатие активировано
                    emit(ctx, cfg, BTN_EVT_LONG_START, 0, now_us);
                    st->last_repeat_time = now_us;
                }
                
                // Repeat (Long Hold)
                if (cfg->repeat_period_ms > 0) {
                    if ((now_us - st->last_repeat_time) > MS_TO_US(cfg->repeat_period_ms)) {
                        static uint8_t rep_cnt = 0; // Можно хранить в state если нужно точно
                        rep_cnt++;
                        emit(ctx, cfg, BTN_EVT_LONG_HOLD, rep_cnt, now_us);
                        st->last_repeat_time = now_us;
                    }
                }
            }
        } else {
            // == Кнопка ОТПУЩЕНА ==
            // Проверка таймаута мульти-клика
            if (st->click_count > 0 && st->click_count != 0xFF) {
                uint64_t release_time = now_us - st->state_start_time; // start_time обновлен при отпускании
                if (release_time > MS_TO_US(cfg->click_timeout_ms)) {
                    // Таймаут вышел, отправляем накопленные клики
                    // 1 = Single, 2 = Double, 3 = Triple...
                    if (st->click_count == 1) {
                         emit(ctx, cfg, BTN_EVT_CLICK, 1, now_us); // Single
                    } else {
                         // Double и более. Тип CLICK, в поле count число.
                         // Можно сделать отдельный enum для DOUBLE, но универсальный CLICK удобнее.
                         emit(ctx, cfg, BTN_EVT_CLICK, st->click_count, now_us);
                    }
                    st->click_count = 0;
                }
            } else {
                 // Если был Long Press (0xFF), сбрасываем при отпускании
                 if (st->click_count == 0xFF) st->click_count = 0;
            }
        }
    }
}

bool btn_pop_event(button_context_t *ctx, button_event_t *evt) {
    if (ctx->head == ctx->tail) return false;
    *evt = ctx->queue[ctx->tail];
    ctx->tail = (ctx->tail + 1) % ctx->queue_size;
    return true;
}

void btn_flush_queue(button_context_t *ctx) {
    ctx->tail = ctx->head;
}