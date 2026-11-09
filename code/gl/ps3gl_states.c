/* ps3gl_states.c -- GL-to-RSX: render state + batch apply to RSX. */

#include "ps3gl.h"
#include <stdio.h>

/* GL-to-GCM conversion helpers */

/* GL comparison func values match GCM exactly (0x200..0x207) */
static inline uint32_t gl_to_gcm_cmpfunc(GLenum func)
{
    switch (func) {
    case GL_NEVER:    return GCM_NEVER;
    case GL_LESS:     return GCM_LESS;
    case GL_EQUAL:    return GCM_EQUAL;
    case GL_LEQUAL:   return GCM_LEQUAL;
    case GL_GREATER:  return GCM_GREATER;
    case GL_NOTEQUAL: return GCM_NOTEQUAL;
    case GL_GEQUAL:   return GCM_GEQUAL;
    case GL_ALWAYS:   return GCM_ALWAYS;
    default:          return GCM_ALWAYS;
    }
}

static inline uint16_t gl_to_gcm_blend(GLenum factor)
{
    switch (factor) {
    case GL_ZERO:                return GCM_ZERO;
    case GL_ONE:                 return GCM_ONE;
    case GL_SRC_COLOR:           return GCM_SRC_COLOR;
    case GL_ONE_MINUS_SRC_COLOR: return GCM_ONE_MINUS_SRC_COLOR;
    case GL_SRC_ALPHA:           return GCM_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA: return GCM_ONE_MINUS_SRC_ALPHA;
    case GL_DST_ALPHA:           return GCM_DST_ALPHA;
    case GL_ONE_MINUS_DST_ALPHA: return GCM_ONE_MINUS_DST_ALPHA;
    case GL_DST_COLOR:           return GCM_DST_COLOR;
    case GL_ONE_MINUS_DST_COLOR: return GCM_ONE_MINUS_DST_COLOR;
    case GL_SRC_ALPHA_SATURATE:  return GCM_SRC_ALPHA_SATURATE;
    default:                     return GCM_ONE;
    }
}

static inline uint32_t gl_to_gcm_stencilop(GLenum op)
{
    switch (op) {
    case GL_KEEP:    return GCM_KEEP;
    case GL_ZERO:    return GCM_ZERO;
    case GL_REPLACE: return GCM_REPLACE;
    case GL_INCR:    return GCM_INCR;
    case GL_DECR:    return GCM_DECR;
    case GL_INVERT:  return GCM_INVERT;
    default:         return GCM_KEEP;
    }
}

/* GL state setters -- record state + mark dirty */

static void enable_disable(GLenum cap, int val)
{
    switch (cap) {
    case GL_BLEND:
        ps3gl.rs.blend_enable = val;
        ps3gl.dirty |= PS3GL_DIRTY_BLEND;
        break;
    case GL_ALPHA_TEST:
        ps3gl.rs.alpha_test_enable = val;
        ps3gl.dirty |= PS3GL_DIRTY_ALPHA;
        break;
    case GL_DEPTH_TEST:
        ps3gl.rs.depth_test_enable = val;
        ps3gl.dirty |= PS3GL_DIRTY_DEPTH;
        break;
    case GL_CULL_FACE:
        ps3gl.rs.cull_enable = val;
        ps3gl.dirty |= PS3GL_DIRTY_CULL;
        break;
    case GL_SCISSOR_TEST:
        ps3gl.rs.scissor_enable = val;
        ps3gl.dirty |= PS3GL_DIRTY_SCISSOR;
        break;
    case GL_STENCIL_TEST:
        ps3gl.rs.stencil_enable = val;
        ps3gl.dirty |= PS3GL_DIRTY_STENCIL;
        break;
    case GL_POLYGON_OFFSET_FILL:
        ps3gl.rs.polyoffset_fill = val;
        ps3gl.dirty |= PS3GL_DIRTY_POLYOFFSET;
        break;
    case GL_TEXTURE_2D:
        if (ps3gl.tmu[ps3gl.active_tmu].enabled != val)
            ps3gl.tmu[ps3gl.active_tmu].dirty = 1;
        ps3gl.tmu[ps3gl.active_tmu].enabled = val;
        break;
    case GL_FOG:
        ps3gl.fog.enabled = val;    /* picks the fog fragment programs */
        break;
    case GL_MULTISAMPLE:
    case GL_NORMALIZE:
    case GL_POLYGON_OFFSET_LINE:
        break;
    case GL_CLIP_PLANE0:
        ps3gl.clip_plane_enabled = val;
        break;
    default:
        break;
    }
}

