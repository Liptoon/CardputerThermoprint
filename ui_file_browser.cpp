#include <Arduino.h>
#include <SD.h>
#include "ui_file_browser.h"
#include "ui_menu.h"

static constexpr int MAX_FILES = 64;
static constexpr int MAX_NAME  = 32;

static bool ext_match(const char* filename, const char* const* extensions)
{
    const char* dot = strrchr(filename, '.');
    if (!dot) return false;
    for (int i = 0; extensions[i]; i++) {
        if (strcasecmp(dot, extensions[i]) == 0) return true;
    }
    return false;
}

bool file_browser_run(const char* const* extensions,
                       char* selected_path,
                       int   path_buf_len)
{
    // Collect matching files from SD root.
    static char names[MAX_FILES][MAX_NAME];
    int n_files = 0;

    File root = SD.open("/");
    if (!root) {
        Serial.println("[FB] Cannot open SD root");
        return false;
    }

    while (n_files < MAX_FILES) {
        File entry = root.openNextFile();
        if (!entry) break;
        if (entry.isDirectory()) { entry.close(); continue; }

        // entry.name() may return "/filename" or "filename" depending on
        // ESP32 Arduino SD library version. Strip leading slash to normalise.
        const char* raw_name = entry.name();
        const char* name = (raw_name[0] == '/') ? raw_name + 1 : raw_name;
        Serial.printf("[FB] found: '%s'\n", name);

        if (ext_match(name, extensions)) {
            strncpy(names[n_files], name, MAX_NAME - 1);
            names[n_files][MAX_NAME - 1] = '\0';
            n_files++;
        }
        entry.close();
    }
    root.close();

    if (n_files == 0) {
        const char* msg[] = {"(no files found)"};
        menu_run("SD Card", msg, 1);
        return false;
    }

    // Build pointer array for menu_run.
    const char* ptrs[MAX_FILES];
    for (int i = 0; i < n_files; i++) ptrs[i] = names[i];

    int sel = menu_run("Select File", ptrs, n_files);
    if (sel < 0) return false;

    // Build full path.
    snprintf(selected_path, path_buf_len, "/%s", names[sel]);
    return true;
}
