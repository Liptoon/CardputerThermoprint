#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>

#include "keyboard.h"
#include "screen.h"
#include "splash.h"

#include "ble_printer.h"
#include "printer_protocol.h"
#include "printer_fischero.h"
#include "printer_cat.h"
#include "image_pipeline.h"
#include "text_render.h"
#include "ui_menu.h"
#include "ui_text_editor.h"
#include "ui_file_browser.h"
#include "config.h"

// ---------------------------------------------------------------------------
// Global state.
// ---------------------------------------------------------------------------
static BLEPrinter g_ble;
static Config     g_cfg;
static bool       g_sd_ok   = false;
static bool       g_connected = false;

// SD SPI instance - pointer so construction is deferred to setup().
// Assigned in setup() after M5Cardputer.begin().
static SPIClass*  g_sd_spi  = nullptr;

// Reinitialise SD after directory traversal leaves stale handles.
static bool sd_reinit()
{
    if (!g_sd_spi) return false;
    SD.end();
    return SD.begin(M5.getPin(m5::pin_name_t::sd_spi_ss), *g_sd_spi);
}

// ---------------------------------------------------------------------------
// Screen helpers (supplement your screen.h with colour variants).
// ---------------------------------------------------------------------------
static void show_message(const char* line1,
                          const char* line2 = nullptr,
                          const char* line3 = nullptr)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(0, 10);
    if (line1) M5.Display.println(line1);
    if (line2) M5.Display.println(line2);
    if (line3) M5.Display.println(line3);
}

static void show_error(const char* msg)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.setCursor(0, 10);
    M5.Display.println("ERROR:");
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.println(msg);
    M5.Display.println();
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.println("(any key)");
    char c; while (!keyPressed(c)) delay(50);
}

static void show_ok(const char* msg)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.setCursor(0, 10);
    M5.Display.println("OK:");
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.println(msg);
    delay(1200);
}

