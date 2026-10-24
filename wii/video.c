#include "wii_video.h"

#include <gccore.h>
#include <ogc/conf.h>

#include <string.h>

#include "render.h"

#define GX_FIFO_SIZE (256 * 1024)
#define XFB_COUNT 2
#define RGB565_LEVELS 256

static GXRModeObj *video_mode;
static void *framebuffers[XFB_COUNT];
static unsigned displayed_framebuffer;
static bool display_is_widescreen;
static uint16_t viewport_x;
static uint16_t viewport_width;
static uint8_t gx_fifo[GX_FIFO_SIZE] __attribute__((aligned(32)));
static uint16_t texture_pixels[SCREEN_WIDTH * SCREEN_HEIGHT] __attribute__((aligned(32)));
static uint16_t red565[RGB565_LEVELS];
static uint16_t green565[RGB565_LEVELS];
static uint16_t blue565[RGB565_LEVELS];
static GXTexObj game_texture;

static void init_color_conversion(void) {
    // The renderer supplies the project's original 64-color NES master
    // palette as RGB888. These tables apply the same RGB565 quantization
    // as the previous direct shifts; they do not tint or replace the palette.
    for (unsigned value = 0; value < RGB565_LEVELS; value++) {
        red565[value] = (uint16_t)((value >> 3) << 11);
        green565[value] = (uint16_t)((value >> 2) << 5);
        blue565[value] = (uint16_t)(value >> 3);
    }
}

static void set_game_viewport(void) {
    uint16_t framebuffer_width = video_mode->fbWidth;
    viewport_width = display_is_widescreen
        ? (uint16_t)(((uint32_t)framebuffer_width * 3u) / 4u)
        : framebuffer_width;
    viewport_x = (uint16_t)((framebuffer_width - viewport_width) / 2u);

    // Wii widescreen is anamorphic: drawing into the centered 3/4-width area
    // keeps the game itself 4:3 while leaving black pillarboxes on a 16:9 TV.
    GX_SetViewport((f32)viewport_x, 0.0f, (f32)viewport_width,
                   (f32)video_mode->efbHeight, 0.0f, 1.0f);
    GX_SetScissor(viewport_x, 0, viewport_width, video_mode->efbHeight);
}

static void setup_gx(void) {
    GX_Init(gx_fifo, sizeof(gx_fifo));
    GX_SetCopyClear((GXColor){ 0, 0, 0, 255 }, 0x00ffffff);
    GX_SetViewport(0.0f, 0.0f, (f32)video_mode->fbWidth,
                   (f32)video_mode->efbHeight, 0.0f, 1.0f);
    GX_SetScissor(0, 0, video_mode->fbWidth, video_mode->efbHeight);
    GX_SetDispCopySrc(0, 0, video_mode->fbWidth, video_mode->efbHeight);
    GX_SetDispCopyDst(video_mode->fbWidth, video_mode->xfbHeight);
    GX_SetDispCopyYScale(GX_GetYScaleFactor(video_mode->efbHeight,
                                            video_mode->xfbHeight));
    GX_SetCopyFilter(video_mode->aa, video_mode->sample_pattern, GX_TRUE,
                     video_mode->vfilter);
    GX_SetFieldMode(video_mode->field_rendering,
                    video_mode->viHeight == 2 * video_mode->xfbHeight);
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaUpdate(GX_FALSE);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetNumChans(0);
    GX_SetNumTexGens(1);
    GX_SetNumIndStages(0);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

    Mtx model;
    Mtx44 projection;
    guMtxIdentity(model);
    guOrtho(projection, 0.0f, (f32)SCREEN_HEIGHT,
            0.0f, (f32)SCREEN_WIDTH, 0.0f, 1.0f);
    GX_LoadPosMtxImm(model, GX_PNMTX0);
    GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
    GX_SetCurrentMtx(GX_PNMTX0);

    GX_InitTexObj(&game_texture, texture_pixels, SCREEN_WIDTH, SCREEN_HEIGHT,
                  GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&game_texture, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f,
                     GX_FALSE, GX_FALSE, GX_ANISO_1);

    // Seed a black EFB so the first 4:3 frame has clean pillarboxes too.
    GX_DrawDone();
    GX_CopyDisp(framebuffers[0], GX_TRUE);
    GX_DrawDone();
    set_game_viewport();
}

