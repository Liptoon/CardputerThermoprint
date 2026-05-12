#include <Arduino.h>
#include <M5Cardputer.h>
#include "ui_menu.h"

// Display parameters.
static constexpr int SCREEN_W     = 240;
static constexpr int SCREEN_H     = 135;
static constexpr int TITLE_H      = 10; // text size 1 = 8px tall + 2px padding
static constexpr int ITEM_SIZE    = 2;
static constexpr int LINE_H       = 16; // pixels per line at text size 2
static constexpr int MAX_VISIBLE  = (SCREEN_H - TITLE_H) / LINE_H; // 7

int menu_run(const char* title,
             const char* const* items,
             int count)
{
    if (count <= 0) return -1;

    int selected = 0;
    int scroll   = 0;

    auto draw = [&]() {
        M5.Display.fillScreen(TFT_BLACK);

        // Title bar - small font so long strings (MAC address) fit.
        M5.Display.fillRect(0, 0, SCREEN_W, TITLE_H, TFT_WHITE);
        M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
        M5.Display.setTextSize(1);
        M5.Display.setCursor(2, 1);
        M5.Display.print(title);

        // Items - larger font for readability.
        M5.Display.setTextSize(ITEM_SIZE);
        int visible = (count - scroll < MAX_VISIBLE) ? count - scroll : MAX_VISIBLE;
        for (int i = 0; i < visible; i++) {
            int idx = scroll + i;
            int y   = TITLE_H + i * LINE_H;
            if (idx == selected) {
                M5.Display.fillRect(0, y, SCREEN_W, LINE_H, TFT_BLUE);
                M5.Display.setTextColor(TFT_WHITE, TFT_BLUE);
            } else {
                M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            }
            M5.Display.setCursor(4, y + 1);
            M5.Display.print(items[idx]);
        }

        // Scroll indicator.
        if (count > MAX_VISIBLE) {
            M5.Display.setTextSize(1);
            M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
            M5.Display.setCursor(SCREEN_W - 24, TITLE_H + 2);
            M5.Display.printf("%d/%d", selected + 1, count);
        }
    };

    draw();

    while (true) {
        // Single update call per iteration - do not call keyPressed() here
        // as it also calls M5Cardputer.update(), causing double-update.
        M5Cardputer.update();

        if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
            delay(20);
            continue;
        }

        auto ks = M5Cardputer.Keyboard.keysState();

        // Confirm.
        if (ks.enter) {
            return selected;
        }

        // Cancel / back.
        if (ks.del) {
            return -1;
        }

        // Navigate: iterate word chars like community examples do.
        // ;  = up (arrow up on Cardputer physical layout)
        // .  = down (arrow down)
        // Enter = confirm, Del = back.
        bool go_up   = false;
        bool go_down = false;
        for (auto k : ks.word) {
            if (k == ';') go_up   = true;
            if (k == '.') go_down = true;
        }

        if (go_up) {
            if (selected > 0) {
                selected--;
                if (selected < scroll) scroll = selected;
                draw();
            }
        } else if (go_down) {
            if (selected < count - 1) {
                selected++;
                if (selected >= scroll + MAX_VISIBLE)
                    scroll = selected - MAX_VISIBLE + 1;
                draw();
            }
        }
    }
}
