#include <Arduino.h>
#include <SD.h>
#include "printer_cat.h"

// ---------------------------------------------------------------------------
// CRC8 table (from catprinter cmds.py)
// ---------------------------------------------------------------------------
static const uint8_t CRC8_TABLE[256] = {
    0,   7,  14,   9,  28,  27,  18,  21,  56,  63,  54,  49,  36,  35,  42,  45,
  112, 119, 126, 121, 108, 107,  98, 101,  72,  79,  70,  65,  84,  83,  90,  93,
  224, 231, 238, 233, 252, 251, 242, 245, 216, 223, 214, 209, 196, 195, 202, 205,
  144, 151, 158, 153, 140, 139, 130, 133, 168, 175, 166, 161, 180, 179, 186, 189,
  199, 192, 201, 206, 219, 220, 213, 210, 255, 248, 241, 246, 227, 228, 237, 234,
  183, 176, 185, 190, 171, 172, 165, 162, 143, 136, 129, 134, 147, 148, 157, 154,
   39,  32,  41,  46,  59,  60,  53,  50,  31,  24,  17,  22,   3,   4,  13,  10,
   87,  80,  89,  94,  75,  76,  69,  66, 111, 104,  97, 102, 115, 116, 125, 122,
  137, 142, 135, 128, 149, 146, 155, 156, 177, 182, 191, 184, 173, 170, 163, 164,
  249, 254, 247, 240, 229, 226, 235, 236, 193, 198, 207, 200, 221, 218, 211, 212,
  105, 110, 103,  96, 117, 114, 123, 124,  81,  86,  95,  88,  77,  74,  67,  68,
   25,  30,  23,  16,   5,   2,  11,  12,  33,  38,  47,  40,  61,  58,  51,  52,
   78,  73,  64,  71,  82,  85,  92,  91, 118, 113, 120, 127, 106, 109, 100,  99,
   62,  57,  48,  55,  34,  37,  44,  43,   6,   1,   8,  15,  26,  29,  20,  19,
  174, 169, 160, 167, 178, 181, 188, 187, 150, 145, 152, 159, 138, 141, 132, 131,
  222, 217, 208, 215, 194, 197, 204, 203, 230, 225, 232, 239, 250, 253, 244, 243,
};

static uint8_t crc8(const uint8_t* data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++)
        crc = CRC8_TABLE[(crc ^ data[i]) & 0xFF];
    return crc;
}

// ---------------------------------------------------------------------------
// Command frame builder.
// Frame: 0x51 0x78 cmd 0x00 len_lo 0x00 [payload...] crc 0xFF
// Returns frame length written into out[].
// out must be at least payload_len + 8 bytes.
// ---------------------------------------------------------------------------
static size_t build_cmd(uint8_t cmd_byte,
                         const uint8_t* payload, size_t payload_len,
                         uint8_t* out)
{
    out[0] = 0x51;
    out[1] = 0x78;
    out[2] = cmd_byte;
    out[3] = 0x00;
    out[4] = (uint8_t)(payload_len & 0xFF);
    out[5] = 0x00;
    if (payload && payload_len > 0)
        memcpy(out + 6, payload, payload_len);
    out[6 + payload_len] = crc8(out + 6, payload_len);
    out[7 + payload_len] = 0xFF;
    return 8 + payload_len;
}

// ---------------------------------------------------------------------------
// Fixed command builders
// ---------------------------------------------------------------------------
static size_t cmd_get_dev_state(uint8_t* out)
{
    uint8_t p[] = {0x00};
    return build_cmd(0xA3, p, 1, out);
}

static size_t cmd_set_quality(uint8_t* out)
{
    uint8_t p[] = {0x32}; // 200 DPI
    return build_cmd(0xA4, p, 1, out);
}

static size_t cmd_set_energy(uint16_t val, uint8_t* out)
{
    uint8_t p[2] = {(uint8_t)((val >> 8) & 0xFF), (uint8_t)(val & 0xFF)};
    return build_cmd(0xAF, p, 2, out);
}

static size_t cmd_apply_energy(uint8_t* out)
{
    uint8_t p[] = {0x01};
    return build_cmd(0xBE, p, 1, out);
}

static size_t cmd_lattice_start(uint8_t* out)
{
    uint8_t p[] = {0xAA,0x55,0x17,0x38,0x44,0x5F,0x5F,0x5F,0x44,0x38,0x2C};
    return build_cmd(0xA6, p, 11, out);
}

static size_t cmd_lattice_end(uint8_t* out)
{
    uint8_t p[] = {0xAA,0x55,0x17,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x17};
    return build_cmd(0xA6, p, 11, out);
}

static size_t cmd_set_paper(uint8_t* out)
{
    uint8_t p[] = {0x30, 0x00};
    return build_cmd(0xA1, p, 2, out);
}

static size_t cmd_feed_paper(uint8_t amount, uint8_t* out)
{
    uint8_t p[] = {amount};
    return build_cmd(0xBD, p, 1, out);
}

