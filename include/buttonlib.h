/**
 * @file buttonlib.h
 * @brief Professional button handling library for RP2040 and similar MCUs.
 * @version 3.1.2
 *
 * The library provides:
 *  - debouncing
 *  - multi-click (single / double / triple ...)
 *  - long press detection
 *  - auto-repeat on long press
 *  - suppression for combos (chords)
 *
 * Hardware access is abstracted via a user-provided read callback.
 */

#ifndef BUTTONLIB_H
#define BUTTONLIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* -------------------------------------------------------------------------- */
/*  Event types                                                               */
/* -------------------------------------------------------------------------- */

typedef enum {
    BTN_EVT_DOWN = 0,       ///< Logical press (debounced rising edge)
    BTN_EVT_UP,             ///< Logical release (debounced falling edge)
    BTN_EVT_CLICK,          ///< Completed click series (single / double / triple ...)
    BTN_EVT_LONG_START,     ///< Long press threshold reached (fired once per hold)
    BTN_EVT_LONG_HOLD,      ///< Auto-repeat while held
} btn_event_type_t;

/**
 * @brief Button event descriptor.
 *
 * timestamp semantics:
 *  - BTN_EVT_DOWN / BTN_EVT_UP:
 *      debounced press/release moment.
 *  - BTN_EVT_LONG_START / BTN_EVT_LONG_HOLD:
 *      moment when long-press threshold or repeat interval is reached.
 *  - BTN_EVT_CLICK:
 *      moment of the last release in the click series (not the timeout end).
 */
typedef struct {
    uint8_t          btn_id;     ///< Button ID (from configuration)
    btn_event_type_t type;       ///< Event type
    uint8_t          clicks;     ///< Click count (for CLICK) or repeat index (for LONG_HOLD)
    uint64_t         timestamp;  ///< Event time in microseconds
} btn_event_t;

/* -------------------------------------------------------------------------- */
/*  Callback types                                                            */
/* -------------------------------------------------------------------------- */

typedef bool (*btn_read_fn_t)(void *arg);                 ///< Hardware read callback
typedef bool (*btn_cb_t)(const btn_event_t *evt, void *user_data); ///< Optional per-button callback

/* -------------------------------------------------------------------------- */
/*  Configuration and state                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Button configuration.
 *
 * All timing values are specified in milliseconds.
 */
typedef struct {
    uint8_t id;              ///< Unique button ID (application-defined)
    bool    active_low;      ///< true if button shorts to GND when pressed

    // Hardware driver
    btn_read_fn_t read_fn;   ///< Function to read raw electrical state
    void         *hw_arg;    ///< Opaque argument passed to read_fn

    // Optional event callback
    btn_cb_t callback;       ///< NULL if not used
    void    *cb_user_data;   ///< User data passed to callback

    // Timings (milliseconds)
    uint16_t debounce_ms;       ///< Debounce window
    uint16_t click_timeout_ms;  ///< Time window to accumulate multi-clicks
    uint16_t long_press_ms;     ///< Long press threshold
    uint16_t repeat_period_ms;  ///< Auto-repeat period (0 disables repeat)
} btn_config_t;

/**
 * @brief Internal per-button state.
 *
 * The user only allocates this structure and passes its pointer to btn_setup().
 * Fields are fully managed by the library.
 */
typedef struct {
    bool logic_state;           ///< Debounced logical state (true = pressed)
    bool raw_state;             ///< Raw state as read from hardware
    bool suppressed;            ///< Suppression flag (for combos / chords)

    uint64_t last_debounce_time;
    uint64_t state_start_time;  ///< Time of logical press (for hold duration)
    uint64_t last_release_time; ///< Time of last logical release (for click timeout)
    uint64_t last_repeat_time;  ///< Time of last LONG_HOLD repeat

    uint8_t click_count;        ///< Click accumulator or LONG_PRESS_ACTIVE marker
    uint8_t hold_repeat_count;  ///< LONG_HOLD repeat counter within a single hold
} btn_state_t;

