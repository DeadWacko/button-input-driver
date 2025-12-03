#include <stdio.h>
#include "pico/stdlib.h"
#include "buttonlib.h"

// --- Пины ---
#define PIN_LEFT   16
#define PIN_CENTER 17
#define PIN_RIGHT  18

// --- Память (Static Allocation) ---
#define BTN_COUNT 3
#define QUEUE_SIZE 16

static button_t buttons[BTN_COUNT];
static button_state_t states[BTN_COUNT];
static button_event_t queue_mem[QUEUE_SIZE];
static button_context_t ctx;

// --- HAL: Чтение пина ---
bool read_gpio(void *arg) {
    return gpio_get((uint)(uintptr_t)arg);
}

// --- CALLBACK ДЛЯ ПРАВОЙ КНОПКИ ---
// Демонстрация мгновенной реакции (до очереди событий)
bool right_btn_callback(const button_event_t *evt, void *user_data) {
    // Просто пишем в лог мгновенно при нажатии/отпускании
    if (evt->type == BTN_EVT_DOWN) {
        printf("[CALLBACK] Right Button (18) PRESSED NOW!\n");
    } 
    
    // Возвращаем false, чтобы событие НЕ исчезло и попало также в общую очередь
    // Если вернуть true, то в switch(evt.type) в main() оно не придет.
    return false; 
}

// --- Конфигурации ---

// 1. Левая (16): Классика (Одиночный клик, Долгое нажатие)
const button_config_t cfg_left = {
    .id = 1,
    .active_low = true,
    .read_fn = read_gpio, .hw_arg = (void*)PIN_LEFT,
    .callback = NULL,
    
    .debounce_ms = 25,
    .click_timeout_ms = 250, 
    .long_press_ms = 1000,
    .repeat_period_ms = 0
};

// 2. Центральная (17): МУЛЬТИ-КЛИК и АВТОПОВТОР
const button_config_t cfg_center = {
    .id = 2,
    .active_low = true,
    .read_fn = read_gpio, .hw_arg = (void*)PIN_CENTER,
    .callback = NULL,

    .debounce_ms = 25,
    .click_timeout_ms = 400, // Чуть больше времени, чтобы накликивать комбо
    .long_press_ms = 800,
    .repeat_period_ms = 200  // Спам событиями при удержании
};

// 3. Правая (18): С CALLBACK (Моментальная реакция)
const button_config_t cfg_right = {
    .id = 3,
    .active_low = true,
    .read_fn = read_gpio, .hw_arg = (void*)PIN_RIGHT,
    
    // Подключаем колбэк!
    .callback = right_btn_callback,
    .cb_user_data = NULL,

    .debounce_ms = 20,
    .click_timeout_ms = 300,
    .long_press_ms = 1500,
    .repeat_period_ms = 0
};

int main() {
    stdio_init_all();
    sleep_ms(2000); // Время на подключение консоли
    printf("=== ButtonLib v2.0: Clean Log Demo ===\n");
    printf("Pins: Left=%d, Center=%d, Right=%d\n", PIN_LEFT, PIN_CENTER, PIN_RIGHT);

    // Инициализация GPIO
    const uint pins[] = {PIN_LEFT, PIN_CENTER, PIN_RIGHT};
    for(int i=0; i<3; i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_pull_up(pins[i]);
    }
    
    // Инициализация библиотеки
    btn_init(&ctx, buttons, BTN_COUNT, queue_mem, QUEUE_SIZE);
    
    btn_setup(&ctx, 0, &cfg_left,   &states[0]);
    btn_setup(&ctx, 1, &cfg_center, &states[1]);
    btn_setup(&ctx, 2, &cfg_right,  &states[2]);

    printf("System Ready. Waiting for inputs...\n");

    while (true) {
        // 1. Ядро библиотеки (опрос кнопок)
        btn_update(&ctx, time_us_64());

        // 2. Чтение очереди событий
        button_event_t evt;
        while (btn_pop_event(&ctx, &evt)) {
            // Определяем имя для красивого лога
            const char *name = (evt.button_id == 1) ? "LEFT  (16)" : 
                               (evt.button_id == 2) ? "CENTER(17)" : "RIGHT (18)";

            switch (evt.type) {
                // DOWN и UP можно раскомментировать для детальной отладки
                case BTN_EVT_DOWN:
                    // printf("%s -> Down\n", name); 
                    break;
                case BTN_EVT_UP:
                    // printf("%s -> Up\n", name);
                    break;
                
                case BTN_EVT_CLICK:
                    if (evt.clicks_count == 1) {
                        printf(">>> %s: Single Click\n", name);
                    } else if (evt.clicks_count == 2) {
                        printf(">>> %s: DOUBLE CLICK\n", name);
                    } else if (evt.clicks_count == 3) {
                        printf(">>> %s: TRIPLE CLICK (Combo!)\n", name);
                    } else {
                        printf(">>> %s: %d-CLICKS MEGA COMBO\n", name, evt.clicks_count);
                    }
                    break;

                case BTN_EVT_LONG_START:
                    printf(">>> %s: Long Press Start\n", name);
                    break;

                case BTN_EVT_LONG_HOLD:
                    printf(">>> %s: Hold Repeat (%d)\n", name, evt.clicks_count);
                    break;
            }
        }
        
        sleep_us(500); // Небольшая пауза, чтобы не грузить CPU на 100%
    }
}