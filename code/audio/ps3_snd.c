/* ps3_snd.c -- PSL1GHT libaudio backend (SNDDMA_*) for the engine's mixer.
 * From IoQuake3-PS3 (Mayo1970). The S_* dispatch is iortcw's own snd_main.c. */
/* Compiled with -maltivec (per-file Makefile rule) for VMX audio conversion. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <altivec.h>

#include <ppu-types.h>
#include <audio/audio.h>
#include <sys/thread.h>
#include <sys/event_queue.h>
#include <sysmodule/sysmodule.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "client/snd_local.h"

#include "sys/ps3_log.h"

/* The engine mixes at 22050 Hz, the rate of RTCW's sounds: loaded sounds are
 * resampled to the mix rate, so mixing at 48 kHz made every sound 2.2x bigger
 * in the 18 MB sound cache, which then kept evicting and re-reading them from
 * the HDD in the middle of the game (25 ms stalls). The audio thread converts
 * 22050 -> 48000 Hz (linear) for the hardware port.
 * Ring buffer: 2048 sample pairs, ~93 ms at 22050 Hz. */
#define PS3_AUDIO_RATE       48000  /* hardware port */
#define PS3_MIX_RATE         22050  /* engine mixer (dma.speed) */
#define PS3_AUDIO_CHANNELS   2
#define PS3_AUDIO_BITS       16
#define PS3_MIX_SAMPLES      2048   /* ring, in sample pairs at the mix rate */
#define PS3_AUDIO_BLOCK_SIZE 256    /* PS3 hardware block size (48 kHz frames) */
/* 22050 / 48000 = 441 / 960 exactly: mix frame = out frame * 441 / 960 */
#define PS3_RATE_NUM         441
#define PS3_RATE_DEN         960

static audioPortConfig  ps3_audio_config;
static u32              ps3_audio_port = 0;
static volatile int     ps3_audio_running = 0;

static sys_event_queue_t ps3_audio_eventQ;
static sys_ipc_key_t     ps3_audio_queueKey;
static sys_ppu_thread_t  ps3_audio_thread;
static volatile int      ps3_audio_quit = 0;
static byte ps3_audio_buffer[PS3_MIX_SAMPLES * PS3_AUDIO_CHANNELS * (PS3_AUDIO_BITS / 8)] __attribute__((aligned(16)));
static volatile u64 ps3_audio_frames_out = 0;   /* 48 kHz frames handed to the port */