void ps3gl_Enable(GLenum cap)
{
    PS3GL_FRONT_CHECK();
    enable_disable(cap, 1);
}
void ps3gl_Disable(GLenum cap)
{
    PS3GL_FRONT_CHECK();
    enable_disable(cap, 0);
}
void ps3gl_BlendFunc(GLenum sfactor, GLenum dfactor)
{
    PS3GL_FRONT_CHECK();
    ps3gl.rs.blend_src = gl_to_gcm_blend(sfactor);
    ps3gl.rs.blend_dst = gl_to_gcm_blend(dfactor);
    ps3gl.dirty |= PS3GL_DIRTY_BLEND;
}

void ps3gl_AlphaFunc(GLenum func, GLclampf ref)
{
    ps3gl.rs.alpha_func = gl_to_gcm_cmpfunc(func);
    ps3gl.rs.alpha_ref  = (uint32_t)(ref * 255.0f + 0.5f);
    if (ps3gl.rs.alpha_ref > 255) ps3gl.rs.alpha_ref = 255;
    ps3gl.dirty |= PS3GL_DIRTY_ALPHA;
}

void ps3gl_DepthFunc(GLenum func)
{
    ps3gl.rs.depth_func = gl_to_gcm_cmpfunc(func);
    ps3gl.dirty |= PS3GL_DIRTY_DEPTH;
}

void ps3gl_DepthMask(GLboolean flag)
{
    PS3GL_FRONT_CHECK();
    ps3gl.rs.depth_mask = flag ? 1 : 0;
    ps3gl.dirty |= PS3GL_DIRTY_DEPTH;
}

void ps3gl_DepthRange(GLclampd n, GLclampd f)
{
    ps3gl.rs.depth_near = (float)n;
    ps3gl.rs.depth_far  = (float)f;
    ps3gl.dirty |= PS3GL_DIRTY_VIEWPORT;
}

void ps3gl_ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    ps3gl.rs.color_mask_r = r;
    ps3gl.rs.color_mask_g = g;
    ps3gl.rs.color_mask_b = b;
    ps3gl.rs.color_mask_a = a;
    ps3gl.dirty |= PS3GL_DIRTY_COLORMASK;
}

void ps3gl_CullFace(GLenum mode)
{
    switch (mode) {
    case GL_FRONT:          ps3gl.rs.cull_face = GCM_CULL_FRONT; break;
    case GL_BACK:           ps3gl.rs.cull_face = GCM_CULL_BACK; break;
    case GL_FRONT_AND_BACK: ps3gl.rs.cull_face = GCM_CULL_ALL; break;
    default: break;
    }
    ps3gl.dirty |= PS3GL_DIRTY_CULL;
}

void ps3gl_FrontFace(GLenum mode)
{
    ps3gl.rs.front_face = (mode == GL_CW) ? GCM_FRONTFACE_CW : GCM_FRONTFACE_CCW;
    ps3gl.dirty |= PS3GL_DIRTY_CULL;
}

/* A GL rect (Y=0 at the bottom of the engine's screen_w x screen_h screen)
 * into the RSX surface (Y=0 at the top), scaled into the screen-fit rect. */
