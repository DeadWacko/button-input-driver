#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "buttonlib.h"

// Простая "виртуальная кнопка"
typedef struct {
    bool level;
} virtual_btn_t;

bool vbtn_read_fn(void *arg) {
    virtual_btn_t *vb = (virtual_btn_t*)arg;
    return vb->level;
}

static void push_time(uint64_t *now_us, uint32_t delta_ms) {
    *now_us += (uint64_t)delta_ms * 1000ULL;
}

int main(void) {
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
        .repeat_period_ms = 100
    };

    btn_init(&ctx, buttons, 1, queue, 16);
    btn_setup(&ctx, 0, &cfg, &states[0]);

    uint64_t now = 0;

    printf("=== TEST 1: Single CLICK ===\n");
    // Нажали
    vbtn.level = true;
    btn_update(&ctx, now);

    push_time(&now, 15); // > debounce
    btn_update(&ctx, now);

    // Отпустили
    vbtn.level = false;
    btn_update(&ctx, now);

    push_time(&now, 15);
    btn_update(&ctx, now);

    // Дождаться окончания окна клика
    push_time(&now, 250);
    btn_update(&ctx, now);

    btn_event_t evt;
    while (btn_pop_event(&ctx, &evt)) {
        printf("EVT: type=%d clicks=%u ts=%llu\n",
               evt.type, evt.clicks, (unsigned long long)evt.timestamp);
    }

    printf("=== TEST 2: LONG_START + LONG_HOLD ===\n");
    // Снова — длинное удержание
    now = 0;
    vbtn.level = true;
    btn_update(&ctx, now);

    push_time(&now, 20);  // debounce
    btn_update(&ctx, now);

    // Дотянуть до long_press_ms
    push_time(&now, 600);
    btn_update(&ctx, now);

    // Несколько периодов auto-repeat
    for (int i = 0; i < 5; ++i) {
        push_time(&now, 120);
        btn_update(&ctx, now);
    }

    // Отпустить
    vbtn.level = false;
    btn_update(&ctx, now);
    push_time(&now, 50);
    btn_update(&ctx, now);

    while (btn_pop_event(&ctx, &evt)) {
        printf("EVT: type=%d clicks=%u ts=%llu\n",
               evt.type, evt.clicks, (unsigned long long)evt.timestamp);
    }

    return 0;
}