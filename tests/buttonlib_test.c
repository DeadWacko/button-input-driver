#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "buttonlib.h"

/*
 * Simple virtual button model for unit testing.
 * We fully control the "hardware" level and the time flow.
 */

typedef struct {
    bool level;
} virtual_btn_t;

static bool vbtn_read_fn(void *arg) {
    virtual_btn_t *vb = (virtual_btn_t*)arg;
    return vb->level;
}

static void advance_ms(uint64_t *now_us, uint32_t delta_ms) {
    *now_us += (uint64_t)delta_ms * 1000ULL;
}

static void print_event(const char *label, const btn_event_t *evt) {
    printf("%s: id=%u type=%d clicks=%u ts=%llu\n",
           label,
           evt->btn_id,
           (int)evt->type,
           evt->clicks,
           (unsigned long long)evt->timestamp);
}

/* -------------------------------------------------------------------------- */
/*  Test 1: Single click                                                      */
/* -------------------------------------------------------------------------- */

static void test_single_click(void) {
    printf("=== TEST: single click ===\n");

    btn_instance_t buttons[1];
    btn_state_t    states[1];
    btn_event_t    queue[16];
    btn_context_t  ctx;

    virtual_btn_t vbtn = { .level = false };

    const btn_config_t cfg = {
        .id = 1,
        .active_low = false,
        .read_fn = vbtn_read_fn,
        .hw_arg = &vbtn,
        .callback = NULL,
        .cb_user_data = NULL,
        .debounce_ms = 10,
        .click_timeout_ms = 200,
        .long_press_ms = 500,
        .repeat_period_ms = 0
    };

    btn_init(&ctx, buttons, 1, queue, 16);
    btn_setup(&ctx, 0, &cfg, &states[0]);

    uint64_t now = 0;

    // Press
    vbtn.level = true;
    btn_update(&ctx, now);
    advance_ms(&now, 15); // debounce
    btn_update(&ctx, now);

    // Release
    vbtn.level = false;
    btn_update(&ctx, now);
    advance_ms(&now, 15);
    btn_update(&ctx, now);

    // Wait for click_timeout
    advance_ms(&now, 250);
    btn_update(&ctx, now);

    btn_event_t evt;
    while (btn_pop_event(&ctx, &evt)) {
        print_event("EVT", &evt);
    }
}

/* -------------------------------------------------------------------------- */
/*  Test 2: Double and triple click                                           */
/* -------------------------------------------------------------------------- */

static void test_multi_click(void) {
    printf("=== TEST: multi click (double / triple) ===\n");

    btn_instance_t buttons[1];
    btn_state_t    states[1];
    btn_event_t    queue[16];
    btn_context_t  ctx;

    virtual_btn_t vbtn = { .level = false };

    const btn_config_t cfg = {
        .id = 1,
        .active_low = false,
        .read_fn = vbtn_read_fn,
        .hw_arg = &vbtn,
        .callback = NULL,
        .cb_user_data = NULL,
        .debounce_ms = 10,
        .click_timeout_ms = 200,
        .long_press_ms = 500,
        .repeat_period_ms = 0
    };

    btn_init(&ctx, buttons, 1, queue, 16);
    btn_setup(&ctx, 0, &cfg, &states[0]);

    uint64_t now = 0;
    btn_event_t evt;

    // --- Double click ---
    for (int click = 0; click < 2; ++click) {
        vbtn.level = true;
        btn_update(&ctx, now);
        advance_ms(&now, 15);
        btn_update(&ctx, now);

        vbtn.level = false;
        btn_update(&ctx, now);
        advance_ms(&now, 15);
        btn_update(&ctx, now);

        // Gap between clicks, less than click_timeout
        advance_ms(&now, 100);
        btn_update(&ctx, now);
    }

    // Wait for click_timeout to expire
    advance_ms(&now, 250);
    btn_update(&ctx, now);

    printf("--- Events after double click ---\n");
    while (btn_pop_event(&ctx, &evt)) {
        print_event("EVT", &evt);
    }

    // --- Triple click ---
    now = 0;
    vbtn.level = false;

    // Re-init context and state
    btn_init(&ctx, buttons, 1, queue, 16);
    btn_setup(&ctx, 0, &cfg, &states[0]);

    for (int click = 0; click < 3; ++click) {
        vbtn.level = true;
        btn_update(&ctx, now);
        advance_ms(&now, 15);
        btn_update(&ctx, now);

        vbtn.level = false;
        btn_update(&ctx, now);
        advance_ms(&now, 15);
        btn_update(&ctx, now);

        advance_ms(&now, 80);
        btn_update(&ctx, now);
    }

    advance_ms(&now, 250);
    btn_update(&ctx, now);

    printf("--- Events after triple click ---\n");
    while (btn_pop_event(&ctx, &evt)) {
        print_event("EVT", &evt);
    }
}

/* -------------------------------------------------------------------------- */
/*  Test 3: Long press + auto-repeat                                          */
/* -------------------------------------------------------------------------- */

