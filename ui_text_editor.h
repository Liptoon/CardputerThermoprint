#pragma once
#include <Arduino.h>

// Full-screen text editor.
// Enter        = confirm/print and return true.
// Esc (`)      = cancel, return false (back to menu).
// Opt+Enter    = insert newline.
// Backspace    = delete char before cursor.
// ; and .      = reserved for menu navigation, filtered out of typed text.
// text_buf: caller-provided buffer. max_len includes null terminator.
bool text_editor_run(char* text_buf, int max_len);
