#pragma once
#include <Arduino.h>
#include "ble_printer.h"
#include "printer_protocol.h"

// ---------------------------------------------------------------------------
// Cat printer protocol (GT01, GB01/02/03, MX05/06/08/10, MXTP, YT01).
// 384px wide, 200 DPI, 57mm continuous roll.
// Protocol identical across all models; service UUID differs (ae30 vs af30).
// No bidirectional status query - connection state only.
// ---------------------------------------------------------------------------

// energy: 0x0000 (lightest) .. 0xFFFF (darkest, default).
// img.width must be 384.
bool cat_print(BLEPrinter* ble, const BinImage& img, uint16_t energy = 0x8000);

// Feed roll by dots (wrapped in lattice session).
bool cat_feed(BLEPrinter* ble, int dots, uint16_t energy = 0x8000);

// Map density 0/1/2 to energy values.
uint16_t cat_density_to_energy(int density);

// Print head width - always 384 for Cat printers.
static constexpr int CAT_PRINT_WIDTH = 384;

// Cat printer ready notification magic bytes.
static const uint8_t CAT_READY_MAGIC[] = {
    0x51, 0x78, 0xAE, 0x01, 0x01, 0x00, 0x00, 0x00, 0xFF
};
static constexpr size_t CAT_READY_MAGIC_LEN = 9;
