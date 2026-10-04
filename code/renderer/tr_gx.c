/* tr_gx.c — native GX backend: state, matrices, draw (active under -DWII_NATIVE_GX only).
 * Never mix GL/ogx_ calls here, or GX_ calls into the OpenGX path. */
#if defined(WII_NATIVE_GX)

#include "tr_local.h"     /* Q3 types, tess global, glState, glConfig, etc. */
/* gccore.h and libogc headers arrive via wii_platform.h (force-included) */
#include "tr_gx.h"
#include <malloc.h>       /* memalign — vertex staging ring */


gx_state_t gxState;

/* Polygon offset for decals: units = constant z bias (gxbe_load_projection),
 * factor = per-vertex slope pull (gxbe_offset_slope). GX has no polygon offset. */
static int   s_polyOffsetFill;
static float s_polyOffsetUnits;
static float s_polyOffsetFactor;

/* Portal/mirror clip plane (eye space), applied on the CPU in GXBE_DrawTess. GX has no clip
 * planes, and its 6-param projection drops an oblique z-row's x/y terms (black mirrors). */
static qboolean s_clipActive;
static float    s_clipEye[4];

#define GXBE_MAX_SPLIT  128   /* portal-clip triangles split per draw; any more are kept whole */
#define GXBE_MAX_VERTS  (SHADER_MAX_VERTEXES + 2 * GXBE_MAX_SPLIT)
#define GXBE_CLIP_EPS   0.1f  /* keeps vertices that lie on the portal plane */

/* Vertex staging ring: decouples CPU tess rewrites from GPU reads via GX_SetDrawSync. */

#define GXBE_RING_HALF  (128 * 1024)  /* worst-case draw ~65 KB (1000 + 256 clip verts,
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

/* Load projection plus the polygon-offset units term: a constant NDC z bias (eps * w row),
 * unlike a clip-space translation, which decays as 1/distance. */
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
    s_polyOffsetFactor     = 0.0f;
    s_clipActive           = qfalse;

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

    /* Full-screen viewport so DepthRange/Clear work before the first SetViewportAndScissor.
     * Goes through GXBE_SetViewport so the TV-border inset applies from the first frame. */
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
    gxbe_ring_reset();
}

/* qglFinish equivalent - full GP sync. */
void GXBE_Finish(void)
{
    GX_DrawDone();
    gxbe_ring_reset();
}

/* Read one EFB Z value for flare visibility tests, top-down EFB coords.
   Window depth matches GL's thanks to the z-row conversion in LoadProjectionGL. */
float GXBE_PeekDepth(int x, int y)
{
    u32 z = 0;

    if (x < 0) x = 0;
    if (x > glConfig.vidWidth - 1)  x = glConfig.vidWidth - 1;
    if (y < 0) y = 0;
    if (y > glConfig.vidHeight - 1) y = glConfig.vidHeight - 1;

    /* Unconditional sync - cheap when GP's already idle, r_flares is low-frequency anyway. */
    GXBE_Finish();

    GX_PeekZ((u16)x, (u16)y, &z);
    return (float)z / 16777215.0f;   /* 24-bit -> [0,1] */
}

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

/* Load Q3's GL projection into GX. GL z is [-w,+w]; GX z is [-w,0], so zGX = (zGL - w) / 2.
 * GX keeps only z-row [2][2]/[2][3]: never pass an oblique matrix here. */
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

