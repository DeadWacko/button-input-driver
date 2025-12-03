/**
 * @file buttonlib.h
 * @brief Professional Button Library for RP2040
 * @version 3.0.0 (Gold Master)
 */

#ifndef BUTTONLIB_H
#define BUTTONLIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// --- События ---

typedef enum {
    BTN_EVT_DOWN = 0,       ///< Нажатие (фронт)
    BTN_EVT_UP,             ///< Отпускание (спад)
    BTN_EVT_CLICK,          ///< Клик завершен (Single, Double, Triple...)
    BTN_EVT_LONG_START,     ///< Старт долгого нажатия
    BTN_EVT_LONG_HOLD,      ///< Удержание (автоповтор)
} btn_event_type_t;

typedef struct {
    uint8_t btn_id;         ///< ID кнопки
    btn_event_type_t type;  ///< Тип
    uint8_t clicks;         ///< Количество кликов (для EVT_CLICK) или счетчик повторов (для HOLD)
    uint64_t timestamp;     ///< Время события (мкс)
} btn_event_t;

// --- Типы функций ---

typedef bool (*btn_read_fn_t)(void *arg);
typedef bool (*btn_cb_t)(const btn_event_t *evt, void *user_data);

// --- Конфигурация и Состояние ---

typedef struct {
    uint8_t id;             ///< Уникальный ID
    bool active_low;        ///< true = замыкание на землю (GND)
    
    // Драйвер
    btn_read_fn_t read_fn;
    void *hw_arg;

    // Опциональный колбэк
    btn_cb_t callback;
    void *cb_user_data;

    // Тайминги (мс)
    uint16_t debounce_ms;       ///< Антидребезг (20-50)
    uint16_t click_timeout_ms;  ///< Таймаут для мульти-клика (200-500)
    uint16_t long_press_ms;     ///< Время удержания
    uint16_t repeat_period_ms;  ///< Период повтора (0 = выкл)
} btn_config_t;

typedef struct {
    bool logic_state;           ///< Текущее логическое состояние
    bool raw_state;             ///< Сырое состояние
    bool suppressed;            ///< Флаг подавления (для комбо)
    
    uint64_t last_debounce_time;
    uint64_t state_start_time;  ///< Время нажатия
    uint64_t last_release_time; ///< Время последнего отпускания (для таймаута клика)
    uint64_t last_repeat_time;  
    
    uint8_t click_count;        ///< Накопитель кликов
    uint8_t hold_repeat_count;  ///< Счётчик повторов LONG_HOLD в рамках одного удержания
} btn_state_t;

typedef struct {
    const btn_config_t *config;
    btn_state_t *state;
} btn_instance_t;

typedef struct {
    btn_instance_t *buttons;
    size_t btn_count;
    btn_event_t *queue;
    size_t queue_size;
    size_t head;
    size_t tail;
} btn_context_t;

// --- API ---

// Инициализация системы
bool btn_init(btn_context_t *ctx, btn_instance_t *buttons, size_t count, btn_event_t *queue, size_t q_size);

// Настройка отдельной кнопки
void btn_setup(btn_context_t *ctx, uint8_t index, const btn_config_t *cfg, btn_state_t *st);

// Главный цикл (вызывать каждые 1-10 мс)
void btn_update(btn_context_t *ctx, uint64_t now_us);

// Чтение события из очереди
bool btn_pop_event(btn_context_t *ctx, btn_event_t *evt);

// --- Хелперы ---

// Проверка нажатия (true/false)
bool btn_is_pressed(btn_context_t *ctx, uint8_t btn_id);

// Время удержания в мкс (0 если не нажата)
uint64_t btn_get_duration(btn_context_t *ctx, uint8_t btn_id, uint64_t now_us);

// Принудительный сброс состояния (для обработки комбо)
void btn_suppress_events(btn_context_t *ctx, uint8_t btn_id);

#ifdef __cplusplus
}
#endif

#endif // BUTTONLIB_H
