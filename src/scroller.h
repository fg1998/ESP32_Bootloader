#pragma once

// ---------------------------------------------------------------------------
// Scroller demoscene - texto rolando da direita para a esquerda com rainbow
// ---------------------------------------------------------------------------

#define SCROLL_Y      228   // linha Y do scroll (parte inferior)
#define SCROLL_H      8     // altura em pixels (fonte 6x8)
#define SCROLL_SPEED  2     // pixels por frame
#define SCROLL_FPS    30    // frames por segundo

const char* scrollText =
"                                    "
    " ***   GREETINGS PROGRAMS   ***"
    "  Alternative Bits proudly presents"
    "   *** ESP32 BOOTLOADER by Fernando Garcia (fg1998) ***   "
    "Load multiple emulators from SD card - no computer needed!   "
    "Supported: ESPectrum (ZX Spectrum) - CPCESP (Amstrad) - MSPX (MSX) - VIC20 - PCEmulator - COCO and many others !  "
    "github.com/fg1998/esp32-bootloader   "
    "alternativebits.com/esp32   "      
    "Thanks to: EremusONE, Fabrizio Di Vittorio, David Crespo, OulanB, bitluni, VTrucco, reyco2000, Rodolfo Guerra, Miguel Roberto, @joselitooliveira24, Sir Clive Sinclair, Guys from RobGo_RG WhatsApp Group (don't be shy, everyone!), MSX and Spectrum people from Brazil, Uruguay, Spain and around the World !!!!!!!!          "
    "   ";


const uint8_t scrollColors[] = {
    COLOR_RED, COLOR_YELLOW, COLOR_GREEN, COLOR_CYAN,
    COLOR_BLUE, COLOR_MAGENTA, COLOR_WHITE
};
#define SCROLL_COLOR_COUNT 7

int scrollPos = 0;
int scrollColorOffset = 0;

void scrollDrawChar(int x, char ch, uint8_t ink, uint8_t paper) {
    if (ch < 32) ch = 32;
    uint8_t *crt = _frameBuffer;
    ink   |= (uint8_t)(Video.SBits & 0xFF);
    paper |= (uint8_t)(Video.SBits & 0xFF);

    for (int row = 0; row < 8; row++) {
        uint8_t pixels = charsetData[((ch - 32) << 3) + row];
        int screenY = SCROLL_Y + row;
        if (screenY >= VRES/VDIV) continue;

        for (int col = 0; col < 6; col++) {
            int screenX = x + col;
            if (screenX < 0 || screenX >= HRES) continue;
            int ix = screenY * HRES + screenX;
            uint8_t bit = (pixels >> (5 - col)) & 1;
            crt[ix ^ 2] = bit ? ink : paper;
        }
    }
}

void scrollDrawFrame() {
    // Limpa a linha do scroll
    uint8_t bg = COLOR_BLACK | (uint8_t)(Video.SBits & 0xFF);
    uint8_t *crt = _frameBuffer;
    for (int row = SCROLL_Y; row < SCROLL_Y + SCROLL_H; row++) {
        if (row >= VRES/VDIV) break;
        int ix = row * HRES;
        for (int col = 0; col < HRES; col++) {
            crt[(ix + col) ^ 2] = bg;
        }
    }

    // Desenha os caracteres visíveis com cor rainbow
    int len = strlen(scrollText);
    int startChar = scrollPos / 6;
    int pixelOffset = scrollPos % 6;

    for (int i = 0; i < (HRES / 6) + 2; i++) {
        int charIdx = (startChar + i) % len;
        uint8_t ink = scrollColors[(i + scrollColorOffset) % SCROLL_COLOR_COUNT];
        int x = i * 6 - pixelOffset;
        scrollDrawChar(x, scrollText[charIdx], ink, COLOR_BLACK);
    }

    // Avança cor e posição
    scrollPos = (scrollPos + SCROLL_SPEED) % (len * 6);
    scrollColorOffset = (scrollColorOffset + 1) % (SCROLL_COLOR_COUNT * 6);
}

void scrollTask(void* param) {
    while (true) {
        scrollDrawFrame();
        vTaskDelay(pdMS_TO_TICKS(1000 / SCROLL_FPS));
    }
}

void scrollStart() {
    xTaskCreatePinnedToCore(
        scrollTask,
        "scroller",
        2048,
        NULL,
        1,
        NULL,
        0
    );
}

