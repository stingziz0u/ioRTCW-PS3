/* ps3_glimp.c -- RSX/GCM display init, frame management and GLimp_*.
 *
 * From IoQuake3-PS3 (Mayo1970), adapted to the iortcw SP renderer. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>

#include <ppu-types.h>
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>
#include <sysutil/video.h>

#include "renderer/tr_local.h"
#include "../sys/ps3_glimp.h"
#include "../gl/ps3gl.h"
#include "ps3_log.h"

extern void QGL_Init(void);

/* RSX state */
#define RSX_FB_COUNT 3

static gcmContextData *ps3_gcm_context = NULL;
static gcmContextData *ps3_gcm_context_backup = (gcmContextData *)0xDEAD;
static u32 ps3_display_width  = 1280;
static u32 ps3_display_height = 720;
static u32 ps3_color_pitch    = 0;
static u32 ps3_color_offset[RSX_FB_COUNT];
static u32 *ps3_color_buffer[RSX_FB_COUNT];
static u32 ps3_depth_offset;
static u32 *ps3_depth_buffer = NULL;
static int ps3_current_fb = 0;
static int ps3_current_rt = -1;

/* RSX IO window, published for ps3gl_draw.c: gcmAddressToOffset does not
 * validate an unmapped main-memory address, so callers must range-check. */
const void *ps3_rsx_io_base = NULL;
size_t      ps3_rsx_io_size = 0;

static int ps3_rsx_up = 0;
static int ps3_fit_border = 0;
static cvar_t *ps3_fit_cvar;
static int ps3_fit_pct = -1;

/* vid_ps3_screenfit (70..100 %) into ps3gl, when it changed. Called from
 * GLimp_Init and each frame begin (render thread, owner of ps3gl). */
static void PS3_ApplyScreenFit(void)
{
    int pct, w, h;

    if (!ps3_fit_cvar || ps3_fit_cvar->integer == ps3_fit_pct)
        return;
    pct = ps3_fit_cvar->integer;
    if (pct < 70) pct = 70;
    if (pct > 100) pct = 100;
    ps3_fit_pct = ps3_fit_cvar->integer;
    w = ((int)ps3_display_width * pct / 100) & ~1;
    h = ((int)ps3_display_height * pct / 100) & ~1;
    ps3gl_set_fit(((int)ps3_display_width - w) / 2, ((int)ps3_display_height - h) / 2, w, h);
    ps3_fit_border = (pct < 100);
    PS3_Logf("[video] output %ux%u, screen fit %d%% -> %dx%d",
             ps3_display_width, ps3_display_height, pct, w, h);
}

static volatile u32 ps3_flip_queued    = 0;
static volatile u32 ps3_flip_completed = 0;

static void ps3_flip_handler(const u32 head)
{
    (void)head;
    ps3_flip_completed++;
}

#define RSX_FB_ALIGN    64
#define RSX_DEPTH_ALIGN 64

static void PS3_RSX_AllocFramebuffers(void)
{
    ps3_color_pitch = ps3_display_width * 4; /* ARGB8888 */

    u32 color_size = ps3_color_pitch * ps3_display_height;
    u32 depth_size = ps3_display_width * ps3_display_height * 4; /* 32-bit depth */

    for (int i = 0; i < RSX_FB_COUNT; i++) {
        ps3_color_buffer[i] = (u32 *)rsxMemalign(RSX_FB_ALIGN, color_size);
        if (!ps3_color_buffer[i]) {
            printf("[ps3] FATAL: rsxMemalign failed for color buffer %d\n", i);
            return;
        }
        rsxAddressToOffset(ps3_color_buffer[i], &ps3_color_offset[i]);

        gcmSetDisplayBuffer(i, ps3_color_offset[i],
                            ps3_color_pitch, ps3_display_width, ps3_display_height);
    }

    ps3_depth_buffer = (u32 *)rsxMemalign(RSX_DEPTH_ALIGN, depth_size);
    if (ps3_depth_buffer) {
        rsxAddressToOffset(ps3_depth_buffer, &ps3_depth_offset);
    }
}

