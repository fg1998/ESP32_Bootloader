#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Preferences.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "driver/spi_master.h"
#include "esp_vfs_fat.h"
#include "video.h"
#include "charset.h"
#include "logo.h"



#define SD_CLK   14
#define SD_MISO   2
#define SD_MOSI  12
#define SD_CS    13
#define FIRMWARE_FILE  "/firmware.bin"
#define VERSION_FILE   "/version.txt"

#define PS2_DAT 32
#define PS2_CLK 33

#define KEY_UP    0x75
#define KEY_DOWN  0x72
#define KEY_ENTER 0x5A

#define PS2_BUFFER_SIZE 16
volatile uint8_t ps2_buffer[PS2_BUFFER_SIZE];
volatile int ps2_head = 0, ps2_tail = 0;
volatile int ps2_bit = 0;
volatile uint8_t ps2_data = 0;

SPIClass spiSD(HSPI);
Preferences prefs;

RTC_DATA_ATTR int dummy = 0;

#define COLOR_BLACK   (0b000000)
#define COLOR_RED     (0b110000)
#define COLOR_GREEN   (0b001100)
#define COLOR_YELLOW  (0b111100)
#define COLOR_BLUE    (0b000011)
#define COLOR_MAGENTA (0b110011)
#define COLOR_CYAN    (0b001111)
#define COLOR_WHITE   (0b111111)

#include "scroller.h"

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void drawString(int x, int y, const char* str, uint8_t ink, uint8_t paper) {
    uint8_t *crt = _frameBuffer;
    ink   |= (uint8_t)(Video.SBits & 0xFF);
    paper |= (uint8_t)(Video.SBits & 0xFF);
    int charCount = strlen(str);
    for (int row = 0; row < 8; row++) {
        int ix = (y + row) * HRES + x;
        for (int c = 0; c < charCount; c++) {
            char ch = str[c];
            if (ch < 32) ch = 32;
            uint8_t pixels = charsetData[((ch - 32) << 3) + row];
            crt[ix++^2] = (pixels & 0x20) ? ink : paper;
            crt[ix++^2] = (pixels & 0x10) ? ink : paper;
            crt[ix++^2] = (pixels & 0x08) ? ink : paper;
            crt[ix++^2] = (pixels & 0x04) ? ink : paper;
            crt[ix++^2] = (pixels & 0x02) ? ink : paper;
            crt[ix++^2] = (pixels & 0x01) ? ink : paper;
        }
    }
}

void drawLogo(int x, int y) {
    uint8_t *crt = _frameBuffer;
    for (int row = 0; row < LOGO_H; row++) {
        int ix = (y + row) * HRES + x;
        for (int col = 0; col < LOGO_W; col++) {
            uint8_t c = logo_data[row * LOGO_W + col];
            c |= (uint8_t)(Video.SBits & 0xFF);
            crt[ix++^2] = c;
        }
    }
}

void fillRect(int x, int y, int w, int h, uint8_t color) {
    uint8_t *crt = _frameBuffer;
    color |= (uint8_t)(Video.SBits & 0xFF);
    for (int row = y; row < y + h; row++) {
        int ix = row * HRES + x;
        for (int col = 0; col < w; col++) {
            crt[ix++^2] = color;
        }
    }
}

void drawLine(int x1, int y1, int x2, int color) {
    uint8_t *crt = _frameBuffer;
    uint8_t c = color | (uint8_t)(Video.SBits & 0xFF);
    int ix = y1 * HRES + x1;
    for (int x = x1; x <= x2; x++) {
        crt[ix++^2] = c;
    }
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------
int statusY = 94;

void statusLine(const char* label, const char* value, uint8_t color) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%-14s %s", label, value);
    drawString(20, statusY, buf, color, COLOR_BLACK);
    Serial.printf("[%s] %s\n", label, value);
    statusY += 10;
}

