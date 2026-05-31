/* Wii renderer glue: ri global, pre-boot stub, two-phase GetRefAPI. */

#undef COLOR_BLACK
#undef COLOR_RED
#undef COLOR_GREEN
#undef COLOR_YELLOW
#undef COLOR_BLUE
#undef COLOR_CYAN
#undef COLOR_MAGENTA
#undef COLOR_WHITE
#undef COLOR_ORANGE

#include <gccore.h>
#include <stdio.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "renderercommon/tr_types.h"
#include "renderercommon/tr_public.h"
#include "sys/wii_glimp.h"

refimport_t ri;

static void QDECL wii_ri_Printf(int level, const char *fmt, ...)
{
    va_list ap;
    char buf[1024];
    va_start(ap, fmt);
    Q_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Com_Printf("%s", buf);
    (void)level;
}

static Q_NO_RETURN void QDECL wii_ri_Error(int level, const char *fmt, ...)
{
    va_list ap;
    char buf[1024];
    va_start(ap, fmt);
    Q_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Com_Error(level, "%s", buf);
}

static void *wii_ri_Hunk_Alloc(int size, ha_pref pref, char *label,
                                char *file, int line)
{
    return Hunk_AllocDebug(size, pref, label, file, line);
}

static void *wii_ri_Malloc(int size)
{
    return Z_Malloc(size);
}

static void wii_ri_init(void)
{
    ri.Printf                    = wii_ri_Printf;
    ri.Error                     = wii_ri_Error;
    ri.Milliseconds              = Sys_Milliseconds;
    ri.Hunk_AllocDebug           = wii_ri_Hunk_Alloc;
    ri.Hunk_AllocateTempMemory   = Hunk_AllocateTempMemory;
    ri.Hunk_FreeTempMemory       = Hunk_FreeTempMemory;
    ri.Malloc                    = wii_ri_Malloc;
    ri.Free                      = Z_Free;
    ri.Cvar_Get                  = Cvar_Get;
    ri.Cvar_Set                  = Cvar_Set;
    ri.Cvar_SetValue             = Cvar_SetValue;
    ri.Cvar_CheckRange           = Cvar_CheckRange;
    ri.Cvar_SetDescription       = Cvar_SetDescription;
    ri.Cvar_VariableIntegerValue = Cvar_VariableIntegerValue;
    ri.Cmd_AddCommand            = Cmd_AddCommand;
    ri.Cmd_RemoveCommand         = Cmd_RemoveCommand;
    ri.Cmd_Argc                  = Cmd_Argc;
    ri.Cmd_Argv                  = Cmd_Argv;
    ri.Cmd_ExecuteText           = Cbuf_ExecuteText;
    ri.CM_ClusterPVS             = CM_ClusterPVS;
    ri.CM_DrawDebugSurface       = CM_DrawDebugSurface;
    ri.FS_FileIsInPAK            = FS_FileIsInPAK;
    ri.FS_ReadFile               = FS_ReadFile;
    ri.FS_FreeFile               = FS_FreeFile;
    ri.FS_ListFiles              = FS_ListFiles;
    ri.FS_FreeFileList           = FS_FreeFileList;
    ri.FS_WriteFile              = FS_WriteFile;
}

static void s_Shutdown(qboolean d)  { (void)d; }

static void s_BeginRegistration(glconfig_t *c)
{
    if (!c) return;
    GXRModeObj *r = Wii_GX_GetRMode();
    c->vidWidth             = r ? (int)r->fbWidth   : 640;
    c->vidHeight            = r ? (int)r->efbHeight : 480;
    c->windowAspect         = (float)c->vidWidth / (float)c->vidHeight;
    c->colorBits            = 24;
    c->depthBits            = 24;
    c->stencilBits          = 0;
    c->deviceSupportsGamma  = qfalse;
    c->textureCompression   = TC_NONE;
    c->textureEnvAddAvailable = qfalse;
    Q_strncpyz(c->renderer_string, "GX/Wii (pre-boot)", sizeof(c->renderer_string));
    Q_strncpyz(c->vendor_string,   "Nintendo",          sizeof(c->vendor_string));
    Q_strncpyz(c->version_string,  "GX 1.0",            sizeof(c->version_string));
}

static qhandle_t s_RegisterModel(const char *n)       { (void)n; return 0; }
static qhandle_t s_RegisterSkin(const char *n)        { (void)n; return 0; }
static qhandle_t s_RegisterShader(const char *n)      { (void)n; return 0; }
static qhandle_t s_RegisterShaderNoMip(const char *n) { (void)n; return 0; }
static void      s_LoadWorld(const char *n)            { (void)n; }
static void      s_SetWorldVisData(const byte *v)      { (void)v; }
static void      s_EndRegistration(void)               { }
static void      s_ClearScene(void)                    { }
static void      s_AddRefEntityToScene(const refEntity_t *e) { (void)e; }
static void      s_AddPolyToScene(qhandle_t h, int n,
                     const polyVert_t *v, int p)
    { (void)h;(void)n;(void)v;(void)p; }
