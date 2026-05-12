#include <Arduino.h>
#include <NimBLEDevice.h>
#include "ble_printer.h"

// ---------------------------------------------------------------------------
// Known service / characteristic UUIDs
// ---------------------------------------------------------------------------

// Fischero D11s (AiYin) - try in order, first found wins.
struct FischeroService {
    const char* svc;
    const char* write_chr;
    const char* notify_chr;
};

static const FischeroService FISCHERO_SVCS[] = {
    {
        "000018f0-0000-1000-8000-00805f9b34fb",
        "00002af1-0000-1000-8000-00805f9b34fb",
        "00002af0-0000-1000-8000-00805f9b34fb"
    },
    {
        "0000ff00-0000-1000-8000-00805f9b34fb",
        "0000ff02-0000-1000-8000-00805f9b34fb",
        "0000ff01-0000-1000-8000-00805f9b34fb"
    },
    {
        "e7810a71-73ae-499d-8c15-faa9aef0c3f2",
        "bef8d6c9-9c25-11e1-9125-0800200c9a66",
        "bef8d6c9-9c25-11e1-9125-0800200c9a66"
    },
    {
        "49535343-fe7d-4ae5-8fa9-9fafd205e455",
        "49535343-8841-43f4-a8d4-ecbe34729bb3",
        "49535343-1e4d-4bd9-ba61-23c647249616"
    },
};
static const int FISCHERO_SVC_COUNT = 4;

// Cat printer service UUIDs (ae30 = GT01/GB0x, af30 = MX series).
static const char* CAT_SVC_GT01  = "0000ae30-0000-1000-8000-00805f9b34fb";
static const char* CAT_SVC_MX10  = "0000af30-0000-1000-8000-00805f9b34fb";
static const char* CAT_TX_UUID   = "0000ae01-0000-1000-8000-00805f9b34fb";
static const char* CAT_RX_UUID   = "0000ae02-0000-1000-8000-00805f9b34fb";

// Cat printer BLE name prefixes.
static const char* CAT_PREFIXES[] = {
    "GB01","GB02","GB03","GT01","YT01",
    "MX05","MX06","MX08","MX10","MXTP",
    nullptr
};