void drawProgress(int percent, size_t written, size_t total) {
    char buf[50];
    snprintf(buf, sizeof(buf), "Flashing %3d%%  (%dKB/%dKB)  ",
             percent, (int)(written/1024), (int)(total/1024));
    drawString(20, statusY, buf, COLOR_YELLOW, COLOR_BLACK);
    int barW = (216 * percent) / 100;
    fillRect(20,        statusY + 10, barW,       6, COLOR_GREEN);
    fillRect(20 + barW, statusY + 10, 216 - barW, 6, COLOR_BLACK);
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------
void drawHeader() {
    fillRect(0, 0, HRES, VRES/VDIV, COLOR_BLACK);
    //drawString(115,  8,  "ESP32",        COLOR_YELLOW, COLOR_BLACK);
    //drawString(100,  18, "BOOTLOADER",   COLOR_YELLOW, COLOR_BLACK);
    drawLogo((HRES - LOGO_W) / 2, 4);  // centralizado, Y=4


    drawString(100, 64, "Ver 0.2.1a",    COLOR_WHITE,  COLOR_BLACK);

    drawLine(8, 74, HRES-9, COLOR_BLUE);
    drawString(57, 79, "alternativebits.com/esp32",         COLOR_CYAN,  COLOR_BLACK);
    drawLine(8, 90, HRES-9, COLOR_BLUE);

}

// ---------------------------------------------------------------------------
// NVS
// ---------------------------------------------------------------------------
bool needsUpdate(const char* versionOnSD) {
    prefs.begin("sdloader", true);
    String stored = prefs.getString("version", "");
    prefs.end();
    Serial.printf("NVS: '%s'  SD: '%s'\n", stored.c_str(), versionOnSD);
    return (stored != String(versionOnSD));
}

void saveVersion(const char* version) {
    prefs.begin("sdloader", false);
    size_t written = prefs.putString("version", version);
    if (written == 0) {
        prefs.clear();
        written = prefs.putString("version", version);
    }
    prefs.end();
    if (written == 0) statusLine("NVS", "FULL - cannot save!", COLOR_RED);
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------
void bootEmulatorDirect() {
    Serial.println("Iniciando emulador (direto)...");
    detachInterrupt(digitalPinToInterrupt(PS2_CLK));

    const esp_partition_t* ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (ota0) {
        esp_ota_set_boot_partition(ota0);
        Serial.printf("Boot partition: %s @ 0x%x\n", ota0->label, ota0->address);
    }
    delay(500);
    SD.end(); spiSD.end(); spi_bus_free(HSPI_HOST);
    delay(200);
    ESP.restart();
}

void bootEmulator() {

    Serial.println("Iniciando emulador...");
    detachInterrupt(digitalPinToInterrupt(PS2_CLK));
    
    const esp_partition_t* ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (ota0) esp_ota_set_boot_partition(ota0);
    const esp_partition_t* otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (otadata) {
        esp_partition_erase_range(otadata, 0, otadata->size);
        Serial.println("otadata apagado");
    }
    delay(500);
    SD.end(); spiSD.end(); spi_bus_free(HSPI_HOST);
    delay(500);
    ESP.restart();
}

// ---------------------------------------------------------------------------
// OTA genérico (recebe o caminho do firmware.bin)
// ---------------------------------------------------------------------------
bool doOTA(const char* binPath, const char* versionName) {

    File bin = SD.open(binPath);
    if (!bin) { statusLine("firmware.bin", "OPEN ERROR", COLOR_RED); return false; }

    size_t fileSize = bin.size();
    char sizeStr[40];
    snprintf(sizeStr, sizeof(sizeStr), "FOUND %dKB", (int)(fileSize/1024));
    statusLine("firmware.bin", sizeStr, COLOR_GREEN);

    const esp_partition_t* ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (!ota0) { statusLine("OTA", "ota_0 NOT FOUND!", COLOR_RED); bin.close(); return false; }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(ota0, fileSize, &ota_handle);
    if (err != ESP_OK) {
        char msg[50]; snprintf(msg, sizeof(msg), "ota_begin err:%d", err);
        statusLine("OTA", msg, COLOR_RED); bin.close(); return false;
    }

    size_t written = 0;
    const size_t CHUNK = 4096;
    uint8_t buf[CHUNK];
    while (written < fileSize) {
        size_t rd = bin.read(buf, min(CHUNK, fileSize - written));
        if (rd == 0) break;
        err = esp_ota_write(ota_handle, buf, rd);
        if (err != ESP_OK) {
            char msg[50]; snprintf(msg, sizeof(msg), "ota_write err:%d", err);
            bin.close(); statusLine("OTA", msg, COLOR_RED);
            esp_ota_abort(ota_handle); return false;
        }
        written += rd;
        drawProgress((int)((written * 100) / fileSize), written, fileSize);
    }
    bin.close();

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        char msg[50]; snprintf(msg, sizeof(msg), "ota_end err:%d", err);
        statusLine("OTA", msg, COLOR_RED); return false;
    }

    saveVersion(versionName);
    statusY += 20;
    statusLine("Status", "FLASHED OK!", COLOR_GREEN);
    return true;
}

// ---------------------------------------------------------------------------
// PS2 por interrupção
// ---------------------------------------------------------------------------
void IRAM_ATTR ps2_isr() {
    int dat = digitalRead(PS2_DAT);
    ps2_bit++;
    if (ps2_bit >= 2 && ps2_bit <= 9) {
        ps2_data >>= 1;
        if (dat) ps2_data |= 0x80;
    } else if (ps2_bit == 11) {
        ps2_buffer[ps2_head] = ps2_data;
        ps2_head = (ps2_head + 1) % PS2_BUFFER_SIZE;
        ps2_bit = 0;
        ps2_data = 0;
    }
}

void ps2_init() {
    pinMode(PS2_DAT, INPUT_PULLUP);
    pinMode(PS2_CLK, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PS2_CLK), ps2_isr, FALLING);
}

