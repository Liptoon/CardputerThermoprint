#include <Arduino.h>
#include "printer_fischero.h"

// ---------------------------------------------------------------------------
// Command byte sequences (from PROTOCOL-fischero.md)
// ---------------------------------------------------------------------------
static const uint8_t CMD_GET_MODEL[]    = {0x10, 0xFF, 0x20, 0xF0};
static const uint8_t CMD_GET_FIRMWARE[] = {0x10, 0xFF, 0x20, 0xF1};
static const uint8_t CMD_GET_BATTERY[]  = {0x10, 0xFF, 0x50, 0xF1};
static const uint8_t CMD_GET_STATUS[]   = {0x10, 0xFF, 0x40};
static const uint8_t CMD_WAKE[]         = {0,0,0,0,0,0,0,0,0,0,0,0};
static const uint8_t CMD_ENABLE[]       = {0x10, 0xFF, 0xFE, 0x01};
static const uint8_t CMD_STOP[]         = {0x10, 0xFF, 0xFE, 0x45};
static const uint8_t CMD_FORM_FEED[]    = {0x1D, 0x0C};

// Set density: 0=light, 1=medium, 2=dark.
static void cmd_set_density(uint8_t level, uint8_t* out)
{
    out[0] = 0x10; out[1] = 0xFF; out[2] = 0x10;
    out[3] = 0x00; out[4] = level & 0xFF;
}

// Set paper type to gap/label (0).
static const uint8_t CMD_PAPER_GAP[] = {0x10, 0xFF, 0x84, 0x00};

// Raster image header: GS v 0
static void make_raster_header(uint8_t* out,
                                int width_bytes, int height_rows)
{
    out[0] = 0x1D; out[1] = 0x76; out[2] = 0x30; out[3] = 0x00;
    out[4] = (uint8_t)(width_bytes & 0xFF);
    out[5] = (uint8_t)((width_bytes >> 8) & 0xFF);
    out[6] = (uint8_t)(height_rows & 0xFF);
    out[7] = (uint8_t)((height_rows >> 8) & 0xFF);
}

// ---------------------------------------------------------------------------
// fischero_print
// ---------------------------------------------------------------------------
bool fischero_print(BLEPrinter* ble, const BinImage& img, int density)
{
    if (!ble || !ble->is_connected()) return false;
    if (!img.valid()) return false;

    const int WIDTH_BYTES = 12; // 96px / 8
    if (img.width != FISCHERO_PRINT_WIDTH) {
        Serial.printf("[FISCHERO] Image width %d != 96\n", img.width);
        return false;
    }

    Serial.printf("[FISCHERO] Printing %dx%d, density=%d\n",
                  img.width, img.height, density);

    // 1. Set density.
    uint8_t den_cmd[5];
    cmd_set_density((uint8_t)(density & 0xFF), den_cmd);
    uint8_t resp[16];
    size_t  resp_len;
    resp_len = ble->send_command(den_cmd, 5, resp, sizeof(resp), 1500);
    Serial.printf("[FISCHERO] density resp %u bytes\n", (unsigned)resp_len);

    // 2. Set paper type gap/label.
    resp_len = ble->send_command(CMD_PAPER_GAP, sizeof(CMD_PAPER_GAP),
                                  resp, sizeof(resp), 1500);

    // 3. Wake (12 null bytes).
    ble->send_chunked(CMD_WAKE, sizeof(CMD_WAKE), 20, 5);
    delay(50);

    // 4. Enable printer (AiYin-specific).
    ble->send_chunked(CMD_ENABLE, sizeof(CMD_ENABLE), 20, 5);
    delay(100);

    // 5. Raster header + pixel data.
    uint8_t hdr[8];
    make_raster_header(hdr, WIDTH_BYTES, img.height);
    ble->send_chunked(hdr, 8, 20, 5);

    // Send pixel data row by row so we don't need a second PSRAM buffer.
    // img.data is already packed 12 bytes/row in the correct format.
    ble->send_chunked(img.data, img.data_len, 20, 10);

    delay(100);

    // 6. Form feed - advance to next label.
    ble->send_chunked(CMD_FORM_FEED, sizeof(CMD_FORM_FEED), 20, 5);
    delay(100);

    // 7. Stop print (AiYin-specific) - wait for any notify response.
    // Printer signals completion within 1-3s of paper advancing.
    ble->clear_notify();
    ble->send_chunked(CMD_STOP, sizeof(CMD_STOP), 20, 5);

    Serial.println("[FISCHERO] Waiting for completion...");
    uint8_t finish_buf[16];
    size_t finish_len = ble->wait_notify(finish_buf, sizeof(finish_buf), 8000);

    if (finish_len > 0) {
        Serial.printf("[FISCHERO] Print complete (%u bytes response).\n",
                      (unsigned)finish_len);
        return true;
    }
    Serial.println("[FISCHERO] Timeout waiting for completion (printed anyway).");
    return true; // printer likely printed even without response
}