static void PS3_RSX_SetRenderTarget(int index)
{
    if (index == ps3_current_rt) return;
    ps3_current_rt = index;

    gcmSurface sf;
    memset(&sf, 0, sizeof(sf));

    sf.colorFormat    = GCM_SURFACE_A8R8G8B8;
    sf.colorTarget    = GCM_SURFACE_TARGET_0;
    sf.colorLocation[0] = GCM_LOCATION_RSX;
    sf.colorOffset[0]   = ps3_color_offset[index];
    sf.colorPitch[0]    = ps3_color_pitch;

    for (int i = 1; i < 4; i++) {
        sf.colorLocation[i] = GCM_LOCATION_RSX;
        sf.colorOffset[i]   = ps3_color_offset[index];
        sf.colorPitch[i]    = 64;
    }

    sf.depthFormat    = GCM_SURFACE_ZETA_Z24S8;
    sf.depthLocation  = GCM_LOCATION_RSX;
    sf.depthOffset    = ps3_depth_offset;
    sf.depthPitch     = ps3_display_width * 4;

    sf.type           = GCM_SURFACE_TYPE_LINEAR;
    sf.antiAlias      = GCM_SURFACE_CENTER_1;

    sf.width          = ps3_display_width;
    sf.height         = ps3_display_height;
    sf.x              = 0;
    sf.y              = 0;

    rsxSetSurface(ps3_gcm_context, &sf);
}

/* Public interface */

/* GCM IO buffer (main memory the RSX can see). ps3gl keeps textures, vertex
 * ring and shaders in RSX local memory, so only the 1 MB command buffer lives
 * here: 8 MB instead of IoQuake3-PS3's 32 gives 24 MB back to the hunk. */
#define RSX_CB_SIZE     (1 * 1024 * 1024)   /* 1 MB command buffer */
#define RSX_HOST_SIZE   (8 * 1024 * 1024)   /* 8 MB IO buffer      */

