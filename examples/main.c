#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "buttonlib.h"

/*
 * Example application:
 * - Three buttons: Left / Center / Right
 * - Simple text menu system on serial terminal
 *
 * Left/Right: navigation and value change
 * Center: select / enter / exit
 */

// --- Hardware pin mapping ----------------------------------------------------

#define PIN_L  16
#define PIN_C  17
#define PIN_R  18

#define ID_L   10
#define ID_C   20
#define ID_R   30

// --- Menu system types -------------------------------------------------------

typedef enum {
    TYPE_SUBMENU,
    TYPE_SETTING,
    TYPE_ACTION
} item_type_t;

struct MenuNode; // Forward declaration

typedef struct {
    const char  *label;
    item_type_t  type;
    union {
        struct MenuNode *child;                  // TYPE_SUBMENU
        struct { int *val; int min; int max; } setting; // TYPE_SETTING
        void (*action)(void);                    // TYPE_ACTION
    } data;
} MenuItem;

typedef struct MenuNode {
    const char      *title;
    const MenuItem  *items;
    int              count;
} MenuNode;

// --- Application data --------------------------------------------------------

int volume    = 50;
int backlight = 80;
int wifi      = 1;

static void act_save(void)   { printf("\n>> SAVING TO FLASH...\n"); sleep_ms(500); }
static void act_reboot(void) { printf("\n>> REBOOTING...\n");      sleep_ms(500); }

// --- Menu tree ---------------------------------------------------------------

// Advanced menu
const MenuItem adv_items[] = {
    { "Backlight", TYPE_SETTING, .data.setting = { &backlight, 0, 100 } },
    { "Reboot",    TYPE_ACTION,  .data.action  = act_reboot             }
};
const MenuNode menu_adv = { "ADVANCED", adv_items, 2 };

// Settings menu
const MenuItem set_items[] = {
    { "Volume",   TYPE_SETTING, .data.setting = { &volume, 0, 100 } },
    { "WiFi Pwr", TYPE_SETTING, .data.setting = { &wifi,   0, 1   } },
    { "System >", TYPE_SUBMENU, .data.child   = (MenuNode*)&menu_adv }
};
const MenuNode menu_set = { "SETTINGS", set_items, 3 };

// Root menu
const MenuItem root_items[] = {
    { "Config >", TYPE_SUBMENU, .data.child  = (MenuNode*)&menu_set },
    { "Save All", TYPE_ACTION,  .data.action = act_save             }
};
const MenuNode menu_root = { "MAIN MENU", root_items, 2 };

// --- Navigation state --------------------------------------------------------

#define MAX_DEPTH 4

typedef struct {
    const MenuNode *node;
    int             cursor;
} NavFrame;

static NavFrame stack[MAX_DEPTH];
static int      sp           = 0;     // Stack pointer
static bool     menu_active  = false;
static bool     edit_mode    = false;
static bool     need_redraw  = true;

// --- Helpers -----------------------------------------------------------------

static void term_cls(void) {
    // Clear screen and move cursor to top-left
    printf("\033[2J\033[H");
}

static bool read_pin(void *arg) {
    return gpio_get((uint)(uintptr_t)arg);
}

// --- Menu logic --------------------------------------------------------------

static void menu_open(void) {
    menu_active = true;
    sp = 0;
    stack[0].node   = &menu_root;
    stack[0].cursor = 0;
    edit_mode       = false;
    need_redraw     = true;
}

static void menu_close(void) {
    menu_active = false;
    need_redraw = true;
    term_cls();
}

static void menu_push(const MenuNode *node) {
    if (sp < MAX_DEPTH - 1) {
        sp++;
        stack[sp].node   = node;
        stack[sp].cursor = 0;
        need_redraw      = true;
    }
}

static void menu_pop(void) {
    if (sp > 0) {
        sp--;
        need_redraw = true;
    } else {
        // Exit from root menu back to dashboard
        menu_close();
    }
}

static void render(void) {
    if (!need_redraw) return;
    need_redraw = false;

    term_cls();

    if (!menu_active) {
        printf("=== DASHBOARD ===\r\n");
        printf(" Status: RUNNING\r\n");
        printf(" Vol: %d%% | WiFi: %s\r\n\r\n",
               volume,
               wifi ? "ON" : "OFF");
        printf(" [HOLD C]: Menu\r\n");
        printf(" [HOLD L+R]: Lock\r\n");
        return;
    }

    NavFrame *frame = &stack[sp];
    printf("--- %s ---\r\n", frame->node->title);

    for (int i = 0; i < frame->node->count; i++) {
        const MenuItem *it = &frame->node->items[i];
        bool selected      = (i == frame->cursor);

        printf(" %c %-12s", selected ? '>' : ' ', it->label);

        if (it->type == TYPE_SETTING) {
            int val = *it->data.setting.val;
            if (selected && edit_mode) {
                printf(" < %d >", val);
            } else {
                printf(" : %d", val);
            }
        }
        printf("\r\n");
    }
    printf("\r\n[L/R]: Nav  [C]: OK  [2xC]: Back\r\n");
}

