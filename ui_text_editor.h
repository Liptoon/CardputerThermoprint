#pragma once
#include <Arduino.h>

// Full-screen text editor.
// Enter        = confirm/print and return true.
// Fn + `       = Escape / cancel, return false (back to menu).
// Opt + Enter  = insert newline.
// Backspace    = delete char before cursor.
// Fn + ;       = cursor up one display line.
// Fn + .       = cursor down one display line.
// Fn + ,       = cursor left one character.
// Fn + /       = cursor right one character.
// All other printable keys (including ; , . / `) type normally.
// text_buf: caller-provided buffer. max_len includes null terminator.
bool text_editor_run(char* text_buf, int max_len);
