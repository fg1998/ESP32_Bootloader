#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Preferences.h>
#include <WiFi.h>
#include <fabgl.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "logo.h"

#include <SimpleFTPServer.h>

FtpServer ftpSrv;


#define SD_CLK   14
#define SD_MISO   2
#define SD_MOSI  12
#define SD_CS    13
#define FIRMWARE_FILE  "/firmware.bin"
#define VERSION_FILE   "/version.txt"

#define PS2_DAT 32
#define PS2_CLK 33

#define KEY_UP      0x75
#define KEY_Q       0x15
#define KEY_DOWN    0x72
#define KEY_A_NAV   0x1C
#define KEY_ENTER   0x5A
#define KEY_F1      0x05
#define KEY_1       0x16
#define KEY_N_KEY   0x31
#define KEY_A_KEY   0x1C
#define KEY_O_KEY   0x44
#define KEY_R_KEY   0x2D
#define KEY_ESC     0x76
#define KEY_SPACE   0x29

#define SPEAKER_PIN 25
#define VERSION "ver 0.4.0a"

static fabgl::Bitmap* infoBitmap = nullptr;
static char lastBitmapPath[128] = "";

#define HRES  320
#define VRES  240

int statusY = 43;

#define MENU_LINE_H   12
#define MAX_VISIBLE   12   
#define MENU_Y_START  43
#define MAX_ENTRIES   35


#define PANEL_X  165   // começa após a lista (~metade da tela)
#define PANEL_W  (HRES - PANEL_X - 4)
#define PANEL_H  (VRES - MENU_Y_START - 12)

// Buffer global para o info.txt
char infoText[512] = "";

//#include "scroller.h"

const uint32_t AUTOBOOT_MS = 10000;

// ---------------------------------------------------------------------------
// FabGL globals
// ---------------------------------------------------------------------------
fabgl::VGA16Controller DisplayController;
fabgl::Canvas          cv(&DisplayController);

// ---------------------------------------------------------------------------
// PS2 por interrupção (mantém o mesmo driver)
// ---------------------------------------------------------------------------
#define PS2_BUFFER_SIZE 16
volatile uint8_t ps2_buffer[PS2_BUFFER_SIZE];
volatile int ps2_head = 0, ps2_tail = 0;
volatile int ps2_bit = 0;
volatile uint8_t ps2_data = 0;

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

void ps2_shutdown() {
    detachInterrupt(digitalPinToInterrupt(PS2_CLK));
    gpio_reset_pin(GPIO_NUM_32);
    gpio_reset_pin(GPIO_NUM_33);
    WRITE_PERI_REG(0x3ff48094, 0);
}

// ---------------------------------------------------------------------------
// Cores FabGL
// ---------------------------------------------------------------------------
#define C_BLACK   Color::Black
#define C_RED     Color::Red
#define C_GREEN   Color::BrightGreen
#define C_YELLOW  Color::Yellow
#define C_BLUE    Color::Blue
#define C_MAGENTA Color::Magenta
#define C_CYAN    Color::Cyan
#define C_WHITE   Color::White

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void drawString(int x, int y, const char* str, Color ink, Color paper) {
    cv.selectFont(&fabgl::FONT_6x12);
    cv.setPenColor(ink);
    cv.setBrushColor(paper);
    cv.setGlyphOptions(GlyphOptions().FillBackground(true));
    cv.drawText( x, y, str);
}

void drawString(const fabgl::FontInfo* font, int x, int y, const char* str, Color ink, Color paper) {
    cv.selectFont(font);
    cv.setPenColor(ink);
    cv.setBrushColor(paper);
    cv.setGlyphOptions(GlyphOptions().FillBackground(true));
    cv.drawText( x, y, str);
}

void fillRect(int x, int y, int w, int h, Color color) {
    cv.setBrushColor(color);
    cv.fillRectangle(x, y, x + w - 1, y + h - 1);
}

void drawLine(int x1, int y1, int x2, Color color) {
    cv.setPenColor(color);
    cv.drawLine(x1, y1, x2, y1);
}

void drawLogo(int x, int y) {
    // Logo 6-bit -> desenha pixel a pixel
    const int levels[4] = {0, 85, 170, 255};
    for (int row = 0; row < LOGO_H; row++) {
        for (int col = 0; col < LOGO_W; col++) {
            uint8_t c = logo_data[row * LOGO_W + col];
            int r = levels[(c >> 4) & 3];
            int g = levels[(c >> 2) & 3];
            int b = levels[c & 3];
            cv.setPenColor(RGB888(r, g, b));
            cv.setPixel(x + col, y + row);
        }
    }
}

