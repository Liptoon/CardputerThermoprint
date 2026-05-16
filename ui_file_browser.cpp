#include <Arduino.h>
#include <SD.h>
#include "ui_file_browser.h"
#include "ui_menu.h"

static constexpr int MAX_FILES = 64;
static constexpr int MAX_NAME  = 32;
static constexpr const char* PRINT_DIR = "/thermoprint";

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
    // Ensure /thermoprint directory exists.
    if (!SD.exists(PRINT_DIR)) {
        SD.mkdir(PRINT_DIR);
        Serial.printf("[FB] Created directory %s\n", PRINT_DIR);
    }

    File dir = SD.open(PRINT_DIR);
    if (!dir) {
        Serial.printf("[FB] Cannot open %s\n", PRINT_DIR);
        const char* msg[] = {"Cannot open /thermoprint", "(Backspace=back)"};
        menu_run("SD Error", msg, 2);
        return false;
    }

    static char names[MAX_FILES][MAX_NAME];
    int n_files = 0;

    while (n_files < MAX_FILES) {
        File entry = dir.openNextFile();
        if (!entry) break;
        if (entry.isDirectory()) { entry.close(); continue; }

        // Strip any path prefix — keep only the bare filename.
        const char* raw = entry.name();
        // entry.name() may return full path "/thermoprint/file.txt"
        // or just "file.txt" depending on SD lib version.
        const char* slash = strrchr(raw, '/');
        const char* name = slash ? slash + 1 : raw;
        Serial.printf("[FB] found: '%s'\n", name);

        if (ext_match(name, extensions)) {
            strncpy(names[n_files], name, MAX_NAME - 1);
            names[n_files][MAX_NAME - 1] = '\0';
            n_files++;
        }
        entry.close();
    }
    dir.close();

    if (n_files == 0) {
        const char* msg[] = {"No files in /thermoprint", "(Backspace=back)"};
        menu_run("Select File", msg, 2);
        return false;
    }

    const char* ptrs[MAX_FILES];
    for (int i = 0; i < n_files; i++) ptrs[i] = names[i];

    // menu_run returns -1 on Backspace — propagates as false (back to main menu).
    int sel = menu_run("Select File (Bksp=back)", ptrs, n_files);
    if (sel < 0) return false;

    snprintf(selected_path, path_buf_len, "%s/%s", PRINT_DIR, names[sel]);
    return true;
}
