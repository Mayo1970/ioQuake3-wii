/* tr_gx.c — native GX backend: state, matrices, draw (active under -DWII_NATIVE_GX only).
 * Never mix GL/ogx_ calls here, or GX_ calls into the OpenGX path. */
#if defined(WII_NATIVE_GX)

#include "tr_local.h"     /* Q3 types, tess global, glState, glConfig, etc. */
/* gccore.h and libogc headers arrive via wii_platform.h (force-included) */
#include "tr_gx.h"
#include <malloc.h>       /* memalign — vertex staging ring */


gx_state_t gxState;

/* Polygon offset for decals; applied as constant window-depth bias in gxbe_load_projection. */
static int   s_polyOffsetFill;
static float s_polyOffsetUnits;

/* Vertex staging ring: decouples CPU tess rewrites from GPU reads via GX_SetDrawSync. */

#define GXBE_RING_HALF  (128 * 1024)  /* worst-case draw ~52 KB (1000 verts,
                                       * stride-16 pos + 2x stride-16 tex) */
static u8      *s_vtxRing;            /* memalign(32, 2 * GXBE_RING_HALF) */
static u32      s_ringPos;            /* write offset within current half */
static int      s_ringHalf;           /* 0 or 1 */
static u16      s_syncToken;          /* last GX_SetDrawSync token issued */
static u16      s_halfFence[2];       /* token sealing each half's draws */
static qboolean s_halfFenceSet[2];

/* Spin until GP token register passes `fence` (mod-64K). Falls back to GX_DrawDone on timeout. */
static void gxbe_ring_wait(u16 fence)
{
    int spins = 0;

    GX_Flush();   /* push the pending token out of the CPU write-gather pipe */
    while ((u16)(GX_GetDrawSync() - fence) & 0x8000u) {
        if (++spins > 10000000) {
            static qboolean s_warned;
            if (!s_warned) { s_warned = qtrue;
                wii_diag("[gxbe] ring fence timeout — GX_DrawDone fallback\n"); }
            GX_DrawDone();
            break;
        }
    }
}

/* Allocate staging space (32-byte aligned); returns NULL if ring missing or request too large. */
static void *gxbe_ring_alloc(u32 size)
{
    void *p;

    if (!s_vtxRing)
        return NULL;
    size = (size + 31) & ~31u;
    if (size > GXBE_RING_HALF)
        return NULL;

    if (s_ringPos + size > GXBE_RING_HALF) {
        /* Seal this half behind a fence token and move to the other one. */
        GX_SetDrawSync(++s_syncToken);
        s_halfFence[s_ringHalf]    = s_syncToken;
        s_halfFenceSet[s_ringHalf] = qtrue;
        s_ringHalf ^= 1;
        s_ringPos = 0;
        if (s_halfFenceSet[s_ringHalf]) {
            gxbe_ring_wait(s_halfFence[s_ringHalf]);
            s_halfFenceSet[s_ringHalf] = qfalse;
        }
    }

    p = s_vtxRing + (u32)s_ringHalf * GXBE_RING_HALF + s_ringPos;
    s_ringPos += size;
    return p;
}

/* Reset ring to empty. Only valid after GX_DrawDone (all staged draws consumed). */
static void gxbe_ring_reset(void)
{
    s_ringPos = 0;
    s_ringHalf = 0;
    s_halfFenceSet[0] = qfalse;
    s_halfFenceSet[1] = qfalse;
    /* Rebase token to 0 so mod-64K compare never wraps, no matter how long the session runs. */
    GX_SetDrawSync(0);
    s_syncToken = 0;
}

/* Load projection with optional polygon-offset z bias.
 * Bias is a constant NDC offset (eps * w row) so it's distance-independent,
 * unlike a clip-space translation which decays as 1/distance. */
static void gxbe_load_projection(void)
{
    if (s_polyOffsetFill && s_polyOffsetUnits != 0.0f) {
        Mtx44 m;
        float eps = -s_polyOffsetUnits * 2.0e-6f;
        Com_Memcpy(m, gxState.projMtx, sizeof(m));
        if (gxState.projType == GX_PERSPECTIVE)
            m[2][2] += eps;
        else
            m[2][3] -= eps;
        GX_LoadProjectionMtx(m, gxState.projType);
    } else {
        GX_LoadProjectionMtx(gxState.projMtx, gxState.projType);
    }
}

/* DCFlushRange rounded to 32-byte cache line. Unaligned bases are safe (libogc extends length). */
#define GXBE_DCFLUSH(p, n)  DCFlushRange((void *)(p), (u32)(((n) + 31) & ~31u))

