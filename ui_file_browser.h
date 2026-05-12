#pragma once
#include <Arduino.h>

// Browse SD root directory, filter by extension list.
// extensions: null-terminated array, e.g. {".jpg",".png",".bmp",".txt",nullptr}
// selected_path: output buffer for selected file path (including leading /).
// path_buf_len: size of selected_path buffer.
// Returns true if file was selected, false if cancelled.
bool file_browser_run(const char* const* extensions,
                       char* selected_path,
                       int   path_buf_len);
