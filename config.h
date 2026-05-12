#pragma once
#include <Arduino.h>
#include "ble_printer.h"

static constexpr const char* CONFIG_PATH = "/thermoprint.cfg";

struct Config {
    char        last_addr[18];  // BLE address of last connected printer
    PrinterType last_type;      // Fischero or Cat
    int         density;        // 0=light, 1=medium, 2=dark
    int         font_size_idx;  // 0..6 (index into FONT_METRICS[])
    int         dither;         // 0=Floyd, 1=Atkinson, 2=MeanThreshold
    int         cat_energy;     // Cat printer energy (stored as int, cast to uint16_t)
    bool        verbose_diag;   // show diagnostic screens before print
};

// Load from SD. Returns false if file missing (fills defaults).
bool config_load(Config& cfg);

// Save to SD. Returns false on error.
bool config_save(const Config& cfg);

// Fill with sensible defaults.
void config_defaults(Config& cfg);