/* GLS_SRCBLEND_* → GX blend factor. */
static u8 gls_src_to_gx(unsigned long bits)
{
    switch (bits & GLS_SRCBLEND_BITS) {
    case GLS_SRCBLEND_ZERO:                 return GX_BL_ZERO;
    case GLS_SRCBLEND_ONE:                  return GX_BL_ONE;
    case GLS_SRCBLEND_DST_COLOR:            return GX_BL_DSTCLR;
    case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:  return GX_BL_INVDSTCLR;
    case GLS_SRCBLEND_SRC_ALPHA:            return GX_BL_SRCALPHA;
    case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:  return GX_BL_INVSRCALPHA;
    case GLS_SRCBLEND_DST_ALPHA:
        /* No EFB alpha in RGB8_Z24 — log once and fall back to ONE */
        { static qboolean s_warned; if (!s_warned) { s_warned = qtrue;
          wii_diag("[gxbe] GXBE: GLS_SRCBLEND_DST_ALPHA unsupported, using ONE\n"); } }
        return GX_BL_ONE;
    case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
        { static qboolean s_warned; if (!s_warned) { s_warned = qtrue;
          wii_diag("[gxbe] GXBE: GLS_SRCBLEND_ONE_MINUS_DST_ALPHA unsupported, using ZERO\n"); } }
        return GX_BL_ZERO;
    case GLS_SRCBLEND_ALPHA_SATURATE:
        { static qboolean s_warned; if (!s_warned) { s_warned = qtrue;
          wii_diag("[gxbe] GXBE: GLS_SRCBLEND_ALPHA_SATURATE, using SRCALPHA\n"); } }
        return GX_BL_SRCALPHA;
    default:
        return GX_BL_ONE;
    }
}

/* GLS_DSTBLEND_* → GX blend factor. */
static u8 gls_dst_to_gx(unsigned long bits)
{
    switch (bits & GLS_DSTBLEND_BITS) {
    case GLS_DSTBLEND_ZERO:                 return GX_BL_ZERO;
    case GLS_DSTBLEND_ONE:                  return GX_BL_ONE;
    case GLS_DSTBLEND_SRC_COLOR:            return GX_BL_SRCCLR;
    case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:  return GX_BL_INVSRCCLR;
    case GLS_DSTBLEND_SRC_ALPHA:            return GX_BL_SRCALPHA;
    case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:  return GX_BL_INVSRCALPHA;
    case GLS_DSTBLEND_DST_ALPHA:
        { static qboolean s_warned; if (!s_warned) { s_warned = qtrue;
          wii_diag("[gxbe] GXBE: GLS_DSTBLEND_DST_ALPHA unsupported, using ONE\n"); } }
        return GX_BL_ONE;
    case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
        { static qboolean s_warned; if (!s_warned) { s_warned = qtrue;
          wii_diag("[gxbe] GXBE: GLS_DSTBLEND_ONE_MINUS_DST_ALPHA unsupported, using ZERO\n"); } }
        return GX_BL_ZERO;
    default:
        return GX_BL_ZERO;
    }
}

/* Configure TEV stage 0 (CPREV = 0 on first stage). */
static void gxbe_set_stage0_env(int env)
{
    switch (env) {
    case GL_MODULATE:
    default:
        /* texColor * vertexColor (RASC = rasterized vertex color) */
        GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
        break;
    case GL_REPLACE:
        GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
        break;
    case GL_DECAL:
        GX_SetTevOp(GX_TEVSTAGE0, GX_DECAL);
        break;
    case GL_ADD:
        /* texColor + vertexColor. Formula: a*(1-c)+b*c+d
         * With a=TEXC, b=0, c=0, d=RASC => TEXC + RASC */
        GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
        GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        return;   /* explicit in/op set, skip GX_SetTevOp */
    }
}

/* Configure TEV stage 1 (CPREV = stage0 output). env is Q3's multitextureEnv. */
static void gxbe_set_stage1_env(int env)
{
    if (env == GL_REPLACE) {
        /* r_lightmap debug: show only the lightmap */
        GX_SetTevOp(GX_TEVSTAGE1, GX_REPLACE);
        return;
    }
    if (env == GL_ADD) {
        /* CPREV + lightmap: a=CPREV, b=0, c=0, d=TEXC => CPREV + TEXC */
        GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_CPREV, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
        GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    } else {
        /* GL_MODULATE (default): CPREV * lightmap.
         * Formula: 0*(1-TEXC) + CPREV*TEXC + 0 = CPREV*TEXC */
        GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_TEXC, GX_CC_ZERO);
        GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    }
    /* Alpha: pass through stage0 alpha unchanged */
    GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
    GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
}

/* Commit pending TEV stage setup to GX. Called lazily before each draw. */
static void gxbe_commit_tev(int numTex)
{
    /* One vertex color channel: vertex color as material, lighting disabled. */
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
                   0, GX_DF_NONE, GX_AF_NONE);

    /* Tex coord generators: pass through UV from indexed arrays. */
    GX_SetNumTexGens(numTex);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    if (numTex == 2)
        GX_SetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, GX_IDENTITY);

    GX_SetNumTevStages(numTex);

    /* Stage 0 */
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    gxbe_set_stage0_env(gxState.texenv[0]);

    /* Stage 1 (multitex only) */
    if (numTex == 2) {
        GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLORNULL);
        gxbe_set_stage1_env(gxState.texenv[1]);
    }
}