// --- Button configurations ---------------------------------------------------
// Left/Right: short click timeout for snappy navigation
static const btn_config_t cfg_l = {
    .id = ID_L,
    .active_low = true,
    .read_fn = read_pin,
    .hw_arg = (void*)PIN_L,
    .callback = NULL,
    .cb_user_data = NULL,
    .debounce_ms = 20,
    .click_timeout_ms = 200,
    .long_press_ms = 800,
    .repeat_period_ms = 100
};

static const btn_config_t cfg_r = {
    .id = ID_R,
    .active_low = true,
    .read_fn = read_pin,
    .hw_arg = (void*)PIN_R,
    .callback = NULL,
    .cb_user_data = NULL,
    .debounce_ms = 20,
    .click_timeout_ms = 200,
    .long_press_ms = 800,
    .repeat_period_ms = 100
};

// Center: slightly longer click timeout to make double-click more comfortable
static const btn_config_t cfg_c = {
    .id = ID_C,
    .active_low = true,
    .read_fn = read_pin,
    .hw_arg = (void*)PIN_C,
    .callback = NULL,
    .cb_user_data = NULL,
    .debounce_ms = 20,
    .click_timeout_ms = 300,
    .long_press_ms = 1000,
    .repeat_period_ms = 0
};

static btn_instance_t buttons[3];
static btn_state_t    states[3];
static btn_event_t    queue[32];
static btn_context_t  ctx;

// --- Main --------------------------------------------------------------------

int main(void) {
    stdio_init_all();
    sleep_ms(2000); // Small delay for terminal connection

    gpio_init(PIN_L); gpio_set_dir(PIN_L, GPIO_IN); gpio_pull_up(PIN_L);
    gpio_init(PIN_C); gpio_set_dir(PIN_C, GPIO_IN); gpio_pull_up(PIN_C);
    gpio_init(PIN_R); gpio_set_dir(PIN_R, GPIO_IN); gpio_pull_up(PIN_R);

    btn_init(&ctx, buttons, 3, queue, 32);
    btn_setup(&ctx, 0, &cfg_l, &states[0]);
    btn_setup(&ctx, 1, &cfg_c, &states[1]);
    btn_setup(&ctx, 2, &cfg_r, &states[2]);

    menu_close(); // Start in dashboard mode

    while (true) {
        uint64_t now = time_us_64();
        btn_update(&ctx, now);

        /* Combo: emergency exit / lock (L + R held > 1s) */
        if (btn_is_pressed(&ctx, ID_L) && btn_is_pressed(&ctx, ID_R)) {
            if (btn_get_duration(&ctx, ID_L, now) > 1000000ULL &&
                btn_get_duration(&ctx, ID_R, now) > 1000000ULL) {

                btn_suppress_events(&ctx, ID_L);
                btn_suppress_events(&ctx, ID_R);

                menu_close();
                printf("\n>> LOCKED / HOME <<\n");
                sleep_ms(500);
            }
        }

        /* Process button events */
        btn_event_t evt;
        while (btn_pop_event(&ctx, &evt)) {

            // Dashboard mode: only CENTER long press opens menu
            if (!menu_active) {
                if (evt.btn_id == ID_C &&
                    evt.type == BTN_EVT_LONG_START) {
                    menu_open();
                }
                continue;
            }

            // Menu mode
            NavFrame *frame = &stack[sp];
            const MenuItem *it = &frame->node->items[frame->cursor];

            // 1. Navigation / value editing via L/R
            if (evt.btn_id == ID_L || evt.btn_id == ID_R) {
                int  dir    = (evt.btn_id == ID_R) ? 1 : -1;
                bool active = (evt.type == BTN_EVT_CLICK ||
                               evt.type == BTN_EVT_LONG_HOLD);

                if (active) {
                    if (edit_mode && it->type == TYPE_SETTING) {
                        int *val = it->data.setting.val;
                        int step = (evt.type == BTN_EVT_LONG_HOLD) ? 5 : 1;

                        *val += dir * step;
                        if (*val > it->data.setting.max) *val = it->data.setting.max;
                        if (*val < it->data.setting.min) *val = it->data.setting.min;
                    } else if (!edit_mode) {
                        frame->cursor += dir;
                        if (frame->cursor < 0)                      frame->cursor = frame->node->count - 1;
                        if (frame->cursor >= frame->node->count)   frame->cursor = 0;
                    }
                    need_redraw = true;
                }
            }

            // 2. Selection / back via CENTER
            if (evt.btn_id == ID_C) {
                if (evt.type == BTN_EVT_CLICK && evt.clicks == 1) {
                    if (edit_mode) {
                        edit_mode = false; // Confirm value
                    } else {
                        if (it->type == TYPE_SUBMENU) {
                            menu_push(it->data.child);
                        } else if (it->type == TYPE_SETTING) {
                            edit_mode = true;
                        } else if (it->type == TYPE_ACTION) {
                            if (it->data.action) it->data.action();
                        }
                    }
                    need_redraw = true;
                } else if (evt.type == BTN_EVT_CLICK && evt.clicks == 2) {
                    // Double click: leave setting or go up in menu
                    if (edit_mode) {
                        edit_mode = false;
                    } else {
                        menu_pop();
                    }
                    need_redraw = true;
                }
            }
        }

        render();
        sleep_ms(5);
    }
}
