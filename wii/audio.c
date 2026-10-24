#include "wii_audio.h"

#include <gccore.h>
#include <ogc/audio.h>

#include <stdint.h>

#include "apu.h"

#define AUDIO_BUFFER_FRAMES 512
#define AUDIO_CHANNELS 2
#define AUDIO_BUFFER_SAMPLES (AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS)
#define AUDIO_BUFFER_BYTES (AUDIO_BUFFER_SAMPLES * sizeof(int16_t))

static int16_t audio_buffers[2][AUDIO_BUFFER_SAMPLES] __attribute__((aligned(32)));
static float mono_samples[AUDIO_BUFFER_FRAMES] __attribute__((aligned(32)));
static unsigned current_buffer;

static void render_audio(int16_t *destination) {
    apu_render(mono_samples, AUDIO_BUFFER_FRAMES);

    // apu_render() already clamps its output to [-1, 1], so the PCM
    // conversion can avoid repeating those bounds checks in the DMA callback.
    for (unsigned i = 0; i < AUDIO_BUFFER_FRAMES; i++) {
        int16_t pcm = (int16_t)(mono_samples[i] * 32767.0f);
        destination[i * AUDIO_CHANNELS] = pcm;
        destination[i * AUDIO_CHANNELS + 1] = pcm;
    }
}

static void audio_dma_callback(void) {
    current_buffer ^= 1u;
    render_audio(audio_buffers[current_buffer]);
    DCFlushRange(audio_buffers[current_buffer], AUDIO_BUFFER_BYTES);

    AUDIO_StopDMA();
    AUDIO_InitDMA((u32)(uintptr_t)audio_buffers[current_buffer], AUDIO_BUFFER_BYTES);
    AUDIO_StartDMA();
}

void wii_audio_init(void) {
    current_buffer = 0;
    AUDIO_Init(NULL);
    AUDIO_SetDSPSampleRate(AI_SAMPLERATE_32KHZ);
    AUDIO_SetStreamVolLeft(255);
    AUDIO_SetStreamVolRight(255);

    render_audio(audio_buffers[current_buffer]);
    DCFlushRange(audio_buffers[current_buffer], AUDIO_BUFFER_BYTES);
    AUDIO_RegisterDMACallback(audio_dma_callback);
    AUDIO_InitDMA((u32)(uintptr_t)audio_buffers[current_buffer], AUDIO_BUFFER_BYTES);
    AUDIO_StartDMA();
}

void wii_audio_shutdown(void) {
    AUDIO_RegisterDMACallback(NULL);
    AUDIO_StopDMA();
}