void GXBE_SetDefaultState(void)
{
    Mtx identity;

    /* Zero gxState, then fill force-apply sentinels. */
    Com_Memset(&gxState, 0, sizeof(gxState));
    gxState.boundtex[0]    = -1;
    gxState.boundtex[1]    = -1;
    gxState.faceCulling    = -1;   /* force-apply on first GL_Cull */
    gxState.numActiveTMUs  = 1;
    gxState.depthNear      = 0.0f;
    gxState.depthFar       = 1.0f;
    gxState.texenv[0]      = GL_MODULATE;
    gxState.texenv[1]      = GL_MODULATE;
    gxState.vtxDescNumTex  = -1;   /* force-set on first draw */
    gxState.tevDirty       = qtrue;
    s_polyOffsetFill       = 0;
    s_polyOffsetUnits      = 0.0f;

    /* Staging ring allocated once; survives vid_restart. Failure is non-fatal (DrawDone fallback). */
    if (!s_vtxRing) {
        s_vtxRing = (u8 *)memalign(32, 2 * GXBE_RING_HALF);
        if (!s_vtxRing)
            wii_diag("[gxbe] staging ring alloc failed — conservative sync path\n");
    }
    gxbe_ring_reset();

    /* Default to tess svars; qgl pointer wrappers retarget per stage iterator. */
    gxState.posPtr       = tess.xyz;
    gxState.posStride    = sizeof(tess.xyz[0]);
    gxState.clrPtr       = tess.svars.colors;
    gxState.clrStride    = sizeof(color4ub_t);
    gxState.texPtr[0]    = tess.svars.texcoords[0];
    gxState.texPtr[1]    = tess.svars.texcoords[1];
    gxState.texStride[0] = sizeof(vec2_t);
    gxState.texStride[1] = sizeof(vec2_t);

    /* Full-screen viewport so DepthRange/Clear have sane geometry before first
     * SetViewportAndScissor. Routed through GXBE_SetViewport (not raw GX_SetViewport)
     * so the TV-border inset applies from the very first frame. */
    gxState.depthNear = 0.0f;
    gxState.depthFar  = 1.0f;
    GXBE_SetViewport(0, 0, glConfig.vidWidth, glConfig.vidHeight);

    /* Seed stored matrices so GXBE_Clear can restore them. */
    guOrtho(gxState.projMtx, 0.0f, (f32)glConfig.vidHeight,
            0.0f, (f32)glConfig.vidWidth, 0.0f, 1.0f);
    gxState.projType = GX_ORTHOGRAPHIC;
    gxbe_load_projection();
    guMtxIdentity(gxState.mvMtx);

    /* GX blend: off (one source) */
    GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);

    /* Depth: disabled, write on (mirrors GL_SetDefaultState's initial state) */
    GX_SetZMode(GX_DISABLE, GX_LEQUAL, GX_TRUE);
    GX_SetZCompLoc(GX_TRUE);
    gxState.alphaTestActive = qfalse;

    /* Alpha compare: always pass */
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);

    /* Cull: disabled (CT_TWO_SIDED) — Q3 sets cull per surface */
    GX_SetCullMode(GX_CULL_NONE);

    /* Identity vertex matrix */
    guMtxIdentity(identity);
    GX_LoadPosMtxImm(identity, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);

    /* Channel/TEV defaults */
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
                   0, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);

    /* Scissor: full-screen (will be set properly in RB_SetGL2D). Routed through
     * GXBE_SetScissor so the TV-border inset applies from the very first frame. */
    GXBE_SetScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);

    /* Keep glState.glStateBits in sync for subsequent GL_State diffs. */
    glState.glStateBits = GLS_DEPTHTEST_DISABLE | GLS_DEPTHMASK_TRUE;
}


void GXBE_FrameEnd(void)
{
    /* Wii_GX_EndFrame has just returned from GX_DrawDone(): the GP is idle,
     * so every staged draw has been consumed and the ring can restart. */
    gxState.arraysInFlight = qfalse;
    gxbe_ring_reset();
}

/* ----------------------------------------------------------------- */
/* GXBE_Finish — qglFinish equivalent (full GP sync)                  */
/* ----------------------------------------------------------------- */

void GXBE_Finish(void)
{
    GX_DrawDone();
    gxState.arraysInFlight = qfalse;
    gxbe_ring_reset();
}

/* ----------------------------------------------------------------- */
/* GXBE_PeekDepth — read one EFB Z value (flare visibility tests)     */
/*                                                                    */
/* x/y are EFB pixel coordinates, TOP-DOWN origin. The Z buffer holds */
/* 24-bit linear screen z (GX_ZC_LINEAR pixel format in wii_glimp.c). */
/* Because GXBE_LoadProjectionGL's z-row conversion zGX = (zGL - w)/2 */
/* makes GX window depth identical to GL window depth, the normalized */
/* result feeds GL-style depth reconstruction unchanged.              */
/* ----------------------------------------------------------------- */

