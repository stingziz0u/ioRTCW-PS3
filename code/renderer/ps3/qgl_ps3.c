/* qgl_ps3.c -- defines the renderer's qgl* pointers and wires them to ps3gl.
 *
 * Based on qgl_ps3.c from IoQuake3-PS3 (Mayo1970). Every GL entry point the
 * renderer knows first gets a generated stub that logs its name once and
 * returns 0, so a call ps3gl does not implement shows up in the log instead
 * of jumping through a NULL pointer. The real implementations are then wired
 * on top. */

#include "renderer/tr_local.h"
#include "../gl/ps3gl.h"
#include "sys/ps3_log.h"

/* qgl* pointer definitions (sdl_glimp.c defines them upstream). */
#define GLE(ret, name, ...) name##proc * qgl##name = NULL;
QGL_1_1_PROCS;
QGL_1_1_FIXED_FUNCTION_PROCS;
QGL_DESKTOP_1_1_PROCS;
QGL_DESKTOP_1_1_FIXED_FUNCTION_PROCS;
QGL_ES_1_1_PROCS;
QGL_ES_1_1_FIXED_FUNCTION_PROCS;
QGL_1_3_PROCS;
QGL_1_5_PROCS;
QGL_2_0_PROCS;
QGL_3_0_PROCS;
QGL_ARB_occlusion_query_PROCS;
QGL_ARB_framebuffer_object_PROCS;
QGL_ARB_vertex_array_object_PROCS;
QGL_EXT_direct_state_access_PROCS;
#undef GLE

void (APIENTRYP qglActiveTextureARB) (GLenum texture);
void (APIENTRYP qglClientActiveTextureARB) (GLenum texture);
void (APIENTRYP qglMultiTexCoord2fARB) (GLenum target, GLfloat s, GLfloat t);
void (APIENTRYP qglLockArraysEXT) (GLint first, GLsizei count);
void (APIENTRYP qglUnlockArraysEXT) (void);

int qglMajorVersion = 1, qglMinorVersion = 1;
int qglesMajorVersion = 0, qglesMinorVersion = 0;

/* ---- Stubs for everything ps3gl does not implement ----------------------- */

static void PS3_GLUnimplemented(const char *name, int *logged)
{
    if (!*logged) {
        *logged = 1;
        PS3_Logf("ps3gl: gl%s is not implemented (call ignored)", name);
    }
}

#define GLE(ret, name, ...) \
    static ret APIENTRY stub_##name(__VA_ARGS__) { \
        static int logged; \
        PS3_GLUnimplemented(#name, &logged); \
        return (ret)0; \
    }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
QGL_1_1_PROCS;
QGL_1_1_FIXED_FUNCTION_PROCS;
QGL_DESKTOP_1_1_PROCS;
QGL_DESKTOP_1_1_FIXED_FUNCTION_PROCS;
QGL_1_3_PROCS;
#pragma GCC diagnostic pop
#undef GLE

static void QGL_InstallStubs(void)
{
#define GLE(ret, name, ...) qgl##name = stub_##name;
    QGL_1_1_PROCS;
    QGL_1_1_FIXED_FUNCTION_PROCS;
    QGL_DESKTOP_1_1_PROCS;
    QGL_DESKTOP_1_1_FIXED_FUNCTION_PROCS;
    QGL_1_3_PROCS;
#undef GLE
}

/* ---- Small entry points ps3gl has no function for ------------------------ */

static void APIENTRY ps3_Hint(GLenum target, GLenum mode) { (void)target; (void)mode; }
static void APIENTRY ps3_NormalPointer(GLenum type, GLsizei stride, const GLvoid *ptr)
    { (void)type; (void)stride; (void)ptr; }


/* ---- Wiring -------------------------------------------------------------- */

