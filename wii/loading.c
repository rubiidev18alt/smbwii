#include "loading.h"

#include <stddef.h>
#include <stdint.h>

#include "video.h"
#include "wii_video.h"

#define RGB_FRAME_BYTES (SCREEN_WIDTH * SCREEN_HEIGHT * 3)
#define FONT_WIDTH 5
#define FONT_HEIGHT 7
#define FONT_SPACING 1

typedef struct {
    char character;
    uint8_t rows[FONT_HEIGHT];
} FontGlyph;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} Color;

// Compact 5x7 bitmap font for the title and status text on the loader screen.
static const FontGlyph font[] = {
    { 'A', { 14, 17, 17, 31, 17, 17, 17 } },
    { 'B', { 30, 17, 17, 30, 17, 17, 30 } },
    { 'C', { 14, 17, 16, 16, 16, 17, 14 } },
    { 'D', { 30, 17, 17, 17, 17, 17, 30 } },
    { 'E', { 31, 16, 16, 30, 16, 16, 31 } },
    { 'F', { 31, 16, 16, 30, 16, 16, 16 } },
    { 'G', { 14, 17, 16, 23, 17, 17, 15 } },
    { 'H', { 17, 17, 17, 31, 17, 17, 17 } },
    { 'I', { 14, 4, 4, 4, 4, 4, 14 } },
    { 'J', { 7, 2, 2, 2, 18, 18, 12 } },
    { 'K', { 17, 18, 20, 24, 20, 18, 17 } },
    { 'L', { 16, 16, 16, 16, 16, 16, 31 } },
    { 'M', { 17, 27, 21, 21, 17, 17, 17 } },
    { 'N', { 17, 25, 21, 19, 17, 17, 17 } },
    { 'O', { 14, 17, 17, 17, 17, 17, 14 } },
    { 'P', { 30, 17, 17, 30, 16, 16, 16 } },
    { 'Q', { 14, 17, 17, 17, 21, 18, 13 } },
    { 'R', { 30, 17, 17, 30, 20, 18, 17 } },
    { 'S', { 15, 16, 16, 14, 1, 1, 30 } },
    { 'T', { 31, 4, 4, 4, 4, 4, 4 } },
    { 'U', { 17, 17, 17, 17, 17, 17, 14 } },
    { 'V', { 17, 17, 17, 17, 17, 10, 4 } },
    { 'W', { 17, 17, 17, 21, 21, 21, 10 } },
    { 'X', { 17, 17, 10, 4, 10, 17, 17 } },
    { 'Y', { 17, 17, 10, 4, 4, 4, 4 } },
    { 'Z', { 31, 1, 2, 4, 8, 16, 31 } },
    { '0', { 14, 17, 19, 21, 25, 17, 14 } },
    { '1', { 4, 12, 4, 4, 4, 4, 14 } },
    { '2', { 14, 17, 1, 2, 4, 8, 31 } },
    { '3', { 30, 1, 1, 14, 1, 1, 30 } },
    { '4', { 2, 6, 10, 18, 31, 2, 2 } },
    { '5', { 31, 16, 16, 30, 1, 1, 30 } },
    { '6', { 14, 16, 16, 30, 17, 17, 14 } },
    { '7', { 31, 1, 2, 4, 8, 8, 8 } },
    { '8', { 14, 17, 17, 14, 17, 17, 14 } },
    { '9', { 14, 17, 17, 15, 1, 1, 14 } },
    { '.', { 0, 0, 0, 0, 0, 4, 4 } },
    { ':', { 0, 4, 4, 0, 4, 4, 0 } },
    { '-', { 0, 0, 0, 31, 0, 0, 0 } },
    { '/', { 1, 2, 2, 4, 8, 8, 16 } },
    { '?', { 14, 17, 1, 2, 4, 0, 4 } },
    { ' ', { 0, 0, 0, 0, 0, 0, 0 } },
};
static const FontGlyph fallback_glyph = { '?', { 14, 17, 1, 2, 4, 0, 4 } };

static uint8_t loading_frame[RGB_FRAME_BYTES];

static void fill_rect(int x, int y, int width, int height, Color color) {
    for (int row = 0; row < height; row++) {
        int py = y + row;
        if (py < 0 || py >= SCREEN_HEIGHT) continue;
        for (int column = 0; column < width; column++) {
            int px = x + column;
            if (px < 0 || px >= SCREEN_WIDTH) continue;
            size_t offset = ((size_t)py * SCREEN_WIDTH + (size_t)px) * 3;
            loading_frame[offset] = color.red;
            loading_frame[offset + 1] = color.green;
            loading_frame[offset + 2] = color.blue;
        }
    }
}

