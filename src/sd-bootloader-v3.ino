#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Update.h>
#include <Preferences.h>
#include "fabgl.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

// ─── RTC Memory - sobrevive ao reset, limpa no power on ──────────────────────
RTC_DATA_ATTR int bootCount = 0;

// ─── Pinos SD (TTGO VGA32 v1.4) ──────────────────────────────────────────────
#define SD_CLK   14
#define SD_MISO   2
#define SD_MOSI  12
#define SD_CS    13

// ─── Arquivos no cartão ───────────────────────────────────────────────────────
#define FIRMWARE_FILE  "/firmware.bin"
#define VERSION_FILE   "/version.txt"

// ─── VGA (igual ao Basic-vga que funciona) ────────────────────────────────────
fabgl::VGAController VGAController;
fabgl::PS2Controller PS2Controller;
fabgl::Terminal      Terminal;

SPIClass spiSD(HSPI);
Preferences prefs;

// ─── Inicializa VGA ───────────────────────────────────────────────────────────
void initVGA() {
    // PS2Controller precisa vir antes do VGAController (requisito FabGL)
    PS2Controller.begin(PS2Preset::KeyboardPort0);
    VGAController.begin(
        GPIO_NUM_22, GPIO_NUM_21,
        GPIO_NUM_19, GPIO_NUM_18,
        GPIO_NUM_5,  GPIO_NUM_4,
        GPIO_NUM_23, GPIO_NUM_15
    );
    //VGAController.setResolution(VGA_640x200_70Hz);  <--- Quando acionado DEU PAU !!! Sei lá pq !
    VGAController.setResolution(VGA_320x200_70Hz);
    
    Terminal.begin(&VGAController);
    Terminal.connectLocally();
    Terminal.clear();
    Terminal.enableCursor(false);
    Terminal.loadFont(&fabgl::FONT_6x8);
}

// ─── Tela de boot ─────────────────────────────────────────────────────────────
void drawHeader() {
    Terminal.write("\e[1;37m Carregamento de Firmware via SD\r\n");
    Terminal.write("\e[0;34m por fg1998  github.com/fg1998\r\n");
    Terminal.write("\e[0;34m ------------------------------------------------\r\n");
}

void drawHeaderANSI() {

Terminal.write("ESP32\r\n");

Terminal.write("\r\n\r\n\r\n\r\n\r\n\r\n");



Terminal.write("\e[1;36m");  // ciano
Terminal.write("ESP32\r\n");
Terminal.write("\e[1;33m");  // amarelo
Terminal.write("BOOTLOADER\r\n\r\n");

/*

    // separador
    Terminal.write("\e[0;34m \xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\r\n");
    Terminal.write("\e[0;32m  by fg1998  \xf9  github.com/fg1998  \xf9  SD Card Firmware Loader\r\n");
    Terminal.write("\e[0;34m \xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\r\n");

    */
    }

// ─── Linha de status ──────────────────────────────────────────────────────────
void statusLine(const char* label, const char* value, const char* color) {
    char buf[80];
    snprintf(buf, sizeof(buf), "\e[0;34m %-16s %s%s\r\n", label, color, value);
    Terminal.write(buf);
    Serial.printf("[%s] %s\n", label, value);
}

// ─── Barra de progresso ───────────────────────────────────────────────────────
void drawProgress(int percent, size_t written, size_t total) {
    char buf[80];
    snprintf(buf, sizeof(buf),
        "\e[0;34m Gravando...     \e[1;33m%3d%%  (%d KB / %d KB)\r",
        percent, (int)(written/1024), (int)(total/1024));
    Terminal.write(buf);
    Serial.printf("Gravando: %d%%\n", percent);
}

// ─── Verifica versão gravada na NVS ──────────────────────────────────────────
bool needsUpdate(const char* versionOnSD) {
    prefs.begin("sdloader", true);
    String stored = prefs.getString("version", "");
    prefs.end();
    Serial.printf("NVS: '%s'  SD: '%s'\n", stored.c_str(), versionOnSD);
    return (stored != String(versionOnSD));
}

void saveVersion(const char* version) {
    prefs.begin("sdloader", false);
    prefs.putString("version", version);
    prefs.end();
}

// ─── Boot para ota_0 sem apagar otadata (boot normal) ────────────────────────
void bootEmulatorDirect() {
    Serial.println("Iniciando emulador (direto)...");
    const esp_partition_t* ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_OTA_0,
        NULL
    );
    if (ota0) {
        esp_ota_set_boot_partition(ota0);
        Serial.printf("Boot partition: %s @ 0x%x\n", ota0->label, ota0->address);
    }
    delay(500);
    ESP.restart();
}

// ─── Boot para ota_0 apagando otadata (após novo firmware) ───────────────────
void bootEmulator() {
    Serial.println("Iniciando emulador...");

    const esp_partition_t* ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_OTA_0,
        NULL
    );
    if (ota0) {
        esp_ota_set_boot_partition(ota0);
        Serial.printf("Boot partition: %s @ 0x%x\n", ota0->label, ota0->address);
    } else {
        Serial.println("ERRO: ota_0 nao encontrada!");
    }

    // Apaga otadata para que na próxima vez a factory rode primeiro
    const esp_partition_t* otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_OTA,
        NULL
    );
    if (otadata) {
        esp_partition_erase_range(otadata, 0, otadata->size);
        Serial.println("otadata apagado - factory vai rodar no proximo boot");
    }

    delay(500);
    ESP.restart();
}