void PS3_RSX_Init(void)
{
    ps3_log("PS3_RSX_Init: entered");

    /* Allocate the 1 MB-aligned IO buffer. */
    void *host_addr = memalign(1024 * 1024, RSX_HOST_SIZE);
    if (!host_addr) {
        ps3_log("PS3_RSX_Init: FATAL memalign failed for the IO buffer");
        return;
    }

    {
        char dbg[128];
        snprintf(dbg, sizeof(dbg),
                 "PS3_RSX_Init: host_addr=%p cmdSize=0x%x ioSize=0x%x",
                 host_addr, RSX_CB_SIZE, RSX_HOST_SIZE);
        ps3_log(dbg);
    }

    ps3_rsx_io_base = host_addr;
    ps3_rsx_io_size = RSX_HOST_SIZE;

    s32 ret = rsxInit(&ps3_gcm_context, RSX_CB_SIZE, RSX_HOST_SIZE, host_addr);
    {
        char dbg[128];
        snprintf(dbg, sizeof(dbg), "PS3_RSX_Init: rsxInit ret=%d ctx=%p",
                 (int)ret, (void*)ps3_gcm_context);
        ps3_log(dbg);
    }
    if (ret != 0 || !ps3_gcm_context) {
        char dbg[128];
        snprintf(dbg, sizeof(dbg),
                 "PS3_RSX_Init: rsxInit FAILED (ret=0x%08x), trying fallbacks",
                 (unsigned)(ret & 0xFFFFFFFF));
        ps3_log(dbg);

        /* Try to recover existing GCM context. */
        rsxSetDefaultCommandBuffer(&ps3_gcm_context);
        snprintf(dbg, sizeof(dbg),
                 "PS3_RSX_Init: rsxSetDefaultCommandBuffer ctx=%p",
                 (void*)ps3_gcm_context);
        ps3_log(dbg);

        if (ps3_gcm_context) {
            ps3_log("PS3_RSX_Init: recovered existing GCM context");
        } else {
            /* Try gcmInitBody as final fallback. */
            ret = gcmInitBody(&ps3_gcm_context, RSX_CB_SIZE, RSX_HOST_SIZE, host_addr);
            snprintf(dbg, sizeof(dbg),
                     "PS3_RSX_Init: gcmInitBody ret=%d ctx=%p",
                     (int)ret, (void*)ps3_gcm_context);
            ps3_log(dbg);

            if (ret == 0 && ps3_gcm_context) {
                rsxHeapInit();
                ps3_log("PS3_RSX_Init: gcmInitBody fallback succeeded");
            } else {
                ps3_log("PS3_RSX_Init: FATAL all GCM init attempts failed");
                return;
            }
        }
    }

    /* Try 720p; fall back to TV default if unavailable. */
    s32 vid_res = VIDEO_RESOLUTION_720;
    u8  vid_aspect = VIDEO_ASPECT_16_9;

    if (!videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720,
                                        VIDEO_ASPECT_16_9, 0)) {
        videoState state;
        videoGetState(0, 0, &state);
        vid_res = state.displayMode.resolution;
        vid_aspect = state.displayMode.aspect;
        ps3_log("PS3_RSX_Init: 720p not available, using TV default");
    }

    videoResolution res;
    videoGetResolution(vid_res, &res);

    ps3_display_width  = res.width;
    ps3_display_height = res.height;
    printf("[ps3] Display: %ux%u\n", ps3_display_width, ps3_display_height);

    videoConfiguration vconfig;
    memset(&vconfig, 0, sizeof(vconfig));
    vconfig.resolution  = vid_res;
    vconfig.format      = VIDEO_BUFFER_FORMAT_XRGB;
    vconfig.pitch       = ps3_display_width * 4;
    vconfig.aspect      = vid_aspect;
    videoConfigure(0, &vconfig, NULL, 0);

    PS3_RSX_AllocFramebuffers();

    ps3_current_fb = 0;
    ps3_flip_queued    = 0;
    ps3_flip_completed = 0;
    gcmSetFlipHandler(ps3_flip_handler);
    PS3_RSX_SetRenderTarget(ps3_current_fb);

    ps3gl_init(ps3_gcm_context, ps3_display_width, ps3_display_height);

    {
        char dbg[256];
        snprintf(dbg, sizeof(dbg),
                 "PS3_RSX_Init: after ps3gl_init ps3gl_ptr=%p ctx=%p",
                 (void*)ps3gl_ptr, (void*)ps3_gcm_context);
        ps3_log(dbg);
    }

    /* Backup context pointer. */
    ps3_gcm_context_backup = ps3_gcm_context;
    ps3_rsx_up = 1;

    {
        char dbg[256];
        snprintf(dbg, sizeof(dbg),
                 "PS3_RSX_Init: ctx=%p &ctx=%p backup=%p ps3gl_ptr=%p sizeof=%u",
                 (void*)ps3_gcm_context,
                 (void*)&ps3_gcm_context,
                 (void*)ps3_gcm_context_backup,
                 (void*)ps3gl_ptr,
                 (unsigned)sizeof(ps3gl_state_t));
        ps3_log(dbg);
    }
}

void PS3_RSX_Shutdown(void)
{
    static u32 finish_ref = 0x1000;

    if (!ps3_rsx_up)
        return;
    ps3_rsx_up = 0;

    /* Flip handler first: a flip completing during teardown must not call
     * into freed state (Doom64-PS3 lesson). */
    gcmSetFlipHandler(NULL);

    ps3_current_rt = -1;
    ps3gl_shutdown();

    /* A new reference every time: rsxFinish with a value it already saw can
     * hang the whole console. */
    rsxFinish(ps3_gcm_context, ++finish_ref);

    for (int i = 0; i < RSX_FB_COUNT; i++) {
        if (ps3_color_buffer[i]) {
            rsxFree(ps3_color_buffer[i]);
            ps3_color_buffer[i] = NULL;
        }
    }
    if (ps3_depth_buffer) {
        rsxFree(ps3_depth_buffer);
        ps3_depth_buffer = NULL;
    }
}

/* Block only if both non-displayed buffers are in flight (queued - completed > 1).
 * With 3 buffers one buffer is always free for the CPU to render into. */