/* Portal views pass the portal plane in eye space (keep the >= 0 side); NULL turns it off. */
void GXBE_SetClipPlane(const float *eyePlane)
{
    s_clipActive = (eyePlane != NULL);
    if (eyePlane)
        Com_Memcpy(s_clipEye, eyePlane, sizeof(s_clipEye));
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

/* Insets a rect toward the center by r_tvborder (TV overscan safe area). Done here, not via
 * VI timing/rmode: VIDEO_Configure/rmode edits are the documented flicker trap (CLAUDE.md). */
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
    s_polyOffsetFactor = factor;   /* used per draw, no reload needed */
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

/* GX has no clear call - fake it by drawing a full-viewport quad, scissored
   the same as glClear would be. Everything touched here gets restored after. */

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

/* GL's slope term: pull each vertex toward the eye along its view ray (same screen position)
 * until its depth drops by |factor| pixels of its triangle's depth slope (Q3 uses 1 px). */
static void gxbe_offset_slope(u8 *pos, int stride, int numVerts,
                              const glIndex_t *indexes, int numIndexes)
{
    static vec3_t s_eyePos[GXBE_MAX_VERTS];
    static float  s_dInvW[GXBE_MAX_VERTS];
    f32 (*m)[4] = gxState.mvMtx;
    float c00, c01, c02, c10, c11, c12, c20, c21, c22, det;
    float pxX, pxY, scale;
    vec3_t eye;
    int i;

    if (numVerts > GXBE_MAX_VERTS || gxState.vpW <= 0 || gxState.vpH <= 0)
        return;

    /* Eye in model space: E = -L^-1 * t, via the adjugate (entity axes may be scaled). */
    c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
    c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
    c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
    c10 = m[0][2] * m[2][1] - m[0][1] * m[2][2];
    c11 = m[0][0] * m[2][2] - m[0][2] * m[2][0];
    c12 = m[0][1] * m[2][0] - m[0][0] * m[2][1];
    c20 = m[0][1] * m[1][2] - m[0][2] * m[1][1];
    c21 = m[0][2] * m[1][0] - m[0][0] * m[1][2];
    c22 = m[0][0] * m[1][1] - m[0][1] * m[1][0];
    det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
    if (fabsf(det) < 1e-12f)
        return;
    eye[0] = -(c00 * m[0][3] + c10 * m[1][3] + c20 * m[2][3]) / det;
    eye[1] = -(c01 * m[0][3] + c11 * m[1][3] + c21 * m[2][3]) / det;
    eye[2] = -(c02 * m[0][3] + c12 * m[1][3] + c22 * m[2][3]) / det;

    /* For eye-space plane N.P = k: d(1/w)/dpixel = N_x / (k * P00 * vpW/2), same for y. */
    pxX = 2.0f / (fabsf(gxState.projMtx[0][0]) * (float)gxState.vpW);
    pxY = 2.0f / (fabsf(gxState.projMtx[1][1]) * (float)gxState.vpH);
    scale = -s_polyOffsetFactor;

    for (i = 0; i < numVerts; i++) {
        const float *p = (const float *)(pos + i * stride);
        s_eyePos[i][0] = m[0][0] * p[0] + m[0][1] * p[1] + m[0][2] * p[2] + m[0][3];
        s_eyePos[i][1] = m[1][0] * p[0] + m[1][1] * p[1] + m[1][2] * p[2] + m[1][3];
        s_eyePos[i][2] = m[2][0] * p[0] + m[2][1] * p[1] + m[2][2] * p[2] + m[2][3];
        s_dInvW[i] = 0.0f;
    }

    for (i = 0; i + 2 < numIndexes; i += 3) {
        int ia = (int)indexes[i], ib = (int)indexes[i + 1], ic = (int)indexes[i + 2];
        vec3_t e1, e2, n;
        float sx, sy, slope, k, d;

        VectorSubtract(s_eyePos[ib], s_eyePos[ia], e1);
        VectorSubtract(s_eyePos[ic], s_eyePos[ia], e2);
        CrossProduct(e1, e2, n);
        /* Slivers cover no pixels and their normal is fp noise. */
        if (DotProduct(n, n) <= 1e-6f * DotProduct(e1, e1) * DotProduct(e2, e2))
            continue;
        sx = fabsf(n[0]) * pxX;
        sy = fabsf(n[1]) * pxY;
        slope = scale * (sx > sy ? sx : sy);
        k = fabsf(DotProduct(n, s_eyePos[ia]));
        d = (slope < k) ? slope / k : 1.0f;   /* plane through the eye: capped below */
        if (d > s_dInvW[ia]) s_dInvW[ia] = d;
        if (d > s_dInvW[ib]) s_dInvW[ib] = d;
        if (d > s_dInvW[ic]) s_dInvW[ic] = d;
    }

    for (i = 0; i < numVerts; i++) {
        float *p = (float *)(pos + i * stride);
        float f = -s_eyePos[i][2] * s_dInvW[i];   /* new w = w / (1 + f) */
        float s;

        if (f <= 0.0f)
            continue;   /* no pull, or vertex behind the eye */
        if (f > 0.5f)
            f = 0.5f;   /* near edge-on planes: pull at most 1/3 of the distance */
        s = 1.0f / (1.0f + f);
        p[0] = eye[0] + s * (p[0] - eye[0]);
        p[1] = eye[1] + s * (p[1] - eye[1]);
        p[2] = eye[2] + s * (p[2] - eye[2]);
    }
}

static float     s_clipDist[SHADER_MAX_VERTEXES];
static glIndex_t s_clipIdx[SHADER_MAX_INDEXES + 3 * GXBE_MAX_SPLIT];
static glIndex_t s_clipTri[GXBE_MAX_SPLIT][3];   /* split triangles, lone vertex first */
static int       s_clipNumSplit;

/* Clips the triangles to the front of the portal plane. Returns -1 if all are in front (draw
 * as is), else the s_clipIdx count. Split verts go after numVerts, 2 per s_clipTri entry. */
static int gxbe_clip_tris(int numVerts, const glIndex_t *indexes, int numIndexes, int maxSplit)
{
    f32 (*m)[4] = gxState.mvMtx;
    float pl[4];
    int i, nIn = 0, n = 0;

    s_clipNumSplit = 0;

    /* Plane into model space: eye = M * p, so plane_model = M^T * plane_eye. */
    for (i = 0; i < 4; i++)
        pl[i] = s_clipEye[0] * m[0][i] + s_clipEye[1] * m[1][i] + s_clipEye[2] * m[2][i];
    pl[3] += s_clipEye[3] + GXBE_CLIP_EPS;

    for (i = 0; i < numVerts; i++) {
        const float *p = (const float *)((const u8 *)gxState.posPtr + i * gxState.posStride);
        s_clipDist[i] = pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] + pl[3];
        nIn += (s_clipDist[i] >= 0.0f);
    }
    if (nIn == numVerts)
        return -1;
    if (nIn == 0)
        return 0;

    for (i = 0; i + 2 < numIndexes; i += 3) {
        const glIndex_t *v = indexes + i;
        int in0 = (s_clipDist[v[0]] >= 0.0f);
        int in1 = (s_clipDist[v[1]] >= 0.0f);
        int in2 = (s_clipDist[v[2]] >= 0.0f);
        int in = in0 + in1 + in2, lone;
        glIndex_t *t, k;

        if (in == 0)
            continue;
        if (in == 3 || s_clipNumSplit >= maxSplit) {
            s_clipIdx[n++] = v[0];
            s_clipIdx[n++] = v[1];
            s_clipIdx[n++] = v[2];
            continue;
        }
        /* Rotate (keeps the winding) so the vertex alone on its side comes first. */
        if (in == 1)
            lone = in0 ? 0 : (in1 ? 1 : 2);
        else
            lone = !in0 ? 0 : (!in1 ? 1 : 2);
        t = s_clipTri[s_clipNumSplit];
        t[0] = v[lone];
        t[1] = v[(lone + 1) % 3];
        t[2] = v[(lone + 2) % 3];
        k = (glIndex_t)(numVerts + 2 * s_clipNumSplit++);   /* k on edge t0-t1, k+1 on t0-t2 */
        if (in == 1) {
            s_clipIdx[n++] = t[0]; s_clipIdx[n++] = k;    s_clipIdx[n++] = k + 1;
        } else {
            s_clipIdx[n++] = k;    s_clipIdx[n++] = t[1]; s_clipIdx[n++] = t[2];
            s_clipIdx[n++] = k;    s_clipIdx[n++] = t[2]; s_clipIdx[n++] = k + 1;
        }
    }
    return n;
}

