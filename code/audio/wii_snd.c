/* ASND-based audio backend for ioquake3-wii. */

#include <asndlib.h>
#include <ogc/cache.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>

#include "wii_snd.h"
#include "client/snd_local.h"   /* dma_t */

#define SND_VOICE       0
#define SND_FREQ        22050
#define SND_CHANNELS    2
#define SND_SAMPLEBITS  16
#define SND_SAMPLES     2048    /* ~93 ms at 22 kHz; submission_chunk = half */
#define SND_BYTES       (SND_SAMPLES * SND_CHANNELS * (SND_SAMPLEBITS / 8))

/* Output rate ASND uses internally (set by ASND_Init → AUDIO_SetDSPSampleRate) */
#define ASND_OUTPUT_RATE 48000

static qboolean s_snd_init   = qfalse;
static qboolean s_asnd_ready = qfalse;

/* ioQ3 mixer writes here as a ring; ASND reads it via infinite loop. */
static u8 *s_buf = NULL;

void Wii_Snd_Init(void)
{
    ASND_Init();
    ASND_Pause(0);
    s_asnd_ready = qtrue;
}

void Wii_Snd_Shutdown(void)
{
    if (s_snd_init) {
        ASND_StopVoice(SND_VOICE);
        if (s_buf) { free(s_buf); s_buf = NULL; }
        s_snd_init = qfalse;
    }
    if (s_asnd_ready) {
        ASND_End();
        s_asnd_ready = qfalse;
    }
}

qboolean SNDDMA_Init(void)
{
    if (!s_asnd_ready)
        return qfalse;

    s_buf = (u8 *)memalign(32, SND_BYTES);
    if (!s_buf)
        return qfalse;
    memset(s_buf, 0, SND_BYTES);
    DCFlushRange(s_buf, SND_BYTES);

    dma.samplebits       = SND_SAMPLEBITS;
    dma.isfloat          = 0;
    dma.speed            = SND_FREQ;
    dma.channels         = SND_CHANNELS;
    dma.samples          = SND_SAMPLES * SND_CHANNELS; /* total interleaved */
    dma.fullsamples      = SND_SAMPLES;                /* sample-pairs      */
    dma.submission_chunk = SND_SAMPLES / 2;            /* 1024 pairs        */
    dma.buffer           = s_buf;

    /*
     * Use infinite voice: ASND loops s_buf forever at SND_FREQ input rate.
     * No callback needed — Q3 mixer writes into the ring, SNDDMA_Submit
     * flushes the DCache, and ASND's DMA sees the freshly-written samples.
     */
    ASND_SetInfiniteVoice(SND_VOICE,
                          VOICE_STEREO_16BIT,
                          SND_FREQ,
                          0,            /* delay ms */
                          s_buf, SND_BYTES,
                          255, 255);    /* left/right volume (full) */

    s_snd_init = qtrue;
    return qtrue;
}

/*
 * Derive hardware read position from ASND's tick counter.
 *
 * ASND_GetTickCounterVoice returns ticks at ASND_OUTPUT_RATE (48 kHz).
 * Convert to source-rate sample-pairs, wrap to ring size, and return the
 * interleaved sample count so S_GetSoundtime's /dma.channels gives the
 * correct frame offset in [0, SND_SAMPLES).
 */
int SNDDMA_GetDMAPos(void)
{
    if (!s_snd_init)
        return 0;

    u32 ticks = ASND_GetTickCounterVoice(SND_VOICE);
    u32 src_frames = (u32)(((u64)ticks * SND_FREQ) / ASND_OUTPUT_RATE);
    u32 pos = src_frames % (u32)SND_SAMPLES;

    return (int)(pos * (u32)SND_CHANNELS);
}

void SNDDMA_BeginPainting(void)
{
    /* no-op: dma.buffer never changes */
}

/* Writeback dcache so ASND DMA sees freshly mixed samples. */
void SNDDMA_Submit(void)
{
    if (!s_snd_init)
        return;

    DCFlushRange(s_buf, SND_BYTES);
}

void SNDDMA_Shutdown(void)
{
    Wii_Snd_Shutdown();
}

qboolean Wii_Snd_SNDDMA_Init(void)         { return SNDDMA_Init(); }
int      Wii_Snd_SNDDMA_GetDMAPos(void)    { return SNDDMA_GetDMAPos(); }
void     Wii_Snd_SNDDMA_BeginPainting(void){ SNDDMA_BeginPainting(); }
void     Wii_Snd_SNDDMA_Submit(void)       { SNDDMA_Submit(); }
void     Wii_Snd_SNDDMA_Shutdown(void)     { SNDDMA_Shutdown(); }