// ─── OTA: grava firmware.bin EXPLICITAMENTE em ota_0 ─────────────────────────
bool performOTA(const char* versionName) {
    File bin = SD.open(FIRMWARE_FILE);
    if (!bin) {
        statusLine("firmware.bin", "ERRO AO ABRIR", "\e[1;31m");
        return false;
    }

    size_t fileSize = bin.size();
    char sizeStr[40];
    snprintf(sizeStr, sizeof(sizeStr), "ENCONTRADO  %d KB", (int)(fileSize/1024));
    statusLine("firmware.bin", sizeStr, "\e[1;32m");

    // Encontra ota_0 EXPLICITAMENTE — nunca deixar o framework escolher
    const esp_partition_t* ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_OTA_0,
        NULL
    );
    if (!ota0) {
        statusLine("OTA", "ota_0 NAO ENCONTRADA!", "\e[1;31m");
        bin.close();
        return false;
    }
    Serial.printf("Gravando em: %s @ 0x%x (tamanho max: %d KB)\n",
        ota0->label, ota0->address, ota0->size/1024);

    // Usa esp_ota_begin para gravar DIRETAMENTE em ota_0
    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(ota0, fileSize, &ota_handle);
    if (err != ESP_OK) {
        char msg[50];
        snprintf(msg, sizeof(msg), "esp_ota_begin falhou: %d", err);
        statusLine("OTA", msg, "\e[1;31m");
        bin.close();
        return false;
    }

    Serial.printf("OTA iniciado. Tamanho: %d bytes\n", fileSize);

    size_t written = 0;
    const size_t CHUNK = 4096;
    uint8_t buf[CHUNK];

    while (written < fileSize) {
        size_t toRead = min(CHUNK, fileSize - written);
        size_t rd = bin.read(buf, toRead);
        if (rd == 0) {
            Serial.println("Leitura retornou 0 bytes - fim do arquivo");
            break;
        }
        err = esp_ota_write(ota_handle, buf, rd);
        if (err != ESP_OK) {
            char msg[50];
            snprintf(msg, sizeof(msg), "esp_ota_write falhou: %d", err);
            bin.close();
            statusLine("OTA", msg, "\e[1;31m");
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
        snprintf(msg, sizeof(msg), "esp_ota_end falhou: %d", err);
        statusLine("OTA", msg, "\e[1;31m");
        return false;
    }

    saveVersion(versionName);
    Terminal.write("\r\n");
    statusLine("Status", "GRAVADO COM SUCESSO!", "\e[1;32m");
    return true;
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    bootCount++;
    Serial.printf("\n=== SD Card Bootloader v1.0 === (boot #%d)\n", bootCount);

    // Se bootCount > 1 é reset interno (após OTA) → boot direto no emulador
    if (bootCount > 1) {
        Serial.println("Reset interno - boot direto");
        bootEmulator();
        return;
    }

    // bootCount == 1 → power on → mostra carregador
    Serial.printf("Heap: %d  PSRAM: %s\n", ESP.getFreeHeap(), psramFound() ? "SIM" : "NAO");

    // Inicializa   
    Serial.println("Iniciando VGA...");
    initVGA();
    Serial.println("VGA OK");
    drawHeader();
    delay(5000);

    // Inicializa SD
    spiSD.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);

    if (!SD.begin(SD_CS, spiSD)) {
        statusLine("Cartao SD", "NAO DETECTADO", "\e[1;31m");
        statusLine("Status", "Iniciando em 5s...", "\e[0;34m");
        Serial.println("SD: nao detectado - boot em 5s");
        delay(5000);
        bootEmulatorDirect();
        return;
    }

    statusLine("Cartao SD", "DETECTADO", "\e[1;32m");

    // Verifica firmware.bin
    if (!SD.exists(FIRMWARE_FILE)) {
        statusLine("firmware.bin", "NAO ENCONTRADO", "\e[1;33m");
        statusLine("Status", "Iniciando em 5s...", "\e[0;34m");
        Serial.println("firmware.bin: nao encontrado - boot em 5s");
        SD.end();
        delay(5000);
        bootEmulatorDirect();
        return;
    }

    // Lê version.txt
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
    statusLine("version.txt", versionName, "\e[1;36m");

    // Já está gravado?
    if (!needsUpdate(versionName)) {
        statusLine("Status", "Firmware OK - iniciando em 5s...", "\e[1;32m");
        Serial.println("Firmware ja gravado - boot em 5s");
        delay(5000);
        SD.end();
        bootEmulatorDirect();
        return;
    }

    // Novo firmware - grava!
    statusLine("Status", "Novo firmware detectado!", "\e[1;33m");
    bool ok = performOTA(versionName);
    SD.end();

    if (ok) {
        statusLine("Status", "Reiniciando em 3s...", "\e[1;32m");
        delay(3000);
        bootEmulator();
    } else {
        statusLine("Status", "FALHOU! Pressione RESET para tentar novamente", "\e[1;31m");
        Serial.println("OTA falhou. Aguardando reset manual.");
        // Fica aqui para sempre - usuario ve o erro na tela
        while(true) { delay(1000); }
    }
}

void loop() {}