float GXBE_PeekDepth(int x, int y)
{
    u32 z = 0;

    if (x < 0) x = 0;
    if (x > glConfig.vidWidth - 1)  x = glConfig.vidWidth - 1;
    if (y < 0) y = 0;
    if (y > glConfig.vidHeight - 1) y = glConfig.vidHeight - 1;

    /* Every prior draw's Z must have landed in the EFB before the peek.
     * Unconditional sync: cheap when the GP is already idle, and this is
     * a low-frequency path (r_flares defaults to 0). */
    GXBE_Finish();

    GX_PeekZ((u16)x, (u16)y, &z);
    return (float)z / 16777215.0f;   /* 24-bit -> [0,1] */
}

/* ----------------------------------------------------------------- */
/* GXBE_GL_State                                                      */
/* ----------------------------------------------------------------- */

void GXBE_GL_State(unsigned long stateBits)
{
    unsigned long diff = stateBits ^ glState.glStateBits;

    if (!diff)
        return;

    /* Blend */
    if (diff & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) {
        if (stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) {
            u8 src = gls_src_to_gx(stateBits);
            u8 dst = gls_dst_to_gx(stateBits);
            GX_SetBlendMode(GX_BM_BLEND, src, dst, GX_LO_CLEAR);
        } else {
            GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
        }
    }

    /* Depth mask */
    if (diff & GLS_DEPTHMASK_TRUE) {
        /* Z-mode is re-issued in the depth-test block below.
         * If ONLY the mask changed we still need to re-issue. */
        GX_SetZMode(!(stateBits & GLS_DEPTHTEST_DISABLE),
                    (stateBits & GLS_DEPTHFUNC_EQUAL) ? GX_EQUAL : GX_LEQUAL,
                    !!(stateBits & GLS_DEPTHMASK_TRUE));
        diff &= ~GLS_DEPTHTEST_DISABLE;  /* already handled */
        diff &= ~GLS_DEPTHFUNC_EQUAL;
    }

    /* Depth test / depth func */
    if (diff & (GLS_DEPTHTEST_DISABLE | GLS_DEPTHFUNC_EQUAL)) {
        GX_SetZMode(!(stateBits & GLS_DEPTHTEST_DISABLE),
                    (stateBits & GLS_DEPTHFUNC_EQUAL) ? GX_EQUAL : GX_LEQUAL,
                    !!(stateBits & GLS_DEPTHMASK_TRUE));
    }

    /* Alpha test */
    if (diff & GLS_ATEST_BITS) {
        switch (stateBits & GLS_ATEST_BITS) {
        case 0:
            GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
            if (gxState.alphaTestActive) {
                GX_SetZCompLoc(GX_TRUE);   /* Z before texture: faster */
                gxState.alphaTestActive = qfalse;
            }
            break;
        case GLS_ATEST_GT_0:
            GX_SetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0);
            if (!gxState.alphaTestActive) {
                GX_SetZCompLoc(GX_FALSE);  /* Z after texture: needed for alpha test */
                gxState.alphaTestActive = qtrue;
            }
            break;
        case GLS_ATEST_LT_80:
            GX_SetAlphaCompare(GX_LESS, 128, GX_AOP_AND, GX_ALWAYS, 0);
            if (!gxState.alphaTestActive) {
                GX_SetZCompLoc(GX_FALSE);
                gxState.alphaTestActive = qtrue;
            }
            break;
        case GLS_ATEST_GE_80:
            GX_SetAlphaCompare(GX_GEQUAL, 128, GX_AOP_AND, GX_ALWAYS, 0);
            if (!gxState.alphaTestActive) {
                GX_SetZCompLoc(GX_FALSE);
                gxState.alphaTestActive = qtrue;
            }
            break;
        default:
            break;
        }
    }

    /* Wire-frame: not available on GX — log once */
    if (diff & GLS_POLYMODE_LINE) {
        static qboolean s_warned;
        if (!s_warned && (stateBits & GLS_POLYMODE_LINE)) {
            s_warned = qtrue;
            wii_diag("[gxbe] GXBE: GLS_POLYMODE_LINE unsupported on GX\n");
        }
    }

    glState.glStateBits = stateBits;
}

void GXBE_GL_Cull(int cullType)
{
    if (glState.faceCulling == cullType)
        return;

    glState.faceCulling = cullType;

    if (cullType == CT_TWO_SIDED) {
        GX_SetCullMode(GX_CULL_NONE);
    } else {
        qboolean cullFront = (cullType == CT_FRONT_SIDED);
        if (backEnd.viewParms.isMirror)
            cullFront = !cullFront;
        /* GX winding is inverted relative to OpenGL:
         * Q3's "cull front" (CCW front faces) → GX_CULL_BACK */
        GX_SetCullMode(cullFront ? GX_CULL_BACK : GX_CULL_FRONT);
    }
}

