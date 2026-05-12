#include <Arduino.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include "text_render.h"
#include "image_pipeline.h"
#include "font_data.h"

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
// Word-wrap into fixed line buffers. Skips \r.
// ---------------------------------------------------------------------------
static constexpr int MAX_LINES    = 128;
static constexpr int MAX_LINE_LEN = 128; // reduced from 256

static int word_wrap(const char* text, int max_chars,
                     char line_buf[][MAX_LINE_LEN], int max_lines)
{
    int n_lines = 0;
    const char* p = text;

    while (*p && n_lines < max_lines) {
        while (*p == '\r') p++;
        if (*p == '\0') break;

        if (*p == '\n') {
            line_buf[n_lines][0] = '\0';
            n_lines++;
            p++;
            continue;
        }

        // Collect up to max_chars printable chars, tracking last space.
        int  len     = 0;
        int  last_sp = -1;
        const char* line_start = p;

        while (*p && *p != '\n' && len < max_chars) {
            if (*p == '\r') { p++; continue; }
            if (*p == ' ') last_sp = len;
            len++;
            p++;
        }

        // Soft-wrap at last space if we stopped mid-word.
        if (*p && *p != '\n' && last_sp >= 0) {
            int rewind = len - last_sp - 1;
            p -= rewind;
            len = last_sp;
        }

        // Copy to line_buf, stripping trailing spaces.
        int copy_len = (len < MAX_LINE_LEN - 1) ? len : MAX_LINE_LEN - 1;
        memcpy(line_buf[n_lines], line_start, copy_len);
        line_buf[n_lines][copy_len] = '\0';
        // Trim trailing spaces.
        int tl = (int)strlen(line_buf[n_lines]);
        while (tl > 0 && line_buf[n_lines][tl-1] == ' ') {
            line_buf[n_lines][--tl] = '\0';
        }
        n_lines++;

        if (*p == '\n') p++;
        while (*p == ' ') p++;
    }
    return n_lines;
}

// ---------------------------------------------------------------------------
// render_text_label
//
// Row-by-row rendering: for each row of the output canvas, iterate over
// all visible glyphs and set pixels that intersect that row.
// Never allocates more than one canvas row at a time.
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

    // Word-wrap. line_buf is static (BSS) to avoid stack pressure.
    static char line_buf[MAX_LINES][MAX_LINE_LEN];
    int n_lines = word_wrap(text, max_chars, line_buf, MAX_LINES);
    if (n_lines == 0) return false;

    // Canvas dimensions.
    int canvas_w = render_w + 2 * MARGIN;
    int canvas_h = n_lines * line_h - line_spacing + 2 * MARGIN;
    int max_canvas_h = rotate ? (target_w - 2 * MARGIN) : 0;
    if (max_canvas_h > 0 && canvas_h > max_canvas_h)
        canvas_h = max_canvas_h;

    Serial.printf("[TEXT] n=%d gw=%d gh=%d canvas=%dx%d rot=%d\n",
                  n_lines, gw, gh, canvas_w, canvas_h, rotate);

    // Allocate the output packed BinImage directly.
    BinImage canvas_img;
    if (!binimage_alloc(canvas_img, canvas_w, canvas_h)) {
        Serial.printf("[TEXT] BinImage alloc failed %dx%d\n", canvas_w, canvas_h);
        return false;
    }
    // binimage_alloc zero-initialises (all white).

    // Precompute per-line x offsets and glyph pointers.
    // Render each row of the canvas by scanning all lines whose y-range
    // intersects that row.
    int line_y[MAX_LINES]; // canvas y of top of each line
    int line_x[MAX_LINES]; // canvas x of first glyph of each line
    int line_len[MAX_LINES];
    for (int li = 0; li < n_lines; li++) {
        line_len[li] = (int)strlen(line_buf[li]);
        line_y[li]   = MARGIN + li * line_h;
        int line_px  = line_len[li] * gw;
        line_x[li]   = MARGIN + (render_w - line_px) / 2;
        if (line_x[li] < MARGIN) line_x[li] = MARGIN;
    }

    // Row-by-row rendering.
    for (int row = 0; row < canvas_h; row++) {
        // Find which text line this canvas row belongs to.
        // row is within a line if: line_y[li] <= row < line_y[li] + gh
        for (int li = 0; li < n_lines; li++) {
            int gy = row - line_y[li]; // row within glyph (0..gh-1)
            if (gy < 0 || gy >= gh) continue;

            // This row intersects line li, glyph row gy.
            for (int ci = 0; ci < line_len[li]; ci++) {
                uint32_t cp = (uint32_t)(uint8_t)line_buf[li][ci];
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

    // Rotate+center for Fischero, or just center for Cat.
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
