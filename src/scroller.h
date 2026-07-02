#pragma once
#include <fabgl.h>

extern fabgl::Canvas cv;

// ---------------------------------------------------------------------------
// Scroller demoscene - FabGL version
// ---------------------------------------------------------------------------

#define SCROLL_Y      228
#define SCROLL_SPEED  2
#define SCROLL_FPS    40

const char* scrollText =
"                                    "
    " ***   GREETINGS PROGRAMS   ***"
    "  Alternative Bits proudly presents"
    "   *** ESP32 BOOTLOADER by Fernando Garcia (fg1998) ***   "
    "Load multiple emulators from SD card - no computer needed!   "
    "Supported: ESPectrum (ZX Spectrum) - CPCESP (Amstrad) - MSPX (MSX) - VIC20 - PCEmulator - COCO and many others !  "
    "github.com/fg1998/esp32-bootloader   "
    "alternativebits.com/esp32   "
    "Thanks to: EremusONE, Fabrizio Di Vittorio, David Crespo, OulanB, bitluni, VTrucco, leomanes, reyco2000, Rodolfo Guerra, Miguel Roberto, @joselitooliveira24, Sir Clive Sinclair, Guys from RobGo_RG WhatsApp Group, MSX and Spectrum people from Brazil, Uruguay, Spain and around the World !!!!!!!!          "
    "   ";

const Color scrollColors[] = {
    Color::Red, Color::Yellow, Color::BrightGreen, Color::Cyan,
    Color::Blue, Color::Magenta, Color::White
};
#define SCROLL_COLOR_COUNT 7

int scrollPos = 0;
int scrollColorOffset = 0;

// Largura de cada caractere na FONT_8x8
#define SCROLL_CHAR_W 6
#define SCROLL_CHAR_H 12

void scrollDrawFrame() {
    int len = strlen(scrollText);
    int startChar = scrollPos / SCROLL_CHAR_W;
    int pixelOffset = scrollPos % SCROLL_CHAR_W;

    // Limpa linha do scroll
    cv.setBrushColor(Color::Black);
    cv.fillRectangle(0, SCROLL_Y, HRES - 1, SCROLL_Y + SCROLL_CHAR_H - 1);

    // Desenha caracteres
    cv.setGlyphOptions(GlyphOptions().FillBackground(true));
    cv.selectFont(&fabgl::FONT_6x12);

    Color ink = scrollColors[scrollColorOffset / 6 % SCROLL_COLOR_COUNT];

    int charsOnScreen = (HRES / SCROLL_CHAR_W) + 2;
    for (int i = 0; i < charsOnScreen; i++) {
        int charIdx = (startChar + i) % len;
        char buf[2] = { scrollText[charIdx], 0 };
        int x = i * SCROLL_CHAR_W - pixelOffset;
        if (x >= HRES) break;
        cv.setPenColor(ink);
        cv.setBrushColor(Color::Black);
        cv.drawText(x, SCROLL_Y, buf);
    }

    scrollPos = (scrollPos + SCROLL_SPEED) % (len * SCROLL_CHAR_W);
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
        4096,
        NULL,
        1,
        NULL,
        0
    );
}