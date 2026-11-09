/* ps3gl_shaders.c -- GL-to-RSX layer: shader program management. One shared vertex
 * program, one fragment program per texenv mode. If PS3GL_SHADERS_AVAILABLE==0 the shaders weren't compiled -- every draw is a silent no-op, nothing renders. */

#include "ps3gl.h"
#include "ps3gl_shader_data.h"
#include <stdio.h>

extern void ps3_log(const char *msg);

/* Forward declarations */
extern const float *ps3gl_get_mvp(void);
extern uint32_t ps3gl_get_mvp_generation(void);

#if PS3GL_SHADERS_AVAILABLE

/* Shader loading helpers */

static void load_vp(ps3gl_shader_t *s, const void *data, uint32_t size)
{
    s->vp = (rsxVertexProgram *)data;
    rsxVertexProgramGetUCode(s->vp, &s->vp_ucode, &s->vp_ucode_size);
}

static void load_fp(ps3gl_shader_t *s, const void *data, uint32_t size)
{
    s->fp = (rsxFragmentProgram *)data;

    void *ucode;
    rsxFragmentProgramGetUCode(s->fp, &ucode, &s->fp_ucode_size);

    /* Fragment program ucode must reside in RSX-accessible memory */
    s->fp_ucode = rsxMemalign(64, s->fp_ucode_size);
    if (s->fp_ucode) {
        memcpy(s->fp_ucode, ucode, s->fp_ucode_size);
        rsxAddressToOffset(s->fp_ucode, &s->fp_offset);
    }
}

void ps3gl_shaders_init(void)
{
    printf("[ps3gl] Loading compiled shaders...\n");

    /* All shader slots share the same vertex program */
    for (int i = 0; i < 2 * PS3GL_TENV_COUNT; i++) {
        memset(&ps3gl.shaders[i], 0, sizeof(ps3gl_shader_t));
        load_vp(&ps3gl.shaders[i], shader_vp_data, shader_vp_data_size);
        ps3gl.shaders[i].mvp_const = rsxVertexProgramGetConst(ps3gl.shaders[i].vp, "mvp");
        ps3gl.shaders[i].fog_params_const = rsxVertexProgramGetConst(ps3gl.shaders[i].vp, "fogParams");
        ps3gl.shaders[i].fog_color_const = rsxVertexProgramGetConst(ps3gl.shaders[i].vp, "fogColor");
    }

    /* Load per-mode fragment programs */
    load_fp(&ps3gl.shaders[PS3GL_TENV_DISABLED], shader_fp_coloronly_data, shader_fp_coloronly_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_MODULATE], shader_fp_modulate_data, shader_fp_modulate_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_REPLACE],  shader_fp_replace_data,  shader_fp_replace_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_DECAL],    shader_fp_decal_data,    shader_fp_decal_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_ADD],       shader_fp_add_data,      shader_fp_add_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_BLEND],     shader_fp_blend_data,    shader_fp_blend_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_MODULATE2], shader_fp_modulate2_data, shader_fp_modulate2_data_size);

#if PS3GL_SHADERS_FOG
    /* the same modes with GL fog (shaders/asm/q3_fp_*_fog.fpa) */
    {
        ps3gl_shader_t *f = &ps3gl.shaders[PS3GL_TENV_COUNT];
        load_fp(&f[PS3GL_TENV_DISABLED],  shader_fp_coloronly_fog_data, shader_fp_coloronly_fog_data_size);
        load_fp(&f[PS3GL_TENV_MODULATE],  shader_fp_modulate_fog_data,  shader_fp_modulate_fog_data_size);
        load_fp(&f[PS3GL_TENV_REPLACE],   shader_fp_replace_fog_data,   shader_fp_replace_fog_data_size);
        load_fp(&f[PS3GL_TENV_DECAL],     shader_fp_decal_fog_data,     shader_fp_decal_fog_data_size);
        load_fp(&f[PS3GL_TENV_ADD],       shader_fp_add_fog_data,       shader_fp_add_fog_data_size);
        load_fp(&f[PS3GL_TENV_BLEND],     shader_fp_blend_fog_data,     shader_fp_blend_fog_data_size);
        load_fp(&f[PS3GL_TENV_MODULATE2], shader_fp_modulate2_fog_data, shader_fp_modulate2_fog_data_size);
    }
    ps3_log(ps3gl.shaders[0].fog_params_const && ps3gl.shaders[0].fog_color_const
            ? "ps3gl: fog shaders loaded" : "ps3gl: fog shaders NOT usable (VP without fog constants)");
#endif
    ps3gl.fog_uploaded_gen = 0;

    ps3gl.active_shader = -1;
    ps3gl.active_vp = NULL;
    ps3gl.mvp_uploaded_gen = 0;
    printf("[ps3gl] Shaders loaded: %d modes\n", PS3GL_TENV_COUNT);
}

void ps3gl_shaders_shutdown(void)
{
    for (int i = 0; i < 2 * PS3GL_TENV_COUNT; i++) {
        if (ps3gl.shaders[i].fp_ucode) {
            rsxFree(ps3gl.shaders[i].fp_ucode);
            ps3gl.shaders[i].fp_ucode = NULL;
        }
    }
}

int ps3gl_shader_key(void)
{
    int tmu0 = ps3gl.tmu[0].enabled && ps3gl.tmu[0].bound && ps3gl.tmu[0].bound->data;
    int tmu1 = ps3gl.tmu[1].enabled && ps3gl.tmu[1].bound && ps3gl.tmu[1].bound->data;

    /* Both TMUs active: use dual-texture modulate (diffuse * lightmap). */
    if (tmu0 && tmu1)
        return PS3GL_TENV_MODULATE2;

    if (tmu0)
        return ps3gl.tmu[0].texenv;

    return PS3GL_TENV_DISABLED;
}

