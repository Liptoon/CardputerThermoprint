# cardputer_thermoprint

Thermal label printer driver for the M5 Cardputer ADV. Prints text and images over BLE to two supported printer families.

## Supported Hardware

| Printer | Width | DPI | Format |
|---|---|---|---|
| Fischero D11s (AiYin) | 96 px / 14 mm | 203 | 30 mm gap labels, landscape |
| Cat GT01 / GB01 / GB02 / GB03 / MX05 / MX06 / MX08 / MX10 / MXTP / YT01 | 384 px / 57 mm | 200 | Continuous roll |

Both printers are connected via BLE. The printer type is detected automatically during scan.

## Features

- Print text typed on the Cardputer keyboard (UTF-8, including Polish characters)
- Print text files and images (JPG, PNG, BMP) from SD card
- Floyd-Steinberg, Atkinson, and mean-threshold dithering for images
- Seven font sizes (5–22 px cell width)
- Configurable print density / thermal energy
- Settings persisted to SD card (`/thermoprint.cfg`)
- Optional verbose diagnostic screen before each print job

## Requirements

- M5 Cardputer ADV
- PlatformIO with VS Code
- SD card (FAT32, files in root directory)
- `stb_image.h` and `stb_image_resize2.h` from [github.com/nothings/stb](https://github.com/nothings/stb) — copy both into `src/`

## Build

1. Clone or unzip the project.
2. Copy `stb_image.h` and `stb_image_resize2.h` into `src/`.
3. Open the project folder in VS Code (PlatformIO detects `platformio.ini` automatically).
4. Click **Build** (✓) then **Upload** (→) in the PlatformIO toolbar.

## Usage

### Navigation

| Key | Action |
|---|---|
| `;` | Up |
| `.` | Down |
| `Enter` | Confirm / select |
| `Backspace` | Back |

### Text editor

| Key | Action |
|---|---|
| `Enter` | Confirm and print |
| `` ` `` | Cancel, return to menu |
| `Opt + Enter` | Insert new line |
| `Backspace` | Delete character |

### SD card

Place files in the SD card root. Supported formats: `.txt`, `.jpg`, `.jpeg`, `.png`, `.bmp`.

## Settings

Access via **Settings** in the main menu.

| Setting | Description |
|---|---|
| Density | Print darkness: 0 Light / 1 Medium / 2 Dark |
| Font | Glyph size index 0–6 (5 px to 22 px cell width) |
| Dither | Image binarisation algorithm |
| Energy | Cat printer thermal energy (0x3000–0xFFFF) |
| Verbose | Show diagnostic info before each print |

Settings are saved automatically to `/thermoprint.cfg` on the SD card.

## Project Structure

```
src/
  main.cpp                 Application entry point and state machine
  ble_printer.h/cpp        NimBLE scan, connect, chunked write, notify
  printer_fischero.h/cpp   Fischero D11s protocol (ESC/POS raster, AiYin commands)
  printer_cat.h/cpp        Cat printer protocol (RLE row encoding, CRC8)
  printer_protocol.h/cpp   Shared BinImage type and allocator
  image_pipeline.h/cpp     stb_image decode, resize, dither, rotate, pack to 1bpp
  text_render.h/cpp        UTF-8 word wrap, bitmap font rendering
  font_data.h              Embedded bitmap font (U+0020–U+017E, 7 sizes)
  ui_menu.h/cpp            Scrollable menu widget
  ui_text_editor.h/cpp     Full-screen text editor
  ui_file_browser.h/cpp    SD card file browser
  config.h/cpp             Settings load/save (plain key=value on SD)
  keyboard.h/cpp           M5Cardputer keyboard wrapper
  screen.h/cpp             Display helpers
  splash.h/cpp             Boot splash screen
  stb_image.h              (add manually)
  stb_image_resize2.h      (add manually)
```

## License

MIT