void GXBE_GL_TexEnv(int unit, int env)
{
    if (gxState.texenv[unit] == env)
        return;
    gxState.texenv[unit] = env;
    gxState.tevDirty = qtrue;
}

void GXBE_BindTexnum(int tmu, int texnum)
{
    /* Track multi-tex activation */
    if (tmu == 1) {
        gxState.numActiveTMUs = 2;
        gxState.tevDirty = qtrue;
    }

    if (gxState.boundtex[tmu] == texnum)
        return;

    gxState.boundtex[tmu] = texnum;

    if (texnum < 0 || texnum >= GX_MAX_TEXOBJS || !s_gx_texobj_valid[texnum])
        return; /* texobj not yet initialized (during R_CreateImage setup) */

    GX_LoadTexObj(&s_gx_texobjs[texnum], (tmu == 0) ? GX_TEXMAP0 : GX_TEXMAP1);

}

void GXBE_LoadOrtho2D(int vidWidth, int vidHeight)
{
    /* guOrtho: top=0, bottom=vidHeight, left=0, right=vidWidth (Q3 2D: origin top-left, Y down). */
    guOrtho(gxState.projMtx, 0, (f32)vidHeight, 0, (f32)vidWidth, 0.0f, 1.0f);
    gxState.projType = GX_ORTHOGRAPHIC;
    gxbe_load_projection();
}

void GXBE_LoadIdentityModelview(void)
{
    guMtxIdentity(gxState.mvMtx);
    GX_LoadPosMtxImm(gxState.mvMtx, GX_PNMTX0);
}

/* Load Q3's GL projection into GX. GL z is [-w,+w]; GX z is [-w,0].
 * Z row is remapped: zGX = (zGL - w) / 2. Works for oblique projections too. */
void GXBE_LoadProjectionGL(const float *gl16)
{
    int r, c;

    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            gxState.projMtx[r][c] = gl16[c * 4 + r];

    /* z row: zGX = (zGL - w) / 2, per column of the GL matrix */
    for (c = 0; c < 4; c++)
        gxState.projMtx[2][c] = 0.5f * (gl16[c * 4 + 2] - gl16[c * 4 + 3]);

    gxState.projType = GX_PERSPECTIVE;
    gxbe_load_projection();
}

/* Oblique near-plane projection for portals/mirrors (Lengyel). GX has no clip planes,
 * so the z row is rebuilt so the near plane coincides with the portal plane. */
void GXBE_LoadProjectionObliqueGL(const float *gl16, const float *eyePlane)
{
    float m[16];
    float qx, qy, qz, qw, d, a;

    /* Degenerate: camera on portal plane — fall back to plain projection. */
    if (eyePlane[3] > -1e-4f) {
        GXBE_LoadProjectionGL(gl16);
        return;
    }

    Com_Memcpy(m, gl16, sizeof(m));

    /* Far-plane corner nearest the clip plane, back-projected to eye space (Lengyel, GL column-major). */
    qx = ((eyePlane[0] < 0.0f ? -1.0f : 1.0f) + m[8]) / m[0];
    qy = ((eyePlane[1] < 0.0f ? -1.0f : 1.0f) + m[9]) / m[5];
    qz = -1.0f;
    qw = (1.0f + m[10]) / m[14];

    d = eyePlane[0] * qx + eyePlane[1] * qy + eyePlane[2] * qz + eyePlane[3] * qw;
    if (d > -1e-6f && d < 1e-6f) {
        GXBE_LoadProjectionGL(gl16);
        return;
    }
    a = 2.0f / d;

    /* Replace the GL z row: near plane == portal plane */
    m[2]  = eyePlane[0] * a;
    m[6]  = eyePlane[1] * a;
    m[10] = eyePlane[2] * a + 1.0f;
    m[14] = eyePlane[3] * a;

    GXBE_LoadProjectionGL(m);   /* general z-row conversion handles oblique */
}

/* Transpose GL column-major modelview into libogc row-major 3x4 Mtx and load as position matrix. */
void GXBE_LoadModelviewGL(const float *gl16)
{
    int r, c;

    for (r = 0; r < 3; r++)
        for (c = 0; c < 4; c++)
            gxState.mvMtx[r][c] = gl16[c * 4 + r];

    GX_LoadPosMtxImm(gxState.mvMtx, GX_PNMTX0);
}