static void ps3gl_map_rect(const int *r, int16_t *ox, int16_t *oy, uint16_t *ow, uint16_t *oh)
{
    int sw = ps3gl.screen_w ? (int)ps3gl.screen_w : 1;
    int sh = ps3gl.screen_h ? (int)ps3gl.screen_h : 1;
    int top = sh - r[1] - r[3];     /* GL bottom-up -> RSX top-down */
    int x0 = ps3gl.fit_x + (r[0] * ps3gl.fit_w + sw / 2) / sw;
    int x1 = ps3gl.fit_x + ((r[0] + r[2]) * ps3gl.fit_w + sw / 2) / sw;
    int y0 = ps3gl.fit_y + (top * ps3gl.fit_h + sh / 2) / sh;
    int y1 = ps3gl.fit_y + ((top + r[3]) * ps3gl.fit_h + sh / 2) / sh;

    *ox = (int16_t)x0;
    *oy = (int16_t)y0;
    *ow = (uint16_t)(x1 > x0 ? x1 - x0 : 0);
    *oh = (uint16_t)(y1 > y0 ? y1 - y0 : 0);
}

void ps3gl_Scissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
    PS3GL_FRONT_CHECK();
    ps3gl.gl_sc[0] = x;
    ps3gl.gl_sc[1] = y;
    ps3gl.gl_sc[2] = w;
    ps3gl.gl_sc[3] = h;
    ps3gl_map_rect(ps3gl.gl_sc, &ps3gl.rs.scissor_x, &ps3gl.rs.scissor_y,
                   &ps3gl.rs.scissor_w, &ps3gl.rs.scissor_h);
    ps3gl.dirty |= PS3GL_DIRTY_SCISSOR;
}

void ps3gl_Viewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    PS3GL_FRONT_CHECK();
    /* ioq3 viewport Y is GL-convention (Y=0 at bottom); the RSX surface has
     * Y=0 at top. Screen fit: scaled into the fit rect (ps3gl_map_rect). */
    ps3gl.gl_vp[0] = x;
    ps3gl.gl_vp[1] = y;
    ps3gl.gl_vp[2] = w;
    ps3gl.gl_vp[3] = h;
    ps3gl_map_rect(ps3gl.gl_vp, &ps3gl.rs.vp_x, &ps3gl.rs.vp_y,
                   &ps3gl.rs.vp_w, &ps3gl.rs.vp_h);
    ps3gl.dirty |= PS3GL_DIRTY_VIEWPORT;
}

void ps3gl_ShadeModel(GLenum mode)
{
    ps3gl.rs.shade_model = (mode == GL_FLAT) ? GCM_SHADE_MODEL_FLAT
                                             : GCM_SHADE_MODEL_SMOOTH;
    ps3gl.dirty |= PS3GL_DIRTY_SHADE;
}

void ps3gl_PolygonOffset(GLfloat factor, GLfloat units)
{
    ps3gl.rs.polyoffset_factor = factor;
    ps3gl.rs.polyoffset_units  = units;
    ps3gl.dirty |= PS3GL_DIRTY_POLYOFFSET;
}

void ps3gl_PolygonMode(GLenum face, GLenum mode)
{
    (void)face; (void)mode;
    /* RSX supports polygon modes but Q3 only uses GL_FILL */
}

void ps3gl_StencilFunc(GLenum func, GLint ref, GLuint mask)
{
    ps3gl.rs.stencil_func = gl_to_gcm_cmpfunc(func);
    ps3gl.rs.stencil_ref  = (uint32_t)ref;
    ps3gl.rs.stencil_mask = mask;
    ps3gl.dirty |= PS3GL_DIRTY_STENCIL;
}

void ps3gl_StencilMask(GLuint mask)
{
    ps3gl.rs.stencil_writemask = mask;
    ps3gl.dirty |= PS3GL_DIRTY_STENCIL;
}

void ps3gl_StencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
    ps3gl.rs.stencil_fail  = gl_to_gcm_stencilop(fail);
    ps3gl.rs.stencil_zfail = gl_to_gcm_stencilop(zfail);
    ps3gl.rs.stencil_zpass = gl_to_gcm_stencilop(zpass);
    ps3gl.dirty |= PS3GL_DIRTY_STENCIL;
}

void ps3gl_LineWidth(GLfloat width)  { (void)width; }

