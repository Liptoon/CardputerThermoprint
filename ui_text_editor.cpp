#include <Arduino.h>
#include <M5Cardputer.h>
#include "ui_text_editor.h"

static constexpr int SCREEN_W  = 240;
static constexpr int SCREEN_H  = 135;
static constexpr int TEXT_SIZE = 1;
static constexpr int LINE_H    = 9;
static constexpr int CHAR_W    = 6;
static constexpr int MAX_COLS  = SCREEN_W / CHAR_W;  // 40
static constexpr int STATUS_H  = LINE_H * 2;  // two-row status bar

// ---------------------------------------------------------------------------
// UTF-8 helpers
// ---------------------------------------------------------------------------

// Decode one UTF-8 sequence at buf[*i], advance *i, return codepoint.
static uint32_t decode_utf8_at(const char* buf, int* i)
{
    uint8_t c = (uint8_t)buf[*i];
    if (c < 0x80) { (*i)++; return c; }
    if ((c & 0xE0) == 0xC0 && (uint8_t)buf[*i+1] >= 0x80) {
        uint32_t cp = ((c & 0x1F) << 6) | ((uint8_t)buf[*i+1] & 0x3F);
        (*i) += 2; return cp;
    }
    if ((c & 0xF0) == 0xE0 && (uint8_t)buf[*i+1] >= 0x80 && (uint8_t)buf[*i+2] >= 0x80) {
        uint32_t cp = ((c & 0x0F) << 12) | (((uint8_t)buf[*i+1] & 0x3F) << 6)
                    | ((uint8_t)buf[*i+2] & 0x3F);
        (*i) += 3; return cp;
    }
    (*i)++; return '?';
}

// Map codepoint to a displayable ASCII char.
// Polish letters are substituted with their base Latin equivalents.
static char codepoint_to_display(uint32_t cp)
{
    if (cp < 0x80) return (char)cp;
    switch (cp) {
        case 0x00D3: return 'O'; // Ó
        case 0x00F3: return 'o'; // ó
        case 0x0104: return 'A'; // Ą
        case 0x0105: return 'a'; // ą
        case 0x0106: return 'C'; // Ć
        case 0x0107: return 'c'; // ć
        case 0x0118: return 'E'; // Ę
        case 0x0119: return 'e'; // ę
        case 0x0141: return 'L'; // Ł
        case 0x0142: return 'l'; // ł
        case 0x0143: return 'N'; // Ń
        case 0x0144: return 'n'; // ń
        case 0x015A: return 'S'; // Ś
        case 0x015B: return 's'; // ś
        case 0x0179: return 'Z'; // Ź
        case 0x017A: return 'z'; // ź
        case 0x017B: return 'Z'; // Ż
        case 0x017C: return 'z'; // ż
        default:     return '?';
    }
}

// ---------------------------------------------------------------------------
// draw_editor: renders buffer contents to display.
// Iterates by UTF-8 character; Polish chars shown as Latin equivalents.
// cursor is a byte index into buf.
// ---------------------------------------------------------------------------
static void draw_editor(const char* buf, int len, int cursor, int display_start)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(TEXT_SIZE);

    // Status bar - two rows.
    M5.Display.fillRect(0, SCREEN_H - STATUS_H, SCREEN_W, STATUS_H, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setCursor(0, SCREEN_H - STATUS_H + 1);
    M5.Display.print("Enter=print  Fn+`=back  Opt+Enter=NL");
    M5.Display.setCursor(0, SCREEN_H - LINE_H + 1);
    M5.Display.print("Fn+; . , /  = up dn lt rt");

    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    int screen_lines = (SCREEN_H - STATUS_H) / LINE_H;
    int line = 0;
    int col  = 0;
    int i    = 0;

    while (i <= len) {
        bool is_cursor = (i == cursor);

        // Decode one character (or handle end-of-buffer cursor).
        uint32_t cp = 0;
        if (i < len) {
            int prev_i = i;
            cp = decode_utf8_at(buf, &i);
            (void)prev_i;
        } else {
            i++; // step past end for cursor-at-end rendering
        }

        if (line - display_start >= screen_lines) break;

        if (line >= display_start) {
            int draw_line = line - display_start;
            int x = col * CHAR_W;
            int y = draw_line * LINE_H;

            char dch = (cp && cp != (uint32_t)'\n') ? codepoint_to_display(cp) : ' ';

            if (is_cursor) {
                M5.Display.fillRect(x, y, CHAR_W, LINE_H, TFT_WHITE);
                M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
                M5.Display.drawChar(dch, x, y);
                M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            } else if (cp && cp != (uint32_t)'\n') {
                M5.Display.drawChar(dch, x, y);
            }
        }

        // Advance layout.
        if (cp == (uint32_t)'\n' || col >= MAX_COLS - 1) {
            col = 0; line++;
        } else if (cp != 0) {
            col++;
        }
    }
}