/* qglLoadMatrixf + qglTranslatef(origin) — sky/sun geometry is centered on the view origin. */
void GXBE_LoadModelviewTranslatedGL(const float *gl16, const vec3_t origin)
{
    float m[16];

    Com_Memcpy(m, gl16, sizeof(m));
    /* GL post-multiply: M' = M * T(origin) — only the translation column moves */
    m[12] = gl16[0] * origin[0] + gl16[4] * origin[1] + gl16[8]  * origin[2] + gl16[12];
    m[13] = gl16[1] * origin[0] + gl16[5] * origin[1] + gl16[9]  * origin[2] + gl16[13];
    m[14] = gl16[2] * origin[0] + gl16[6] * origin[1] + gl16[10] * origin[2] + gl16[14];

    GXBE_LoadModelviewGL(m);
}

/* Insets a full-screen-space rect toward the center by r_tvborder (TV overscan
 * safe area), so every viewport/scissor consumer lands inside the TV-safe
 * area without touching VI timing/rmode (see CLAUDE.md renderer section —
 * VIDEO_Configure/rmode edits are the documented flicker trap). */
static void gxbe_apply_tvborder(int *x, int *y, int *w, int *h)
{
    float b, sx, sy;
    int insetX, insetY;

    if (!r_tvborder || r_tvborder->value <= 0.0f)
        return;

    b = r_tvborder->value;
    if (b > 0.15f) b = 0.15f;

    insetX = (int)(glConfig.vidWidth  * b);
    insetY = (int)(glConfig.vidHeight * b);
    sx = (float)(glConfig.vidWidth  - 2 * insetX) / (float)glConfig.vidWidth;
    sy = (float)(glConfig.vidHeight - 2 * insetY) / (float)glConfig.vidHeight;

    *x = insetX + (int)(*x * sx);
    *y = insetY + (int)(*y * sy);
    *w = (int)(*w * sx);
    *h = (int)(*h * sy);
}

void GXBE_SetViewport(int x, int y, int w, int h)
{
    gxbe_apply_tvborder(&x, &y, &w, &h);

    gxState.vpX = x;  gxState.vpY = y;
    gxState.vpW = w;  gxState.vpH = h;
    /* GX viewport: nearZ/farZ map GX clip-space Z range to EFB Z [0,1]. */
    GX_SetViewport((f32)x, (f32)y, (f32)w, (f32)h,
                   gxState.depthNear, gxState.depthFar);
}

void GXBE_SetScissor(int x, int y, int w, int h)
{
    gxbe_apply_tvborder(&x, &y, &w, &h);

    /* GX scissor uses absolute top-left pixel coordinates. */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    GX_SetScissor((u32)x, (u32)y, (u32)w, (u32)h);
}

void GXBE_DepthRange(float n, float f)
{
    gxState.depthNear = n;
    gxState.depthFar  = f;
    /* Re-issue viewport with updated depth range */
    GX_SetViewport((f32)gxState.vpX, (f32)gxState.vpY,
                   (f32)gxState.vpW, (f32)gxState.vpH,
                   n, f);
}

/* Q3 toggles GL_POLYGON_OFFSET_FILL without reloading the projection, so reload it here. */
void GXBE_PolygonOffset(float factor, float units)
{
    (void)factor;   /* OpenGX ignores it too */
    if (s_polyOffsetUnits == units)
        return;
    s_polyOffsetUnits = units;
    if (s_polyOffsetFill)
        gxbe_load_projection();
}

void GXBE_SetPolygonOffsetEnabled(int enabled)
{
    if (s_polyOffsetFill == enabled)
        return;
    s_polyOffsetFill = enabled;
    gxbe_load_projection();
}

/* ----------------------------------------------------------------- */
/* GXBE_Clear — glClear emulation                                     */
/*                                                                    */
/* GX has no clear call; mid-frame clears (per-view depth clear,      */
/* hyperspace color flash) are done by drawing a quad covering the    */
/* current viewport, clipped by the current scissor — the same region */
/* glClear would affect. Depth clears write the far plane by forcing  */
/* the viewport z range to [1,1] for the quad.                        */
/* All pipeline state touched here is restored before returning.      */
/* ----------------------------------------------------------------- */