/* Lerps one 2-float texcoord along a cut edge into the new vertex slot. */
static void gxbe_clip_lerp_st(u8 *arr, int stride, int a, int b, int o, float f)
{
    const float *ta = (const float *)(arr + a * stride);
    const float *tb = (const float *)(arr + b * stride);
    float *to = (float *)(arr + o * stride);

    to[0] = ta[0] + f * (tb[0] - ta[0]);
    to[1] = ta[1] + f * (tb[1] - ta[1]);
}

/* Fills the split vertices in the staged arrays (tex1 may be NULL). */
static void gxbe_clip_fill(u8 *pos, u8 *clr, u8 *tex0, u8 *tex1, int numVerts)
{
    int s, e, c;

    for (s = 0; s < s_clipNumSplit; s++) {
        for (e = 0; e < 2; e++) {
            int a = (int)s_clipTri[s][0], b = (int)s_clipTri[s][1 + e];
            int o = numVerts + 2 * s + e;
            float f = s_clipDist[a] / (s_clipDist[a] - s_clipDist[b]);   /* signs differ */
            const float *pa = (const float *)(pos + a * gxState.posStride);
            const float *pb = (const float *)(pos + b * gxState.posStride);
            float *po = (float *)(pos + o * gxState.posStride);
            const u8 *ca = clr + a * gxState.clrStride;
            const u8 *cb = clr + b * gxState.clrStride;
            u8 *co = clr + o * gxState.clrStride;

            for (c = 0; c < 3; c++)
                po[c] = pa[c] + f * (pb[c] - pa[c]);
            for (c = 0; c < 4; c++)
                co[c] = (u8)((float)ca[c] + f * (float)(cb[c] - ca[c]) + 0.5f);
            gxbe_clip_lerp_st(tex0, gxState.texStride[0], a, b, o, f);
            if (tex1)
                gxbe_clip_lerp_st(tex1, gxState.texStride[1], a, b, o, f);
        }
    }
}