void ps3gl_ClipPlane(GLenum plane, const GLdouble *eq)
{
    /* Intentional no-op: Q3's portal renderer uses ps3gl_SetWorldClipPlane
     * instead, which stores the plane in world space for correct evaluation. */
    (void)plane; (void)eq;
}

void ps3gl_SetWorldClipPlane(float nx, float ny, float nz, float dist)
{
    ps3gl.clip_plane[0] = nx;
    ps3gl.clip_plane[1] = ny;
    ps3gl.clip_plane[2] = nz;
    ps3gl.clip_plane[3] = dist;
}

/* Clear */

void ps3gl_ClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{
    PS3GL_FRONT_CHECK();
    uint32_t ri = (uint32_t)(r * 255.0f);
    uint32_t gi = (uint32_t)(g * 255.0f);
    uint32_t bi = (uint32_t)(b * 255.0f);
    uint32_t ai = (uint32_t)(a * 255.0f);
    ps3gl.rs.clear_color = (ai << 24) | (ri << 16) | (gi << 8) | bi;
}

void ps3gl_ClearDepth(GLclampd depth)
{
    ps3gl.rs.clear_depth = (float)depth;
}

void ps3gl_ClearStencil(GLint s)
{
    ps3gl.rs.clear_stencil = (uint32_t)s;
}

void ps3gl_Clear(GLbitfield mask)
{
    PS3GL_FRONT_CHECK();
    gcmContextData *ctx = ps3gl_get_ctx();
    if (!ctx) return;
    uint32_t gcm_mask = 0;

    if (mask & GL_COLOR_BUFFER_BIT) {
        rsxSetClearColor(ctx, ps3gl.rs.clear_color);
        gcm_mask |= GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A;
    }
    if (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) {
        /* Z24S8: upper 24 bits = depth, lower 8 = stencil */
        uint32_t z24 = (uint32_t)(ps3gl.rs.clear_depth * 0xFFFFFF);
        uint32_t ds  = (z24 << 8) | (ps3gl.rs.clear_stencil & 0xFF);
        rsxSetClearDepthStencil(ctx, ds);
        if (mask & GL_DEPTH_BUFFER_BIT)   gcm_mask |= GCM_CLEAR_Z;
        if (mask & GL_STENCIL_BUFFER_BIT) gcm_mask |= GCM_CLEAR_S;
    }

    if (gcm_mask)
        rsxClearSurface(ctx, gcm_mask);
}

void ps3gl_set_fit(int x, int y, int w, int h)
{
    ps3gl.fit_x = x;
    ps3gl.fit_y = y;
    ps3gl.fit_w = w;
    ps3gl.fit_h = h;
    /* the engine's current viewport and scissor, into the new rect */
    ps3gl_map_rect(ps3gl.gl_vp, &ps3gl.rs.vp_x, &ps3gl.rs.vp_y,
                   &ps3gl.rs.vp_w, &ps3gl.rs.vp_h);
    ps3gl_map_rect(ps3gl.gl_sc, &ps3gl.rs.scissor_x, &ps3gl.rs.scissor_y,
                   &ps3gl.rs.scissor_w, &ps3gl.rs.scissor_h);
    ps3gl.dirty |= PS3GL_DIRTY_VIEWPORT | PS3GL_DIRTY_SCISSOR;
}

/* Black out the whole surface (the screen-fit border included). The RSX
 * clear honors the scissor and the viewport, so both are opened to the full
 * surface first and marked dirty for the next draw. */