static void ps3_audio_thread_func(void *arg)
{
    (void)arg;
    sys_event_t event;
    const s16 *src = (const s16 *)ps3_audio_buffer;
    int block_samples = PS3_AUDIO_BLOCK_SIZE * PS3_AUDIO_CHANNELS;
    int diag_count = 0;
    int timeout_count = 0;

    volatile u64 *readIndexPtr = (volatile u64 *)((u64)ps3_audio_config.readIndex);
    f32 *dataStart = (f32 *)((u64)ps3_audio_config.audioDataStart);
    u64 numBlocks = ps3_audio_config.numBlocks;

    {
        char buf[192];
        snprintf(buf, sizeof(buf),
                 "PS3_AUDIO: thread started readIndexPtr=%p dataStart=%p numBlocks=%u",
                 (void *)readIndexPtr, (void *)dataStart, (unsigned)numBlocks);
        ps3_log(buf);
    }

    if (!readIndexPtr || !dataStart || !numBlocks) {
        ps3_log("PS3_AUDIO: FATAL bad port config pointers, thread exiting");
        sysThreadExit(1);
        return;
    }

    while (!ps3_audio_quit) {
        s32 ret = sysEventQueueReceive(ps3_audio_eventQ, &event, 20 * 1000);
        if (ret != 0) {
            timeout_count++;
            /* First few timeouts are expected at startup; keep sampling every
             * 100th one after so a later starvation window still logs. */
            if (timeout_count <= 3 || (timeout_count % 100) == 0) {
                char buf[96];
                snprintf(buf, sizeof(buf),
                         "PS3_AUDIO: eventQ timeout #%d ret=0x%08x",
                         timeout_count, (unsigned)ret);
                ps3_log(buf);
            }
            continue;
        }

        if (ps3_audio_quit) break;

        u64 currentBlock = *readIndexPtr;
        u32 writeBlock = (u32)((currentBlock + 1) % numBlocks);

        f32 *dst = dataStart + writeBlock * PS3_AUDIO_CHANNELS * PS3_AUDIO_BLOCK_SIZE;

        /* 22050 -> 48000 Hz, linear interpolation, into an aligned staging
         * buffer then memcpy (audioDataStart's alignment isn't guaranteed) */
        {
            static f32 staging[PS3_AUDIO_BLOCK_SIZE * PS3_AUDIO_CHANNELS] __attribute__((aligned(16)));
            const f32 scale = 1.0f / 32768.0f;
            u64 out = ps3_audio_frames_out;
            int j;

            for (j = 0; j < PS3_AUDIO_BLOCK_SIZE; j++) {
                u64 num = (out + j) * PS3_RATE_NUM;
                u64 i0 = num / PS3_RATE_DEN;
                f32 frac = (f32)(num - i0 * PS3_RATE_DEN) * (1.0f / PS3_RATE_DEN);
                const s16 *a = src + (i0 % PS3_MIX_SAMPLES) * PS3_AUDIO_CHANNELS;
                const s16 *b = src + ((i0 + 1) % PS3_MIX_SAMPLES) * PS3_AUDIO_CHANNELS;

                staging[j * 2]     = ((f32)a[0] + ((f32)b[0] - (f32)a[0]) * frac) * scale;
                staging[j * 2 + 1] = ((f32)a[1] + ((f32)b[1] - (f32)a[1]) * frac) * scale;
            }
            memcpy(dst, staging, (size_t)block_samples * sizeof(f32));
            ps3_audio_frames_out = out + PS3_AUDIO_BLOCK_SIZE;
        }

        if (diag_count < 5) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "PS3_AUDIO: frames=%u curRd=%u wr=%u",
                     (unsigned)ps3_audio_frames_out,
                     (unsigned)currentBlock, (unsigned)writeBlock);
            ps3_log(buf);
            diag_count++;
        }
    }

    ps3_log("PS3_AUDIO: thread exiting");
    sysThreadExit(0);
}

