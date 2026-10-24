#include <fat.h>
#include <gccore.h>
#include <wiiuse/wpad.h>

#include <stdbool.h>
#include <stdint.h>

#include "common.h"
#include "cpu.h"
#include "debug_chr.h"
#include "loading.h"
#include "render.h"
#include "sound.h"
#include "wii_audio.h"
#include "wii_video.h"

#ifdef MAPPER
#include "cartridge.h"
#include "mapper.h"
#endif

#ifndef ROM_PATH
#define ROM_PATH "sd:/smb.nes"
#endif
#ifndef WII_GAME_TITLE
#define WII_GAME_TITLE "Super Mario Bros."
#endif

#define NES_RIGHT  0x80
#define NES_LEFT   0x40
#define NES_DOWN   0x20
#define NES_UP     0x10
#define NES_START  0x08
#define NES_SELECT 0x04
#define NES_B      0x02
#define NES_A      0x01

// Shared system controls exposed by every supported WPAD family: Wii Remote
// and Nunchuk use the WPAD buttons; Classic and Pro use the CLASSIC variants.
#define WPAD_NES_START_BUTTONS  (WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS)
#define WPAD_NES_SELECT_BUTTONS (WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS)
#define WPAD_HOME_BUTTONS       (WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME)

#define WII_CONTROLLER_COUNT 4

static uint8_t map_buttons(u32 held) {
    uint8_t state = 0;

    if (held & (WPAD_BUTTON_RIGHT | WPAD_CLASSIC_BUTTON_RIGHT)) state |= NES_RIGHT;
    if (held & (WPAD_BUTTON_LEFT | WPAD_CLASSIC_BUTTON_LEFT)) state |= NES_LEFT;
    if (held & (WPAD_BUTTON_DOWN | WPAD_CLASSIC_BUTTON_DOWN)) state |= NES_DOWN;
    if (held & (WPAD_BUTTON_UP | WPAD_CLASSIC_BUTTON_UP)) state |= NES_UP;

    // NES A jumps and NES B runs. On a sideways Remote, 2 jumps and 1 runs;
    // a Classic Controller/Pro uses A to jump and either B or Y to run.
    if (held & (WPAD_BUTTON_2 | WPAD_CLASSIC_BUTTON_A)) state |= NES_A;
    if (held & (WPAD_BUTTON_1 | WPAD_CLASSIC_BUTTON_B | WPAD_CLASSIC_BUTTON_Y)) state |= NES_B;

    // Plus maps to NES Start and minus to NES Select on both controller families.
    if (held & WPAD_NES_START_BUTTONS) state |= NES_START;
    if (held & WPAD_NES_SELECT_BUTTONS) state |= NES_SELECT;

    return state;
}

static uint8_t poll_controller(bool *should_exit) {
    uint8_t state = 0;

    WPAD_ScanPads();
    for (u32 channel = 0; channel < WII_CONTROLLER_COUNT; channel++) {
        u32 held = WPAD_ButtonsHeld(channel);
        u32 pressed = WPAD_ButtonsDown(channel);
        state |= map_buttons(held);
        if (pressed & WPAD_HOME_BUTTONS) {
            *should_exit = true;
        }
    }

    return state;
}

static void init_controllers(void) {
    WPAD_Init();
    for (u32 channel = 0; channel < WII_CONTROLLER_COUNT; channel++) {
        WPAD_SetDataFormat(channel, WPAD_FMT_BTNS);
    }
}

int main(void) {
    if (!wii_video_init()) return 1;
    wii_loading_screen_show(WII_GAME_TITLE, "PREPARING SD CARD");

    init_controllers();
    fatInitDefault();
    wii_loading_screen_show(WII_GAME_TITLE, "READING GAME ROM AND CHR GRAPHICS");

    cpu_init();
    apu_init(WII_AUDIO_SAMPLE_RATE);

#ifdef MAPPER
    if (load_cartridge(ROM_PATH) != 0) {
        wii_loading_screen_show(WII_GAME_TITLE, "ROM LOAD FAILED - CHECK SD CARD");
        return 1;
    }
    mapper_init(cartridge_mapper);
#else
    if (read_chr_rom((char *)ROM_PATH) != 0) {
        wii_loading_screen_show(WII_GAME_TITLE, "ROM UNAVAILABLE - USING DEBUG TILES");
        uint8_t chr[DEBUG_CHR_SIZE];
        debug_chr_generate(chr);
        video_init(chr);
        render_init();
    }
#endif

    wii_loading_screen_show(WII_GAME_TITLE, "STARTING GAME");
    wii_audio_init();
    cpu_reset_entry();

    bool done = false;
    while (!done) {
        uint8_t controller = poll_controller(&done);
        if (done) break;
        update_controller1(controller);
        next_frame();
        sound_end_frame();
        render_frame();
        wii_video_present(frame);
    }

    wii_audio_shutdown();
    wii_video_shutdown();
    // Returning normally lets the Homebrew Channel resume its app menu.
    return 0;
}