// ---------------------------------------------------------------------------
// Speaker
// ---------------------------------------------------------------------------
void speakerClick() {
    for (int i = 0; i < 50; i++) {
        dacWrite(SPEAKER_PIN, 255);
        delayMicroseconds(500);
        dacWrite(SPEAKER_PIN, 0);
        delayMicroseconds(500);
    }
    dacWrite(SPEAKER_PIN, 128);
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------
SPIClass spiSD(HSPI);
Preferences prefs;



void statusLine(const char* label, const char* value, Color color) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%-14s %s", label, value);
    drawString(20, statusY, buf, color, C_BLACK);
    Serial.printf("[%s] %s\n", label, value);
    statusY += 10;
}

void drawProgress(int percent, size_t written, size_t total) {
    char buf[50];
    snprintf(buf, sizeof(buf), "Flashing %3d%%  (%dKB/%dKB)  ",
             percent, (int)(written/1024), (int)(total/1024));
    drawString(20, statusY, buf, C_YELLOW, C_BLACK);
    int barW = (300 * percent) / 100;
    fillRect(20,        statusY + 15, barW,       6, C_GREEN);
    fillRect(20 + barW, statusY + 15, 280 - barW, 6, C_BLACK);
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
String wifiIP = "";


void wifiInit() {
    if (!SD.exists("/wificonfig.rc")) {
        Serial.println("WiFi: wificonfig.rc nao encontrado");
        return;
    }
    File f = SD.open("/wificonfig.rc");
    if (!f) return;
    String ssid = f.readStringUntil('\n'); ssid.trim();
    String pass = f.readStringUntil('\n'); pass.trim();
    f.close();
    if (ssid.length() == 0) return;
    Serial.printf("WiFi: conectando em '%s'...\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
        delay(500); tries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        wifiIP = WiFi.localIP().toString();
        Serial.printf("WiFi: conectado! IP: %s\n", wifiIP.c_str());
        ftpSrv.begin("esp32", "esp32");
        Serial.println("FTP: iniciado porta 21");
    } else {
        Serial.println("WiFi: falhou");
    }
}


void drawWifiStatus() {
    char buf[30];
    if (wifiIP.length() > 0) {
        snprintf(buf, sizeof(buf), "FTP://%s", wifiIP.c_str());
        drawString(&fabgl::FONT_5x7, 200, 23, buf, C_GREEN, C_BLACK);
    } else {
        drawString(&fabgl::FONT_5x7, 280, 23, "NO FTP", C_RED, C_BLACK);
    }
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------
void drawHeader() {
    fillRect(0, 0, HRES, VRES, C_BLACK);
    drawLogo((HRES - LOGO_W) / 2, 4);
    drawString(130, 19, VERSION, C_WHITE, C_BLACK);
    drawString(80, 29, "www.alternativebits.com/esp32", C_CYAN, C_BLACK);
    drawLine(8, 42, HRES - 9, C_BLUE);
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
    if (written == 0) { prefs.clear(); prefs.putString("version", version); }
    prefs.end();
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------
void bootEmulatorDirect() {
    Serial.println("Iniciando emulador (direto)...");
    ps2_shutdown();
    const esp_partition_t* ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (ota0) {
        esp_ota_set_boot_partition(ota0);
        Serial.printf("Boot partition: %s @ 0x%x\n", ota0->label, ota0->address);
    }
    delay(500);
    gpio_reset_pin(GPIO_NUM_25);
    SD.end(); spiSD.end(); spi_bus_free(HSPI_HOST);
    delay(200);
    ESP.restart();
}

void bootEmulator() {
    Serial.println("Iniciando emulador...");
    ps2_shutdown();
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
    gpio_reset_pin(GPIO_NUM_25);
    SD.end(); spiSD.end(); spi_bus_free(HSPI_HOST);
    delay(500);
    ESP.restart();
}

// ---------------------------------------------------------------------------
// OTA
// ---------------------------------------------------------------------------
bool doOTA(const char* binPath, const char* versionName) {
    File bin = SD.open(binPath);
    if (!bin) { statusLine("firmware.bin", "OPEN ERROR", C_RED); return false; }
    size_t fileSize = bin.size();
    char sizeStr[40];
    snprintf(sizeStr, sizeof(sizeStr), "FOUND %dKB", (int)(fileSize/1024));
    statusLine("firmware.bin", sizeStr, C_GREEN);
    const esp_partition_t* ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (!ota0) { statusLine("OTA", "ota_0 NOT FOUND!", C_RED); bin.close(); return false; }
    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(ota0, fileSize, &ota_handle);
    if (err != ESP_OK) {
        char msg[50]; snprintf(msg, sizeof(msg), "ota_begin err:%d", err);
        statusLine("OTA", msg, C_RED); bin.close(); return false;
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
            bin.close(); statusLine("OTA", msg, C_RED);
            esp_ota_abort(ota_handle); return false;
        }
        written += rd;
        drawProgress((int)((written * 100) / fileSize), written, fileSize);
    }
    bin.close();
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        char msg[50]; snprintf(msg, sizeof(msg), "ota_end err:%d", err);
        statusLine("OTA", msg, C_RED); return false;
    }
    saveVersion(versionName);
    statusY += 20;
    statusLine("Status", "FLASHED OK!", C_GREEN);
    return true;
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------
struct MenuEntry {
    char name[64];
    char version[64];
    char path[128];
};

MenuEntry menuEntries[MAX_ENTRIES];
int menuCount = 0;

void showMaintenanceMenu() {
fillRect(0, MENU_Y_START, HRES, VRES - MENU_Y_START + 12, C_BLACK);
drawString(10, MENU_Y_START + 14,      "*** MENU ***",      C_YELLOW, C_BLACK);
drawString(10, MENU_Y_START + 28, "N - Clear Bootloader NVS", C_WHITE,  C_BLACK);
drawString(10, MENU_Y_START + 42, "A - Clear ALL NVS",        C_WHITE,  C_BLACK);
drawString(10, MENU_Y_START + 56, "O - Clear Otadata",        C_WHITE,  C_BLACK);
drawString(10, MENU_Y_START + 70, "R - Reset ESP32",          C_WHITE,  C_BLACK);
drawString(10, MENU_Y_START + 84, "ESC/SPACE - Cancel",       C_CYAN,   C_BLACK);

    while (true) {
        uint8_t key = ps2_get_key();
        if (key == 0) continue;

        const char* msg  = nullptr;
        const char* done = nullptr;

        if      (key == KEY_N_KEY) { msg = "Clear Bootloader NVS?"; done = "Bootloader NVS cleared!"; }
        else if (key == KEY_A_KEY) { msg = "Clear ALL NVS?";         done = "ALL NVS cleared!"; }
        else if (key == KEY_O_KEY) { msg = "Clear Otadata?";         done = "Otadata cleared!"; }
        else if (key == KEY_R_KEY) { msg = "Reset ESP32?";           done = "Resetting..."; }
        else if (key == KEY_ESC || key == KEY_SPACE) { break; }

        if (!msg) continue;

        fillRect(0, MENU_Y_START - 12, HRES, VRES - MENU_Y_START + 12, C_BLACK);
        drawString(10, MENU_Y_START,      msg,                         C_YELLOW, C_BLACK);
        drawString(10, MENU_Y_START + 14, "Y to confirm, N to cancel", C_WHITE,  C_BLACK);

        uint8_t conf = 0;
        while (conf == 0) conf = ps2_get_key();

        if (conf != 0x35) {
            // N — volta pro menu
            fillRect(0, MENU_Y_START - 12, HRES, VRES - MENU_Y_START + 12, C_BLACK);
            drawString(10, MENU_Y_START,      "*** MAINTENANCE ***",      C_YELLOW, C_BLACK);
            drawString(10, MENU_Y_START + 14, "N - Clear Bootloader NVS", C_WHITE,  C_BLACK);
            drawString(10, MENU_Y_START + 24, "A - Clear ALL NVS",        C_WHITE,  C_BLACK);
            drawString(10, MENU_Y_START + 34, "O - Clear Otadata",        C_WHITE,  C_BLACK);
            drawString(10, MENU_Y_START + 44, "R - Reset ESP32",          C_WHITE,  C_BLACK);
            drawString(10, MENU_Y_START + 58, "ESC/SPACE - Cancel",       C_CYAN,   C_BLACK);
            continue;
        }

        if      (key == KEY_N_KEY) { prefs.begin("sdloader", false); prefs.clear(); prefs.end(); }
        else if (key == KEY_A_KEY) { nvs_flash_erase(); nvs_flash_init(); }
        else if (key == KEY_O_KEY) {
            const esp_partition_t* otadata = esp_partition_find_first(
                ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
            if (otadata) esp_partition_erase_range(otadata, 0, otadata->size);
        }
        else if (key == KEY_R_KEY) { ESP.restart(); }

        fillRect(0, MENU_Y_START - 12, HRES, VRES - MENU_Y_START + 12, C_BLACK);
        drawString(10, MENU_Y_START,      done,                        C_GREEN, C_BLACK);
        drawString(10, MENU_Y_START + 14, "Press any key to reboot",   C_WHITE, C_BLACK);
        while (ps2_get_key() == 0);
        ESP.restart();
    }
}

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
    qsort(menuEntries, menuCount, sizeof(MenuEntry), [](const void* a, const void* b) {
        return strcasecmp(((MenuEntry*)a)->name, ((MenuEntry*)b)->name);
    });
    root.close();
}
void loadInfoText(const char* folderPath) {
    infoText[0] = '\0';
    char infoPath[140];
    snprintf(infoPath, sizeof(infoPath), "%s/info.txt", folderPath);
    if (!SD.exists(infoPath)) return;
    File f = SD.open(infoPath);
    if (!f) return;
    int i = 0;
    while (f.available() && i < 511) {
        infoText[i++] = f.read();
    }
    infoText[i] = '\0';
    f.close();
}

void loadInfoBitmap(const char* folderPath) {
    char bmpPath[140];
    snprintf(bmpPath, sizeof(bmpPath), "%s/info.bmp", folderPath);

    // Já carregado?
    if (strcmp(lastBitmapPath, bmpPath) == 0) return;
    strcpy(lastBitmapPath, bmpPath);

    // Libera bitmap anterior
    if (infoBitmap) {
        free(infoBitmap->data);
        delete infoBitmap;
        infoBitmap = nullptr;
    }

    if (!SD.exists(bmpPath)) return;

    File f = SD.open(bmpPath);
    if (!f) return;

    // Lê header BMP
    uint8_t header[54];
    f.read(header, 54);

    int w = *(int*)&header[18];
    int h = *(int*)&header[22];
    int dataOffset = *(int*)&header[10];
    int bpp = *(short*)&header[28];

    Serial.printf("[BMP] %dx%d bpp=%d\n", w, h, bpp);

    if (bpp != 8 && bpp != 4) {
        Serial.println("[BMP] apenas 4bpp e 8bpp suportados");
        f.close();
        return;
    }

    // Lê paleta (para 4bpp: 16 cores * 4 bytes)
    int paletteSize = (bpp == 4) ? 16 : 256;
    uint8_t palette[256 * 4];
    f.seek(54);
    f.read(palette, paletteSize * 4);

    // Cria bitmap FabGL
    infoBitmap = new fabgl::Bitmap(w, h, nullptr, fabgl::PixelFormat::RGBA8888, false);
    infoBitmap->data = (uint8_t*)ps_malloc(w * h * 4);

    // Lê pixels (BMP é de baixo para cima)
    f.seek(dataOffset);
    int rowSize = ((bpp * w + 31) / 32) * 4;
    uint8_t rowBuf[256];

    for (int y = h - 1; y >= 0; y--) {
        f.read(rowBuf, rowSize);
        for (int x = 0; x < w; x++) {
            int colorIdx;
            if (bpp == 4) {
                colorIdx = (x % 2 == 0) ? (rowBuf[x/2] >> 4) : (rowBuf[x/2] & 0x0F);
            } else {
                colorIdx = rowBuf[x];
            }
            uint8_t b = palette[colorIdx * 4 + 0];
            uint8_t g = palette[colorIdx * 4 + 1];
            uint8_t r = palette[colorIdx * 4 + 2];
            int idx = (y * w + x) * 4;
            infoBitmap->data[idx + 0] = r;
            infoBitmap->data[idx + 1] = g;
            infoBitmap->data[idx + 2] = b;
            infoBitmap->data[idx + 3] = 255;
        }
    }
    f.close();
    Serial.println("[BMP] carregado OK");
}



void drawInfoPanel(int x, int y, int w, int h) {
    // Retângulo branco de borda
    cv.selectFont(&fabgl::FONT_5x7);
    cv.setPenColor(C_WHITE);
    cv.drawRectangle(x, y, x + w, y + h - 21);

    // Área interna com 4px de margem
    fillRect(x + 1, y + 1, w - 2, h - 25, C_BLACK);

    if (infoText[0] == '\0') return;

    cv.selectFont(&fabgl::FONT_5x7);
    cv.setPenColor(C_CYAN);
    cv.setBrushColor(C_BLACK);
    cv.setGlyphOptions(GlyphOptions().FillBackground(true));

    int lineY = y + 6;           // 4px de margem interna vertical
    int lineH = 9;
    int maxChars = (w - 8) / 5; // 4px de margem em cada lado

    char line[64];
    int li = 0;

    for (int i = 0; infoText[i] != '\0'; i++) {
        char c = infoText[i];
        if (c == '\n' || li >= maxChars - 1) {
            line[li] = '\0';
            cv.drawText(x + 6, lineY, line);  // 4px de margem horizontal
            lineY += lineH;
            if (lineY + lineH > y + h - 4) break;
            li = 0;
            if (c == '\n') continue;
        }
        line[li++] = c;
    }
    if (li > 0 && lineY + lineH <= y + h - 4) {
        line[li] = '\0';
        cv.drawText(x + 6, lineY, line);
    }
}







void drawMenu(int selected, int scrollOffset) {





    fillRect(0, MENU_Y_START, HRES, VRES - MENU_Y_START + 12, C_BLACK);
    drawString(10, MENU_Y_START, "SELECT [Q/UP - A/DOWN - ENTER]", C_WHITE,   C_BLACK);
    drawString(220, MENU_Y_START, "F1/1=Menu/Help",                        C_MAGENTA, C_BLACK);

    prefs.begin("sdloader", true);
    String currentVersion = prefs.getString("version", "");
    prefs.end();

    int visible = min(menuCount, MAX_VISIBLE);
    for (int i = 0; i < visible; i++) {
        int idx = i + scrollOffset;
        if (idx >= menuCount) break;
        bool isSel = (idx == selected);
        Color ink   = isSel ? C_BLACK : C_GREEN;
        Color paper = isSel ? C_GREEN : C_BLACK;
        bool isInstalled = (String(menuEntries[idx].version) == currentVersion);
        char line[42];
        snprintf(line, sizeof(line), "%s%-24s", isInstalled ? "*" : " ", menuEntries[idx].name);
        int y = MENU_Y_START + 12 + i * MENU_LINE_H;
        fillRect(8, y, 150, MENU_LINE_H, paper);   // x=8, largura=150 (~metade da tela)
        cv.setPenColor(C_WHITE);
        cv.setBrushColor(C_BLACK);
        cv.selectFont(&fabgl::FONT_6x12);
        drawString(8, y, line, ink, paper);
    }

    //loadInfoText(menuEntries[selected].path);
    //drawInfoPanel(PANEL_X, MENU_Y_START + 12, PANEL_W, PANEL_H);
    drawWifiStatus();
}

int runMenu() {

    cv.setPenColor(C_WHITE);
    cv.setBrushColor(C_BLACK);
    cv.selectFont(&fabgl::FONT_6x12);

    if (menuCount == 0) return -1;

    prefs.begin("sdloader", true);
    String currentVersion = prefs.getString("version", "");
    prefs.end();

    int selected = 0;
    for (int i = 0; i < menuCount; i++) {
        if (String(menuEntries[i].version) == currentVersion) { selected = i; break; }
    }

    int scrollOffset = 0;
    if (selected >= MAX_VISIBLE) scrollOffset = selected - MAX_VISIBLE + 1;

    ps2_init();
    delay(500);
    ps2_head = ps2_tail = 0;
    ps2_bit = 0;
    ps2_data = 0;

    drawMenu(selected, scrollOffset);

    bool hasInstalled = (currentVersion.length() > 0);
    uint32_t autobootStart = millis();

    while (true) {

        


        if (hasInstalled) {
            uint32_t elapsed = millis() - autobootStart;
            if (elapsed >= AUTOBOOT_MS) return selected;
            int secsLeft = (AUTOBOOT_MS - elapsed) / 1000 + 1;
            char countdown[16];
            snprintf(countdown, sizeof(countdown), "%ds ", secsLeft);
            drawString(300, MENU_Y_START - 10, countdown, C_YELLOW, C_BLACK);
        }

        if (ps2_head == ps2_tail) { 
            ftpSrv.handleFTP();  
            continue; 
        }

        uint8_t key = ps2_get_key();
        if (key == 0) continue;

        if (hasInstalled) {
            hasInstalled = false;
            drawString(300, MENU_Y_START - 10, "   ", C_BLACK, C_BLACK);
        }

        speakerClick();
        Serial.printf("Key: 0x%02X\n", key);

        if (key == KEY_F1 || key == KEY_1) {
            showMaintenanceMenu();
            drawMenu(selected, scrollOffset);
            continue;
        }
        if ((key == KEY_UP || key == KEY_Q) && selected > 0) {
            selected--;
            if (selected < scrollOffset) scrollOffset--;
            drawMenu(selected, scrollOffset);
        } else if ((key == KEY_DOWN || key == KEY_A_NAV) && selected < menuCount - 1) {
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
    DisplayController.begin();
    DisplayController.setResolution(QVGA_320x240_60Hz);
   
    cv.selectFont(&fabgl::FONT_7x14);
    Serial.println("VGA OK");

    drawHeader();
    //scrollStart();
    delay(3000);

    Serial.printf("Heap: %d  PSRAM: %s\n", ESP.getFreeHeap(), psramFound() ? "SIM" : "NAO");

    spiSD.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, spiSD)) {
        statusLine("SD Card", "NOT FOUND", C_RED);
        statusLine("Status", "Press any key to retry", C_YELLOW);
        ps2_init();
        while (ps2_get_key() == 0);
        ESP.restart();
        return;
    }

    statusLine("SD Card", "FOUND", C_GREEN);

    wifiInit();
    //drawWifiStatus();

    // --- Modo 1: firmware.bin + version.txt na raiz ---
    if (SD.exists(FIRMWARE_FILE) && SD.exists(VERSION_FILE)) {
        char versionName[64] = "firmware_sem_versao";
        File vf = SD.open(VERSION_FILE);
        if (vf) {
            String v = vf.readStringUntil('\n'); v.trim();
            strncpy(versionName, v.c_str(), sizeof(versionName)-1);
            vf.close();
        }
        statusLine("version.txt", versionName, C_CYAN);
        if (!needsUpdate(versionName)) {
            statusLine("Status", "Firmware OK - Starting...", C_GREEN);
            delay(1000); SD.end(); bootEmulatorDirect(); return;
        }
        statusLine("Status", "New firmware found!", C_YELLOW);
        bool ok = doOTA(FIRMWARE_FILE, versionName);
        SD.end();
        if (ok) { statusLine("Status", "Restarting in 3s...", C_GREEN); delay(3000); bootEmulator(); }
        else { statusLine("Status", "ERROR! Press RESET", C_RED); while(true) delay(1000); }
        return;
    }

    // --- Modo 2: menu ---
    statusLine("Status", "Reading", C_YELLOW);
    scanFolders();

    if (menuCount == 0) {
        statusLine("Status", "No emulators found!", C_RED);
        delay(5000); ESP.restart(); return;
    }

    int selected = runMenu();
    if (selected < 0) { delay(3000); ESP.restart(); return; }

    fillRect(0, MENU_Y_START - 12, HRES, VRES - MENU_Y_START + 12, C_BLACK);
    statusY = MENU_Y_START;
    statusLine("Selected", menuEntries[selected].name, C_GREEN);

    char binPath[140];
    snprintf(binPath, sizeof(binPath), "%s/firmware.bin", menuEntries[selected].path);
    const char* versionName = menuEntries[selected].version;

    if (!needsUpdate(versionName)) {
        statusLine("Status", "Firmware OK - Starting...", C_GREEN);
        delay(1000); SD.end(); bootEmulatorDirect(); return;
    }

    statusLine("Status", "New firmware found!", C_YELLOW);
    bool ok = doOTA(binPath, versionName);
    SD.end();

    if (ok) { statusLine("Status", "Restarting in 3s...", C_GREEN); delay(3000); bootEmulator(); }
    else { statusLine("Status", "ERROR! Press RESET", C_RED); while(true) delay(1000); }



    
}

void loop() {

}