void GXBE_DrawTess(int numIndexes, const glIndex_t *indexes)
{
    int i;
    int numTex;
    int numVerts;
    const glIndex_t *srcIndexes = indexes;
    int srcNumIndexes = numIndexes;
    const void *posPtr, *clrPtr, *texPtr0, *texPtr1;
    u32 posSize, clrSize, texSize0, texSize1;
    qboolean syncAfterDraw = qfalse;

    if (numIndexes <= 0 || tess.numVertexes <= 0)
        return;

    numTex = gxState.numActiveTMUs;
    numVerts = tess.numVertexes;

    /* Portal view: drop or split the triangles behind the portal plane. */
    if (s_clipActive && gxState.projType == GX_PERSPECTIVE) {
        int n = gxbe_clip_tris(tess.numVertexes, indexes, numIndexes, GXBE_MAX_SPLIT);

        if (n == 0)
            return;
        if (n > 0) {
            indexes    = s_clipIdx;
            numIndexes = n;
            numVerts  += 2 * s_clipNumSplit;
        }
    }

    /* Stage vertex data into the fenced ring so the CPU can rewrite tess freely.
     * Stride bytes per vertex copied = exactly what the GP would fetch in place. */
    posSize  = (u32)tess.numVertexes * (u32)gxState.posStride;
    clrSize  = (u32)tess.numVertexes * (u32)gxState.clrStride;
    texSize0 = (u32)tess.numVertexes * (u32)gxState.texStride[0];
    texSize1 = (numTex == 2) ? (u32)tess.numVertexes * (u32)gxState.texStride[1] : 0;

    {
        /* Room for the portal-clip split vertices after the copied ones. */
        u32 extra = (u32)(numVerts - tess.numVertexes);
        u32 a_pos = (posSize  + extra * gxState.posStride + 31) & ~31u;
        u32 a_clr = (clrSize  + extra * gxState.clrStride + 31) & ~31u;
        u32 a_t0  = (texSize0 + extra * gxState.texStride[0] + 31) & ~31u;
        u32 a_t1  = (numTex == 2) ? (texSize1 + extra * gxState.texStride[1] + 31) & ~31u : 0;
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

            if (extra)
                gxbe_clip_fill(blk, blk + a_pos, blk + a_pos + a_clr,
                               (numTex == 2) ? blk + a_pos + a_clr + a_t0 : NULL, tess.numVertexes);

            /* Edits the staged copy only, so every stage pulls from the same tess.xyz. */
            if (s_polyOffsetFill && s_polyOffsetFactor < 0.0f && gxState.projType == GX_PERSPECTIVE)
                gxbe_offset_slope(blk, gxState.posStride, numVerts, indexes, numIndexes);

            DCFlushRange(blk, a_pos + a_clr + a_t0 + a_t1);
        } else {
            /* Ring alloc failed: draw client arrays in place and sync right after GX_End,
             * since the caller rewrites tess as soon as we return. No slope offset here. */
            if (extra)   /* no room for split verts: keep the cut triangles whole */
                numIndexes = gxbe_clip_tris(tess.numVertexes, srcIndexes, srcNumIndexes, 0);
            syncAfterDraw = qtrue;
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

    /* Fallback: the GP still reads the client arrays, so block before the caller rewrites
     * them. The ring copy stays untouched until its fence recycles it. */
    if (syncAfterDraw)
        GX_DrawDone();
}

#else  /* !WII_NATIVE_GX */

/* Non-empty placeholder for the baseline build. */
typedef int tr_gx_translation_unit_placeholder;

#endif /* WII_NATIVE_GX */
