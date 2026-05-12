#pragma once
#include <Arduino.h>

// Display a menu and handle Up/Down/Enter navigation.
// title: shown on first line (highlighted).
// items: array of item strings, count items.
// Returns selected index (0-based), or -1 if user pressed Backspace/Esc.
int menu_run(const char* title,
             const char* const* items,
             int count);