static int       s_LightForPoint(vec3_t p, vec3_t a,
                     vec3_t d, vec3_t dir)
    { (void)p;(void)a;(void)d;(void)dir; return 0; }
static void      s_AddLightToScene(const vec3_t o, float r,
                     float i, float g, float b)
    { (void)o;(void)r;(void)i;(void)g;(void)b; }
static void      s_AddAdditiveLightToScene(const vec3_t o, float r,
                     float i, float g, float b)
    { (void)o;(void)r;(void)i;(void)g;(void)b; }
static void      s_RenderScene(const refdef_t *rd)     { (void)rd; }
static void      s_SetColor(const float *rgba)         { (void)rgba; }
static void      s_DrawStretchPic(float x, float y, float w, float h,
                     float s1, float t1, float s2, float t2, qhandle_t sh)
    { (void)x;(void)y;(void)w;(void)h;(void)s1;(void)t1;(void)s2;(void)t2;(void)sh; }
static void      s_DrawStretchRaw(int x, int y, int w, int h,
                     int cols, int rows, const byte *d, int c, qboolean dirty)
    { (void)x;(void)y;(void)w;(void)h;(void)cols;(void)rows;(void)d;(void)c;(void)dirty; }
static void      s_UploadCinematic(int w, int h, int cols, int rows,
                     const byte *d, int c, qboolean dirty)
    { (void)w;(void)h;(void)cols;(void)rows;(void)d;(void)c;(void)dirty; }
static void      s_BeginFrame(stereoFrame_t sf)  { (void)sf; }
static void      s_EndFrame(int *f, int *b)
{
    (void)f; (void)b;
    Wii_GX_EndFrame();
}
static int       s_MarkFragments(int n, const vec3_t *o, const vec3_t proj,
                     int ms, vec_t *pts, int mf, markFragment_t *mfp)
    { (void)n;(void)o;(void)proj;(void)ms;(void)pts;(void)mf;(void)mfp; return 0; }
static int       s_LerpTag(orientation_t *tag, qhandle_t h, int sf,
                     int ef, float f, const char *name)
    { (void)tag;(void)h;(void)sf;(void)ef;(void)f;(void)name; return 0; }
static void      s_ModelBounds(qhandle_t h, vec3_t mn, vec3_t mx)
    { (void)h; VectorClear(mn); VectorClear(mx); }
static void      s_RegisterFont(const char *n, int ps, fontInfo_t *fi)
    { (void)n;(void)ps;(void)fi; }
static void      s_RemapShader(const char *o, const char *n, const char *off)
    { (void)o;(void)n;(void)off; }
static qboolean  s_GetEntityToken(char *buf, int s)
    { (void)buf;(void)s; return qfalse; }
static qboolean  s_inPVS(const vec3_t p1, const vec3_t p2)
    { (void)p1;(void)p2; return qtrue; }
static void      s_TakeVideoFrame(int h, int w, byte *cap, byte *enc,
                     qboolean mj)
    { (void)h;(void)w;(void)cap;(void)enc;(void)mj; }

static refexport_t wii_re = {
    s_Shutdown,            s_BeginRegistration,
    s_RegisterModel,       s_RegisterSkin,
    s_RegisterShader,      s_RegisterShaderNoMip,
    s_LoadWorld,           s_SetWorldVisData,    s_EndRegistration,
    s_ClearScene,          s_AddRefEntityToScene, s_AddPolyToScene,
    s_LightForPoint,       s_AddLightToScene,    s_AddAdditiveLightToScene,
    s_RenderScene,         s_SetColor,           s_DrawStretchPic,
    s_DrawStretchRaw,      s_UploadCinematic,
    s_BeginFrame,          s_EndFrame,
    s_MarkFragments,       s_LerpTag,            s_ModelBounds,
    s_RegisterFont,        s_RemapShader,        s_GetEntityToken,
    s_inPVS,               s_TakeVideoFrame,
};

/*
 * Called twice: (1) pre-boot with rimp==NULL → return no-op stub,
 * (2) from CL_InitRef with real rimp → wire qgl* and activate renderergl1.
 */
extern void QGL_Init(void);
extern refexport_t *tr_init_GetRefAPI_unused(int apiVersion, refimport_t *rimp);

refexport_t *GetRefAPI(int apiVersion, refimport_t *rimp)
{
    wii_ri_init();

    if (rimp)
        ri = *rimp;

    if (!rimp) {
        return &wii_re;
    }

    QGL_Init();

    return tr_init_GetRefAPI_unused(apiVersion, &ri);
}