static void PS3_RSX_WaitFlips(void)
{
    int waited = 0;
    while ((int)(ps3_flip_queued - ps3_flip_completed) > RSX_FB_COUNT - 2) {
        usleep(100);
        if (++waited > 20000) {
            printf("[ps3] flip fence timed out, force-syncing\n");
            ps3_flip_completed = ps3_flip_queued;
            break;
        }
    }
}

void PS3_RSX_BeginFrame(void)
{
    /* Restore from .data backup if corrupted */
    if ((uintptr_t)ps3_gcm_context <= 0x1000
        && (uintptr_t)ps3_gcm_context_backup > 0x1000)
        ps3_gcm_context = ps3_gcm_context_backup;

    if ((uintptr_t)ps3_gcm_context <= 0x1000) return;

    /* Block until a buffer is free to render into. */
    {
        unsigned long long t0 = PS3_ProfTick();
        PS3_RSX_WaitFlips();
        PS3_ProfAdd(PS3_PROF_FLIPWAIT, PS3_ProfTick() - t0);
    }

    PS3_RSX_SetRenderTarget(ps3_current_fb);
    PS3_ApplyScreenFit();
    {
        /* waits for the GPU to be done with this vertex ring segment */
        unsigned long long t0 = PS3_ProfTick();
        ps3gl_begin_frame();
        PS3_ProfAdd(PS3_PROF_FENCEWAIT, PS3_ProfTick() - t0);
    }
    /* triple buffering: every buffer needs its screen-fit border cleared */
    if (ps3_fit_border)
        ps3gl_clear_full_surface();
}

void PS3_RSX_EndFrame(void)
{
    /* Restore from .data backup if corrupted */
    if ((uintptr_t)ps3_gcm_context <= 0x1000
        && (uintptr_t)ps3_gcm_context_backup > 0x1000)
        ps3_gcm_context = ps3_gcm_context_backup;

    if ((uintptr_t)ps3_gcm_context <= 0x1000) return;

    ps3gl_end_frame();

    /* Queue flip + flush (non-blocking). Wait happens in next BeginFrame. */
    gcmSetWaitFlip(ps3_gcm_context);
    gcmSetFlip(ps3_gcm_context, ps3_current_fb);
    rsxFlushBuffer(ps3_gcm_context);

    ps3_flip_queued++;
    ps3_current_fb = (ps3_current_fb + 1) % RSX_FB_COUNT;
}

/* GLimp interface -- called by the iortcw renderer */
void GLimp_Init(qboolean fixedFunction)
{
    (void)fixedFunction;

    QGL_Init();

    /* Screen fit (TV overscan): the engine sees the whole 1280x720 and
     * ps3gl scales it into a centered rect of vid_ps3_screenfit percent
     * (PS3_ApplyScreenFit, every frame: the menu slider applies live). */
    ps3_fit_cvar = ri.Cvar_Get("vid_ps3_screenfit", PS3_DEFAULT_SCREENFIT, CVAR_ARCHIVE);
    ps3_fit_pct = -1;
    PS3_ApplyScreenFit();
    glConfig.vidWidth  = ps3_display_width;
    glConfig.vidHeight = ps3_display_height;
    glConfig.windowAspect       = (float)glConfig.vidWidth / (float)glConfig.vidHeight;
    glConfig.colorBits          = 32;
    glConfig.depthBits          = 24;
    glConfig.stencilBits        = 8;
    glConfig.isFullscreen       = qtrue;
    glConfig.stereoEnabled      = qfalse;
    glConfig.smpActive          = qfalse;
    glConfig.displayFrequency   = 60;
    glConfig.driverType         = GLDRV_ICD;
    glConfig.hardwareType       = GLHW_GENERIC;
    glConfig.deviceSupportsGamma = qfalse;
    glConfig.maxTextureSize     = 2048;
    glConfig.numTextureUnits    = PS3GL_MAX_TMUS;
    glConfig.textureCompression = TC_NONE;
    glConfig.textureEnvAddAvailable = qtrue;
    glConfig.anisotropicAvailable = qfalse;
    glConfig.textureFilterAnisotropicAvailable = qfalse;
    glConfig.NVFogAvailable     = qfalse;
    glConfig.ATIMaxTruformTess  = 0;

    textureFilterAnisotropic = qfalse;
    maxAnisotropy = 0;
    haveClampToEdge = qtrue;

    Q_strncpyz(glConfig.vendor_string, "Sony", sizeof(glConfig.vendor_string));
    Q_strncpyz(glConfig.renderer_string, "RSX (ps3gl)", sizeof(glConfig.renderer_string));
    Q_strncpyz(glConfig.version_string, "1.1 ps3gl", sizeof(glConfig.version_string));
    Q_strncpyz(glConfig.extensions_string,
               "GL_ARB_multitexture GL_EXT_compiled_vertex_array GL_EXT_texture_env_add",
               sizeof(glConfig.extensions_string));

    ri.Cvar_Get("r_availableModes", "", CVAR_ROM);

    ri.IN_Init(NULL);
}

