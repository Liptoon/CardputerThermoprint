#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// BinImage - 1-bit packed bitmap in PSRAM.
// Row stride = (width + 7) / 8 bytes.
// MSB of first byte = leftmost pixel (matches printer wire format).
// Caller must call free_data() when done.
// ---------------------------------------------------------------------------
struct BinImage {
    int      width    = 0;
    int      height   = 0;
    uint8_t* data     = nullptr;
    size_t   data_len = 0;

    int row_bytes() const { return (width + 7) / 8; }

    void free_data() {
        if (data) { free(data); data = nullptr; }
        width = height = 0;
        data_len = 0;
    }

    bool valid() const { return data != nullptr && width > 0 && height > 0; }
};

// Allocate a zeroed BinImage in PSRAM.
// Returns false if allocation fails.
bool binimage_alloc(BinImage& img, int width, int height);

// Set a single pixel (1=black, 0=white).
inline void binimage_set_pixel(BinImage& img, int x, int y, bool black)
{
    if (x < 0 || x >= img.width || y < 0 || y >= img.height) return;
    int byte_idx = y * img.row_bytes() + x / 8;
    uint8_t mask = 0x80 >> (x & 7);
    if (black) img.data[byte_idx] |=  mask;
    else        img.data[byte_idx] &= ~mask;
}

inline bool binimage_get_pixel(const BinImage& img, int x, int y)
{
    if (x < 0 || x >= img.width || y < 0 || y >= img.height) return false;
    int byte_idx = y * img.row_bytes() + x / 8;
    uint8_t mask = 0x80 >> (x & 7);
    return (img.data[byte_idx] & mask) != 0;
}
