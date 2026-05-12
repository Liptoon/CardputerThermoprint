#pragma once
#include <Arduino.h>
#include "ble_printer.h"
#include "printer_protocol.h"

// ---------------------------------------------------------------------------
// Fischero D11s (AiYin) label printer protocol.
// 96px wide, 203 DPI, 14mm wide labels.
// All commands from PROTOCOL-fischero.md, verified on hardware.
// ---------------------------------------------------------------------------

// Print a label. img must be 96px wide, height = label dots.
// density: 0=light, 1=medium, 2=dark.
// Returns true when printer signals completion (or after timeout).
bool fischero_print(BLEPrinter* ble, const BinImage& img, int density = 1);

// Query commands - return true on success.
bool fischero_get_model(BLEPrinter* ble, char* buf, size_t buf_len);
bool fischero_get_firmware(BLEPrinter* ble, char* buf, size_t buf_len);
bool fischero_get_battery(BLEPrinter* ble, int* status_byte, int* percent);
bool fischero_get_status(BLEPrinter* ble, uint8_t* status_byte);

// Decode status byte to human-readable string.
// buf must be at least 64 bytes.
void fischero_decode_status(uint8_t status, char* buf);

// Feed paper forward by dots.
bool fischero_feed(BLEPrinter* ble, int dots);

// Advance to next label gap.
bool fischero_form_feed(BLEPrinter* ble);

// Print head width in pixels - always 96 for D11s.
static constexpr int FISCHERO_PRINT_WIDTH = 96;

// Default label: 30mm at 203 DPI = ~240 dots along feed axis.
static constexpr int FISCHERO_LABEL_30MM_DOTS = 240;