void GXBE_Clear(qboolean clearColor, qboolean clearDepth, float r, float g, float b)
{
    Mtx44 proj;
    Mtx mv;
    u8 cr, cg, cb;

    if (!clearColor && !clearDepth)
        return;

    cr = (u8)(r * 255.0f);
    cg = (u8)(g * 255.0f);
    cb = (u8)(b * 255.0f);

    /* Self-contained pipeline: vertex-color passthrough, no texture */
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
                   0, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(0);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);

    GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetZCompLoc(GX_TRUE);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetColorUpdate(clearColor ? GX_TRUE : GX_FALSE);
    GX_SetZMode(clearDepth ? GX_TRUE : GX_FALSE, GX_ALWAYS,
                clearDepth ? GX_TRUE : GX_FALSE);

    /* Force every covered pixel to maximum (farthest) depth */
    GX_SetViewport((f32)gxState.vpX, (f32)gxState.vpY,
                   (f32)gxState.vpW, (f32)gxState.vpH, 1.0f, 1.0f);

    /* Unit ortho over the viewport, identity position matrix */
    guOrtho(proj, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
    guMtxIdentity(mv);
    GX_LoadPosMtxImm(mv, GX_PNMTX0);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS,  GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_POS,  GX_POS_XYZ,  GX_F32,   0);
    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_InvVtxCache();   /* descriptor flipped indexed->direct (OpenGX glClear does this too) */

    GX_Begin(GX_QUADS, GX_VTXFMT1, 4);
        GX_Position3f32(0.0f, 0.0f, -0.5f); GX_Color4u8(cr, cg, cb, 255);
        GX_Position3f32(1.0f, 0.0f, -0.5f); GX_Color4u8(cr, cg, cb, 255);
        GX_Position3f32(1.0f, 1.0f, -0.5f); GX_Color4u8(cr, cg, cb, 255);
        GX_Position3f32(0.0f, 1.0f, -0.5f); GX_Color4u8(cr, cg, cb, 255);
    GX_End();

    /* ---- restore everything we touched ---- */
    GX_SetColorUpdate(GX_TRUE);
    GX_SetViewport((f32)gxState.vpX, (f32)gxState.vpY,
                   (f32)gxState.vpW, (f32)gxState.vpH,
                   gxState.depthNear, gxState.depthFar);
    gxbe_load_projection();
    GX_LoadPosMtxImm(gxState.mvMtx, GX_PNMTX0);

    /* Re-apply blend/depth/alpha-test exactly as glState.glStateBits says */
    {
        unsigned long bits = glState.glStateBits;
        glState.glStateBits = ~bits;        /* force every group to re-issue */
        gxState.alphaTestActive = qfalse;   /* clear left ZCompLoc at GX_TRUE */
        GXBE_GL_State(bits);
    }
    glState.faceCulling   = -1;     /* cull mode was overridden */
    gxState.tevDirty      = qtrue;  /* TEV + texgens were overridden */
    gxState.vtxDescNumTex = -1;     /* vertex descriptor was overridden */
}

void GXBE_ImmediateBegin(int primGL, int numVerts)
{
    u8 prim;

    switch (primGL) {
    case GL_TRIANGLE_STRIP: prim = GX_TRIANGLESTRIP; break;
    case GL_TRIANGLE_FAN:   prim = GX_TRIANGLEFAN;   break;
    case GL_LINES:          prim = GX_LINES;         break;
    case GL_QUADS:          prim = GX_QUADS;         break;
    default:
        { static qboolean s_warned; if (!s_warned) { s_warned = qtrue;
          wii_diag("[gxbe] GXBE_ImmediateBegin: unmapped GL prim 0x%x\n", primGL); } }
        prim = GX_TRIANGLES;
        break;
    }

    /* Single-texture TEV with the current texenv */
    gxbe_commit_tev(1);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS,  GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_POS,  GX_POS_XYZ,  GX_F32,   0);
    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_TEX0, GX_TEX_ST,   GX_F32,   0);

    /* force indexed descriptor + TEV recommit on the next DrawTess */
    gxState.vtxDescNumTex = -1;
    gxState.tevDirty      = qtrue;

    GX_InvVtxCache();   /* descriptor flipped indexed->direct */

    GX_Begin(prim, GX_VTXFMT1, (u16)numVerts);
}

void GXBE_ImmediateTexVertex(const float *st, const float *xyz,
                             byte r, byte g, byte b, byte a)
{
    GX_Position3f32(xyz[0], xyz[1], xyz[2]);
    GX_Color4u8(r, g, b, a);
    GX_TexCoord2f32(st[0], st[1]);
}

void GXBE_ImmediateEnd(void)
{
    GX_End();
}

void GXBE_SetVertexPtr(const void *p, int stride)
{
    gxState.posPtr    = p;
    gxState.posStride = stride;
}

void GXBE_SetColorPtr(const void *p, int stride)
{
    gxState.clrPtr    = p;
    gxState.clrStride = stride;
}

void GXBE_SetTexCoordPtr(const void *p, int stride)
{
    gxState.texPtr[gxState.currenttmu]    = p;
    gxState.texStride[gxState.currenttmu] = stride;
}

/* Only meaningful on TMU 1 — Q3's multitexture on/off switch. TMU 0 is always on. */
void GXBE_SetTexture2DEnabled(int enabled)
{
    if (gxState.currenttmu == 1) {
        int want = enabled ? 2 : 1;
        if (gxState.numActiveTMUs != want) {
            gxState.numActiveTMUs = want;
            gxState.tevDirty = qtrue;
        }
    }
}

