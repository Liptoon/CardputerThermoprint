#include <Arduino.h>
#include <SD.h>
#include "config.h"

void config_defaults(Config& cfg)
{
    memset(cfg.last_addr, 0, sizeof(cfg.last_addr));
    cfg.last_type    = PrinterType::Unknown;
    cfg.density      = 1;
    cfg.font_size_idx = 1;
    cfg.dither       = 0;
    cfg.cat_energy   = 0x8000;
    cfg.verbose_diag = false;
}

bool config_load(Config& cfg)
{
    config_defaults(cfg);

    File f = SD.open(CONFIG_PATH, FILE_READ);
    if (!f) return false;

    char line[64];
    while (f.available()) {
        int len = 0;
        while (f.available() && len < 63) {
            char c = (char)f.read();
            if (c == '\n') break;
            if (c != '\r') line[len++] = c;
        }
        line[len] = '\0';
        if (len == 0 || line[0] == '#') continue;

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = line;
        const char* val = eq + 1;

        if (strcmp(key, "last_addr") == 0) {
            strncpy(cfg.last_addr, val, 17);
            cfg.last_addr[17] = '\0';
        } else if (strcmp(key, "last_type") == 0) {
            int t = atoi(val);
            cfg.last_type = (t == 1) ? PrinterType::Fischero :
                            (t == 2) ? PrinterType::Cat :
                                       PrinterType::Unknown;
        } else if (strcmp(key, "density") == 0) {
            cfg.density = atoi(val);
        } else if (strcmp(key, "font_size_idx") == 0) {
            cfg.font_size_idx = atoi(val);
        } else if (strcmp(key, "dither") == 0) {
            cfg.dither = atoi(val);
        } else if (strcmp(key, "cat_energy") == 0) {
            cfg.cat_energy = (int)strtol(val, nullptr, 0);
        } else if (strcmp(key, "verbose_diag") == 0) {
            cfg.verbose_diag = (strcmp(val, "1") == 0);
        }
    }
    f.close();
    return true;
}

bool config_save(const Config& cfg)
{
    // SD.open with FILE_WRITE creates or truncates.
    SD.remove(CONFIG_PATH);
    File f = SD.open(CONFIG_PATH, FILE_WRITE);
    if (!f) {
        Serial.println("[CFG] Cannot write config");
        return false;
    }
    f.printf("last_addr=%s\n", cfg.last_addr);
    f.printf("last_type=%d\n", (int)cfg.last_type);
    f.printf("density=%d\n",   cfg.density);
    f.printf("font_size_idx=%d\n", cfg.font_size_idx);
    f.printf("dither=%d\n",    cfg.dither);
    f.printf("cat_energy=0x%04X\n", (unsigned)cfg.cat_energy);
    f.printf("verbose_diag=%d\n",   cfg.verbose_diag ? 1 : 0);
    f.close();
    return true;
}
