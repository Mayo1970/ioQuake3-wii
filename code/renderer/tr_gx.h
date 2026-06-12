/* tr_gx.h — native GX backend declarations (active under -DWII_NATIVE_GX only).
 * Never mix GL/ogx_ calls into the native path or GX_ calls into the OpenGX path. */
#ifndef TR_GX_H
#define TR_GX_H

#if defined(WII_NATIVE_GX)

#include <gccore.h>   /* GXTexObj, Mtx, Mtx44, u8, f32, GXBool, etc. */

/* Shadow state for the native GX backend — mirrors glState_t for redundant-call elimination. */
typedef struct {
    int      currenttmu;        /* 0 or 1 – kept in sync with glState.currenttmu */
    int      boundtex[2];       /* texnum bound to GX_TEXMAP0/1; -1 = none */
    int      texenv[2];         /* pending GL_MODULATE etc. per TMU */
    int      faceCulling;       /* last CT_* value; -1 = force-set next call */
    int      numActiveTMUs;     /* 1 or 2, driven by qglEnable/Disable(GL_TEXTURE_2D) on TMU1 */
    float    depthNear;         /* depth range near [0,1] for GX_SetViewport */
    float    depthFar;          /* depth range far  [0,1] */
    int      vpX, vpY, vpW, vpH; /* current viewport (GX top-left coords) */
    qboolean arraysInFlight;    /* fallback only (ring alloc failed): set after drawing from client arrays */
    qboolean tevDirty;          /* needs GX TEV stage recommit before next draw */
    int      vtxDescNumTex;     /* last-set vtx-desc TMU count: 1, 2, or -1=unset */
    qboolean alphaTestActive;   /* last GX_SetZCompLoc state */

    /* Client vertex arrays — DrawTess reads these; fast-path iterators bind raw tess arrays here. */
    const void *posPtr;         /* xyz array (stride 16 in all Q3 paths) */
    const void *clrPtr;         /* color4ub array */
    const void *texPtr[2];      /* texcoord array per TMU */
    int      posStride;
    int      clrStride;
    int      texStride[2];

    /* Last-loaded matrices; GXBE_Clear restores them after drawing its clear quad. */
    Mtx44    projMtx;
    Mtx      mvMtx;
    u8       projType;          /* GX_PERSPECTIVE or GX_ORTHOGRAPHIC */
} gx_state_t;

extern gx_state_t gxState;

/* Texture table — indexed by image->texnum (0-based counter, not a GL name). */
#define GX_MAX_TEXOBJS  2048  /* matches MAX_DRAWIMAGES */

extern GXTexObj  s_gx_texobjs[GX_MAX_TEXOBJS];
extern qboolean  s_gx_texobj_valid[GX_MAX_TEXOBJS];
extern void     *s_gx_texbufs[GX_MAX_TEXOBJS]; /* memalign'd swizzled data */
extern int       s_gx_next_texnum;

/* GXBE_ function declarations — called from WII_NATIVE_GX arms in choke-point files. */

/* Init */
void GXBE_SetDefaultState(void);
void GXBE_FrameEnd(void);          /* called from Wii_GX_EndFrame */

/* Full GP sync (qglFinish equivalent): drains FIFO, clears arraysInFlight. */
void GXBE_Finish(void);

/* State */
void GXBE_GL_State(unsigned long stateBits);
void GXBE_GL_Cull(int cullType);
void GXBE_GL_TexEnv(int unit, int env);
void GXBE_BindTexnum(int tmu, int texnum);

/* Matrices / viewport (2D path, Phase 2) */
void GXBE_LoadOrtho2D(int vidWidth, int vidHeight);
void GXBE_LoadIdentityModelview(void);
void GXBE_SetViewport(int x, int y, int w, int h);
void GXBE_SetScissor(int x, int y, int w, int h);
void GXBE_DepthRange(float n, float f);

/* Polygon offset for decals. Routed from qgl_wii.c wrappers; factor ignored (OpenGX ignores it too). */
void GXBE_PolygonOffset(float factor, float units);
void GXBE_SetPolygonOffsetEnabled(int enabled);

/* Matrices (3D path, Phase 3). Inputs are Q3's column-major GL float[16]. */
void GXBE_LoadProjectionGL(const float *gl16);   /* perspective; z row rebuilt for GX clip space */
void GXBE_LoadProjectionObliqueGL(const float *gl16, const float *eyePlane); /* portal clip: oblique near plane (Lengyel), GX has no clip planes */
void GXBE_LoadModelviewGL(const float *gl16);
void GXBE_LoadModelviewTranslatedGL(const float *gl16, const vec3_t origin);

/* GX has no glClear — draws a viewport-covering quad, then restores pipeline state. */
void GXBE_Clear(qboolean clearColor, qboolean clearDepth, float r, float g, float b);

/* Immediate-mode emulation for sky/cinematic/beam paths. numVerts MUST match emitted count exactly. */
void GXBE_ImmediateBegin(int primGL, int numVerts);
void GXBE_ImmediateTexVertex(const float *st, const float *xyz,
                             byte r, byte g, byte b, byte a);
void GXBE_ImmediateEnd(void);

/* Client array pointers (called from the qgl wrappers in qgl_wii.c) */
void GXBE_SetVertexPtr(const void *p, int stride);
void GXBE_SetColorPtr(const void *p, int stride);
void GXBE_SetTexCoordPtr(const void *p, int stride);  /* applies to gxState.currenttmu */
void GXBE_SetTexture2DEnabled(int enabled);           /* applies to gxState.currenttmu */

/* Draw */
void GXBE_DrawTess(int numIndexes, const glIndex_t *indexes);

/* EFB readbacks. PeekDepth: GX window depth == GL depth (zGX=(zGL-w)/2), feeds RB_TestFlare.
 * ReadPixelsRGB: EFB→RGBA8 tex-copy, de-tile into packed GL bottom-up RGB rows. Both sync GP. */
float GXBE_PeekDepth(int x, int y);
void  GXBE_ReadPixelsRGB(int x, int y, int w, int h, int padlen, byte *dst);

/* Texture ops (tr_gx_texture.c). Upload32 expects the final scaled/picmipped buffer, stored
 * as RGB565 or RGB5A3. When mipmap=true, data is reduced IN PLACE level by level. */
void     GXBE_CreateTexnum(GLuint *texnum);
void     GXBE_DeleteTexnum(int texnum);
void     GXBE_Upload32(unsigned *data, int width, int height,
                       int glInternalFormat, int texnum, int wrapClampMode,
                       qboolean mipmap);
void     GXBE_TexSubImage2D(int texnum, int fullW, int fullH, const byte *data);

#endif /* WII_NATIVE_GX */

#endif /* TR_GX_H */