typedef struct {
    const btn_config_t *config;
    btn_state_t        *state;
} btn_instance_t;

/**
 * @brief Button system context.
 *
 * Holds all button instances and event queue state.
 */
typedef struct {
    btn_instance_t *buttons;
    size_t          btn_count;

    btn_event_t *queue;
    size_t       queue_size;
    size_t       head;
    size_t       tail;

    /**
     * @brief Number of dropped (overwritten) events due to queue overflow.
     *
     * This is incremented whenever the queue is full and the oldest event
     * is overwritten with a new one. Useful for diagnostics and tuning.
     */
    size_t dropped_events;
} btn_context_t;

/* -------------------------------------------------------------------------- */
/*  Public API                                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize button context.
 *
 * @param ctx       Context to initialize.
 * @param buttons   Array of button instances (size = count).
 * @param count     Number of button instances.
 * @param queue     Event queue buffer (can be NULL if queue is not used).
 * @param q_size    Queue size (number of events).
 *
 * @return true on success, false on invalid arguments.
 */
bool btn_init(btn_context_t *ctx,
              btn_instance_t *buttons,
              size_t count,
              btn_event_t *queue,
              size_t q_size);

/**
 * @brief Configure a single button instance.
 *
 * @param ctx       Button context.
 * @param index     Index in the buttons array (0 .. btn_count-1).
 * @param cfg       Button configuration (must outlive the context).
 * @param st        Pointer to button state storage.
 *
 * If cfg is NULL or cfg->read_fn is NULL, the button is considered disabled.
 */
void btn_setup(btn_context_t *ctx,
               uint8_t index,
               const btn_config_t *cfg,
               btn_state_t *st);

/**
 * @brief Main update function.
 *
 * Must be called periodically (e.g. every 1–10 ms) with a monotonically
 * increasing timestamp in microseconds.
 *
 * Typical source for RP2040: time_us_64().
 */
void btn_update(btn_context_t *ctx, uint64_t now_us);

/**
 * @brief Pop next event from the queue.
 *
 * @param ctx   Button context.
 * @param evt   Output event.
 * @return true if an event was returned, false if the queue is empty.
 */
bool btn_pop_event(btn_context_t *ctx, btn_event_t *evt);

/* -------------------------------------------------------------------------- */
/*  Helper API                                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Check if a button is logically pressed.
 *
 * Returns false if:
 *  - the button is not found,
 *  - the button is currently suppressed,
 *  - or its logical state is not pressed.
 */
bool btn_is_pressed(btn_context_t *ctx, uint8_t btn_id);

/**
 * @brief Get current hold duration for a button in microseconds.
 *
 * @param ctx       Button context.
 * @param btn_id    Button ID.
 * @param now_us    Current time in microseconds.
 *
 * @return Hold duration if the button is logically pressed and not suppressed,
 *         0 otherwise.
 */
uint64_t btn_get_duration(btn_context_t *ctx,
                          uint8_t btn_id,
                          uint64_t now_us);

/**
 * @brief Suppress all events for a button until the next logical press.
 *
 * After calling this:
 *  - pending clicks are discarded,
 *  - no CLICK/UP/LONG_* events are emitted until a new BTN_EVT_DOWN,
 *  - helpers (btn_is_pressed / btn_get_duration) treat the button as inactive.
 *
 * This is typically used to handle button combos (chords) where normal
 * per-button events must be ignored once the combo is recognized.
 */
void btn_suppress_events(btn_context_t *ctx, uint8_t btn_id);

/**
 * @brief Get number of dropped (overwritten) events due to queue overflow.
 *
 * @param ctx   Button context.
 * @return dropped_events counter value (0 if ctx is NULL).
 */
size_t btn_get_dropped_events(const btn_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif // BUTTONLIB_H