void GLimp_Shutdown(void)
{
    /* The RSX stays up for the whole process (vid_restart keeps it). */
}

void GLimp_EndFrame(void)
{
    PS3_RSX_EndFrame();
}

void GLimp_Minimize(void)
{
}

void GLimp_SetGamma(unsigned char red[256], unsigned char green[256],
                    unsigned char blue[256])
{
    (void)red; (void)green; (void)blue;
}

void GLimp_LogComment(char *comment)
{
    (void)comment;
}

int PS3_RSX_Width(void)  { return (int)ps3_display_width; }
int PS3_RSX_Height(void) { return (int)ps3_display_height; }

/* ---- SMP: the renderer back end on the PPU's second hardware thread -------
 *
 * The front end (game thread: server, cgame, culling, sorting) builds frame
 * N+1 while this thread runs the back end of frame N (shading, skinning,
 * ps3gl, flips). Same scheme as the r_smp of the original RTCW: two
 * backEndData buffers, the front end waits for the back end before handing
 * it the next one (GLimp_FrontEndSleep) and before any GL call of its own
 * (R_IssuePendingRenderCommands). ps3gl is only ever used by one thread at a
 * time; PS3GL_FRONT_CHECK in ps3gl logs and waits if that is ever broken. */

#include <sys/thread.h>
#include <sys/sem.h>
#include <lv2/thread.h>

#define PS3_RENDER_STACK    (512 * 1024)
#define PS3_RENDER_PRIO     1000        /* same as the game thread */

static sys_ppu_thread_t ps3_render_tid;
static sys_sem_t ps3_render_work, ps3_render_done;
static void (*ps3_render_func)(void);
static void *volatile ps3_render_data;
static int ps3_render_up;
static int ps3_render_pending;          /* front end only: a batch not waited for */

volatile int ps3_render_busy;           /* the back end is running a batch */
uintptr_t ps3_render_stack_lo, ps3_render_stack_hi;

static void ps3_render_thread(void *arg)
{
    sys_ppu_thread_stack_t st;
    char marker;

    (void)arg;
    /* the render thread's stack, for PS3GL_FRONT_CHECK; must contain this
     * frame or every back end call would look like the game thread's */
    if (sysThreadGetStackInformation(&st) == 0 && st.addr && st.size
        && (uintptr_t)&marker >= (uintptr_t)st.addr
        && (uintptr_t)&marker < (uintptr_t)st.addr + st.size) {
        ps3_render_stack_lo = (uintptr_t)st.addr;
        ps3_render_stack_hi = (uintptr_t)st.addr + st.size;
    } else {
        ps3_render_stack_hi = (uintptr_t)&marker + 4096;
        ps3_render_stack_lo = ps3_render_stack_hi - PS3_RENDER_STACK + 8192;
    }
    PS3_Logf("[smp] render thread stack 0x%lx-0x%lx",
             (unsigned long)ps3_render_stack_lo, (unsigned long)ps3_render_stack_hi);
    ps3_render_func();
    sysThreadExit(0);
}