// ---------------------------------------------------------------------------
// Print a BinImage on the connected printer.
// ---------------------------------------------------------------------------
static bool do_print(const BinImage& img)
{
    if (!g_connected || !g_ble.is_connected()) {
        show_error("Not connected");
        return false;
    }

    show_message("Printing...", "Please wait");

    bool ok = false;
    if (g_ble.printer_type() == PrinterType::Fischero) {
        ok = fischero_print(&g_ble, img, g_cfg.density);
    } else if (g_ble.printer_type() == PrinterType::Cat) {
        uint16_t energy = (g_cfg.cat_energy > 0)
            ? (uint16_t)g_cfg.cat_energy
            : cat_density_to_energy(g_cfg.density);
        ok = cat_print(&g_ble, img, energy);
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Determine print geometry from connected printer type.
// ---------------------------------------------------------------------------
static void get_print_geometry(int& target_w, int& target_h, bool& rotate)
{
    if (g_ble.printer_type() == PrinterType::Fischero) {
        target_w = FISCHERO_PRINT_WIDTH;          // 96
        target_h = FISCHERO_LABEL_30MM_DOTS;      // 240
        rotate   = true;
    } else {
        target_w = CAT_PRINT_WIDTH;               // 384
        target_h = 0;                             // grows with content
        rotate   = false;
    }
}

// ---------------------------------------------------------------------------
// Action: Print Text (typed by user).
// ---------------------------------------------------------------------------
static void action_print_text()
{
    static char text_buf[512];
    text_buf[0] = '\0';

    if (!text_editor_run(text_buf, sizeof(text_buf))) return;
    if (text_buf[0] == '\0') return;

    show_message("Rendering...");

    int tw, th;
    bool rot;
    get_print_geometry(tw, th, rot);

    BinImage img;
    bool ok = render_text_label(text_buf, tw, th, g_cfg.font_size_idx, rot, img);

    if (g_cfg.verbose_diag) {
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(ok ? TFT_GREEN : TFT_RED, TFT_BLACK);
        M5.Display.setCursor(0, 0);
        M5.Display.printf("render:%s\n", ok ? "OK" : "FAIL");
        if (ok)
            M5.Display.printf("img:%dx%d rb:%d\n",
                              img.width, img.height, img.row_bytes());
        M5.Display.printf("PSRAM:%u SRAM:%u\n",
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        M5.Display.println("(any key)");
        char c; while (!keyPressed(c)) delay(50);
    }

    if (!ok) {
        show_error("Render failed");
        return;
    }

    bool printed = do_print(img);
    img.free_data();
    if (printed) show_ok("Printed!");
    else         show_error("Print failed");
}

// ---------------------------------------------------------------------------
// Action: Print File from SD (image or text).
// ---------------------------------------------------------------------------
static void action_print_file()
{
    if (!g_sd_ok) {
        show_error("No SD card");
        return;
    }

    static const char* EXTS[] = {
        ".jpg", ".jpeg", ".png", ".bmp", ".txt", nullptr
    };
    static char path[64];
    path[0] = '\0';

    if (!file_browser_run(EXTS, path, sizeof(path))) return;

    // Reinitialise SD after directory traversal - the ESP32 SD library
    // leaves stale internal handles after openNextFile() loops that prevent
    // subsequent SD.open() calls from succeeding.
    if (!sd_reinit()) {
        show_error("SD reinit failed");
        return;
    }

    // Open the file exactly once here and pass the File object to the
    // load functions - avoids the double-open bug where SD.open() only
    // works once after sd_reinit().
    File f = SD.open(path, FILE_READ);
    if (!f) {
        show_error("SD.open failed");
        return;
    }
    Serial.printf("[SD] Opened %s size=%u\n", path, (unsigned)f.size());

    show_message("Loading...", path);

    int tw, th;
    bool rot;
    get_print_geometry(tw, th, rot);

    const char* dot = strrchr(path, '.');
    bool is_text = (dot && strcasecmp(dot, ".txt") == 0);

    BinImage img;
    bool load_ok = false;

    if (is_text) {
        char* text = load_text_file_sd(f);
        f.close();

        if (!text) {
            show_error("Text read failed");
            return;
        }

        if (g_cfg.verbose_diag) {
            M5.Display.fillScreen(TFT_BLACK);
            M5.Display.setTextSize(1);
            M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            M5.Display.setCursor(0, 0);
            M5.Display.printf("len:%d tw:%d th:%d rot:%d\n",
                              (int)strlen(text), tw, th, rot);
            M5.Display.printf("font:%d PSRAM:%u SRAM:%u\n",
                              g_cfg.font_size_idx,
                              (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            char preview[41];
            strncpy(preview, text, 40); preview[40] = '\0';
            for (int i = 0; i < 40 && preview[i]; i++)
                if ((uint8_t)preview[i] < 0x20 && preview[i] != '\n')
                    preview[i] = '?';
            M5.Display.printf("txt:[%.40s]\n", preview);
            M5.Display.println("(any key to render)");
            char c; while (!keyPressed(c)) delay(50);
        }

        load_ok = render_text_label(text, tw, th, g_cfg.font_size_idx, rot, img);
        free(text);

        if (g_cfg.verbose_diag) {
            M5.Display.fillScreen(TFT_BLACK);
            M5.Display.setTextSize(1);
            M5.Display.setTextColor(load_ok ? TFT_GREEN : TFT_RED, TFT_BLACK);
            M5.Display.setCursor(0, 0);
            M5.Display.printf("render:%s\n", load_ok ? "OK" : "FAIL");
            if (load_ok)
                M5.Display.printf("img:%dx%d rb:%d\n",
                                  img.width, img.height, img.row_bytes());
            M5.Display.println("(any key)");
            char c; while (!keyPressed(c)) delay(50);
        }

        if (!load_ok) {
            show_error("Render failed");
            return;
        }
    } else {
        Dither d = (Dither)g_cfg.dither;
        load_ok = load_image_from_sd(f, tw, th, d, rot, img);
        f.close();
        if (!load_ok) {
            show_error("Load/render failed");
            return;
        }
    }

    bool ok = do_print(img);
    img.free_data();
    if (ok) show_ok("Printed!");
    else    show_error("Print failed");
}

// ---------------------------------------------------------------------------
// Action: Status screen.
// ---------------------------------------------------------------------------
static void action_status()
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(0, 0);

    if (!g_connected || !g_ble.is_connected()) {
        M5.Display.println("Not connected");
    } else if (g_ble.printer_type() == PrinterType::Fischero) {
        char model[20] = "?";
        char fw[20]    = "?";
        int  bst = -1, bpct = -1;
        uint8_t status_byte = 0xFF;
        fischero_get_model(&g_ble, model, sizeof(model));
        fischero_get_firmware(&g_ble, fw, sizeof(fw));
        fischero_get_battery(&g_ble, &bst, &bpct);
        fischero_get_status(&g_ble, &status_byte);
        char status_str[64];
        fischero_decode_status(status_byte, status_str);

        M5.Display.println("-- Fischero D11s --");
        M5.Display.printf("Model:    %s\n", model);
        M5.Display.printf("FW:       %s\n", fw);
        M5.Display.printf("Battery:  %d%%\n", bpct);
        M5.Display.printf("Status:   %s\n", status_str);
        M5.Display.printf("Addr: %s\n", g_ble.printer_address());
    } else {
        M5.Display.println("-- Cat Printer --");
        M5.Display.printf("Addr: %s\n", g_ble.printer_address());
        M5.Display.println("Status: Connected");
        M5.Display.println("(no query cmd)");
    }

    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.println("\n(any key)");
    char c; while (!keyPressed(c)) delay(50);
}

// ---------------------------------------------------------------------------
// Action: Settings menu.
// ---------------------------------------------------------------------------
static void action_settings()
{
    while (true) {
        char den_str[16], font_str[16], dith_str[24], energy_str[16], verb_str[20];
        snprintf(den_str,    sizeof(den_str),    "Density: %d", g_cfg.density);
        snprintf(font_str,   sizeof(font_str),   "Font:    %d", g_cfg.font_size_idx);
        const char* dith_names[] = {"Floyd", "Atkinson", "Mean", "None"};
        int di = g_cfg.dither;
        if (di < 0 || di > 3) di = 0;
        snprintf(dith_str,   sizeof(dith_str),   "Dither:  %s", dith_names[di]);
        snprintf(energy_str, sizeof(energy_str), "Energy:0x%04X", (unsigned)g_cfg.cat_energy);
        snprintf(verb_str,   sizeof(verb_str),   "Verbose: %s",
                 g_cfg.verbose_diag ? "ON" : "OFF");

        const char* items[] = {
            den_str, font_str, dith_str, energy_str, verb_str, "Back"
        };
        int sel = menu_run("Settings", items, 6);
        if (sel < 0 || sel == 5) break;

        if (sel == 0) {
            const char* opts[] = {"0 Light","1 Medium","2 Dark"};
            int r = menu_run("Density", opts, 3);
            if (r >= 0) g_cfg.density = r;
        } else if (sel == 1) {
            const char* opts[] = {"0 (5px)","1 (8px)","2 (11px)",
                                   "3 (14px)","4 (16px)","5 (19px)","6 (22px)"};
            int r = menu_run("Font Size", opts, 7);
            if (r >= 0) g_cfg.font_size_idx = r;
        } else if (sel == 2) {
            const char* opts[] = {"Floyd-Steinberg","Atkinson","Mean-Threshold","None"};
            int r = menu_run("Dither", opts, 4);
            if (r >= 0) g_cfg.dither = r;
        } else if (sel == 3) {
            const char* opts[] = {"0x3000 Light","0x8000 Medium","0xFFFF Dark"};
            int r = menu_run("Cat Energy", opts, 3);
            if (r == 0) g_cfg.cat_energy = 0x3000;
            else if (r == 1) g_cfg.cat_energy = 0x8000;
            else if (r == 2) g_cfg.cat_energy = 0xFFFF;
        } else if (sel == 4) {
            g_cfg.verbose_diag = !g_cfg.verbose_diag;
        }

        if (g_sd_ok) config_save(g_cfg);
    }
}

// ---------------------------------------------------------------------------
// BLE scan + connect.
// ---------------------------------------------------------------------------
static bool do_connect(bool force_rescan = false)
{
    // Try last known address first (unless forced rescan).
    if (!force_rescan && g_cfg.last_addr[0] != '\0' &&
        g_cfg.last_type != PrinterType::Unknown) {

        show_message("Reconnecting...", g_cfg.last_addr);
        if (g_ble.connect_by_address(g_cfg.last_addr, g_cfg.last_type, 8000)) {
            g_connected = true;
            return true;
        }
    }

    show_message("Scanning BLE...", "up to 15s");
    if (!g_ble.scan_and_connect(15000)) {
        show_error("No printer found");
        g_connected = false;
        return false;
    }

    g_connected = true;
    strncpy(g_cfg.last_addr, g_ble.printer_address(), 17);
    g_cfg.last_addr[17] = '\0';
    g_cfg.last_type = g_ble.printer_type();
    if (g_sd_ok) config_save(g_cfg);
    return true;
}

// ---------------------------------------------------------------------------
// Main menu.
// ---------------------------------------------------------------------------
static void run_main_menu()
{
    while (true) {
        char conn_str[40];
        if (g_connected && g_ble.is_connected()) {
            const char* type = (g_ble.printer_type() == PrinterType::Fischero)
                ? "Fischero" : "Cat";
            snprintf(conn_str, sizeof(conn_str), "%s %s",
                     type, g_ble.printer_address());
        } else {
            snprintf(conn_str, sizeof(conn_str), "(not connected)");
        }

        const char* items[] = {
            "Print Text",
            "Print File (SD)",
            "Status",
            "Settings",
            "Reconnect",
            "Scan (new)"
        };
        int sel = menu_run(conn_str, items, 6);

        switch (sel) {
            case 0: action_print_text(); break;
            case 1: action_print_file(); break;
            case 2: action_status();     break;
            case 3: action_settings();   break;
            case 4: do_connect(false);   break;
            case 5: do_connect(true);    break;
            default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------------
void setup()
{
    M5Cardputer.begin();
    Serial.begin(115200);
    Serial.println("[BOOT] thermoprint starting");

    g_ble.begin();

    screenInit();
    showSplash();

    // SD card init using a second SPI bus instance (HSPI) created locally
    // inside setup() - not as a global, to avoid static init side effects
    // that break the keyboard I2C bus.
    show_message("Init SD...");
    static SPIClass sd_spi(HSPI);
    g_sd_spi = &sd_spi;
    sd_spi.begin(
        M5.getPin(m5::pin_name_t::sd_spi_sclk),
        M5.getPin(m5::pin_name_t::sd_spi_miso),
        M5.getPin(m5::pin_name_t::sd_spi_mosi),
        M5.getPin(m5::pin_name_t::sd_spi_ss));
    if (SD.begin(M5.getPin(m5::pin_name_t::sd_spi_ss), sd_spi)) {
        g_sd_ok = true;
        Serial.printf("[SD] OK type=%d size=%llu MB\n",
                      SD.cardType(), SD.cardSize() / (1024 * 1024));
        show_message("SD OK");
        delay(500);
        config_load(g_cfg);
    } else {
        Serial.println("[SD] init failed");
        config_defaults(g_cfg);
        show_message("SD card not found", "Continuing...");
        delay(1500);
    }

    // Initial BLE connect attempt.
    do_connect(false);

    run_main_menu();
}

void loop()
{
    // run_main_menu() never returns; loop() is unused.
    delay(1000);
}