void ps3gl_clear_full_surface(void)
{
    gcmContextData *ctx = ps3gl_get_ctx();
    float scale[4], offset[4];

    if (!ctx) return;
    scale[0] = ps3gl.screen_w * 0.5f;  scale[1] = ps3gl.screen_h * -0.5f;
    scale[2] = 0.5f;                   scale[3] = 0.0f;
    offset[0] = ps3gl.screen_w * 0.5f; offset[1] = ps3gl.screen_h * 0.5f;
    offset[2] = 0.5f;                  offset[3] = 0.0f;
    rsxSetViewport(ctx, 0, 0, ps3gl.screen_w, ps3gl.screen_h, 0.0f, 1.0f, scale, offset);
    rsxSetScissor(ctx, 0, 0, ps3gl.screen_w, ps3gl.screen_h);
    rsxSetColorMask(ctx, GCM_COLOR_MASK_R | GCM_COLOR_MASK_G | GCM_COLOR_MASK_B | GCM_COLOR_MASK_A);
    rsxSetClearColor(ctx, 0xff000000);
    rsxSetClearDepthStencil(ctx, 0xffffff00);
    rsxClearSurface(ctx, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A |
                         GCM_CLEAR_Z | GCM_CLEAR_S);
    ps3gl.dirty |= PS3GL_DIRTY_VIEWPORT | PS3GL_DIRTY_SCISSOR | PS3GL_DIRTY_COLORMASK;
}

/* Query */

void ps3gl_GetIntegerv(GLenum pname, GLint *params)
{
    if (!params) return;
    switch (pname) {
    case GL_MAX_TEXTURE_SIZE:      *params = 2048; break;
    case GL_MAX_TEXTURE_UNITS_ARB: *params = 2; break;
    case GL_STENCIL_BITS:          *params = 8; break;
    case GL_DEPTH_BITS:            *params = 24; break;
    default:                       *params = 0; break;
    }
}

const GLubyte *ps3gl_GetString(GLenum name)
{
    switch (name) {
    case GL_VENDOR:     return (const GLubyte *)"Sony";
    case GL_RENDERER:   return (const GLubyte *)"RSX Reality Synthesizer";
    case GL_VERSION:    return (const GLubyte *)"1.1 PSL1GHT";
    case GL_EXTENSIONS: return (const GLubyte *)"GL_ARB_multitexture";
    default:            return (const GLubyte *)"";
    }
}

GLenum ps3gl_GetError(void) { return GL_NO_ERROR; }

void ps3gl_GetBooleanv(GLenum pname, GLboolean *params)
{
    if (!params) return;
    switch (pname) {
    case GL_COLOR_WRITEMASK:
        /* ioq3 tr_shadows.c reads rgba[4] */
        params[0] = ps3gl.rs.color_mask_r ? GL_TRUE : GL_FALSE;
        params[1] = ps3gl.rs.color_mask_g ? GL_TRUE : GL_FALSE;
        params[2] = ps3gl.rs.color_mask_b ? GL_TRUE : GL_FALSE;
        params[3] = ps3gl.rs.color_mask_a ? GL_TRUE : GL_FALSE;
        break;
    case GL_DEPTH_WRITEMASK:
        params[0] = ps3gl.rs.depth_mask ? GL_TRUE : GL_FALSE;
        break;
    default:
        params[0] = GL_FALSE;
        break;
    }
}

/* Apply states to RSX before draw */

void ps3gl_states_init(void)
{
    /* Push full state to RSX on first frame */
    ps3gl.dirty = PS3GL_DIRTY_ALL;

    /* GL defaults */
    ps3gl.fog.enabled = 0;
    ps3gl.fog.mode = GL_EXP;
    ps3gl.fog.density = 1.0f;
    ps3gl.fog.start = 0.0f;
    ps3gl.fog.end = 1.0f;
    ps3gl.fog.color[0] = ps3gl.fog.color[1] = ps3gl.fog.color[2] = ps3gl.fog.color[3] = 0.0f;
    ps3gl.fog.gen++;
}

/* ---- Fog (PS3 port) -------------------------------------------------------
 * Only the state; the vertex program constants go up in ps3gl_apply_shader
 * when a fogged draw needs them. */

/* RTCW sets all of them before every shader batch (R_Fog): only a real
 * change re-uploads the constants */
#define PS3GL_FOG_SET(field, v) do { \
    if (ps3gl.fog.field != (v)) { ps3gl.fog.field = (v); ps3gl.fog.gen++; } \
} while (0)