qboolean GLimp_SpawnRenderThread(void (*function)(void))
{
    sys_sem_attr_t attr;
    s32 ret;

    if (ps3_render_up)
        return qtrue;

    memset(&attr, 0, sizeof(attr));
    attr.attr_protocol = SYS_SEM_ATTR_PROTOCOL;
    attr.attr_pshared  = SYS_SEM_ATTR_PSHARED;
    memcpy(attr.name, "rwork", 6);
    if (sysSemCreate(&ps3_render_work, &attr, 0, 1) != 0)
        return qfalse;
    memcpy(attr.name, "rdone", 6);
    if (sysSemCreate(&ps3_render_done, &attr, 0, 1) != 0) {
        sysSemDestroy(ps3_render_work);
        return qfalse;
    }

    ps3_render_func = function;
    ps3_render_data = NULL;
    ps3_render_pending = 0;
    ps3_render_busy = 0;
    ret = sysThreadCreate(&ps3_render_tid, ps3_render_thread, NULL, PS3_RENDER_PRIO,
                          PS3_RENDER_STACK, THREAD_JOINABLE, "render");
    if (ret != 0) {
        PS3_Logf("[smp] sysThreadCreate failed (0x%08x): back end stays on the game thread",
                 (unsigned)ret);
        sysSemDestroy(ps3_render_done);
        sysSemDestroy(ps3_render_work);
        return qfalse;
    }
    ps3_render_up = 1;
    ps3_prof_smp = 1;
    PS3_Logf("[smp] render thread up: the back end runs on the 2nd PPU thread");
    return qtrue;
}

/* render thread: report the last batch done, sleep until the next one */
void *GLimp_RendererSleep(void)
{
    static int had_batch;
    void *data;

    if (had_batch) {
        __asm__ volatile("sync" ::: "memory");
        ps3_render_busy = 0;
        sysSemPost(ps3_render_done, 1);
    }
    sysSemWait(ps3_render_work, 0);
    __asm__ volatile("sync" ::: "memory");
    data = ps3_render_data;
    had_batch = (data != NULL);
    return data;
}

/* front end: wait until the back end is idle (no-op if it already is) */
void GLimp_FrontEndSleep(void)
{
    unsigned long long t0;

    if (!ps3_render_pending)
        return;
    t0 = PS3_ProfTick();
    sysSemWait(ps3_render_done, 0);
    __asm__ volatile("sync" ::: "memory");
    ps3_render_pending = 0;
    PS3_ProfAdd(PS3_PROF_RWAIT, PS3_ProfTick() - t0);
}

/* front end: hand the back end a command list (NULL: the thread exits) */
void GLimp_WakeRenderer(void *data)
{
    GLimp_FrontEndSleep();
    ps3_render_data = data;
    ps3_render_pending = (data != NULL);
    ps3_render_busy = (data != NULL);
    __asm__ volatile("sync" ::: "memory");
    sysSemPost(ps3_render_work, 1);
}

void GLimp_ShutdownRenderThread(void)
{
    u64 retval;

    if (!ps3_render_up)
        return;
    GLimp_WakeRenderer(NULL);
    sysThreadJoin(ps3_render_tid, &retval);
    sysSemDestroy(ps3_render_done);
    sysSemDestroy(ps3_render_work);
    ps3_render_up = 0;
    ps3_prof_smp = 0;
    ps3_render_busy = 0;
    ps3_render_stack_lo = ps3_render_stack_hi = 0;
}

/* ps3gl called from the game thread while the back end runs: a missing sync
 * in the renderer. Say where once, and wait so both do not write RSX commands
 * at the same time. */
void ps3gl_front_call(const char *func)
{
    static const char *logged[16];
    sys_ppu_thread_t self;
    int i;

    /* never wait on ourselves: if the stack range is wrong, stop checking */
    if (sysThreadGetId(&self) == 0 && self == ps3_render_tid) {
        PS3_Logf("[smp] %s: render thread outside its stack range, check off", func);
        ps3_render_stack_lo = 0;
        ps3_render_stack_hi = ~(uintptr_t)0;
        return;
    }

    for (i = 0; i < 16 && logged[i]; i++)
        if (logged[i] == func)
            break;
    if (i < 16 && !logged[i]) {
        logged[i] = func;
        PS3_Logf("[smp] %s from the game thread while the back end runs: waiting", func);
    }
    GLimp_FrontEndSleep();
}