bool wii_video_init(void) {
    VIDEO_Init();
    display_is_widescreen = false;
    if (CONF_Init() == CONF_ERR_OK) {
        display_is_widescreen = CONF_GetAspectRatio() == CONF_ASPECT_16_9;
    }

    video_mode = VIDEO_GetPreferredMode(NULL);
    if (video_mode == NULL) return false;

    for (unsigned i = 0; i < XFB_COUNT; i++) {
        void *framebuffer = SYS_AllocateFramebuffer(video_mode);
        if (framebuffer == NULL) return false;
        framebuffers[i] = MEM_K0_TO_K1(framebuffer);
        memset(framebuffers[i], 0,
               video_mode->fbWidth * video_mode->xfbHeight * VI_DISPLAY_PIX_SZ);
    }

    VIDEO_Configure(video_mode);
    VIDEO_SetNextFramebuffer(framebuffers[0]);
    VIDEO_SetBlack(TRUE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (video_mode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    displayed_framebuffer = 0;
    init_color_conversion();
    setup_gx();
    return true;
}

static void upload_rgb_frame(const uint8_t *rgb_frame) {
    const unsigned tiles_per_row = SCREEN_WIDTH / 4;

    // GX RGB565 textures are arranged in 4x4 tiles. Walk source rows inside
    // each tile so the swizzle and conversion are done without per-pixel
    // division/modulo or repeated tile-index calculations.
    for (unsigned tile_y = 0; tile_y < SCREEN_HEIGHT; tile_y += 4) {
        unsigned tile_row = (tile_y / 4) * tiles_per_row;
        for (unsigned tile_x = 0; tile_x < SCREEN_WIDTH; tile_x += 4) {
            unsigned destination = (tile_row + tile_x / 4) * 16;
            for (unsigned row = 0; row < 4; row++) {
                unsigned source = ((tile_y + row) * SCREEN_WIDTH + tile_x) * 3;
                unsigned output = destination + row * 4;
                for (unsigned column = 0; column < 4; column++) {
                    uint8_t red = rgb_frame[source + column * 3];
                    uint8_t green = rgb_frame[source + column * 3 + 1];
                    uint8_t blue = rgb_frame[source + column * 3 + 2];
                    texture_pixels[output + column] = (uint16_t)(
                        red565[red] | green565[green] | blue565[blue]);
                }
            }
        }
    }

    DCFlushRange(texture_pixels, sizeof(texture_pixels));
    GX_InvalidateTexAll();
}

void wii_video_present(const uint8_t *rgb_frame) {
    unsigned next_framebuffer = displayed_framebuffer ^ 1u;
    upload_rgb_frame(rgb_frame);

    GX_LoadTexObj(&game_texture, GX_TEXMAP0);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
        GX_Position3f32(0.0f, 0.0f, 0.0f);
        GX_TexCoord2f32(0.0f, 0.0f);
        GX_Position3f32((f32)SCREEN_WIDTH, 0.0f, 0.0f);
        GX_TexCoord2f32(1.0f, 0.0f);
        GX_Position3f32((f32)SCREEN_WIDTH, (f32)SCREEN_HEIGHT, 0.0f);
        GX_TexCoord2f32(1.0f, 1.0f);
        GX_Position3f32(0.0f, (f32)SCREEN_HEIGHT, 0.0f);
        GX_TexCoord2f32(0.0f, 1.0f);
    GX_End();

    GX_DrawDone();
    GX_CopyDisp(framebuffers[next_framebuffer], GX_TRUE);
    VIDEO_SetNextFramebuffer(framebuffers[next_framebuffer]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    displayed_framebuffer = next_framebuffer;
}

void wii_video_shutdown(void) {
    GX_DrawDone();
}