/* PS3 port: GL fog as vertex program constants (see shaders/asm/q3_vp.vpa):
 * fogParams = (linear scale, linear bias, exp2 scale, linear weight) of the
 * clip w, which is the eye distance along the view axis. */
static float s_fog_logged[4];

static void ps3gl_upload_fog(gcmContextData *ctx, ps3gl_shader_t *s)
{
    float p[4];

    if (ps3gl.fog.mode == GL_LINEAR) {
        float range = ps3gl.fog.end - ps3gl.fog.start;
        if (range < 1.0f)
            range = 1.0f;
        p[0] = -1.0f / range;
        p[1] = ps3gl.fog.end / range;
        p[2] = 0.0f;
        p[3] = 1.0f;
    } else {                        /* GL_EXP (GL_EXP2 approximated by it) */
        p[0] = 0.0f;
        p[1] = 0.0f;
        p[2] = -ps3gl.fog.density * 1.4426950409f;   /* e^-x = 2^(-x log2 e) */
        p[3] = 0.0f;
    }
    rsxSetVertexProgramParameter(ctx, s->vp, s->fog_params_const, p);
    rsxSetVertexProgramParameter(ctx, s->vp, s->fog_color_const, ps3gl.fog.color);
    ps3gl.fog_uploaded_gen = ps3gl.fog.gen;

    /* say which fogs get drawn (the first few changes) */
    {
        static int logged;
#ifndef PS3_DIAG
        logged = 8;         /* diagnostic builds only (make DIAG=1) */
#endif
        if (logged < 8 && (p[0] != s_fog_logged[0] || p[1] != s_fog_logged[1] || p[2] != s_fog_logged[2])) {
            char msg[160];
            logged++;
            memcpy(s_fog_logged, p, sizeof(s_fog_logged));
            snprintf(msg, sizeof(msg), "[fog] %s start %.0f end %.0f density %.5f color %.2f %.2f %.2f",
                     ps3gl.fog.mode == GL_LINEAR ? "linear" : "exp", ps3gl.fog.start, ps3gl.fog.end,
                     ps3gl.fog.density, ps3gl.fog.color[0], ps3gl.fog.color[1], ps3gl.fog.color[2]);
            ps3_log(msg);
        }
    }
}

void ps3gl_apply_shader(void)
{
    int key = ps3gl_shader_key();
    if (key < 0 || key >= PS3GL_TENV_COUNT) key = PS3GL_TENV_DISABLED;

#if PS3GL_SHADERS_FOG
    if (ps3gl.fog.enabled && ps3gl.shaders[key].fog_params_const && ps3gl.shaders[key].fog_color_const)
        key += PS3GL_TENV_COUNT;
#endif

    ps3gl_shader_t *s = &ps3gl.shaders[key];
    gcmContextData *ctx = ps3gl_get_ctx();
    if (!ctx) return;

    /* All shader slots share one VP (see ps3gl_shaders_init) -- reload it only
     * when it's not already bound, not on every FP/texenv key change. */
    if (s->vp != ps3gl.active_vp) {
        rsxLoadVertexProgram(ctx, s->vp, s->vp_ucode);
        ps3gl.active_vp = s->vp;
    }

    if (key != ps3gl.active_shader) {
        if (s->fp_ucode) {
            rsxLoadFragmentProgramLocation(ctx, s->fp, s->fp_offset,
                                           GCM_LOCATION_RSX);
        }

        ps3gl.active_shader = key;
    }

    /* MVP constant is independent of which FP is bound -- only re-patch it
     * when it changed since the last upload (patching RSX microcode isn't free). */
    uint32_t mvp_gen = ps3gl_get_mvp_generation();
    if (s->mvp_const && mvp_gen != ps3gl.mvp_uploaded_gen) {
        const float *mvp = ps3gl_get_mvp();
        rsxSetVertexProgramParameter(ctx, s->vp, s->mvp_const, mvp);
        ps3gl.mvp_uploaded_gen = mvp_gen;
    }

#if PS3GL_SHADERS_FOG
    if (key >= PS3GL_TENV_COUNT && ps3gl.fog_uploaded_gen != ps3gl.fog.gen)
        ps3gl_upload_fog(ctx, s);
#endif

    /* Upload the world-space clip plane only if this shader binary was recompiled to support
     * it (clip_plane_const is NULL on the old pre-clip-plane binary -- software clip in ps3gl_draw.c covers that case). */
    if (s->clip_plane_const) {
        rsxSetVertexProgramParameter(ctx, s->vp, s->clip_plane_const,
                                     ps3gl.clip_plane);
    }
}

#else /* PS3GL_SHADERS_AVAILABLE == 0 */

/* Stub implementations when shaders are not compiled */

void ps3gl_shaders_init(void)
{
    printf("[ps3gl] WARNING: No compiled shaders available.\n");
    printf("[ps3gl] Build ps3toolchain, then run: cd code/gl/shaders && ./compile_shaders.sh\n");
    ps3gl.active_shader = -1;
}

void ps3gl_shaders_shutdown(void) {}

int ps3gl_shader_key(void) { return PS3GL_TENV_DISABLED; }

void ps3gl_apply_shader(void)
{
    /* No shaders to apply -- draws will produce no visible output */
}

#endif /* PS3GL_SHADERS_AVAILABLE */
