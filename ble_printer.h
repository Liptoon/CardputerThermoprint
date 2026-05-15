#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

// Printer type detected from BLE advertisement name and services.
enum class PrinterType {
    Unknown,
    Fischero,  // FICHERO* / D11s* - 96px, 14mm labels
    Cat        // GB01/GT01/MX* etc - 384px, 57mm roll
};

// Maximum length of a BLE notify response we buffer.
static constexpr size_t BLE_NOTIFY_BUF_LEN = 64;

// BLEPrinter wraps NimBLE scan + connect + chunked write + notify receive.
// One instance, one connection at a time.
class BLEPrinter {
public:
    BLEPrinter();
    ~BLEPrinter();

    // Must be called once from setup(), after M5Cardputer.begin().
    // Initialises the NimBLE stack. Crashes if called before hardware init.
    void begin();

    // Scan for up to timeout_ms, connect to first Fischero or Cat printer found.
    // Returns false on timeout or connection failure.
    bool scan_and_connect(uint32_t timeout_ms = 15000);

    // Connect to a specific BLE address string "XX:XX:XX:XX:XX:XX".
    bool connect_by_address(const char* addr, PrinterType hint, uint32_t timeout_ms = 10000);

    bool is_connected();
    void disconnect();

    PrinterType printer_type() const { return type_; }
    const char* printer_address()   const { return addr_str_; }
    const char* printer_name()      const { return name_str_; }

    // Active service/characteristic UUIDs (set after connect).
    const char* svc_uuid()   const { return svc_uuid_; }
    const char* write_uuid() const { return write_uuid_; }
    const char* notify_uuid() const { return notify_uuid_; }

    // Write data to the write characteristic, in chunks.
    // chunk_size: bytes per BLE packet (20 = safe default).
    // delay_ms: gap between packets.
    // Returns false if write failed or abort was requested.
    bool send_chunked(const uint8_t* data, size_t len,
                      size_t chunk_size = 20, uint32_t delay_ms = 10);

    // Send a short command (< 20 bytes) and wait up to timeout_ms for a notify response.
    // Returns number of bytes received into buf, 0 on timeout.
    size_t send_command(const uint8_t* cmd, size_t cmd_len,
                        uint8_t* resp_buf, size_t resp_buf_len,
                        uint32_t timeout_ms = 2000);

    // Wait for a notify packet (used after sending raster data).
    // Returns bytes received, 0 on timeout.
    size_t wait_notify(uint8_t* buf, size_t buf_len, uint32_t timeout_ms = 60000);

    // Clear the pending notify buffer.
    void clear_notify();

private:
    bool probe_and_subscribe(PrinterType detected_type);

    PrinterType type_  = PrinterType::Unknown;
    char addr_str_[18] = {};
    char name_str_[32] = {};
    char svc_uuid_[64]    = {};
    char write_uuid_[64]  = {};
    char notify_uuid_[64] = {};

    // NimBLE objects (forward-declared via void* to avoid pulling NimBLE
    // headers into every file that includes ble_printer.h).
    void* client_  = nullptr;  // NimBLEClient*
    void* write_c_ = nullptr;  // NimBLERemoteCharacteristic*
    void* notify_c_ = nullptr; // NimBLERemoteCharacteristic*

    // --- accessed by static notify callback - must be public ---
public:
    volatile bool  notify_ready_              = false;
    volatile size_t notify_len_               = 0;
    uint8_t        notify_buf_[BLE_NOTIFY_BUF_LEN] = {};
};

// Provided by main.cpp. Returns true if the user has requested a print abort.
// Called from send_chunked between BLE packets.
extern bool print_abort_requested();
