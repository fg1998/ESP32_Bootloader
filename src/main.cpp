#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Update.h>
#include <Preferences.h>
#include "fabgl.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "driver/spi_master.h"
#include "esp_vfs_fat.h"
//extern "C" esp_err_t esp_wifi_stop(void);
//extern "C" esp_err_t esp_wifi_deinit(void);

#define SD_CLK   14
#define SD_MISO   2
#define SD_MOSI  12
#define SD_CS    13
#define FIRMWARE_FILE  "/firmware.bin"
#define VERSION_FILE   "/version.txt"

fabgl::VGAController VGAController;
fabgl::PS2Controller PS2Controller;

 fabgl::Canvas canvas(&VGAController);

SPIClass spiSD(HSPI);
Preferences prefs;

RTC_DATA_ATTR int dummy = 0; // mantém layout RTC memory senão o ES

int statusY = 100;

void initVGA() {
    PS2Controller.begin(PS2Preset::KeyboardPort0, KbdMode::CreateVirtualKeysQueue);
    VGAController.begin(
        GPIO_NUM_22, GPIO_NUM_21,
        GPIO_NUM_19, GPIO_NUM_18,
        GPIO_NUM_5,  GPIO_NUM_4,
        GPIO_NUM_23, GPIO_NUM_15
    );
    VGAController.setResolution(VGA_640x200_70Hz);
}

void drawHeader() {
   

    canvas.setBrushColor(Color::Black);
    canvas.fillRectangle(0, 0, 639, 199);

    // "ESP32" em ciano - fonte grande
    
    canvas.setGlyphOptions(GlyphOptions().FillBackground(false).DoubleWidth(1).Bold(1));
    
    
    canvas.selectFont(&fabgl::FONT_10x20);
    canvas.setPenColor(Color::Yellow);

    canvas.drawText(251, 11, "ESP32");
    canvas.setPenColor(Color::BrightRed);
    canvas.drawText(250, 10, "ESP32");

    // "BOOTLOADER" em amarelo - fonte grande
    canvas.setPenColor(Color::Yellow);
    canvas.drawText(206, 36, "BOOTLOADER");
    canvas.setPenColor(Color::BrightRed);
    canvas.drawText(205, 35, "BOOTLOADER");

    canvas.setPenColor(Color::BrightGreen);
    canvas.setGlyphOptions(GlyphOptions().FillBackground(false).DoubleWidth(0).Bold(0));


    canvas.setPenColor(Color::BrightWhite);
    canvas.selectFont(&fabgl::FONT_4x6);
    canvas.drawText(420,47,"Ver 0.0.4-ALPHA");

    // Info em verde - fonte normal
    //canvas.setPenColor(Color::BrightGreen);
    //canvas.setGlyphOptions(GlyphOptions().FillBackground(false).DoubleWidth(0).Bold(0));

    canvas.setPenColor(Color::BrightCyan);
    canvas.selectFont(&fabgl::FONT_6x8);
  
    canvas.drawText(225, 60, "by Fernando Garcia 'fg1998'");
    canvas.drawText(260, 70, "github.com/fg1998");

    // Subtítulo em ciano
    canvas.setPenColor(Color::BrightBlue);
    canvas.drawText(235, 80, "SD Card Firmware Loader");

    canvas.drawLine(40, 55, 599, 55);
    canvas.drawLine(40, 92, 599, 92);

    canvas.waitCompletion();
  
}

void statusLine(const char* label, const char* value, fabgl::Color color) {
    fabgl::Canvas canvas(&VGAController);
    canvas.selectFont(&fabgl::FONT_6x8);
    canvas.setGlyphOptions(GlyphOptions().FillBackground(true).Bold(0));
    canvas.setBrushColor(Color::Black);

    canvas.setPenColor(Color::BrightBlue);
    canvas.drawText(40, statusY, label);

    canvas.setPenColor(color);
    canvas.drawText(160, statusY, value);

    canvas.waitCompletion();
    Serial.printf("[%s] %s\n", label, value);
    statusY += 10;
}

void drawProgress(int percent, size_t written, size_t total) {
    fabgl::Canvas canvas(&VGAController);
    canvas.selectFont(&fabgl::FONT_6x8);
    canvas.setGlyphOptions(GlyphOptions().FillBackground(true).Bold(0));
    canvas.setBrushColor(Color::Black);

    char buf[60];
    snprintf(buf, sizeof(buf), "Flashing %3d%%  (%d KB / %d KB)  ", percent, (int)(written/1024), (int)(total/1024));
    canvas.setPenColor(Color::BrightYellow);
    canvas.drawText(40, statusY, buf);

    int barW = (500 * percent) / 100;
    canvas.setBrushColor(Color::BrightGreen);
    canvas.fillRectangle(40, statusY + 10, 40 + barW, statusY + 17);
    canvas.setBrushColor(Color::Black);
    canvas.fillRectangle(40 + barW, statusY + 10, 540, statusY + 17);

    canvas.waitCompletion();
    Serial.printf("Gravando: %d%%\n", percent);
}

bool needsUpdate(const char* versionOnSD) {
    prefs.begin("sdloader", true);
    String stored = prefs.getString("version", "");
    prefs.end();
    Serial.printf("NVS: '%s'  SD: '%s'\n", stored.c_str(), versionOnSD);
    return (stored != String(versionOnSD));
}

//void saveVersion(const char* version) {
//    prefs.begin("sdloader", false);
//    prefs.putString("version", version);
//    prefs.end();
//}


//zera a NVS (só no namespace do bootloader) para evitar crashar se a NVS estiver cheia
void saveVersion(const char* version) {
    prefs.begin("sdloader", false);
    size_t written = prefs.putString("version", version);
    
    if (written == 0) {
        prefs.clear();
        written = prefs.putString("version", version);
    }
    prefs.end();
    
    if (written == 0) {
        statusLine("NVS", "FULL - cannot save version!", Color::BrightRed);
    }
}