// ---------------------------------------------------------------------------
// Query helpers
// ---------------------------------------------------------------------------
bool fischero_get_model(BLEPrinter* ble, char* buf, size_t buf_len)
{
    uint8_t resp[32];
    size_t n = ble->send_command(CMD_GET_MODEL, sizeof(CMD_GET_MODEL),
                                  resp, sizeof(resp), 2000);
    if (n == 0) { strncpy(buf, "?", buf_len); return false; }
    size_t copy = (n < buf_len - 1) ? n : buf_len - 1;
    memcpy(buf, resp, copy);
    buf[copy] = '\0';
    return true;
}

bool fischero_get_firmware(BLEPrinter* ble, char* buf, size_t buf_len)
{
    uint8_t resp[32];
    size_t n = ble->send_command(CMD_GET_FIRMWARE, sizeof(CMD_GET_FIRMWARE),
                                  resp, sizeof(resp), 2000);
    if (n == 0) { strncpy(buf, "?", buf_len); return false; }
    size_t copy = (n < buf_len - 1) ? n : buf_len - 1;
    memcpy(buf, resp, copy);
    buf[copy] = '\0';
    return true;
}

bool fischero_get_battery(BLEPrinter* ble, int* status_byte, int* percent)
{
    uint8_t resp[4];
    size_t n = ble->send_command(CMD_GET_BATTERY, sizeof(CMD_GET_BATTERY),
                                  resp, sizeof(resp), 2000);
    if (n < 2) { *status_byte = -1; *percent = -1; return false; }
    *status_byte = resp[0];
    *percent     = resp[1];
    return true;
}

bool fischero_get_status(BLEPrinter* ble, uint8_t* status_byte)
{
    uint8_t resp[4];
    size_t n = ble->send_command(CMD_GET_STATUS, sizeof(CMD_GET_STATUS),
                                  resp, sizeof(resp), 2000);
    if (n == 0) { *status_byte = 0xFF; return false; }
    *status_byte = resp[0];
    return true;
}

void fischero_decode_status(uint8_t s, char* buf)
{
    if (s == 0x00) { strcpy(buf, "Ready"); return; }
    buf[0] = '\0';
    if (s & 0x01) strcat(buf, "Printing ");
    if (s & 0x02) strcat(buf, "CoverOpen ");
    if (s & 0x04) strcat(buf, "NoPaper ");
    if (s & 0x08) strcat(buf, "LowBatt ");
    if (s & 0x20) strcat(buf, "Charging ");
    if (s & 0x40) strcat(buf, "Overheat ");
    int len = strlen(buf);
    if (len > 0 && buf[len-1] == ' ') buf[len-1] = '\0';
}

bool fischero_feed(BLEPrinter* ble, int dots)
{
    if (!ble || !ble->is_connected()) return false;
    ble->send_chunked(CMD_WAKE, sizeof(CMD_WAKE), 20, 5);
    delay(30);
    ble->send_chunked(CMD_ENABLE, sizeof(CMD_ENABLE), 20, 5);
    delay(50);
    while (dots > 0) {
        int chunk = (dots > 255) ? 255 : dots;
        uint8_t cmd[3] = {0x1B, 0x4A, (uint8_t)chunk};
        ble->send_chunked(cmd, 3, 20, 5);
        dots -= chunk;
    }
    delay(50);
    ble->clear_notify();
    ble->send_chunked(CMD_STOP, sizeof(CMD_STOP), 20, 5);
    uint8_t resp[8];
    ble->wait_notify(resp, sizeof(resp), 3000);
    return true;
}

bool fischero_form_feed(BLEPrinter* ble)
{
    if (!ble || !ble->is_connected()) return false;
    ble->send_chunked(CMD_WAKE, sizeof(CMD_WAKE), 20, 5);
    delay(30);
    ble->send_chunked(CMD_ENABLE, sizeof(CMD_ENABLE), 20, 5);
    delay(50);
    ble->send_chunked(CMD_FORM_FEED, sizeof(CMD_FORM_FEED), 20, 5);
    delay(50);
    ble->clear_notify();
    ble->send_chunked(CMD_STOP, sizeof(CMD_STOP), 20, 5);
    uint8_t resp[8];
    ble->wait_notify(resp, sizeof(resp), 3000);
    return true;
}
