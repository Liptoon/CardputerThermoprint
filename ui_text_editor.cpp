#include <Arduino.h>
#include <M5Cardputer.h>
#include "ui_text_editor.h"

static constexpr int SCREEN_W  = 240;
static constexpr int SCREEN_H  = 135;
static constexpr int TEXT_SIZE = 1;
static constexpr int LINE_H    = 9;
static constexpr int CHAR_W    = 6;
static constexpr int MAX_COLS  = SCREEN_W / CHAR_W;  // 40
static constexpr int STATUS_H  = LINE_H;

static void draw_editor(const char* buf, int len, int cursor, int display_start)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(TEXT_SIZE);

    // Status bar.
    M5.Display.fillRect(0, SCREEN_H - STATUS_H, SCREEN_W, STATUS_H, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setCursor(0, SCREEN_H - STATUS_H + 1);
    M5.Display.print("Enter=print  Esc=back  Opt+Enter=newline");

    // Text area.
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    int screen_lines = (SCREEN_H - STATUS_H) / LINE_H;
    int line = 0;
    int col  = 0;

    for (int i = 0; i <= len; i++) {
        bool is_cursor = (i == cursor);
        char ch = (i < len) ? buf[i] : '\0';

        if (line - display_start >= screen_lines) break;

        if (line >= display_start) {
            int draw_line = line - display_start;
            int x = col * CHAR_W;
            int y = draw_line * LINE_H;

            if (is_cursor) {
                M5.Display.fillRect(x, y, CHAR_W, LINE_H, TFT_WHITE);
                M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
                M5.Display.drawChar((ch && ch != '\n') ? ch : ' ', x, y);
                M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            } else if (ch && ch != '\n') {
                M5.Display.drawChar(ch, x, y);
            }
        }

        if (ch == '\n' || col >= MAX_COLS - 1) {
            col = 0;
            line++;
        } else if (ch != '\0') {
            col++;
        }
    }
}

bool text_editor_run(char* text_buf, int max_len)
{
    if (!text_buf || max_len < 2) return false;

    int  len           = strlen(text_buf);
    int  cursor        = len;
    int  display_start = 0;
    bool needs_redraw  = true;

    auto cursor_line = [&]() -> int {
        int line = 0, col = 0;
        for (int i = 0; i < cursor; i++) {
            if (text_buf[i] == '\n' || col >= MAX_COLS - 1) {
                line++; col = 0;
            } else {
                col++;
            }
        }
        return line;
    };

    while (true) {
        if (needs_redraw) {
            draw_editor(text_buf, len, cursor, display_start);
            needs_redraw = false;
        }

        M5Cardputer.update();
        if (!M5Cardputer.Keyboard.isChange()) {
            delay(20);
            continue;
        }
        if (!M5Cardputer.Keyboard.isPressed()) {
            needs_redraw = true;
            continue;
        }

        auto ks = M5Cardputer.Keyboard.keysState();

        if (ks.opt && ks.enter) {
            // Opt+Enter = insert newline.
            if (len < max_len - 1) {
                memmove(text_buf + cursor + 1, text_buf + cursor, len - cursor);
                text_buf[cursor] = '\n';
                len++;
                cursor++;
            }
            needs_redraw = true;

        } else if (ks.enter) {
            // Enter = confirm/print.
            text_buf[len] = '\0';
            return true;

        } else if (ks.del) {
            // Backspace.
            if (cursor > 0) {
                memmove(text_buf + cursor - 1, text_buf + cursor, len - cursor);
                len--;
                cursor--;
                text_buf[len] = '\0';
            }
            needs_redraw = true;

        } else if (!ks.word.empty()) {
            char c = ks.word[0];
            // Backtick = back to menu.
            if (c == '`') {
                return false;
            }
            // Exclude ; and . (arrow keys) from typed text.
            if (c != ';' && c != '.' && c >= 0x20 && c < 0x7F && len < max_len - 1) {
                memmove(text_buf + cursor + 1, text_buf + cursor, len - cursor);
                text_buf[cursor] = c;
                len++;
                cursor++;
                needs_redraw = true;
            }
        }

        if (needs_redraw) {
            text_buf[len] = '\0';
            int cl = cursor_line();
            int screen_lines = (SCREEN_H - STATUS_H) / LINE_H;
            if (cl < display_start)
                display_start = cl;
            if (cl >= display_start + screen_lines)
                display_start = cl - screen_lines + 1;
        }
    }
}