void GXBE_DrawTess(int numIndexes, const glIndex_t *indexes)
{
    int i;
    int numTex;
    const void *posPtr, *clrPtr, *texPtr0, *texPtr1;
    u32 posSize, clrSize, texSize0, texSize1;

    if (numIndexes <= 0 || tess.numVertexes <= 0)
        return;

    numTex = gxState.numActiveTMUs;

    /* Stage vertex data into the fenced ring so the CPU can rewrite tess freely.
     * Stride bytes per vertex copied = exactly what the GP would fetch in place. */
    posSize  = (u32)tess.numVertexes * (u32)gxState.posStride;
    clrSize  = (u32)tess.numVertexes * (u32)gxState.clrStride;
    texSize0 = (u32)tess.numVertexes * (u32)gxState.texStride[0];
    texSize1 = (numTex == 2) ? (u32)tess.numVertexes * (u32)gxState.texStride[1] : 0;

    {
        u32 a_pos = (posSize  + 31) & ~31u;
        u32 a_clr = (clrSize  + 31) & ~31u;
        u32 a_t0  = (texSize0 + 31) & ~31u;
        u32 a_t1  = (texSize1 + 31) & ~31u;
        u8 *blk = (u8 *)gxbe_ring_alloc(a_pos + a_clr + a_t0 + a_t1);

        if (blk) {
            Com_Memcpy(blk,                       gxState.posPtr,    posSize);
            Com_Memcpy(blk + a_pos,               gxState.clrPtr,    clrSize);
            Com_Memcpy(blk + a_pos + a_clr,       gxState.texPtr[0], texSize0);
            if (numTex == 2)
                Com_Memcpy(blk + a_pos + a_clr + a_t0, gxState.texPtr[1], texSize1);

            posPtr  = blk;
            clrPtr  = blk + a_pos;
            texPtr0 = blk + a_pos + a_clr;
            texPtr1 = blk + a_pos + a_clr + a_t0;

            DCFlushRange(blk, a_pos + a_clr + a_t0 + a_t1);
        } else {
            /* Ring alloc failed: drain GP then draw from client arrays in place. */
            if (gxState.arraysInFlight) {
                GX_DrawDone();
                gxState.arraysInFlight = qfalse;
            }
            posPtr  = gxState.posPtr;
            clrPtr  = gxState.clrPtr;
            texPtr0 = gxState.texPtr[0];
            texPtr1 = gxState.texPtr[1];

            /* DCFlushRange handles misaligned starts (e.g. tess.texCoords[0][1]). */
            GXBE_DCFLUSH(posPtr,  posSize);
            GXBE_DCFLUSH(clrPtr,  clrSize);
            GXBE_DCFLUSH(texPtr0, texSize0);
            if (numTex == 2)
                GXBE_DCFLUSH(texPtr1, texSize1);

            gxState.arraysInFlight = qtrue;  /* sync again before next draw */
        }
    }

    /* Commit pending TEV state and vertex descriptor if dirty or TMU count changed */
    if (gxState.tevDirty || gxState.vtxDescNumTex != numTex) {
        gxbe_commit_tev(numTex);
        gxState.tevDirty = qfalse;

        GX_ClearVtxDesc();
        GX_SetVtxDesc(GX_VA_POS,  GX_INDEX16);
        GX_SetVtxDesc(GX_VA_CLR0, GX_INDEX16);
        GX_SetVtxDesc(GX_VA_TEX0, GX_INDEX16);
        if (numTex == 2)
            GX_SetVtxDesc(GX_VA_TEX1, GX_INDEX16);

        GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS,  GX_POS_XYZ,  GX_F32,   0);
        GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
        GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST,   GX_F32,   0);
        if (numTex == 2)
            GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0);

        gxState.vtxDescNumTex = numTex;
    }

    /* Point GX at staged copies (or client arrays on fallback); data flushed above. */
    GX_SetArray(GX_VA_POS,  (void *)posPtr,  (u8)gxState.posStride);
    GX_SetArray(GX_VA_CLR0, (void *)clrPtr,  (u8)gxState.clrStride);
    GX_SetArray(GX_VA_TEX0, (void *)texPtr0, (u8)gxState.texStride[0]);
    if (numTex == 2)
        GX_SetArray(GX_VA_TEX1, (void *)texPtr1, (u8)gxState.texStride[1]);

    /* Invalidate GP vertex cache: ring addresses recycle, fallback rewrites same addresses.
     * Without this: stale vertices → garbled text / malformed models. */
    GX_InvVtxCache();

    /* GX_Begin count = numIndexes (one attribute group per index). */
    GX_Begin(GX_TRIANGLES, GX_VTXFMT0, (u16)numIndexes);
    for (i = 0; i < numIndexes; i++) {
        u16 idx = (u16)indexes[i];
        GX_Position1x16(idx);
        GX_Color1x16(idx);
        GX_TexCoord1x16(idx);
        if (numTex == 2)
            GX_TexCoord1x16(idx);
    }
    GX_End();
    /* Ring path: no in-flight flag needed — staged copy is immutable until fence-recycled. */
}

#else  /* !WII_NATIVE_GX */

/* Non-empty placeholder for the baseline build. */
typedef int tr_gx_translation_unit_placeholder;

#endif /* WII_NATIVE_GX */
