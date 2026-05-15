#pragma once
#include <Arduino.h>
#include "printer_protocol.h"

// ---------------------------------------------------------------------------
// Render UTF-8 text to a print-ready BinImage.
// Supports ASCII and Polish characters (U+0020..U+017E, font-dependent).
//
// For Fischero landscape:
//   target_w = 96, target_h = 240 (30mm label)
//   rotate   = true
//
// For Cat roll:
//   target_w = 384, target_h = 0 (grows with content)
//   rotate   = false
//
// font_size_idx: 0..6 (maps to FONT_METRICS[] in font_data.h)
// Caller must call out.free_data() when done.
// ---------------------------------------------------------------------------
bool render_text_label(const char* text,
                        int target_w, int target_h,
                        int font_size_idx,
                        bool rotate,
                        BinImage& out);

// Load a text file from SD into a PSRAM-allocated buffer.
// buf is allocated by this function; caller must free() it.
// max_bytes: cap on file size read.
// Returns nullptr on failure.
char* load_text_file_sd(const char* path, size_t max_bytes = 8192);

// Overload that reads from an already-open File object.
// Caller is responsible for closing the file after this returns.
char* load_text_file_sd(File& f, size_t max_bytes = 8192);
