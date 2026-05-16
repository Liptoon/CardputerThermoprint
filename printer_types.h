#pragma once

// Printer type detected during BLE scan.
// Kept in a separate header so config.h can include it without
// pulling in NimBLE headers (which break IntelliSense).
enum class PrinterType {
    Unknown,
    Fischero,  // FICHERO* / D11s* - 96px, 14mm labels
    Cat        // GB01/GT01/MX* etc - 384px, 57mm roll
};
