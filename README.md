# ButtonLib — Professional Button Library for RP2040

**Version:** 3.1.0  
**Target:** RP2040 (Raspberry Pi Pico / Pico W и др.)

ButtonLib — это компактная и надёжная библиотека для обработки физических кнопок:
- антидребезг,
- мульти-клик (single / double / triple),
- длинное нажатие и удержание (auto-repeat),
- комбинации кнопок через suppression,
- независимость от конкретного железа (GPIO / I2C / shift-регистр и т.д.).

---

## Features

- Чёткая модель событий: `DOWN`, `UP`, `CLICK`, `LONG_START`, `LONG_HOLD`.
-  Настраиваемые тайминги:
  - `debounce_ms`,
  - `click_timeout_ms`,
  - `long_press_ms`,
  - `repeat_period_ms`.
- Никакого `malloc` — вся память статическая, задаётся пользователем.
-  Hardware-agnostic: вход — любая функция `bool read_fn(void *arg)`.
- Удобная интеграция в UI / меню / часы (см. `examples/main.c`).

---

## Repository structure

```text
.
├── CMakeLists.txt          # Главный сборочный скрипт
├── .gitignore
├── README.md               # Этот файл
├── include/
│   └── buttonlib.h         # Публичный API
├── src/
│   └── buttonlib.c         # Реализация
├── examples/
│   ├── CMakeLists.txt
│   └── main.c              # Пример интеграции с Pico SDK
└── docs/
    └── buttonlib_spec.md   # Подробная спецификация (SPEC)
````

---

## Quick start (Raspberry Pi Pico / Pico W)

### 1. Подключить библиотеку в CMake

```cmake
add_library(buttonlib
    src/buttonlib.c
)

target_include_directories(buttonlib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Пример
add_executable(button_example examples/main.c)
target_link_libraries(button_example
    pico_stdlib
    buttonlib
)
pico_add_extra_outputs(button_example)
```

### 2. Настроить кнопки

```c
#include "buttonlib.h"

#define ID_L  10
#define ID_C  20
#define ID_R  30

btn_instance_t buttons[3];
btn_state_t    states[3];
btn_event_t    queue[32];
btn_context_t  ctx;

bool read_pin(void *arg) {
    uint gpio = (uint)(uintptr_t)arg;
    return gpio_get(gpio);
}

const btn_config_t cfg_l = {
    .id = ID_L,
    .active_low = true,
    .read_fn = read_pin,
    .hw_arg = (void*)16,
    .callback = NULL,
    .cb_user_data = NULL,
    .debounce_ms = 20,
    .click_timeout_ms = 200,
    .long_press_ms = 800,
    .repeat_period_ms = 100
};
```

### 3. Инициализация

```c
btn_init(&ctx, buttons, 3, queue, 32);
btn_setup(&ctx, 0, &cfg_l, &states[0]);
// ... остальные кнопки
```

### 4. Главный цикл

```c
while (true) {
    uint64_t now = time_us_64();
    btn_update(&ctx, now);

    btn_event_t evt;
    while (btn_pop_event(&ctx, &evt)) {
        // Обработать событие
    }

    sleep_ms(5);
}
```

---

## Events

```c
typedef enum {
    BTN_EVT_DOWN,       // Фронт нажатия
    BTN_EVT_UP,         // Отпускание
    BTN_EVT_CLICK,      // Завершённая серия кликов
    BTN_EVT_LONG_START, // Старт длинного нажатия
    BTN_EVT_LONG_HOLD   // Повтор при удержании
} btn_event_type_t;
```

Поле `clicks`:

* Для `BTN_EVT_CLICK` — количество кликов (1, 2, 3).
* Для `BTN_EVT_LONG_HOLD` — номер повтора (1, 2, 3, ...).

---

## Changelog

Смотри `CHANGELOG.md`.

---

## License

(Укажи нужную лицензию: MIT / BSD / Proprietary / etc.)

````