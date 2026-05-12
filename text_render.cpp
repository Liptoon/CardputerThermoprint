#include <Arduino.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include "text_render.h"
#include "image_pipeline.h"
#include "font_data.h"

// For Cat printer validation (max width = 384)
static constexpr int CAT_PRINT_WIDTH = 384;

// ---------------------------------------------------------------------------
// Glyph lookup
// ---------------------------------------------------------------------------
static const uint8_t* get_glyph(uint32_t cp, int size_idx)
{
    if (cp < FONT_CP_MIN || cp > FONT_CP_MAX) cp = '?';
    if (cp < FONT_CP_MIN || cp > FONT_CP_MAX)
        return FONT_GLYPH_TABLE[size_idx];
    return FONT_GLYPH_TABLE[size_idx]
           + (int)(cp - FONT_CP_MIN) * FONT_GLYPH_BYTES[size_idx];
}

// ---------------------------------------------------------------------------
// UTF-8 decoder: reads a UTF-8 sequence from the string and returns the
// Unicode code point. Advances the index i accordingly.
// Returns 0 on invalid sequence (falls back to '?').
// ---------------------------------------------------------------------------
static uint32_t utf8_decode(const char* str, int* i)
{
    uint8_t c = (uint8_t)str[*i];
    if (c < 0x80) {
        (*i)++;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && (uint8_t)str[*i + 1] != 0) {
        uint32_t cp = ((c & 0x1F) << 6) | ((uint8_t)str[*i + 1] & 0x3F);
        if (cp >= 0x80) {
            (*i) += 2;
            return cp;
        }
    } else if ((c & 0xF0) == 0xE0 && (uint8_t)str[*i + 1] != 0 && (uint8_t)str[*i + 2] != 0) {
        uint32_t cp = ((c & 0x0F) << 12) | (((uint8_t)str[*i + 1] & 0x3F) << 6) | ((uint8_t)str[*i + 2] & 0x3F);
        if (cp >= 0x800) {
            (*i) += 3;
            return cp;
        }
    } else if ((c & 0xF8) == 0xF0 && (uint8_t)str[*i + 1] != 0 && (uint8_t)str[*i + 2] != 0 && (uint8_t)str[*i + 3] != 0) {
        uint32_t cp = ((c & 0x07) << 18) | (((uint8_t)str[*i + 1] & 0x3F) << 12) | (((uint8_t)str[*i + 2] & 0x3F) << 6) | ((uint8_t)str[*i + 3] & 0x3F);
        if (cp >= 0x10000 && cp <= 0x10FFFF) {
            (*i) += 4;
            return cp;
        }
    }
    // Invalid sequence – skip one byte and return '?'
    (*i)++;
    return '?';
}

// ---------------------------------------------------------------------------
// Word-wrap into fixed line buffers. Input text is UTF‑8; we decode
// characters to get correct length for width calculation.
// ---------------------------------------------------------------------------
static constexpr int MAX_LINES    = 128;
static constexpr int MAX_LINE_LEN = 128; // in bytes, not characters

static int word_wrap(const char* text, int max_chars,
                     char line_buf[][MAX_LINE_LEN], int max_lines)
{
    int n_lines = 0;
    int char_count = 0;
    int last_space_pos = -1;        // byte position of last space
    int last_space_chars = 0;       // character count at last space
    int line_start = 0;
    int i = 0;

    while (text[i] && n_lines < max_lines) {
        // Skip \r
        if (text[i] == '\r') { i++; continue; }

        // Handle explicit newline
        if (text[i] == '\n') {
            int copy_len = i - line_start;
            if (copy_len >= MAX_LINE_LEN) copy_len = MAX_LINE_LEN - 1;
            memcpy(line_buf[n_lines], text + line_start, copy_len);
            line_buf[n_lines][copy_len] = '\0';
            int tl = (int)strlen(line_buf[n_lines]);
            while (tl > 0 && line_buf[n_lines][tl-1] == ' ') line_buf[n_lines][--tl] = '\0';
            n_lines++;
            i++; // skip \n
            line_start = i;
            char_count = 0;
            last_space_pos = -1;
            last_space_chars = 0;
            continue;
        }

        int old_i = i;
        uint32_t cp = utf8_decode(text, &i);
        if (cp == ' ') {
            last_space_pos = i;       // byte position after the space
            last_space_chars = char_count;
        }

        char_count++;

        if (char_count > max_chars) {
            if (last_space_pos >= 0) {
                int copy_len = last_space_pos - line_start - 1; // exclude space
                if (copy_len < 0) copy_len = 0;
                if (copy_len >= MAX_LINE_LEN) copy_len = MAX_LINE_LEN - 1;
                memcpy(line_buf[n_lines], text + line_start, copy_len);
                line_buf[n_lines][copy_len] = '\0';
                int tl = (int)strlen(line_buf[n_lines]);
                while (tl > 0 && line_buf[n_lines][tl-1] == ' ') line_buf[n_lines][--tl] = '\0';
                n_lines++;
                line_start = last_space_pos;
                i = line_start;
                char_count = char_count - last_space_chars - 1;
                last_space_pos = -1;
                last_space_chars = 0;
                continue;
            } else {
                // hard break at max_chars
                int copy_len = old_i - line_start;
                if (copy_len >= MAX_LINE_LEN) copy_len = MAX_LINE_LEN - 1;
                memcpy(line_buf[n_lines], text + line_start, copy_len);
                line_buf[n_lines][copy_len] = '\0';
                n_lines++;
                line_start = old_i;
                i = old_i;
                char_count = 0;
                continue;
            }
        }
    }

    // Copy the last line if any
    if (text[line_start] != '\0') {
        int copy_len = i - line_start;
        if (copy_len >= MAX_LINE_LEN) copy_len = MAX_LINE_LEN - 1;
        memcpy(line_buf[n_lines], text + line_start, copy_len);
        line_buf[n_lines][copy_len] = '\0';
        int tl = (int)strlen(line_buf[n_lines]);
        while (tl > 0 && line_buf[n_lines][tl-1] == ' ') line_buf[n_lines][--tl] = '\0';
        n_lines++;
    }

    return n_lines;
}