// Fischero BLE name prefixes.
static const char* FISCHERO_PREFIXES[] = {
    "FICHERO","D11s",
    nullptr
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static PrinterType detect_type_from_name(const char* name)
{
    if (!name || name[0] == '\0') return PrinterType::Unknown;

    for (int i = 0; FISCHERO_PREFIXES[i]; i++) {
        if (strncasecmp(name, FISCHERO_PREFIXES[i],
                        strlen(FISCHERO_PREFIXES[i])) == 0)
            return PrinterType::Fischero;
    }
    for (int i = 0; CAT_PREFIXES[i]; i++) {
        if (strncasecmp(name, CAT_PREFIXES[i],
                        strlen(CAT_PREFIXES[i])) == 0)
            return PrinterType::Cat;
    }
    return PrinterType::Unknown;
}

// ---------------------------------------------------------------------------
// Scan callback - finds first matching printer
// ---------------------------------------------------------------------------
struct ScanResult {
    char addr[18];
    char name[32];
    PrinterType type;
    bool found;
};

class ScanCB : public NimBLEAdvertisedDeviceCallbacks {
public:
    ScanResult* result;
    void onResult(NimBLEAdvertisedDevice* dev) override {
        if (result->found) return;
        const char* name = dev->getName().c_str();
        PrinterType t = detect_type_from_name(name);
        if (t == PrinterType::Unknown) return;
        strncpy(result->addr, dev->getAddress().toString().c_str(), 17);
        result->addr[17] = '\0';
        strncpy(result->name, name, 31);
        result->name[31] = '\0';
        result->type  = t;
        result->found = true;
        NimBLEDevice::getScan()->stop();
    }
};

// ---------------------------------------------------------------------------
// Notify callback - called from NimBLE task
// ---------------------------------------------------------------------------
static BLEPrinter* g_notify_target = nullptr;

static void notify_cb(NimBLERemoteCharacteristic* /*chr*/,
                      uint8_t* data, size_t len, bool /*is_notify*/)
{
    BLEPrinter* p = g_notify_target;
    if (!p) return;
    size_t copy = (len < BLE_NOTIFY_BUF_LEN) ? len : BLE_NOTIFY_BUF_LEN;
    memcpy((void*)p->notify_buf_, data, copy);
    p->notify_len_   = copy;
    p->notify_ready_ = true;
}

// ---------------------------------------------------------------------------
// BLEPrinter
// ---------------------------------------------------------------------------

BLEPrinter::BLEPrinter()
{
    // Intentionally empty. Call begin() from setup() after hardware init.
}

void BLEPrinter::begin()
{
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setMTU(256);
}

BLEPrinter::~BLEPrinter()
{
    disconnect();
}

bool BLEPrinter::scan_and_connect(uint32_t timeout_ms)
{
    Serial.println("[BLE] Starting scan...");

    ScanResult result = {};
    ScanCB cb;
    cb.result = &result;

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&cb, false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->start((uint32_t)(timeout_ms / 1000), false);

    if (!result.found) {
        Serial.println("[BLE] Scan timeout, no printer found.");
        return false;
    }

    Serial.printf("[BLE] Found %s [%s] type=%d\n",
                  result.name, result.addr, (int)result.type);

    return connect_by_address(result.addr, result.type, 10000);
}

bool BLEPrinter::connect_by_address(const char* addr,
                                     PrinterType hint,
                                     uint32_t timeout_ms)
{
    NimBLEClient* client = NimBLEDevice::createClient();
    if (!client) {
        Serial.println("[BLE] createClient failed");
        return false;
    }
    client->setConnectionParams(12, 12, 0, 51);
    client->setConnectTimeout((uint32_t)(timeout_ms / 1000));

    NimBLEAddress ble_addr(addr);
    if (!client->connect(ble_addr)) {
        Serial.printf("[BLE] connect failed to %s\n", addr);
        NimBLEDevice::deleteClient(client);
        return false;
    }

    Serial.printf("[BLE] Connected to %s\n", addr);

    client_  = client;
    type_    = hint;
    strncpy(addr_str_, addr, 17);
    addr_str_[17] = '\0';

    // Attempt to get the remote device name.
    name_str_[0] = '\0';

    if (!probe_and_subscribe(hint)) {
        disconnect();
        return false;
    }

    g_notify_target = this;
    return true;
}

bool BLEPrinter::probe_and_subscribe(PrinterType detected_type)
{
    NimBLEClient* client = reinterpret_cast<NimBLEClient*>(client_);

    if (detected_type == PrinterType::Fischero) {
        for (int i = 0; i < FISCHERO_SVC_COUNT; i++) {
            auto* svc = client->getService(FISCHERO_SVCS[i].svc);
            if (!svc) continue;
            auto* wc = svc->getCharacteristic(FISCHERO_SVCS[i].write_chr);
            if (!wc) continue;

            strncpy(svc_uuid_,    FISCHERO_SVCS[i].svc,       63);
            strncpy(write_uuid_,  FISCHERO_SVCS[i].write_chr, 63);
            strncpy(notify_uuid_, FISCHERO_SVCS[i].notify_chr, 63);

            write_c_ = wc;

            auto* nc = svc->getCharacteristic(FISCHERO_SVCS[i].notify_chr);
            if (nc && nc->canNotify()) {
                if (nc->subscribe(true, notify_cb)) {
                    notify_c_ = nc;
                    Serial.printf("[BLE] Fischero: using service %s\n",
                                  FISCHERO_SVCS[i].svc);
                    return true;
                }
            }
            // If notify subscribe fails we can still send - just no responses.
            Serial.printf("[BLE] Fischero: service %s (no notify)\n",
                          FISCHERO_SVCS[i].svc);
            return true;
        }
        Serial.println("[BLE] Fischero: no known service found.");
        return false;
    }

    if (detected_type == PrinterType::Cat) {
        const char* try_svcs[] = { CAT_SVC_GT01, CAT_SVC_MX10, nullptr };
        for (int i = 0; try_svcs[i]; i++) {
            auto* svc = client->getService(try_svcs[i]);
            if (!svc) continue;
            auto* wc = svc->getCharacteristic(CAT_TX_UUID);
            if (!wc) continue;

            strncpy(svc_uuid_,    try_svcs[i],  63);
            strncpy(write_uuid_,  CAT_TX_UUID,  63);
            strncpy(notify_uuid_, CAT_RX_UUID,  63);

            write_c_ = wc;

            auto* nc = svc->getCharacteristic(CAT_RX_UUID);
            if (nc && nc->canNotify()) {
                nc->subscribe(true, notify_cb);
                notify_c_ = nc;
            }
            Serial.printf("[BLE] Cat: using service %s\n", try_svcs[i]);
            return true;
        }
        Serial.println("[BLE] Cat: no known service found.");
        return false;
    }

    Serial.println("[BLE] Unknown printer type.");
    return false;
}

bool BLEPrinter::is_connected()
{
    if (!client_) return false;
    return reinterpret_cast<NimBLEClient*>(client_)->isConnected();
}

void BLEPrinter::disconnect()
{
    g_notify_target = nullptr;
    if (client_) {
        NimBLEClient* c = reinterpret_cast<NimBLEClient*>(client_);
        if (c->isConnected()) c->disconnect();
        NimBLEDevice::deleteClient(c);
        client_   = nullptr;
        write_c_  = nullptr;
        notify_c_ = nullptr;
    }
}

bool BLEPrinter::send_chunked(const uint8_t* data, size_t len,
                               size_t chunk_size, uint32_t delay_ms)
{
    NimBLERemoteCharacteristic* wc =
        reinterpret_cast<NimBLERemoteCharacteristic*>(write_c_);
    if (!wc) return false;

    size_t offset = 0;
    while (offset < len) {
        size_t end = offset + chunk_size;
        if (end > len) end = len;
        if (!wc->writeValue(data + offset, end - offset, false)) {
            Serial.printf("[BLE] write failed at offset %u\n", (unsigned)offset);
            return false;
        }
        offset = end;
        if (delay_ms > 0) delay(delay_ms);
    }
    return true;
}

size_t BLEPrinter::send_command(const uint8_t* cmd, size_t cmd_len,
                                 uint8_t* resp_buf, size_t resp_buf_len,
                                 uint32_t timeout_ms)
{
    clear_notify();
    send_chunked(cmd, cmd_len, 20, 0);
    return wait_notify(resp_buf, resp_buf_len, timeout_ms);
}

size_t BLEPrinter::wait_notify(uint8_t* buf, size_t buf_len,
                                uint32_t timeout_ms)
{
    uint32_t start = millis();
    while (!notify_ready_) {
        if (millis() - start > timeout_ms) return 0;
        delay(10);
    }
    size_t n = (notify_len_ < buf_len) ? notify_len_ : buf_len;
    memcpy(buf, (void*)notify_buf_, n);
    notify_ready_ = false;
    return n;
}

void BLEPrinter::clear_notify()
{
    notify_ready_ = false;
    notify_len_   = 0;
}