// ---------------------------------------------------------------------------
// text_editor_run
// ---------------------------------------------------------------------------
bool text_editor_run(char* text_buf, int max_len)
{
    if (!text_buf || max_len < 2) return false;

    int  len           = (int)strlen(text_buf);
    int  cursor        = len;
    int  display_start = 0;
    bool needs_redraw  = true;

    // cursor_line: returns the display line number of the current cursor.
    // Operates in visual (character) space, skipping UTF-8 continuation bytes.
    auto cursor_line = [&]() -> int {
        int line = 0, col = 0, i = 0;
        while (i < cursor) {
            uint32_t cp = decode_utf8_at(text_buf, &i);
            if (i > cursor) { i = cursor; break; } // don't overshoot
            if (cp == '\n' || col >= MAX_COLS - 1) { line++; col = 0; }
            else col++;
        }
        return line;
    };

    while (true) {
        if (needs_redraw) {
            draw_editor(text_buf, len, cursor, display_start);
            needs_redraw = false;
        }

        M5Cardputer.update();
        if (!M5Cardputer.Keyboard.isChange()) { delay(20); continue; }
        if (!M5Cardputer.Keyboard.isPressed()) { needs_redraw = true; continue; }

        auto ks = M5Cardputer.Keyboard.keysState();

        // ------------------------------------
        // Fn layer: navigation
        // ------------------------------------
        if (ks.fn) {
            bool handled = true;
            char nav_key = ks.word.empty() ? 0 : ks.word[0];

            if (nav_key == '`') {
                return false; // Fn+` = back to menu

            } else if (nav_key == ',') {
                // Fn+, = left: step back one full UTF-8 character.
                if (cursor > 0) {
                    cursor--;
                    while (cursor > 0 && ((uint8_t)text_buf[cursor] & 0xC0) == 0x80)
                        cursor--;
                }
                needs_redraw = true;

            } else if (nav_key == '/') {
                // Fn+/ = right: step forward one full UTF-8 character.
                if (cursor < len) {
                    cursor++;
                    while (cursor < len && ((uint8_t)text_buf[cursor] & 0xC0) == 0x80)
                        cursor++;
                }
                needs_redraw = true;

            } else if (nav_key == ';') {
                // Fn+; = up: same visual column on previous display line.
                // Measure current visual line and column.
                int cur_line = 0, cur_col = 0, line = 0, col = 0, bi = 0;
                while (bi < cursor) {
                    int prev = bi;
                    uint32_t cp = decode_utf8_at(text_buf, &bi);
                    if (bi > cursor) { bi = cursor; break; }
                    if (cp == '\n' || col >= MAX_COLS - 1) { line++; col = 0; }
                    else col++;
                }
                cur_line = line; cur_col = col;

                if (cur_line > 0) {
                    int target = cur_line - 1;
                    // Find byte start of target line.
                    int line_start = 0;
                    line = 0; col = 0; bi = 0;
                    while (bi < cursor) {
                        int prev = bi;
                        if (line == target && col == 0) { line_start = prev; break; }
                        uint32_t cp = decode_utf8_at(text_buf, &bi);
                        if (cp == '\n' || col >= MAX_COLS - 1) {
                            line++; col = 0;
                            if (line == target) {
                                line_start = (cp == '\n') ? bi : prev;
                                break;
                            }
                        } else col++;
                    }
                    // Advance cur_col visual chars on target line.
                    int nc = line_start, adv = 0;
                    while (nc < len && adv < cur_col) {
                        if (text_buf[nc] == '\n') break;
                        int prev = nc;
                        decode_utf8_at(text_buf, &nc);
                        adv++;
                    }
                    cursor = nc;
                }
                needs_redraw = true;

            } else if (nav_key == '.') {
                // Fn+. = down: same visual column on next display line.
                int cur_col = 0, line = 0, col = 0, bi = 0;
                while (bi < cursor) {
                    uint32_t cp = decode_utf8_at(text_buf, &bi);
                    if (bi > cursor) break;
                    if (cp == '\n' || col >= MAX_COLS - 1) { line++; col = 0; }
                    else col++;
                }
                cur_col = col;

                // Find end of current display line.
                int pos = cursor, c2 = cur_col;
                while (pos < len && text_buf[pos] != '\n' && c2 < MAX_COLS - 1) {
                    int prev = pos;
                    decode_utf8_at(text_buf, &pos);
                    c2++;
                }
                if (pos < len) {
                    if (text_buf[pos] == '\n') pos++;
                    int nc = pos, adv = 0;
                    while (nc < len && adv < cur_col) {
                        if (text_buf[nc] == '\n') break;
                        decode_utf8_at(text_buf, &nc);
                        adv++;
                    }
                    cursor = nc;
                }
                needs_redraw = true;

            } else if (ks.enter) {
                text_buf[len] = '\0';
                return true;
            } else {
                handled = false;
            }

            if (handled) {
                if (needs_redraw) {
                    text_buf[len] = '\0';
                    int cl = cursor_line();
                    int screen_lines = (SCREEN_H - STATUS_H) / LINE_H;
                    if (cl < display_start) display_start = cl;
                    if (cl >= display_start + screen_lines)
                        display_start = cl - screen_lines + 1;
                }
                continue;
            }
        }

        // ------------------------------------
        // Non-Fn keys
        // ------------------------------------
        if (ks.opt && ks.enter) {
            // Opt+Enter = insert newline.
            if (len < max_len - 1) {
                memmove(text_buf + cursor + 1, text_buf + cursor, len - cursor);
                text_buf[cursor] = '\n';
                len++; cursor++;
            }
            needs_redraw = true;

        } else if (ks.enter) {
            text_buf[len] = '\0';
            return true;

        } else if (ks.del) {
            // Backspace: delete one full UTF-8 character before cursor.
            if (cursor > 0) {
                int del_end = cursor;
                cursor--;
                while (cursor > 0 && ((uint8_t)text_buf[cursor] & 0xC0) == 0x80)
                    cursor--;
                int del_bytes = del_end - cursor;
                memmove(text_buf + cursor, text_buf + del_end, len - del_end);
                len -= del_bytes;
                text_buf[len] = '\0';
            }
            needs_redraw = true;

        } else if (!ks.word.empty()) {
            // Insert all bytes from ks.word (handles multi-byte UTF-8).
            int bytes = (int)ks.word.size();
            if (len + bytes < max_len) {
                memmove(text_buf + cursor + bytes, text_buf + cursor, len - cursor);
                for (int bi = 0; bi < bytes; bi++)
                    text_buf[cursor + bi] = ks.word[bi];
                len    += bytes;
                cursor += bytes;
                text_buf[len] = '\0';
                needs_redraw = true;
            }
        }

        if (needs_redraw) {
            text_buf[len] = '\0';
            int cl = cursor_line();
            int screen_lines = (SCREEN_H - STATUS_H) / LINE_H;
            if (cl < display_start) display_start = cl;
            if (cl >= display_start + screen_lines)
                display_start = cl - screen_lines + 1;
        }
    }
}
