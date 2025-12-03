#include <stdio.h>
#include "pico/stdlib.h"
#include "buttonlib.h"

// --- Hardware ---
#define PIN_L  16
#define PIN_C  17
#define PIN_R  18
#define ID_L   10
#define ID_C   20
#define ID_R   30

// --- Menu System ---

typedef enum { TYPE_SUBMENU, TYPE_SETTING, TYPE_ACTION } item_type_t;
struct MenuNode; // Forward decl

typedef struct {
    const char *label;
    item_type_t type;
    union {
        struct MenuNode *child;         // SUBMENU
        struct { int *val; int min; int max; } setting; // SETTING
        void (*action)(void);           // ACTION
    } data;
} MenuItem;

typedef struct MenuNode {
    const char *title;
    const MenuItem *items;
    int count;
} MenuNode;

// --- App Data ---
int volume = 50;
int backlight = 80;
int wifi = 1;

void act_save() { printf("\n>> SAVING TO FLASH...\n"); sleep_ms(500); }
void act_reboot() { printf("\n>> REBOOTING...\n"); sleep_ms(500); }

// --- Menu Tree ---

// Advanced
const MenuItem adv_items[] = {
    {"Backlight", TYPE_SETTING, .data.setting = {&backlight, 0, 100}},
    {"Reboot",    TYPE_ACTION,  .data.action = act_reboot}
};
const MenuNode menu_adv = { "ADVANCED", adv_items, 2 };

// Settings
const MenuItem set_items[] = {
    {"Volume",   TYPE_SETTING, .data.setting = {&volume, 0, 100}},
    {"WiFi Pwr", TYPE_SETTING, .data.setting = {&wifi, 0, 1}},
    {"System >", TYPE_SUBMENU, .data.child = (MenuNode*)&menu_adv}
};
const MenuNode menu_set = { "SETTINGS", set_items, 3 };

// Root
const MenuItem root_items[] = {
    {"Config >", TYPE_SUBMENU, .data.child = (MenuNode*)&menu_set},
    {"Save All", TYPE_ACTION,  .data.action = act_save}
};
const MenuNode menu_root = { "MAIN MENU", root_items, 2 };

// --- Nav State ---
#define MAX_DEPTH 4
typedef struct {
    const MenuNode *node;
    int cursor;
} NavFrame;

NavFrame stack[MAX_DEPTH];
int sp = 0; // Stack pointer
bool menu_active = false;
bool edit_mode = false;
bool need_redraw = true;

// --- Helpers ---
void term_cls() { printf("\033[2J\033[H"); }
bool read_pin(void *arg) { return gpio_get((uint)(uintptr_t)arg); }

// --- Logic ---

void menu_open() {
    menu_active = true;
    sp = 0;
    stack[0].node = &menu_root;
    stack[0].cursor = 0;
    edit_mode = false;
    need_redraw = true;
}

void menu_close() {
    menu_active = false;
    need_redraw = true;
    term_cls();
}

void menu_push(const MenuNode *node) {
    if (sp < MAX_DEPTH - 1) {
        sp++;
        stack[sp].node = node;
        stack[sp].cursor = 0;
        need_redraw = true;
    }
}

void menu_pop() {
    if (sp > 0) {
        sp--;
        need_redraw = true;
    } else {
        menu_close(); // Exit from root
    }
}

void render() {
    if (!need_redraw) return;
    need_redraw = false;

    term_cls();
    if (!menu_active) {
        printf("=== DASHBOARD ===\r\n");
        printf(" Status: RUNNING\r\n");
        printf(" Vol: %d%% | WiFi: %s\r\n\r\n", volume, wifi ? "ON" : "OFF");
        printf(" [HOLD C]: Menu\r\n");
        printf(" [HOLD L+R]: Lock\r\n");
        return;
    }

    NavFrame *frame = &stack[sp];
    printf("--- %s ---\r\n", frame->node->title);
    
    for (int i=0; i < frame->node->count; i++) {
        const MenuItem *it = &frame->node->items[i];
        bool sel = (i == frame->cursor);
        
        printf(" %c %-12s", sel ? '>' : ' ', it->label);
        
        if (it->type == TYPE_SETTING) {
            int val = *it->data.setting.val;
            if (sel && edit_mode) printf(" < %d >", val);
            else printf(" : %d", val);
        }
        printf("\r\n");
    }
    printf("\r\n[L/R]:Nav [C]:Ok [2xC]:Back\r\n");
}