void ps3gl_Fogf(GLenum pname, GLfloat param)
{
    switch (pname) {
    case GL_FOG_MODE:
        PS3GL_FOG_SET(mode, (GLenum)param);
        break;
    case GL_FOG_DENSITY:
        PS3GL_FOG_SET(density, param);
        break;
    case GL_FOG_START:
        PS3GL_FOG_SET(start, param);
        break;
    case GL_FOG_END:
        PS3GL_FOG_SET(end, param);
        break;
    default:
        break;                      /* GL_FOG_INDEX, GL_FOG_DISTANCE_MODE_NV... */
    }
}

void ps3gl_Fogi(GLenum pname, GLint param)
{
    ps3gl_Fogf(pname, (GLfloat)param);
}

void ps3gl_Fogfv(GLenum pname, const GLfloat *params)
{
    if (!params)
        return;
    if (pname == GL_FOG_COLOR) {
        PS3GL_FOG_SET(color[0], params[0]);
        PS3GL_FOG_SET(color[1], params[1]);
        PS3GL_FOG_SET(color[2], params[2]);
        PS3GL_FOG_SET(color[3], params[3]);
    } else {
        ps3gl_Fogf(pname, params[0]);
    }
}

void ps3gl_states_shutdown(void) {}

void ps3gl_apply_states(void)
{
    gcmContextData *ctx = ps3gl_get_ctx();
    uint32_t d = ps3gl.dirty;
    if (d == 0) return;
    if (!ctx) return;

    if (d & PS3GL_DIRTY_BLEND) {
        rsxSetBlendEnable(ctx, ps3gl.rs.blend_enable);
        if (ps3gl.rs.blend_enable) {
            rsxSetBlendFunc(ctx, ps3gl.rs.blend_src, ps3gl.rs.blend_dst,
                            ps3gl.rs.blend_src, ps3gl.rs.blend_dst);
            rsxSetBlendEquation(ctx, GCM_FUNC_ADD, GCM_FUNC_ADD);
        }
    }

    if (d & PS3GL_DIRTY_ALPHA) {
        rsxSetAlphaTestEnable(ctx, ps3gl.rs.alpha_test_enable);
        if (ps3gl.rs.alpha_test_enable) {
            rsxSetAlphaFunc(ctx, ps3gl.rs.alpha_func, ps3gl.rs.alpha_ref);
        }
    }

    if (d & PS3GL_DIRTY_DEPTH) {
        rsxSetDepthTestEnable(ctx, ps3gl.rs.depth_test_enable);
        rsxSetDepthFunc(ctx, ps3gl.rs.depth_func);
        rsxSetDepthWriteEnable(ctx, ps3gl.rs.depth_mask);
    }

    if (d & PS3GL_DIRTY_CULL) {
        rsxSetCullFaceEnable(ctx, ps3gl.rs.cull_enable);
        if (ps3gl.rs.cull_enable) {
            rsxSetCullFace(ctx, ps3gl.rs.cull_face);
            rsxSetFrontFace(ctx, ps3gl.rs.front_face);
        }
    }

    if (d & PS3GL_DIRTY_SCISSOR) {
        if (ps3gl.rs.scissor_enable) {
            /* Clamp scissor to screen bounds (origin can be negative from
             * sub-viewport rendering, e.g. Player Setup model preview). */
            int sx = ps3gl.rs.scissor_x;
            int sy = ps3gl.rs.scissor_y;
            int sw = ps3gl.rs.scissor_w;
            int sh = ps3gl.rs.scissor_h;
            int fx0 = ps3gl.fit_x, fy0 = ps3gl.fit_y;
            int fx1 = ps3gl.fit_x + ps3gl.fit_w, fy1 = ps3gl.fit_y + ps3gl.fit_h;
            if (sx < fx0) { sw -= fx0 - sx; sx = fx0; }
            if (sy < fy0) { sh -= fy0 - sy; sy = fy0; }
            if (sw < 1) sw = 1;
            if (sh < 1) sh = 1;
            if (sx + sw > fx1) sw = fx1 - sx;
            if (sy + sh > fy1) sh = fy1 - sy;
            if (sw < 1) sw = 1;
            if (sh < 1) sh = 1;
            rsxSetScissor(ctx, (uint16_t)sx, (uint16_t)sy,
                          (uint16_t)sw, (uint16_t)sh);
        } else {
            /* nothing is ever drawn outside the fit rect */
            rsxSetScissor(ctx, (uint16_t)ps3gl.fit_x, (uint16_t)ps3gl.fit_y,
                          (uint16_t)ps3gl.fit_w, (uint16_t)ps3gl.fit_h);
        }
    }

    if (d & PS3GL_DIRTY_VIEWPORT) {
        /* Viewport origin can be negative (sub-viewport); clamp rect to screen bounds. */
        int vx = ps3gl.rs.vp_x;
        int vy = ps3gl.rs.vp_y;
        int vw = ps3gl.rs.vp_w;
        int vh = ps3gl.rs.vp_h;
        float zn = ps3gl.rs.depth_near;
        float zf = ps3gl.rs.depth_far;

        /* Scale/offset computed from the FULL viewport (including the
         * off-screen portion) so the projection maps correctly. */
        float scale[4], offset[4];
        scale[0]  = vw * 0.5f;
        scale[1]  = vh * -0.5f;
        scale[2]  = (zf - zn) * 0.5f;
        scale[3]  = 0.0f;
        offset[0] = vx + vw * 0.5f;
        offset[1] = vy + vh * 0.5f;
        offset[2] = (zf + zn) * 0.5f;
        offset[3] = 0.0f;

        /* Clamp the hardware viewport rect to screen bounds.
         * The scissor test will further clip to the visible region. */
        int cx = vx, cy = vy, cw = vw, ch = vh;
        if (cx < 0) { cw += cx; cx = 0; }
        if (cy < 0) { ch += cy; cy = 0; }
        if (cw < 1) cw = 1;
        if (ch < 1) ch = 1;
        if (cx + cw > (int)ps3gl.screen_w) cw = (int)ps3gl.screen_w - cx;
        if (cy + ch > (int)ps3gl.screen_h) ch = (int)ps3gl.screen_h - cy;
        if (cw < 1) cw = 1;
        if (ch < 1) ch = 1;

        rsxSetViewport(ctx, (uint16_t)cx, (uint16_t)cy,
                       (uint16_t)cw, (uint16_t)ch,
                       zn, zf, scale, offset);
        rsxSetViewportClip(ctx, 0, ps3gl.screen_w, ps3gl.screen_h);
    }

    if (d & PS3GL_DIRTY_COLORMASK) {
        uint32_t mask = 0;
        if (ps3gl.rs.color_mask_b) mask |= GCM_COLOR_MASK_B;
        if (ps3gl.rs.color_mask_g) mask |= GCM_COLOR_MASK_G;
        if (ps3gl.rs.color_mask_r) mask |= GCM_COLOR_MASK_R;
        if (ps3gl.rs.color_mask_a) mask |= GCM_COLOR_MASK_A;
        rsxSetColorMask(ctx, mask);
    }

    if (d & PS3GL_DIRTY_POLYOFFSET) {
        rsxSetPolygonOffsetFillEnable(ctx, ps3gl.rs.polyoffset_fill);
        if (ps3gl.rs.polyoffset_fill) {
            rsxSetPolygonOffset(ctx, ps3gl.rs.polyoffset_factor,
                                ps3gl.rs.polyoffset_units);
        }
    }

    if (d & PS3GL_DIRTY_SHADE) {
        rsxSetShadeModel(ctx, ps3gl.rs.shade_model);
    }

    if (d & PS3GL_DIRTY_STENCIL) {
        rsxSetStencilTestEnable(ctx, ps3gl.rs.stencil_enable);
        if (ps3gl.rs.stencil_enable) {
            rsxSetStencilFunc(ctx, ps3gl.rs.stencil_func,
                              ps3gl.rs.stencil_ref, ps3gl.rs.stencil_mask);
            rsxSetStencilOp(ctx, ps3gl.rs.stencil_fail,
                            ps3gl.rs.stencil_zfail, ps3gl.rs.stencil_zpass);
            rsxSetStencilMask(ctx, ps3gl.rs.stencil_writemask);
        }
    }

    ps3gl.dirty = 0;
}