uint8_t ps2_get_key() {
    while (ps2_head == ps2_tail) delay(1);
    uint8_t code = ps2_buffer[ps2_tail];
    ps2_tail = (ps2_tail + 1) % PS2_BUFFER_SIZE;

    if (code == 0xF0) {
        while (ps2_head == ps2_tail) delay(1);
        ps2_tail = (ps2_tail + 1) % PS2_BUFFER_SIZE;
        return 0;
    }
    if (code == 0xE0) {
        while (ps2_head == ps2_tail) delay(1);
        code = ps2_buffer[ps2_tail];
        ps2_tail = (ps2_tail + 1) % PS2_BUFFER_SIZE;
        if (code == 0xF0) {
            while (ps2_head == ps2_tail) delay(1);
            ps2_tail = (ps2_tail + 1) % PS2_BUFFER_SIZE;
            return 0;
        }
    }
    return code;
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------
#define MAX_ENTRIES  16
#define MENU_Y_START 134
#define MENU_LINE_H  10
#define MAX_VISIBLE  13

struct MenuEntry {
    char name[64];
    char version[64];
    char path[128];
};

MenuEntry menuEntries[MAX_ENTRIES];
int menuCount = 0;

void scanFolders() {
    menuCount = 0;
    File root = SD.open("/");
    if (!root) return;

    while (menuCount < MAX_ENTRIES) {
        File entry = root.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) { entry.close(); continue; }

        char folderPath[128];
        snprintf(folderPath, sizeof(folderPath), "/%s", entry.name());

        char binPath[140], verPath[140];
        snprintf(binPath, sizeof(binPath), "%s/firmware.bin", folderPath);
        snprintf(verPath, sizeof(verPath), "%s/version.txt", folderPath);

        if (SD.exists(binPath) && SD.exists(verPath)) {
            strncpy(menuEntries[menuCount].name, entry.name(), sizeof(menuEntries[0].name)-1);
            strncpy(menuEntries[menuCount].path, folderPath, sizeof(menuEntries[0].path)-1);
            File vf = SD.open(verPath);
            if (vf) {
                String v = vf.readStringUntil('\n'); v.trim();
                strncpy(menuEntries[menuCount].version, v.c_str(), sizeof(menuEntries[0].version)-1);
                vf.close();
            }
            Serial.printf("Menu: %s (%s)\n", menuEntries[menuCount].name, menuEntries[menuCount].version);
            menuCount++;
        }
        entry.close();
    }
    root.close();
}

void drawMenu(int selected, int scrollOffset) {
    fillRect(0, MENU_Y_START - 12, HRES, VRES/VDIV - MENU_Y_START + 12, COLOR_BLACK);
    drawString(20, MENU_Y_START - 10, "SELECT EMULATOR  [UP/DOWN/ENTER]", COLOR_WHITE, COLOR_BLACK);

    int visible = min(menuCount, MAX_VISIBLE);
    for (int i = 0; i < visible; i++) {
        int idx = i + scrollOffset;
        if (idx >= menuCount) break;

        bool isSel = (idx == selected);
        uint8_t ink   = isSel ? COLOR_BLACK : COLOR_GREEN;
        uint8_t paper = isSel ? COLOR_GREEN : COLOR_BLACK;

        prefs.begin("sdloader", true);
        String currentVersion = prefs.getString("version", "");
        prefs.end();
        bool isInstalled = (String(menuEntries[idx].version) == currentVersion);

        char line[42];
        snprintf(line, sizeof(line), "  %s%-36s", isInstalled ? "*" : " ",  menuEntries[idx].name);

        int y = MENU_Y_START + i * MENU_LINE_H;
        fillRect(4, y, HRES - 8, MENU_LINE_H, paper);
        drawString(6, y + 1, line, ink, paper);
    }

    if (menuCount > MAX_VISIBLE) {
        char sc[16];
        snprintf(sc, sizeof(sc), "%d/%d", selected+1, menuCount);
        drawString(HRES - 36, MENU_Y_START - 10, sc, COLOR_CYAN, COLOR_BLACK);
    }
}

