/* tr_animation_front.c -- PS3 port: tr_animation.c again, for the game thread.
 *
 * tr_animation.c keeps the MDS bone state in file statics (bones[],
 * lastBoneEntity...). With r_smp the render thread skins characters with it
 * (RB_SurfaceAnim) while cgame asks for tags on the game thread (R_LerpTag ->
 * R_GetBoneTag). A lock between them made each wait for the other and evict
 * the other's cached bones (LerpTag 0.6 -> 2.5 ms, skinning up to 6 ms in a
 * fight). Compiled a second time, the file gets a second set of statics: the
 * game thread uses this copy (PS3_R_GetBoneTagFront), the render thread the
 * original. The exported names are renamed so both copies link. */

#define PS3_BONES_FRONT

#define MC_UnCompress           PS3F_MC_UnCompress
#define RB_CalcMDSLod           PS3F_RB_CalcMDSLod
#define RB_MDRSurfaceAnim       PS3F_RB_MDRSurfaceAnim
#define RB_SurfaceAnim          PS3F_RB_SurfaceAnim
#define R_AddAnimSurfaces       PS3F_R_AddAnimSurfaces
#define R_CalcBone              PS3F_R_CalcBone
#define R_CalcBoneLerp          PS3F_R_CalcBoneLerp
#define R_CalcBones             PS3F_R_CalcBones
#define R_GetBoneTag            PS3_R_GetBoneTagFront
#define R_MDRAddAnimSurfaces    PS3F_R_MDRAddAnimSurfaces
#define R_MDRComputeFogNum      PS3F_R_MDRComputeFogNum
#define R_RecursiveBoneListAdd  PS3F_R_RecursiveBoneListAdd

#include "tr_animation.c"