qboolean SNDDMA_Init(void)
{
    audioPortParam params;
    s32 ret;

    ps3_log("SNDDMA_Init: loading SYSMODULE_AUDIO");
    ret = sysModuleLoad(SYSMODULE_AUDIO);
    if (ret != 0 && ret != 0x8001112E) { /* already loaded */
        char buf[64];
        snprintf(buf, sizeof(buf), "SNDDMA_Init: sysModuleLoad(AUDIO) failed: 0x%08x", (unsigned)ret);
        ps3_log(buf);
        return qfalse;
    }

    ret = audioInit();
    if (ret != 0) {
        ps3_log("SNDDMA_Init: audioInit failed");
        return qfalse;
    }

    memset(&params, 0, sizeof(params));
    params.numChannels = AUDIO_PORT_2CH;
    params.numBlocks   = AUDIO_BLOCK_8;
    params.attrib      = AUDIO_PORT_INITLEVEL;
    params.level       = 1.0f;

    ret = audioPortOpen(&params, &ps3_audio_port);
    if (ret != 0) {
        ps3_log("SNDDMA_Init: audioPortOpen failed");
        audioQuit();
        return qfalse;
    }

    ret = audioGetPortConfig(ps3_audio_port, &ps3_audio_config);
    if (ret != 0) {
        ps3_log("SNDDMA_Init: audioGetPortConfig failed");
        audioPortClose(ps3_audio_port);
        audioQuit();
        return qfalse;
    }

    {
        char buf[192];
        snprintf(buf, sizeof(buf),
                 "SNDDMA_Init: readIndex=0x%08x audioDataStart=0x%08x status=%u ch=%lu nblk=%lu portSize=%u",
                 (unsigned)ps3_audio_config.readIndex,
                 (unsigned)ps3_audio_config.audioDataStart,
                 (unsigned)ps3_audio_config.status,
                 (unsigned long)ps3_audio_config.channelCount,
                 (unsigned long)ps3_audio_config.numBlocks,
                 (unsigned)ps3_audio_config.portSize);
        ps3_log(buf);
    }

    ret = audioCreateNotifyEventQueue(&ps3_audio_eventQ, &ps3_audio_queueKey);
    if (ret != 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "SNDDMA_Init: audioCreateNotifyEventQueue failed: 0x%08x", (unsigned)ret);
        ps3_log(buf);
        audioPortClose(ps3_audio_port);
        audioQuit();
        return qfalse;
    }

    ret = audioSetNotifyEventQueue(ps3_audio_queueKey);
    if (ret != 0) {
        ps3_log("SNDDMA_Init: audioSetNotifyEventQueue failed");
        sysEventQueueDestroy(ps3_audio_eventQ, 0);
        audioPortClose(ps3_audio_port);
        audioQuit();
        return qfalse;
    }

    sysEventQueueDrain(ps3_audio_eventQ);

    ret = audioPortStart(ps3_audio_port);
    if (ret != 0) {
        ps3_log("SNDDMA_Init: audioPortStart failed");
        audioRemoveNotifyEventQueue(ps3_audio_queueKey);
        sysEventQueueDestroy(ps3_audio_eventQ, 0);
        audioPortClose(ps3_audio_port);
        audioQuit();
        return qfalse;
    }

    memset(&dma, 0, sizeof(dma));
    dma.channels         = PS3_AUDIO_CHANNELS;
    dma.samples          = PS3_MIX_SAMPLES * PS3_AUDIO_CHANNELS;
    dma.fullsamples      = PS3_MIX_SAMPLES;
    dma.submission_chunk = PS3_MIX_SAMPLES / 8;     /* 256 pairs, ~12 ms */
    dma.samplebits       = PS3_AUDIO_BITS;
    dma.speed            = PS3_MIX_RATE;
    dma.buffer           = ps3_audio_buffer;

    memset(ps3_audio_buffer, 0, sizeof(ps3_audio_buffer));
    ps3_audio_frames_out = 0;

    ps3_audio_quit = 0;
    ps3_audio_running = 1;

    static char audio_thread_name[] = "AudioThread";
    /* Priority 100, well above the main thread's 1001 (lv2: 0 = highest). This
     * thread has a hard ~5.3ms deadline/block; equal priority risks crackle
     * under heavy main-thread load (cinematics), so it must preempt instead. */
    ret = sysThreadCreate(&ps3_audio_thread, ps3_audio_thread_func, NULL,
                          100, 0x4000, THREAD_JOINABLE, audio_thread_name);
    if (ret != 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "SNDDMA_Init: sysThreadCreate failed: 0x%08x", (unsigned)ret);
        ps3_log(buf);
        ps3_audio_running = 0;
        audioPortStop(ps3_audio_port);
        audioRemoveNotifyEventQueue(ps3_audio_queueKey);
        sysEventQueueDestroy(ps3_audio_eventQ, 0);
        audioPortClose(ps3_audio_port);
        audioQuit();
        return qfalse;
    }

    {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "SNDDMA_Init: OK - mix %d Hz -> port %d Hz, %d-bit, %d ch, %d sample-pairs, thread started",
                 PS3_MIX_RATE, PS3_AUDIO_RATE, PS3_AUDIO_BITS, PS3_AUDIO_CHANNELS, PS3_MIX_SAMPLES);
        ps3_log(buf);
    }
    return qtrue;
}

int SNDDMA_GetDMAPos(void)
{
    if (!ps3_audio_running) return 0;
    /* read position in the mix ring (interleaved samples) */
    u64 mixed = ps3_audio_frames_out * PS3_RATE_NUM / PS3_RATE_DEN;
    return (int)((mixed % PS3_MIX_SAMPLES) * PS3_AUDIO_CHANNELS);
}

void SNDDMA_Shutdown(void)
{
    if (!ps3_audio_running) return;

    ps3_audio_quit = 1;
    ps3_audio_running = 0;

    u64 retval;
    sysThreadJoin(ps3_audio_thread, &retval);

    audioPortStop(ps3_audio_port);
    audioRemoveNotifyEventQueue(ps3_audio_queueKey);
    sysEventQueueDestroy(ps3_audio_eventQ, 0);
    audioPortClose(ps3_audio_port);
    audioQuit();

    ps3_log("SNDDMA_Shutdown: done");
}

void SNDDMA_BeginPainting(void) {}
void SNDDMA_Submit(void) {}

void SNDDMA_StartCapture(void) {}
int  SNDDMA_AvailableCaptureSamples(void) { return 0; }
void SNDDMA_Capture(int samples, byte *data) { (void)samples; (void)data; }
void SNDDMA_StopCapture(void) {}
void SNDDMA_MasterGain(float val) { (void)val; }