int runMenu() {
    if (menuCount == 0) return -1;

    // Acha o item instalado (com *) para usar como selecao inicial
    prefs.begin("sdloader", true);
    String currentVersion = prefs.getString("version", "");
    prefs.end();

    int selected = 0;
    for (int i = 0; i < menuCount; i++) {
        if (String(menuEntries[i].version) == currentVersion) {
            selected = i;
            break;
        }
    }

    int scrollOffset = 0;
    if (selected >= MAX_VISIBLE) scrollOffset = selected - MAX_VISIBLE + 1;

    ps2_init();
    drawMenu(selected, scrollOffset);

    while (true) {
        uint8_t key = ps2_get_key();
        if (key == 0) continue;
        Serial.printf("Key: 0x%02X\n", key);

        if (key == KEY_UP && selected > 0) {
            selected--;
            if (selected < scrollOffset) scrollOffset--;
            drawMenu(selected, scrollOffset);
        } else if (key == KEY_DOWN && selected < menuCount - 1) {
            selected++;
            if (selected >= scrollOffset + MAX_VISIBLE) scrollOffset++;
            drawMenu(selected, scrollOffset);
        } else if (key == KEY_ENTER) {
            return selected;
        }
    }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("Iniciando VGA...");
    videoInit();
    Serial.println("VGA OK");
    drawHeader();
    delay(3000);
    scrollStart();




    Serial.printf("Heap: %d  PSRAM: %s\n", ESP.getFreeHeap(), psramFound() ? "SIM" : "NAO");
    spiSD.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, spiSD)) {
        statusLine("SD Card", "NOT FOUND", COLOR_RED);
        statusLine("Status", "Press any key to retry", COLOR_YELLOW);
        ps2_init();
        while (ps2_get_key() == 0);
        ESP.restart();
        return;
    }


    statusLine("SD Card", "FOUND", COLOR_GREEN);


    // --- Modo 1: firmware.bin + version.txt na raiz ---
    if (SD.exists(FIRMWARE_FILE) && SD.exists(VERSION_FILE)) {
        char versionName[64] = "firmware_sem_versao";
        File vf = SD.open(VERSION_FILE);
        if (vf) {
            String v = vf.readStringUntil('\n'); v.trim();
            strncpy(versionName, v.c_str(), sizeof(versionName)-1);
            vf.close();
        }
        statusLine("version.txt", versionName, COLOR_CYAN);

        if (!needsUpdate(versionName)) {
            statusLine("Status", "Firmware OK - Starting...", COLOR_GREEN);
            delay(1000); SD.end(); bootEmulatorDirect(); return;
        }

        statusLine("Status", "New firmware found!", COLOR_YELLOW);
        bool ok = doOTA(FIRMWARE_FILE, versionName);
        SD.end();
        if (ok) {
            statusLine("Status", "Restarting in 5s...", COLOR_GREEN);
            delay(5000); bootEmulator();
        } else {
            statusLine("Status", "ERROR! Press RESET", COLOR_RED);
            while(true) delay(1000);
        }
        return;
    }

    // --- Modo 2: varre pastas e exibe menu ---
    statusLine("Status", "Scanning folders...", COLOR_YELLOW);
    scanFolders();

    if (menuCount == 0) {
        statusLine("Status", "No emulators found!", COLOR_RED);
        statusLine("Status", "Reset in 5s...", COLOR_YELLOW);
        delay(5000); ESP.restart(); return;
    }

    int selected = runMenu();
    if (selected < 0) {
        statusLine("Status", "No selection!", COLOR_RED);
        delay(3000); ESP.restart(); return;
    }

    // Limpa área de status e mostra o que foi selecionado
    fillRect(0, MENU_Y_START - 12, HRES, VRES/VDIV - MENU_Y_START + 12, COLOR_BLACK);
    statusY = MENU_Y_START;
    statusLine("Selected", menuEntries[selected].name, COLOR_GREEN);

    char binPath[140];
    snprintf(binPath, sizeof(binPath), "%s/firmware.bin", menuEntries[selected].path);
    const char* versionName = menuEntries[selected].version;

    if (!needsUpdate(versionName)) {
        statusLine("Status", "Firmware OK - Starting...", COLOR_GREEN);
        delay(1000); SD.end(); bootEmulatorDirect(); return;
    }

    statusLine("Status", "New firmware found!", COLOR_YELLOW);
    bool ok = doOTA(binPath, versionName);
    SD.end();

    if (ok) {
        statusLine("Status", "Restarting in 5s...", COLOR_GREEN);
        delay(5000); bootEmulator();
    } else {
        statusLine("Status", "ERROR! Press RESET", COLOR_RED);
        while(true) delay(1000);
    }
}

void loop() { delay(1000); }