static void test_long_and_hold(void) {
    printf("=== TEST: long press + hold repeat ===\n");

    btn_instance_t buttons[1];
    btn_state_t    states[1];
    btn_event_t    queue[32];
    btn_context_t  ctx;

    virtual_btn_t vbtn = { .level = false };

    const btn_config_t cfg = {
        .id = 1,
        .active_low = false,
        .read_fn = vbtn_read_fn,
        .hw_arg = &vbtn,
        .callback = NULL,
        .cb_user_data = NULL,
        .debounce_ms = 10,
        .click_timeout_ms = 200,
        .long_press_ms = 300,
        .repeat_period_ms = 100
    };

    btn_init(&ctx, buttons, 1, queue, 32);
    btn_setup(&ctx, 0, &cfg, &states[0]);

    uint64_t now = 0;
    btn_event_t evt;

    vbtn.level = true;
    btn_update(&ctx, now);

    advance_ms(&now, 20);
    btn_update(&ctx, now);

    // Reach long_press_ms
    advance_ms(&now, 350);
    btn_update(&ctx, now);

    // Several repeats
    for (int i = 0; i < 5; ++i) {
        advance_ms(&now, 120);
        btn_update(&ctx, now);
    }

    // Release
    vbtn.level = false;
    btn_update(&ctx, now);
    advance_ms(&now, 20);
    btn_update(&ctx, now);

    while (btn_pop_event(&ctx, &evt)) {
        print_event("EVT", &evt);
    }
}

/* -------------------------------------------------------------------------- */
/*  Test 4: Queue overflow and dropped_events                                 */
/* -------------------------------------------------------------------------- */

static void test_queue_overflow(void) {
    printf("=== TEST: queue overflow and dropped_events ===\n");

    btn_instance_t buttons[1];
    btn_state_t    states[1];
    btn_event_t    queue[4]; // small queue to trigger overflow
    btn_context_t  ctx;

    virtual_btn_t vbtn = { .level = false };

    const btn_config_t cfg = {
        .id = 1,
        .active_low = false,
        .read_fn = vbtn_read_fn,
        .hw_arg = &vbtn,
        .callback = NULL,
        .cb_user_data = NULL,
        .debounce_ms = 0,
        .click_timeout_ms = 50,
        .long_press_ms = 1000,
        .repeat_period_ms = 0
    };

    btn_init(&ctx, buttons, 1, queue, 4);
    btn_setup(&ctx, 0, &cfg, &states[0]);

    uint64_t now = 0;
    btn_event_t evt;

    // Generate many quick DOWN/UP pairs to overflow the queue
    for (int i = 0; i < 10; ++i) {
        vbtn.level = true;
        btn_update(&ctx, now);
        vbtn.level = false;
        btn_update(&ctx, now);
        advance_ms(&now, 10);
        btn_update(&ctx, now);
    }

    printf("Dropped events: %zu\n", btn_get_dropped_events(&ctx));

    while (btn_pop_event(&ctx, &evt)) {
        print_event("EVT", &evt);
    }
}

/* -------------------------------------------------------------------------- */
/*  Test 5: Suppression sanity                                                */
/* -------------------------------------------------------------------------- */

static void test_suppression(void) {
    printf("=== TEST: suppression ===\n");

    btn_instance_t buttons[1];
    btn_state_t    states[1];
    btn_event_t    queue[16];
    btn_context_t  ctx;

    virtual_btn_t vbtn = { .level = false };

    const btn_config_t cfg = {
        .id = 1,
        .active_low = false,
        .read_fn = vbtn_read_fn,
        .hw_arg = &vbtn,
        .callback = NULL,
        .cb_user_data = NULL,
        .debounce_ms = 10,
        .click_timeout_ms = 200,
        .long_press_ms = 300,
        .repeat_period_ms = 0
    };

    btn_init(&ctx, buttons, 1, queue, 16);
    btn_setup(&ctx, 0, &cfg, &states[0]);

    uint64_t now = 0;
    btn_event_t evt;

    // Press and debounce
    vbtn.level = true;
    btn_update(&ctx, now);
    advance_ms(&now, 20);
    btn_update(&ctx, now);

    // Suppress while held (combo-like use-case)
    btn_suppress_events(&ctx, 1);

    // Keep holding and then release
    advance_ms(&now, 200);
    btn_update(&ctx, now);
    vbtn.level = false;
    btn_update(&ctx, now);
    advance_ms(&now, 50);
    btn_update(&ctx, now);

    printf("Events after suppression (should be minimal):\n");
    while (btn_pop_event(&ctx, &evt)) {
        print_event("EVT", &evt);
    }

    printf("Is pressed after release: %s\n",
           btn_is_pressed(&ctx, 1) ? "true" : "false");
}

/* -------------------------------------------------------------------------- */

int main(void) {
    test_single_click();
    test_multi_click();
    test_long_and_hold();
    test_queue_overflow();
    test_suppression();
    return 0;
}