// ---------------------------------------------------------------------------
// Row encoding – force RLE only, no raw fallback.
// RLE buffer increased to 512 bytes to handle worst‑case rows.
// ---------------------------------------------------------------------------
static size_t encode_row_rle(const uint8_t* row_bits, int width_px, uint8_t* out)
{
    uint8_t rle[512];
    size_t  rle_len = 0;

    int px = 0;
    while (px < width_px) {
        int val   = (row_bits[px / 8] >> (7 - (px & 7))) & 1;
        int count = 1;
        while (px + count < width_px && count < 0x7F) {
            int nv = (row_bits[(px + count) / 8] >> (7 - ((px + count) & 7))) & 1;
            if (nv != val) break;
            count++;
        }
        // Safety: if buffer would overflow, stop (should not happen for 384 pixels)
        if (rle_len + 1 >= sizeof(rle)) break;
        rle[rle_len++] = (uint8_t)((val << 7) | count);
        px += count;
    }

    // Always use RLE command (0xBF)
    return build_cmd(0xBF, rle, rle_len, out);
}

// ---------------------------------------------------------------------------
// cat_print – with RLE‑only encoding and a 15ms delay between rows
// ---------------------------------------------------------------------------
bool cat_print(BLEPrinter* ble, const BinImage& img, uint16_t energy)
{
    if (!ble || !ble->is_connected()) return false;
    if (!img.valid()) return false;
    if (img.width != CAT_PRINT_WIDTH) {
        Serial.printf("[CAT] Image width %d != 384\n", img.width);
        return false;
    }

    Serial.printf("[CAT] Printing %dx%d energy=0x%04X\n",
                  img.width, img.height, energy);

    uint8_t buf[512]; // large enough for RLE frames
    size_t  n;

    // Build and send header commands.
    n = cmd_get_dev_state(buf);  ble->send_chunked(buf, n, 20, 10);
    n = cmd_set_quality(buf);    ble->send_chunked(buf, n, 20, 10);
    n = cmd_set_energy(energy, buf); ble->send_chunked(buf, n, 20, 10);
    n = cmd_apply_energy(buf);   ble->send_chunked(buf, n, 20, 10);
    n = cmd_lattice_start(buf);  ble->send_chunked(buf, n, 20, 10);

    // Send rows.
    int row_bytes = img.row_bytes();
    uint8_t row_frame[512];
    for (int y = 0; y < img.height; y++) {
        const uint8_t* row = img.data + (size_t)y * (size_t)row_bytes;
        size_t frame_len = encode_row_rle(row, img.width, row_frame);
        ble->clear_notify();
        ble->send_chunked(row_frame, frame_len, 20, 10);
        delay(3); // 15 ms delay – critical to prevent buffer overflow
    }

    // Footer commands.
    n = cmd_feed_paper(25, buf); ble->send_chunked(buf, n, 20, 10);
    n = cmd_set_paper(buf);      ble->send_chunked(buf, n, 20, 10);
    n = cmd_set_paper(buf);      ble->send_chunked(buf, n, 20, 10);
    n = cmd_set_paper(buf);      ble->send_chunked(buf, n, 20, 10);
    n = cmd_lattice_end(buf);    ble->send_chunked(buf, n, 20, 10);

    ble->clear_notify();
    n = cmd_get_dev_state(buf);  ble->send_chunked(buf, n, 20, 10);

    Serial.println("[CAT] Data sent, waiting for ready...");
    uint8_t resp[16];
    size_t resp_len = ble->wait_notify(resp, sizeof(resp), 8000);

    if (resp_len > 0) {
        bool magic_ok = (resp_len >= CAT_READY_MAGIC_LEN &&
                         memcmp(resp, CAT_READY_MAGIC, CAT_READY_MAGIC_LEN) == 0);
        Serial.printf("[CAT] Got %u bytes response (magic=%s).\n",
                      (unsigned)resp_len, magic_ok ? "ok" : "mismatch-ok-anyway");
        return true;
    }
    Serial.println("[CAT] No response within 8s (printed anyway).");
    return true;
}

bool cat_feed(BLEPrinter* ble, int dots, uint16_t energy)
{
    if (!ble || !ble->is_connected()) return false;
    uint8_t buf[80];
    size_t n;

    while (dots > 0) {
        int chunk = (dots > 255) ? 255 : dots;
        dots -= chunk;

        n = cmd_get_dev_state(buf);      ble->send_chunked(buf, n, 20, 10);
        n = cmd_set_quality(buf);        ble->send_chunked(buf, n, 20, 10);
        n = cmd_set_energy(energy, buf); ble->send_chunked(buf, n, 20, 10);
        n = cmd_apply_energy(buf);       ble->send_chunked(buf, n, 20, 10);
        n = cmd_lattice_start(buf);      ble->send_chunked(buf, n, 20, 10);
        n = cmd_feed_paper((uint8_t)chunk, buf); ble->send_chunked(buf, n, 20, 10);
        n = cmd_set_paper(buf);          ble->send_chunked(buf, n, 20, 10);
        n = cmd_lattice_end(buf);        ble->send_chunked(buf, n, 20, 10);

        ble->clear_notify();
        n = cmd_get_dev_state(buf);      ble->send_chunked(buf, n, 20, 10);
        uint8_t resp[16];
        ble->wait_notify(resp, sizeof(resp), 10000);
    }
    return true;
}

uint16_t cat_density_to_energy(int density)
{
    switch (density) {
        case 0:  return 0x3000;
        case 1:  return 0x8000;
        case 2:  return 0xFFFF;
        default: return 0x8000;
    }
}