// --- Configs ---
// L/R: Быстрый таймаут клика (200мс) для шустрой навигации
const btn_config_t cfg_l = { ID_L, true, read_pin, (void*)PIN_L, NULL, NULL, 20, 200, 800, 100 };
const btn_config_t cfg_r = { ID_R, true, read_pin, (void*)PIN_R, NULL, NULL, 20, 200, 800, 100 };
// Center: Чуть больше таймаут (300мс), чтобы комфортно делать Double Click
const btn_config_t cfg_c = { ID_C, true, read_pin, (void*)PIN_C, NULL, NULL, 20, 300, 1000, 0 };

btn_instance_t buttons[3];
btn_state_t states[3];
btn_event_t queue[32];
btn_context_t ctx;

int main() {
    stdio_init_all();
    sleep_ms(2000);

    gpio_init(PIN_L); gpio_set_dir(PIN_L, GPIO_IN); gpio_pull_up(PIN_L);
    gpio_init(PIN_C); gpio_set_dir(PIN_C, GPIO_IN); gpio_pull_up(PIN_C);
    gpio_init(PIN_R); gpio_set_dir(PIN_R, GPIO_IN); gpio_pull_up(PIN_R);

    btn_init(&ctx, buttons, 3, queue, 32);
    btn_setup(&ctx, 0, &cfg_l, &states[0]);
    btn_setup(&ctx, 1, &cfg_c, &states[1]);
    btn_setup(&ctx, 2, &cfg_r, &states[2]);

    menu_close(); // Start in Dashboard

    while(1) {
        uint64_t now = time_us_64();
        btn_update(&ctx, now);

        // --- COMBO: Emergency Exit / Lock (L + R > 1s) ---
        if (btn_is_pressed(&ctx, ID_L) && btn_is_pressed(&ctx, ID_R)) {
            if (btn_get_duration(&ctx, ID_L, now) > 1000000 &&
                btn_get_duration(&ctx, ID_R, now) > 1000000) {
                
                btn_suppress_events(&ctx, ID_L);
                btn_suppress_events(&ctx, ID_R);
                
                menu_close();
                printf("\n>> LOCKED / HOME <<\n");
                sleep_ms(500);
            }
        }

        btn_event_t evt;
        while(btn_pop_event(&ctx, &evt)) {
            // Dashboard Mode
            if (!menu_active) {
                if (evt.btn_id == ID_C && evt.type == BTN_EVT_LONG_START) {
                    menu_open();
                }
                continue;
            }

            // Menu Mode
            NavFrame *frame = &stack[sp];
            const MenuItem *it = &frame->node->items[frame->cursor];

            // 1. Navigation / Editing
            if (evt.btn_id == ID_L || evt.btn_id == ID_R) {
                int dir = (evt.btn_id == ID_R) ? 1 : -1;
                bool is_act = (evt.type == BTN_EVT_CLICK || evt.type == BTN_EVT_LONG_HOLD);

                if (is_act) {
                    if (edit_mode && it->type == TYPE_SETTING) {
                        int *val = it->data.setting.val;
                        int step = (evt.type == BTN_EVT_LONG_HOLD) ? 5 : 1;
                        *val += dir * step;
                        if (*val > it->data.setting.max) *val = it->data.setting.max;
                        if (*val < it->data.setting.min) *val = it->data.setting.min;
                    } else if (!edit_mode) {
                        frame->cursor += dir;
                        if (frame->cursor < 0) frame->cursor = frame->node->count - 1;
                        if (frame->cursor >= frame->node->count) frame->cursor = 0;
                    }
                    need_redraw = true;
                }
            }

            // 2. Selection / Back
            if (evt.btn_id == ID_C) {
                if (evt.type == BTN_EVT_CLICK && evt.clicks == 1) {
                    if (edit_mode) {
                        edit_mode = false; // Confirm
                    } else {
                        if (it->type == TYPE_SUBMENU) menu_push(it->data.child);
                        else if (it->type == TYPE_SETTING) edit_mode = true;
                        else if (it->type == TYPE_ACTION) if (it->data.action) it->data.action();
                    }
                    need_redraw = true;
                }
                else if (evt.type == BTN_EVT_CLICK && evt.clicks == 2) {
                    if (edit_mode) edit_mode = false;
                    else menu_pop();
                }
            }
        }

        render();
        sleep_ms(5);
    }
}