#include <Arduino.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include "image_pipeline.h"

// Redirect stb_image allocations - try PSRAM, fall back to SRAM.
static void* stbi_malloc_impl(size_t sz) {
    void* p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!p) p = malloc(sz);
    return p;
}
static void* stbi_realloc_impl(void* p, size_t sz) {
    void* np = heap_caps_realloc(p, sz, MALLOC_CAP_SPIRAM);
    if (!np) np = realloc(p, sz);
    return np;
}
#define STBI_MALLOC(sz)         stbi_malloc_impl(sz)
#define STBI_REALLOC(p,sz)      stbi_realloc_impl(p,sz)
#define STBI_FREE(p)            free(p)
#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static void* stbir_malloc_impl(size_t sz, void*) {
    void* p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!p) p = malloc(sz);
    return p;
}
#define STBIR_MALLOC(sz,ctx)    stbir_malloc_impl(sz,ctx)
#define STBIR_FREE(p,ctx)       free(p)
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

// ---------------------------------------------------------------------------
// Internal float grayscale buffer in PSRAM.
// ---------------------------------------------------------------------------
struct GrayBuf {
    float* px   = nullptr;
    int    w    = 0;
    int    h    = 0;

    bool alloc(int width, int height) {
        size_t sz = (size_t)width * height * sizeof(float);
        px = (float*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
        if (!px) {
            Serial.printf("[IMG] GrayBuf PSRAM failed (%u bytes), trying SRAM\n", (unsigned)sz);
            px = (float*)malloc(sz);
        }
        if (!px) return false;
        w = width; h = height;
        return true;
    }
    void free_buf() { if (px) { free(px); px = nullptr; } }

    float& at(int y, int x)       { return px[y * w + x]; }
    float  at(int y, int x) const { return px[y * w + x]; }

    void clamp_add(int y, int x, float delta) {
        if (y < 0 || y >= h || x < 0 || x >= w) return;
        float v = at(y, x) + delta;
        at(y, x) = (v < 0.f) ? 0.f : (v > 255.f) ? 255.f : v;
    }
};

// ---------------------------------------------------------------------------
// Dither algorithms
// ---------------------------------------------------------------------------
static void dither_floyd(GrayBuf& buf)
{
    for (int y = 0; y < buf.h; y++) {
        for (int x = 0; x < buf.w; x++) {
            float old = buf.at(y, x);
            float nw  = (old > 127.f) ? 255.f : 0.f;
            float err = old - nw;
            buf.at(y, x) = nw;
            buf.clamp_add(y,     x + 1,  err * 7.f / 16.f);
            buf.clamp_add(y + 1, x - 1,  err * 3.f / 16.f);
            buf.clamp_add(y + 1, x,      err * 5.f / 16.f);
            buf.clamp_add(y + 1, x + 1,  err * 1.f / 16.f);
        }
    }
}

static void dither_atkinson(GrayBuf& buf)
{
    for (int y = 0; y < buf.h; y++) {
        for (int x = 0; x < buf.w; x++) {
            float old = buf.at(y, x);
            float nw  = (old > 127.f) ? 255.f : 0.f;
            float err = old - nw;
            buf.at(y, x) = nw;
            buf.clamp_add(y,     x + 1,  err / 8.f);
            buf.clamp_add(y,     x + 2,  err / 8.f);
            buf.clamp_add(y + 1, x - 1,  err / 8.f);
            buf.clamp_add(y + 1, x,      err / 8.f);
            buf.clamp_add(y + 1, x + 1,  err / 8.f);
            buf.clamp_add(y + 2, x,      err / 8.f);
        }
    }
}

static void dither_mean(GrayBuf& buf)
{
    double sum = 0;
    int total = buf.w * buf.h;
    for (int i = 0; i < total; i++) sum += buf.px[i];
    float mean = (float)(sum / total);
    for (int i = 0; i < total; i++)
        buf.px[i] = (buf.px[i] > mean) ? 255.f : 0.f;
}

static void dither_none(GrayBuf& buf)
{
    int total = buf.w * buf.h;
    for (int i = 0; i < total; i++)
        buf.px[i] = (buf.px[i] > 127.f) ? 255.f : 0.f;
}

// ---------------------------------------------------------------------------
// Pack float grayscale buffer to BinImage.
// px < 128 -> black (1), >= 128 -> white (0).
// ---------------------------------------------------------------------------
static bool pack_to_binimage(const GrayBuf& buf, BinImage& out)
{
    if (!binimage_alloc(out, buf.w, buf.h)) return false;
    int rb = out.row_bytes();
    for (int y = 0; y < buf.h; y++) {
        uint8_t* row = out.data + y * rb;
        for (int x = 0; x < buf.w; x++) {
            if (buf.at(y, x) < 128.f) {
                row[x / 8] |= (0x80 >> (x & 7));
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// binimage_rotate_90cw
// New image: width = src.height, height = src.width.
// Pixel (x, y) in src -> (src.height - 1 - y, x) in dst.
// ---------------------------------------------------------------------------
bool binimage_rotate_90cw(const BinImage& src, BinImage& out)
{
    if (!binimage_alloc(out, src.height, src.width)) return false;
    for (int y = 0; y < src.height; y++) {
        for (int x = 0; x < src.width; x++) {
            bool black = binimage_get_pixel(src, x, y);
            int dx = src.height - 1 - y;
            int dy = x;
            binimage_set_pixel(out, dx, dy, black);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// binimage_center
// ---------------------------------------------------------------------------
bool binimage_center(const BinImage& src, int target_w, int target_h,
                     BinImage& out)
{
    if (!binimage_alloc(out, target_w, target_h)) return false;
    // Canvas is zero-initialised (white) by binimage_alloc (calloc).
    int off_x = (target_w - src.width)  / 2;
    int off_y = (target_h - src.height) / 2;
    // Clamp source if larger than target.
    int copy_w = src.width;
    int copy_h = src.height;
    int src_x0 = 0, src_y0 = 0;
    if (off_x < 0) { src_x0 = -off_x; copy_w += off_x; off_x = 0; }
    if (off_y < 0) { src_y0 = -off_y; copy_h += off_y; off_y = 0; }
    if (copy_w > target_w - off_x) copy_w = target_w - off_x;
    if (copy_h > target_h - off_y) copy_h = target_h - off_y;

    for (int y = 0; y < copy_h; y++) {
        for (int x = 0; x < copy_w; x++) {
            bool b = binimage_get_pixel(src, src_x0 + x, src_y0 + y);
            binimage_set_pixel(out, off_x + x, off_y + y, b);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// load_image_from_sd
// ---------------------------------------------------------------------------
bool load_image_from_sd(const char* path,
                         int target_w, int target_h,
                         Dither dither, bool rotate,
                         BinImage& out)
{
    File f = SD.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[IMG] SD.open failed: %s\n", path);
        return false;
    }
    bool result = load_image_from_sd(f, target_w, target_h, dither, rotate, out);
    f.close();
    return result;
}

bool load_image_from_sd(File& f,
                         int target_w, int target_h,
                         Dither dither, bool rotate,
                         BinImage& out)
{
    Serial.printf("[IMG] Loading from File -> %dx%d rotate=%d\n",
                  target_w, target_h, rotate);

    size_t file_size = f.size();
    Serial.printf("[IMG] File size: %u bytes\n", (unsigned)file_size);

    // Allocate file buffer - try PSRAM first.
    uint8_t* file_buf = (uint8_t*)heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM);
    if (!file_buf) file_buf = (uint8_t*)malloc(file_size);
    if (!file_buf) {
        Serial.println("[IMG] File buffer alloc failed");
        f.close();
        return false;
    }
    size_t bytes_read = f.readBytes((char*)file_buf, file_size);
    if (bytes_read != file_size) {
        Serial.printf("[IMG] Read %u of %u bytes\n", (unsigned)bytes_read, (unsigned)file_size);
        free(file_buf);
        return false;
    }

    // Decode via stbi_load_from_memory - no fopen involved.
    int orig_w, orig_h, channels;
    unsigned char* raw = stbi_load_from_memory(
        file_buf, (int)file_size, &orig_w, &orig_h, &channels, 1);
    free(file_buf);

    if (!raw) {
        Serial.printf("[IMG] stbi decode failed: %s\n", stbi_failure_reason());
        return false;
    }
    Serial.printf("[IMG] Decoded: %dx%d ch=%d\n", orig_w, orig_h, channels);

    // Determine resize box.
    // If rotating, the image renders into (target_h x target_w) before rotation.
    int box_w = rotate ? target_h : target_w;
    int box_h = rotate ? target_w : target_h;

    // Scale to fit while preserving aspect ratio.
    float scale = (float)box_w / orig_w;
    if ((float)box_h / orig_h < scale) scale = (float)box_h / orig_h;
    int new_w = (int)(orig_w * scale + 0.5f);
    int new_h = (int)(orig_h * scale + 0.5f);
    if (new_w < 1) new_w = 1;
    if (new_h < 1) new_h = 1;

    // Resize.
    unsigned char* resized = (unsigned char*)heap_caps_malloc(
        (size_t)new_w * new_h, MALLOC_CAP_SPIRAM);
    if (!resized) {
        Serial.println("[IMG] resize buf PSRAM failed, trying SRAM");
        resized = (unsigned char*)malloc((size_t)new_w * new_h);
    }
    if (!resized) {
        stbi_image_free(raw);
        Serial.println("[IMG] resize buf alloc failed");
        return false;
    }

    unsigned char* res = stbir_resize_uint8_linear(
        raw, orig_w, orig_h, 0,
        resized, new_w, new_h, 0,
        STBIR_1CHANNEL);
    stbi_image_free(raw);
    if (!res) {
        free(resized);
        Serial.println("[IMG] stbir_resize failed");
        return false;
    }
    Serial.printf("[IMG] Resized to %dx%d\n", new_w, new_h);

    // Convert to float GrayBuf.
    GrayBuf buf;
    if (!buf.alloc(new_w, new_h)) {
        free(resized);
        Serial.println("[IMG] GrayBuf alloc failed");
        return false;
    }
    for (int i = 0; i < new_w * new_h; i++)
        buf.px[i] = (float)resized[i];
    free(resized);

    // Apply dithering.
    switch (dither) {
        case Dither::FloydSteinberg: dither_floyd(buf);   break;
        case Dither::Atkinson:       dither_atkinson(buf); break;
        case Dither::MeanThreshold:  dither_mean(buf);    break;
        case Dither::None:           dither_none(buf);    break;
    }

    // Pack to BinImage.
    BinImage packed;
    if (!pack_to_binimage(buf, packed)) {
        buf.free_buf();
        return false;
    }
    buf.free_buf();

    if (rotate) {
        BinImage rotated;
        if (!binimage_rotate_90cw(packed, rotated)) {
            packed.free_data();
            return false;
        }
        packed.free_data();

        if (!binimage_center(rotated, target_w, target_h, out)) {
            rotated.free_data();
            return false;
        }
        rotated.free_data();
    } else {
        if (!binimage_center(packed, target_w, target_h, out)) {
            packed.free_data();
            return false;
        }
        packed.free_data();
    }

    Serial.printf("[IMG] Final image: %dx%d (%u bytes)\n",
                  out.width, out.height, (unsigned)out.data_len);
    return true;
}
