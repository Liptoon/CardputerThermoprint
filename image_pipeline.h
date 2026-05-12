#pragma once
#include <Arduino.h>
#include "printer_protocol.h"

// Dithering algorithms.
enum class Dither { FloydSteinberg, Atkinson, MeanThreshold, None };

// ---------------------------------------------------------------------------
// Load an image from SD card, resize to fit target_w x target_h,
// apply dithering, rotate 90 CW (for Fischero landscape),
// center on target_w x target_h, pack to 1bpp BinImage in PSRAM.
//
// target_w x target_h: final canvas size (e.g. 96 x 240 for Fischero 30mm).
// rotate: true = rotate 90 CW before centering (Fischero landscape).
//
// Caller must call img.free_data() when done.
// Returns false on error (file not found, alloc failure, decode error).
// ---------------------------------------------------------------------------
bool load_image_from_sd(const char* path,
                         int target_w, int target_h,
                         Dither dither, bool rotate,
                         BinImage& out);

// Overload that reads from an already-open File object.
bool load_image_from_sd(File& f,
                         int target_w, int target_h,
                         Dither dither, bool rotate,
                         BinImage& out);

// ---------------------------------------------------------------------------
// Rotate a BinImage 90 degrees clockwise.
// Returns new BinImage (PSRAM allocated). Caller frees both.
// ---------------------------------------------------------------------------
bool binimage_rotate_90cw(const BinImage& src, BinImage& out);

// ---------------------------------------------------------------------------
// Center src on a target_w x target_h canvas (zero-padded).
// Returns new BinImage (PSRAM allocated). Caller frees both.
// ---------------------------------------------------------------------------
bool binimage_center(const BinImage& src, int target_w, int target_h,
                     BinImage& out);