// ---------------------------------------------------------------------------
// render_text_label
// ---------------------------------------------------------------------------
bool render_text_label(const char* text,
                        int target_w, int target_h,
                        int font_size_idx,
                        bool rotate,
                        BinImage& out)
{
    if (!text || text[0] == '\0') return false;
    if (font_size_idx < 0 || font_size_idx >= FONT_NUM_SIZES)
        font_size_idx = 1;

    const int MARGIN = 1;
    const FontSizeMetrics& m = FONT_METRICS[font_size_idx];
    const int gw = m.cell_w;
    const int gh = m.cell_h;
    const int row_bytes_glyph = (gw + 7) / 8;
    const int line_spacing = 1;
    const int line_h = gh + line_spacing;

    // Render width = long axis for Fischero landscape, print width for Cat.
    int render_w = (rotate ? target_h : target_w) - 2 * MARGIN;
    if (render_w < gw) render_w = gw;
    int max_chars = render_w / gw;
    if (max_chars < 1) max_chars = 1;

    // Word-wrap.
    static char line_buf[MAX_LINES][MAX_LINE_LEN];
    int n_lines = word_wrap(text, max_chars, line_buf, MAX_LINES);
    if (n_lines == 0) return false;

    int canvas_w = render_w + 2 * MARGIN;
    int canvas_h = n_lines * line_h - line_spacing + 2 * MARGIN;

    // Limit lines for Fischero (rotated)
    if (rotate) {
        int max_canvas_h = target_w - 2 * MARGIN;
        if (max_canvas_h < line_h) max_canvas_h = line_h;
        int max_lines_fit = (max_canvas_h - 2 * MARGIN + line_spacing) / line_h;
        if (max_lines_fit < 1) max_lines_fit = 1;
        if (n_lines > max_lines_fit) {
            Serial.printf("[TEXT] Warning: %d lines truncated to %d\n", n_lines, max_lines_fit);
            n_lines = max_lines_fit;
            canvas_h = n_lines * line_h - line_spacing + 2 * MARGIN;
            if (canvas_h > max_canvas_h) canvas_h = max_canvas_h;
        }
    } else {
        const int MAX_SAFE_HEIGHT = 4000;
        if (canvas_h > MAX_SAFE_HEIGHT) {
            Serial.printf("[TEXT] Text too tall (%d px), truncating\n", canvas_h);
            int max_lines_fit = (MAX_SAFE_HEIGHT - 2 * MARGIN + line_spacing) / line_h;
            if (max_lines_fit < 1) max_lines_fit = 1;
            if (n_lines > max_lines_fit) n_lines = max_lines_fit;
            canvas_h = n_lines * line_h - line_spacing + 2 * MARGIN;
            if (canvas_h > MAX_SAFE_HEIGHT) canvas_h = MAX_SAFE_HEIGHT;
        }
    }

    Serial.printf("[TEXT] n=%d gw=%d gh=%d canvas=%dx%d rot=%d\n",
                  n_lines, gw, gh, canvas_w, canvas_h, rotate);

    BinImage canvas_img;
    if (!binimage_alloc(canvas_img, canvas_w, canvas_h)) {
        Serial.printf("[TEXT] BinImage alloc failed %dx%d\n", canvas_w, canvas_h);
        return false;
    }

    // Allocate code-point buffer on the heap (PSRAM first, SRAM fallback)
    uint32_t* cp_array = (uint32_t*)heap_caps_malloc(
        (size_t)MAX_LINES * MAX_LINE_LEN * sizeof(uint32_t),
        MALLOC_CAP_SPIRAM
    );
    if (!cp_array) cp_array = (uint32_t*)malloc(
        (size_t)MAX_LINES * MAX_LINE_LEN * sizeof(uint32_t)
    );
    if (!cp_array) {
        Serial.println("[TEXT] cp_array alloc failed");
        canvas_img.free_data();
        return false;
    }

    int line_lens[MAX_LINES]; // per-line character counts

    for (int li = 0; li < n_lines; li++) {
        int cp_idx = 0;
        int byte_idx = 0;
        const char* p = line_buf[li];
        while (p[byte_idx] && cp_idx < MAX_LINE_LEN) {
            int i = byte_idx;
            cp_array[li * MAX_LINE_LEN + cp_idx++] = utf8_decode(p, &i);
            byte_idx = i;
        }
        line_lens[li] = cp_idx;
    }

    // Precompute line positions
    int line_y[MAX_LINES];
    int line_x[MAX_LINES];
    for (int li = 0; li < n_lines; li++) {
        line_y[li] = MARGIN + li * line_h;
        int line_px = line_lens[li] * gw;
        line_x[li] = MARGIN + (render_w - line_px) / 2;
        if (line_x[li] < MARGIN) line_x[li] = MARGIN;
    }

    // Row-by-row rendering
    for (int row = 0; row < canvas_h; row++) {
        for (int li = 0; li < n_lines; li++) {
            int gy = row - line_y[li];
            if (gy < 0 || gy >= gh) continue;

            for (int ci = 0; ci < line_lens[li]; ci++) {
                uint32_t cp = cp_array[li * MAX_LINE_LEN + ci];
                if (cp < 0x20) cp = ' ';
                const uint8_t* glyph = get_glyph(cp, font_size_idx);
                const uint8_t* glyph_row = glyph + gy * row_bytes_glyph;

                int gx_start = line_x[li] + ci * gw;
                for (int c = 0; c < gw; c++) {
                    int dx = gx_start + c;
                    if (dx < 0 || dx >= canvas_w) continue;
                    uint8_t b = glyph_row[c / 8];
                    if ((b >> (7 - (c % 8))) & 1)
                        binimage_set_pixel(canvas_img, dx, row, true);
                }
            }
        }
    }

    free(cp_array);

    // Rotate and/or center
    if (rotate) {
        BinImage rotated;
        if (!binimage_rotate_90cw(canvas_img, rotated)) {
            canvas_img.free_data();
            return false;
        }
        canvas_img.free_data();
        if (!binimage_center(rotated, target_w, target_h, out)) {
            rotated.free_data();
            return false;
        }
        rotated.free_data();
    } else {
        int final_h = (target_h > 0) ? target_h : canvas_h;
        if (!binimage_center(canvas_img, target_w, final_h, out)) {
            canvas_img.free_data();
            return false;
        }
        canvas_img.free_data();
    }

    // Final sanity check
    if (out.width <= 0 || out.width > CAT_PRINT_WIDTH) {
        Serial.printf("[TEXT] Final image width %d is invalid\n", out.width);
        out.free_data();
        return false;
    }
    if (out.height <= 0 || out.height > 5000) {
        Serial.printf("[TEXT] Final image height %d is invalid\n", out.height);
        out.free_data();
        return false;
    }

    Serial.printf("[TEXT] done: %dx%d\n", out.width, out.height);
    return true;
}

// ---------------------------------------------------------------------------
// load_text_file_sd
// ---------------------------------------------------------------------------
char* load_text_file_sd(const char* path, size_t max_bytes)
{
    File f = SD.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[TEXT] Cannot open %s\n", path);
        return nullptr;
    }
    char* result = load_text_file_sd(f, max_bytes);
    f.close();
    return result;
}

char* load_text_file_sd(File& f, size_t max_bytes)
{
    size_t sz = (size_t)f.size();
    if (sz > max_bytes) sz = max_bytes;

    uint8_t* buf = (uint8_t*)heap_caps_malloc(sz + 1, MALLOC_CAP_SPIRAM);
    if (!buf) buf = (uint8_t*)malloc(sz + 1);
    if (!buf) {
        Serial.println("[TEXT] text buf alloc failed");
        return nullptr;
    }
    size_t nread = f.readBytes((char*)buf, sz);
    buf[nread] = '\0';
    Serial.printf("[TEXT] read %u bytes\n", (unsigned)nread);
    return (char*)buf;
}