static const FontGlyph *find_glyph(char character) {
    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 'A');
    }
    for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); i++) {
        if (font[i].character == character) return &font[i];
    }
    return &fallback_glyph;
}

static int text_width(const char *text, unsigned scale) {
    size_t length = 0;
    while (text[length] != '\0') length++;
    if (length == 0) return 0;
    return (int)(length * (FONT_WIDTH + FONT_SPACING) * scale - FONT_SPACING * scale);
}

static void draw_text(const char *text, int x, int y, unsigned scale, Color color) {
    if (scale == 0) return;

    for (size_t index = 0; text[index] != '\0'; index++) {
        const FontGlyph *glyph = find_glyph(text[index]);
        int glyph_x = x + (int)(index * (FONT_WIDTH + FONT_SPACING) * scale);
        for (unsigned row = 0; row < FONT_HEIGHT; row++) {
            for (unsigned column = 0; column < FONT_WIDTH; column++) {
                if ((glyph->rows[row] & (1u << (FONT_WIDTH - column - 1))) != 0) {
                    fill_rect(glyph_x + (int)(column * scale),
                              y + (int)(row * scale),
                              (int)scale, (int)scale, color);
                }
            }
        }
    }
}

static void draw_centered(const char *text, int y, unsigned scale, Color color) {
    int width = text_width(text, scale);
    if (width > SCREEN_WIDTH - 40 && scale > 1) {
        scale = 1;
        width = text_width(text, scale);
    }
    int x = (SCREEN_WIDTH - width) / 2;
    draw_text(text, x, y, scale, color);
}

void wii_loading_screen_show(const char *title, const char *message) {
    if (title == NULL) title = "NES GAME";
    if (message == NULL) message = "READING GAME ROM AND CHR GRAPHICS";

    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        uint8_t red = (uint8_t)(5 + y / 48);
        uint8_t green = (uint8_t)(13 + y / 16);
        uint8_t blue = (uint8_t)(34 + y / 5);
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            size_t offset = ((size_t)y * SCREEN_WIDTH + (size_t)x) * 3;
            loading_frame[offset] = red;
            loading_frame[offset + 1] = green;
            loading_frame[offset + 2] = blue;
        }
    }

    // Pixel stars and framed title card keep the loader in the game's 8-bit style.
    static const uint8_t stars[][2] = {
        { 24, 24 }, { 48, 47 }, { 221, 26 }, { 203, 53 },
        { 31, 202 }, { 224, 207 }, { 16, 85 }, { 240, 91 },
    };
    const Color star_color = { 126, 193, 247 };
    for (size_t i = 0; i < sizeof(stars) / sizeof(stars[0]); i++) {
        int x = stars[i][0];
        int y = stars[i][1];
        fill_rect(x, y, 2, 2, star_color);
        fill_rect(x - 2, y + 1, 6, 1, star_color);
    }

    fill_rect(15, 52, SCREEN_WIDTH - 30, 86, (Color){ 8, 20, 48 });
    fill_rect(18, 55, SCREEN_WIDTH - 36, 80, (Color){ 11, 31, 67 });
    fill_rect(24, 61, SCREEN_WIDTH - 48, 1, (Color){ 55, 105, 161 });
    fill_rect(24, 127, SCREEN_WIDTH - 48, 1, (Color){ 55, 105, 161 });

    draw_centered("WII HOMEBREW", 68, 1, (Color){ 126, 193, 247 });
    draw_centered(title, 84, 2, (Color){ 255, 221, 111 });

    fill_rect(44, 155, SCREEN_WIDTH - 88, 1, (Color){ 48, 91, 139 });
    draw_centered(message, 169, 1, (Color){ 238, 244, 255 });
    draw_centered("PLEASE WAIT", 187, 1, (Color){ 131, 171, 212 });

    // Animate a small row of pixels so the loading screen remains visible
    // even when reading the small CHR block from SD is nearly instantaneous.
    for (unsigned phase = 0; phase < 8; phase++) {
        for (int i = 0; i < 5; i++) {
            Color color = (unsigned)i == phase % 5
                ? (Color){ 255, 203, 87 }
                : (Color){ 61, 101, 151 };
            fill_rect(106 + i * 10, 207, 6, 4, color);
        }
        wii_video_present(loading_frame);
    }
}
