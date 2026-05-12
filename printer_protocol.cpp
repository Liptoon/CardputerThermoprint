#include <Arduino.h>
#include "printer_protocol.h"
#include <esp_heap_caps.h>

bool binimage_alloc(BinImage& img, int width, int height)
{
    size_t rb  = (width + 7) / 8;
    size_t len = rb * height;
    uint8_t* p = (uint8_t*)heap_caps_calloc(len, 1, MALLOC_CAP_SPIRAM);
    if (!p) {
        Serial.printf("[IMG] PSRAM alloc failed (%u bytes), trying SRAM\n", (unsigned)len);
        p = (uint8_t*)calloc(len, 1);
    }
    if (!p) {
        Serial.printf("[IMG] alloc failed: %u bytes\n", (unsigned)len);
        return false;
    }
    img.width    = width;
    img.height   = height;
    img.data     = p;
    img.data_len = len;
    return true;
}