void QGL_Init(void)
{
    ps3_log("QGL_Init: wiring qgl* to ps3gl");

    QGL_InstallStubs();

    /* Texture operations */
    qglBindTexture      = (BindTextureproc *)ps3gl_BindTexture;
    qglGenTextures      = (GenTexturesproc *)ps3gl_GenTextures;
    qglDeleteTextures   = (DeleteTexturesproc *)ps3gl_DeleteTextures;
    qglTexImage2D       = (TexImage2Dproc *)ps3gl_TexImage2D;
    qglTexSubImage2D    = (TexSubImage2Dproc *)ps3gl_TexSubImage2D;
    qglTexParameterf    = (TexParameterfproc *)ps3gl_TexParameterf;
    qglTexParameteri    = (TexParameteriproc *)ps3gl_TexParameteri;
    qglTexEnvf          = (TexEnvfproc *)ps3gl_TexEnvf;
    qglPixelStorei      = (PixelStoreiproc *)ps3gl_PixelStorei;
    qglCopyTexSubImage2D = (CopyTexSubImage2Dproc *)ps3gl_CopyTexSubImage2D;

    /* State */
    qglEnable           = (Enableproc *)ps3gl_Enable;
    qglDisable          = (Disableproc *)ps3gl_Disable;
    qglBlendFunc        = (BlendFuncproc *)ps3gl_BlendFunc;
    qglAlphaFunc        = (AlphaFuncproc *)ps3gl_AlphaFunc;
    qglDepthFunc        = (DepthFuncproc *)ps3gl_DepthFunc;
    qglDepthMask        = (DepthMaskproc *)ps3gl_DepthMask;
    qglDepthRange       = (DepthRangeproc *)ps3gl_DepthRange;
    qglColorMask        = (ColorMaskproc *)ps3gl_ColorMask;
    qglCullFace         = (CullFaceproc *)ps3gl_CullFace;
    qglScissor          = (Scissorproc *)ps3gl_Scissor;
    qglViewport         = (Viewportproc *)ps3gl_Viewport;
    qglShadeModel       = (ShadeModelproc *)ps3gl_ShadeModel;
    qglPolygonOffset    = (PolygonOffsetproc *)ps3gl_PolygonOffset;
    qglPolygonMode      = (PolygonModeproc *)ps3gl_PolygonMode;
    qglLineWidth        = (LineWidthproc *)ps3gl_LineWidth;
    qglClipPlane        = (ClipPlaneproc *)ps3gl_ClipPlane;
    qglDrawBuffer       = (DrawBufferproc *)ps3gl_DrawBuffer;
    qglHint             = (Hintproc *)ps3_Hint;

    /* Clear */
    qglClear            = (Clearproc *)ps3gl_Clear;
    qglClearColor       = (ClearColorproc *)ps3gl_ClearColor;
    qglClearDepth       = (ClearDepthproc *)ps3gl_ClearDepth;
    qglClearStencil     = (ClearStencilproc *)ps3gl_ClearStencil;

    /* Stencil */
    qglStencilFunc      = (StencilFuncproc *)ps3gl_StencilFunc;
    qglStencilMask      = (StencilMaskproc *)ps3gl_StencilMask;
    qglStencilOp        = (StencilOpproc *)ps3gl_StencilOp;

    /* Matrices */
    qglMatrixMode       = (MatrixModeproc *)ps3gl_MatrixMode;
    qglLoadIdentity     = (LoadIdentityproc *)ps3gl_LoadIdentity;
    qglLoadMatrixf      = (LoadMatrixfproc *)ps3gl_LoadMatrixf;
    qglPushMatrix       = (PushMatrixproc *)ps3gl_PushMatrix;
    qglPopMatrix        = (PopMatrixproc *)ps3gl_PopMatrix;
    qglOrtho            = (Orthoproc *)ps3gl_Ortho;
    qglFrustum          = (Frustumproc *)ps3gl_Frustum;
    qglTranslatef       = (Translatefproc *)ps3gl_Translatef;

    /* Immediate mode */
    qglBegin            = (Beginproc *)ps3gl_Begin;
    qglEnd              = (Endproc *)ps3gl_End;
    qglVertex2f         = (Vertex2fproc *)ps3gl_Vertex2f;
    qglVertex3f         = (Vertex3fproc *)ps3gl_Vertex3f;
    qglVertex3fv        = (Vertex3fvproc *)ps3gl_Vertex3fv;
    qglTexCoord2f       = (TexCoord2fproc *)ps3gl_TexCoord2f;
    qglTexCoord2fv      = (TexCoord2fvproc *)ps3gl_TexCoord2fv;
    qglColor3f          = (Color3fproc *)ps3gl_Color3f;
    qglColor3fv         = (Color3fvproc *)ps3gl_Color3fv;
    qglColor4f          = (Color4fproc *)ps3gl_Color4f;
    qglColor4ubv        = (Color4ubvproc *)ps3gl_Color4ubv;
    qglArrayElement     = (ArrayElementproc *)ps3gl_ArrayElement;

    /* Vertex arrays */
    qglVertexPointer    = (VertexPointerproc *)ps3gl_VertexPointer;
    qglTexCoordPointer  = (TexCoordPointerproc *)ps3gl_TexCoordPointer;
    qglColorPointer     = (ColorPointerproc *)ps3gl_ColorPointer;
    qglNormalPointer    = (NormalPointerproc *)ps3_NormalPointer;
    qglEnableClientState  = (EnableClientStateproc *)ps3gl_EnableClientState;
    qglDisableClientState = (DisableClientStateproc *)ps3gl_DisableClientState;
    qglDrawArrays       = (DrawArraysproc *)ps3gl_DrawArrays;
    qglDrawElements     = (DrawElementsproc *)ps3gl_DrawElements;

    /* Queries / readback */
    qglGetIntegerv      = (GetIntegervproc *)ps3gl_GetIntegerv;
    qglGetBooleanv      = (GetBooleanvproc *)ps3gl_GetBooleanv;
    qglGetString        = (GetStringproc *)ps3gl_GetString;
    qglGetError         = (GetErrorproc *)ps3gl_GetError;
    qglReadPixels       = (ReadPixelsproc *)ps3gl_ReadPixels;

    /* Fog: GL_FOG + glFog* in the shaders (ps3gl_shaders.c) */
    qglFogf             = (Fogfproc *)ps3gl_Fogf;
    qglFogfv            = (Fogfvproc *)ps3gl_Fogfv;
    qglFogi             = (Fogiproc *)ps3gl_Fogi;

    /* Multitexture: two units */
    qglActiveTextureARB       = (void (APIENTRYP)(GLenum))ps3gl_ActiveTextureARB;
    qglClientActiveTextureARB = (void (APIENTRYP)(GLenum))ps3gl_ClientActiveTextureARB;
    qglMultiTexCoord2fARB     = (void (APIENTRYP)(GLenum, GLfloat, GLfloat))ps3gl_MultiTexCoord2fARB;
    qglActiveTexture          = (ActiveTextureproc *)ps3gl_ActiveTextureARB;

    /* Compiled vertex arrays */
    qglLockArraysEXT    = (void (APIENTRYP)(GLint, GLsizei))ps3gl_LockArraysEXT;
    qglUnlockArraysEXT  = (void (APIENTRYP)(void))ps3gl_UnlockArraysEXT;

    /* Misc */
    qglFinish           = (Finishproc *)ps3gl_Finish;
    qglFlush            = (Flushproc *)ps3gl_Flush;

    /* ATI truform (defined in tr_init.c): not available */
    qglPNTrianglesiATI  = NULL;
    qglPNTrianglesfATI  = NULL;

    ps3_log("QGL_Init: done");
}
