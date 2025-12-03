#ifndef BUTTONLIB_H
#define BUTTONLIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// --- Типы событий ---
typedef enum {
    BTN_EVT_DOWN = 0,       // Нажатие
    BTN_EVT_UP,             // Отпускание
    BTN_EVT_CLICK,          // Клик (одинарный, двойной и т.д. см. clicks_count)
    BTN_EVT_LONG_START,     // Старт длинного нажатия
    BTN_EVT_LONG_HOLD,      // Удержание (аналог REPEAT)
} button_event_type_t;

// --- Данные события ---
typedef struct {
    uint8_t button_id;          // ID кнопки
    button_event_type_t type;   // Тип
    uint8_t clicks_count;       // Кол-во кликов (для EVT_CLICK) или повторов (для EVT_LONG_HOLD)
    uint64_t timestamp;         // Время события
} button_event_t;

// --- Прототип функции чтения GPIO ---
typedef bool (*button_read_fn_t)(void *arg);

// --- Прототип Callback'а (Опционально) ---
// Если возвращает true, событие НЕ попадает в общую очередь (перехвачено).
typedef bool (*button_cb_t)(const button_event_t *evt, void *user_data);

// --- Конфигурация (ROM) ---
typedef struct {
    uint8_t id;                 
    bool active_low;            // true: 0 = нажата

    // Драйвер
    button_read_fn_t read_fn;   
    void *hw_arg;               

    // Опциональный callback (если NULL - только очередь)
    button_cb_t callback;
    void *cb_user_data;

    // Тайминги (ms)
    uint16_t debounce_ms;       // 20-50
    uint16_t click_timeout_ms;  // Время ожидания следующего клика (для Multi-click)
    uint16_t long_press_ms;     // Время до срабатывания Long Press
    uint16_t repeat_period_ms;  // 0 = выкл. Период генерации событий удержания
} button_config_t;

// --- Состояние (RAM) ---
typedef struct {
    // Фильтрация
    bool logic_state;           
    bool raw_state;             
    uint64_t last_debounce_time;

    // FSM
    uint64_t state_start_time;  
    uint64_t last_repeat_time;  
    
    // Multi-click logic
    uint8_t click_count;        // Текущий счетчик кликов
    bool waiting_release;       // Ждем отпускания для засчитывания клика
} button_state_t;

// --- Объект кнопки ---
typedef struct {
    const button_config_t *config;
    button_state_t *state;
} button_t;

// --- Контекст ---
typedef struct {
    button_t *buttons;
    size_t button_count;
    button_event_t *queue;
    size_t queue_size;
    size_t head;
    size_t tail;
} button_context_t;

// --- API ---

bool btn_init(button_context_t *ctx, button_t *buttons, size_t count, button_event_t *queue, size_t q_size);

// Настройка кнопки. index < count
void btn_setup(button_context_t *ctx, uint8_t index, const button_config_t *cfg, button_state_t *st);

// Главный цикл. Вызывать каждые 1-10 мс.
void btn_update(button_context_t *ctx, uint64_t now_us);

// Чтение события (FIFO). Возвращает true, если событие есть.
bool btn_pop_event(button_context_t *ctx, button_event_t *evt);

// Очистка очереди
void btn_flush_queue(button_context_t *ctx);

#ifdef __cplusplus
}
#endif
#endif