void bootEmulatorDirect() {
    Serial.println("Iniciando emulador (direto)...");
    VGAController.end();
    PS2Controller.end();
    const esp_partition_t* ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (ota0) {
        esp_ota_set_boot_partition(ota0);
        Serial.printf("Boot partition: %s @ 0x%x\n", ota0->label, ota0->address);
    }
    delay(500);
    SD.end();
    spiSD.end();
    esp_vfs_fat_sdcard_unmount("/sdcard", NULL);
    spi_bus_free(HSPI_HOST);
    delay(200);
    ESP.restart();
}

void bootEmulator() {
    Serial.println("Iniciando emulador...");
    VGAController.end();
    PS2Controller.end();
    const esp_partition_t* ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (ota0) {
        esp_ota_set_boot_partition(ota0);
        Serial.printf("Boot partition: %s @ 0x%x\n", ota0->label, ota0->address);
    } else {
        Serial.println("ERRO: ota_0 nao encontrada!");
    }
    const esp_partition_t* otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (otadata) {
        esp_partition_erase_range(otadata, 0, otadata->size);
        Serial.println("otadata apagado");
    }
    //esp_wifi_stop();
    //esp_wifi_deinit();
    delay(500);
    SD.end();
    spiSD.end();
    spi_bus_free(HSPI_HOST);
    delay(500);
    ESP.restart();
}

bool performOTA(const char* versionName) {
    File bin = SD.open(FIRMWARE_FILE);
    if (!bin) {
        statusLine("firmware.bin", "OPEN ERROR", Color::BrightRed);
        return false;
    }
    size_t fileSize = bin.size();
    char sizeStr[40];
    snprintf(sizeStr, sizeof(sizeStr), "FOUND  %d KB", (int)(fileSize/1024));
    statusLine("firmware.bin", sizeStr, Color::BrightGreen);

    const esp_partition_t* ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (!ota0) {
        statusLine("OTA", "ota_0 NOT FOUND!", Color::BrightRed);
        bin.close();
        return false;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(ota0, fileSize, &ota_handle);
    if (err != ESP_OK) {
        char msg[50];
        snprintf(msg, sizeof(msg), "ota_begin err: %d", err);
        statusLine("OTA", msg, Color::BrightRed);
        bin.close();
        return false;
    }

    size_t written = 0;
    const size_t CHUNK = 4096;
    uint8_t buf[CHUNK];
    while (written < fileSize) {
        size_t rd = bin.read(buf, min(CHUNK, fileSize - written));
        if (rd == 0) break;
        err = esp_ota_write(ota_handle, buf, rd);
        if (err != ESP_OK) {
            char msg[50];
            snprintf(msg, sizeof(msg), "ota_write err: %d", err);
            bin.close();
            statusLine("OTA", msg, Color::BrightRed);
            esp_ota_abort(ota_handle);
            return false;
        }
        written += rd;
        drawProgress((int)((written * 100) / fileSize), written, fileSize);
    }
    bin.close();

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        char msg[50];
        snprintf(msg, sizeof(msg), "ota_end err: %d", err);
        statusLine("OTA", msg, Color::BrightRed);
        return false;
    }

    saveVersion(versionName);
    statusY += 25;
    statusLine("Status", "FLASHED SUCCESSFULLY!", Color::BrightGreen);
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(200);
 
    Serial.println("Iniciando VGA...");
    initVGA();
    Serial.println("VGA OK");
    drawHeader();
    delay(3000);
 
    Serial.printf("Heap: %d  PSRAM: %s\n", ESP.getFreeHeap(), psramFound() ? "SIM" : "NAO");
 
    spiSD.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, spiSD)) {
        statusLine("SD Card", "NOT FOUND", Color::BrightRed);
        statusLine("Status", "Reset in 5s...", Color::BrightYellow);
        delay(5000);
        ESP.restart();
        return;
    }
    statusLine("SD Card", "FOUND", Color::BrightGreen);
 
    if (!SD.exists(FIRMWARE_FILE)) {
        statusLine("firmware.bin", "NOT FOUND", Color::BrightYellow);
        statusLine("Status", "Starting in 5s...", Color::BrightYellow);
        SD.end();
        delay(5000);
        bootEmulatorDirect();
        return;
    }
 
    char versionName[64] = "firmware_sem_versao";
    if (SD.exists(VERSION_FILE)) {
        File vf = SD.open(VERSION_FILE);
        if (vf) {
            String v = vf.readStringUntil('\n');
            v.trim();
            strncpy(versionName, v.c_str(), sizeof(versionName)-1);
            vf.close();
        }
    }
    statusLine("version.txt", versionName, Color::BrightCyan);
 
    if (!needsUpdate(versionName)) {
        statusLine("Status", "Firmware OK - Starting in 5s...", Color::BrightGreen);
        delay(5000);
        SD.end();
        bootEmulatorDirect();
        return;
    }
 
    statusLine("Status", "New firmware found!", Color::BrightYellow);
    bool ok = performOTA(versionName);
    SD.end();
 
    if (ok) {
        statusLine("Status", "Restarting in 5s...", Color::BrightGreen);
        delay(5000);
        bootEmulator();
    } else {
        statusLine("Status", "ERROR! Press RESET", Color::BrightRed);
        while(true) { delay(1000); }
    }
}
 
void loop() {
    for(int i=1; 1<=12; i++) {
    canvas.setPenColor(Color::BrightBlack);
    canvas.drawText(235, 35, "BOOTLOADER");
    delay(200);
    }
    

}