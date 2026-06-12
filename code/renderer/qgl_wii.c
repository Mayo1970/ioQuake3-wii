/* Wire ioQ3's qgl* function pointers to OpenGX. */

#include "tr_local.h"
#ifdef WII_DEBUG
#include "../sys/wii_platform.h"
static void diag_Viewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    static int s_vpLast[4] = {-1,-1,-1,-1};
    if (x != s_vpLast[0] || y != s_vpLast[1] || w != s_vpLast[2] || h != s_vpLast[3]) {
        static int s_vpCount = 0;
        if (s_vpCount < 20)
            wii_diag("[vp] #%d glViewport(%d,%d,%d,%d)\n", s_vpCount++, x, y, w, h);
        s_vpLast[0]=x; s_vpLast[1]=y; s_vpLast[2]=w; s_vpLast[3]=h;
    }
    glViewport(x, y, w, h);
}
static void diag_Scissor(GLint x, GLint y, GLsizei w, GLsizei h) {
    static int s_scLast[4] = {-1,-1,-1,-1};
    if (x != s_scLast[0] || y != s_scLast[1] || w != s_scLast[2] || h != s_scLast[3]) {
        static int s_scCount = 0;
        if (s_scCount < 20)
            wii_diag("[sc] #%d glScissor(%d,%d,%d,%d)\n", s_scCount++, x, y, w, h);
        s_scLast[0]=x; s_scLast[1]=y; s_scLast[2]=w; s_scLast[3]=h;
    }
    glScissor(x, y, w, h);
}
static void diag_Ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t,
                       GLdouble n, GLdouble f) {
    static int s_orthoCount = 0;
    if (s_orthoCount < 10)
        wii_diag("[ortho] #%d l=%.0f r=%.0f b=%.0f t=%.0f\n",
                 s_orthoCount++, l, r, b, t);
    glOrtho(l, r, b, t, n, f);
}
#endif

static void noop_DrawBuffer  (GLenum m)             { (void)m; }
static void noop_PolygonMode (GLenum f, GLenum m)   { (void)f; (void)m; }
static void noop_LockArrays  (GLint f, GLsizei c)   { (void)f; (void)c; }
static void noop_UnlockArrays(void)                 { }

static void noop_CopyTexSubImage2D(GLenum tgt, GLint lvl,
    GLint xo, GLint yo, GLint x, GLint y, GLsizei w, GLsizei h)
{ (void)tgt;(void)lvl;(void)xo;(void)yo;(void)x;(void)y;(void)w;(void)h; }

/* WII_NATIVE_GX no-op stubs: non-choke-point qgl* calls go here instead of crashing on NULL. */
#if defined(WII_NATIVE_GX)
static void ngx_noop_void(void)                        {}
static void ngx_noop_u(GLenum a)                       { (void)a; }
static void ngx_noop_uu(GLenum a, GLenum b)            { (void)a;(void)b; }
static void ngx_noop_i(GLint a)                        { (void)a; }
static void ngx_noop_f(GLfloat a)                      { (void)a; }
static void ngx_noop_d(GLclampd a)                     { (void)a; }
static void ngx_noop_b(GLboolean a)                    { (void)a; }
static void ngx_noop_bbbb(GLboolean a, GLboolean b, GLboolean c, GLboolean d)
    { (void)a;(void)b;(void)c;(void)d; }
static void ngx_noop_nu(GLsizei n, const GLuint *p)    { (void)n;(void)p; }
static void ngx_noop_ni(GLsizei n, GLuint *p)          { (void)n;(void)p; }
static GLenum ngx_noop_ret_u(void)                     { return GL_NO_ERROR; }
static const GLubyte *ngx_noop_ret_str(GLenum a)       { (void)a; return (const GLubyte *)""; }
static void ngx_noop_getintegerv(GLenum pname, GLint *params)
    { (void)pname; if (params) *params = 0; }
static void ngx_noop_getbooleanv(GLenum pname, GLboolean *params)
    { (void)pname; if (params) *params = 0; }
static void ngx_noop_teximage2d(GLenum tgt, GLint lvl, GLint ifmt,
    GLsizei w, GLsizei h, GLint b, GLenum fmt, GLenum tp, const GLvoid *d)
    { (void)tgt;(void)lvl;(void)ifmt;(void)w;(void)h;(void)b;(void)fmt;(void)tp;(void)d; }
static void ngx_noop_texsubimage2d(GLenum tgt, GLint lvl,
    GLint xo, GLint yo, GLsizei w, GLsizei h, GLenum fmt, GLenum tp, const GLvoid *d)
    { (void)tgt;(void)lvl;(void)xo;(void)yo;(void)w;(void)h;(void)fmt;(void)tp;(void)d; }
static void ngx_noop_readpixels(GLint x, GLint y, GLsizei w, GLsizei h,
    GLenum fmt, GLenum tp, GLvoid *d)
    { (void)x;(void)y;(void)w;(void)h;(void)fmt;(void)tp;(void)d; }
static void ngx_noop_scissor(GLint x, GLint y, GLsizei w, GLsizei h)
    { (void)x;(void)y;(void)w;(void)h; }
static void ngx_noop_viewport(GLint x, GLint y, GLsizei w, GLsizei h)
    { (void)x;(void)y;(void)w;(void)h; }
static void ngx_noop_stencil3(GLenum a, GLint b, GLuint c)
    { (void)a;(void)b;(void)c; }
static void ngx_noop_stencil2(GLenum a, GLenum b, GLenum c)
    { (void)a;(void)b;(void)c; }
static void ngx_noop_texparamf(GLenum a, GLenum b, GLfloat c)
    { (void)a;(void)b;(void)c; }
static void ngx_noop_texparami(GLenum a, GLenum b, GLint c)
    { (void)a;(void)b;(void)c; }
static void ngx_noop_alpharef(GLenum a, GLclampf b)    { (void)a;(void)b; }
static void ngx_noop_color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
    { (void)r;(void)g;(void)b;(void)a; }
static void ngx_noop_color3f(GLfloat r, GLfloat g, GLfloat b)
    { (void)r;(void)g;(void)b; }
static void ngx_noop_color4ubv(const GLubyte *v)       { (void)v; }
static void ngx_noop_loadmatrix(const GLfloat *m)      { (void)m; }
static void ngx_noop_translatef(GLfloat x, GLfloat y, GLfloat z)
    { (void)x;(void)y;(void)z; }
static void ngx_noop_frustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t,
    GLdouble n, GLdouble f)
    { (void)l;(void)r;(void)b;(void)t;(void)n;(void)f; }
static void ngx_noop_ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t,
    GLdouble n, GLdouble f)
    { (void)l;(void)r;(void)b;(void)t;(void)n;(void)f; }
static void ngx_noop_clipplane(GLenum p, const GLdouble *eq)
    { (void)p;(void)eq; }
static void ngx_noop_texcoord2f(GLfloat s, GLfloat t)  { (void)s;(void)t; }
static void ngx_noop_texcoord2fv(const GLfloat *v)     { (void)v; }
static void ngx_noop_vertex2f(GLfloat x, GLfloat y)    { (void)x;(void)y; }
static void ngx_noop_vertex3f(GLfloat x, GLfloat y, GLfloat z)
    { (void)x;(void)y;(void)z; }
static void ngx_noop_vertex3fv(const GLfloat *v)       { (void)v; }
static void ngx_noop_arrayelement(GLint i)              { (void)i; }
static void ngx_noop_begin(GLenum m)                   { (void)m; }
static void ngx_noop_drawelements(GLenum m, GLsizei c, GLenum t, const GLvoid *i)
    { (void)m;(void)c;(void)t;(void)i; }
static void ngx_noop_drawarrays(GLenum m, GLint f, GLsizei c)
    { (void)m;(void)f;(void)c; }
static void ngx_noop_texenvf(GLenum t, GLenum p, GLfloat v)
    { (void)t;(void)p;(void)v; }
static void ngx_noop_shademodel(GLenum m)              { (void)m; }
static void ngx_noop_multitex(GLenum t)                { (void)t; }

/* Routed wrappers: forward to GXBE so every call site works without per-site #ifdefs. */
static void ngx_route_depthrange(GLclampd n, GLclampd f)
{
    GXBE_DepthRange((float)n, (float)f);
}
/* GL stride 0 means tightly packed: size * sizeof(type) */
static int ngx_eff_stride(GLint size, GLenum type, GLsizei stride)
{
    if (stride != 0)
        return (int)stride;
    return (int)size * ((type == GL_UNSIGNED_BYTE) ? 1 : 4);
}
static void ngx_route_vertexptr(GLint size, GLenum type, GLsizei stride, const GLvoid *p)
{
    GXBE_SetVertexPtr(p, ngx_eff_stride(size, type, stride));
}
static void ngx_route_colorptr(GLint size, GLenum type, GLsizei stride, const GLvoid *p)
{
    GXBE_SetColorPtr(p, ngx_eff_stride(size, type, stride));
}
static void ngx_route_texcoordptr(GLint size, GLenum type, GLsizei stride, const GLvoid *p)
{
    GXBE_SetTexCoordPtr(p, ngx_eff_stride(size, type, stride));
}
static void ngx_route_enable(GLenum cap)
{
    if (cap == GL_TEXTURE_2D)
        GXBE_SetTexture2DEnabled(1);
    else if (cap == GL_POLYGON_OFFSET_FILL)
        GXBE_SetPolygonOffsetEnabled(1);
    /* all other caps (CLIP_PLANE0, STENCIL_TEST, ...) are handled
     * elsewhere or unsupported on GX — ignore */
}
static void ngx_route_disable(GLenum cap)
{
    if (cap == GL_TEXTURE_2D)
        GXBE_SetTexture2DEnabled(0);
    else if (cap == GL_POLYGON_OFFSET_FILL)
        GXBE_SetPolygonOffsetEnabled(0);
}
static void ngx_route_polygonoffset(GLfloat factor, GLfloat units)
{
    GXBE_PolygonOffset((float)factor, (float)units);
}

static void ngx_noop_multitexcoord(GLenum t, GLfloat s, GLfloat v)
    { (void)t;(void)s;(void)v; }
#endif /* WII_NATIVE_GX */

void QGL_Init(void)
{
#if defined(WII_NATIVE_GX)
    qglBindTexture          = (void(*)(GLenum,GLuint))ngx_noop_uu;
    qglBlendFunc            = (void(*)(GLenum,GLenum))ngx_noop_uu;
    qglClear                = (void(*)(GLbitfield))ngx_noop_u;
    qglClearColor           = (void(*)(GLclampf,GLclampf,GLclampf,GLclampf))ngx_noop_color4f;
    qglClearStencil         = (void(*)(GLint))ngx_noop_i;
    qglColorMask            = (void(*)(GLboolean,GLboolean,GLboolean,GLboolean))ngx_noop_bbbb;
    qglCopyTexSubImage2D    = noop_CopyTexSubImage2D;
    qglCullFace             = (void(*)(GLenum))ngx_noop_u;
    qglDeleteTextures       = (void(*)(GLsizei,const GLuint*))ngx_noop_nu;
    qglDepthFunc            = (void(*)(GLenum))ngx_noop_u;
    qglDepthMask            = (void(*)(GLboolean))ngx_noop_b;
    qglDisable              = ngx_route_disable;
    qglDrawArrays           = (void(*)(GLenum,GLint,GLsizei))ngx_noop_drawarrays;
    qglDrawElements         = (void(*)(GLenum,GLsizei,GLenum,const GLvoid*))ngx_noop_drawelements;
    qglEnable               = ngx_route_enable;
    qglFinish               = (void(*)(void))ngx_noop_void;
    qglFlush                = (void(*)(void))ngx_noop_void;
    qglGenTextures          = (void(*)(GLsizei,GLuint*))ngx_noop_ni;
    qglGetBooleanv          = (void(*)(GLenum,GLboolean*))ngx_noop_getbooleanv;
    qglGetError             = (GLenum(*)(void))ngx_noop_ret_u;
    qglGetIntegerv          = (void(*)(GLenum,GLint*))ngx_noop_getintegerv;
    qglGetString            = (const GLubyte*(*)(GLenum))ngx_noop_ret_str;
    qglLineWidth            = (void(*)(GLfloat))ngx_noop_f;
    qglPolygonOffset        = ngx_route_polygonoffset;
    qglReadPixels           = (void(*)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,GLvoid*))ngx_noop_readpixels;
    qglScissor              = (void(*)(GLint,GLint,GLsizei,GLsizei))ngx_noop_scissor;
    qglStencilFunc          = (void(*)(GLenum,GLint,GLuint))ngx_noop_stencil3;
    qglStencilMask          = (void(*)(GLuint))ngx_noop_u;
    qglStencilOp            = (void(*)(GLenum,GLenum,GLenum))ngx_noop_stencil2;
    qglTexImage2D           = (void(*)(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const GLvoid*))ngx_noop_teximage2d;
    qglTexParameterf        = (void(*)(GLenum,GLenum,GLfloat))ngx_noop_texparamf;
    qglTexParameteri        = (void(*)(GLenum,GLenum,GLint))ngx_noop_texparami;
    qglTexSubImage2D        = (void(*)(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,const GLvoid*))ngx_noop_texsubimage2d;
    qglViewport             = (void(*)(GLint,GLint,GLsizei,GLsizei))ngx_noop_viewport;

    qglAlphaFunc            = (void(*)(GLenum,GLclampf))ngx_noop_alpharef;
    qglColor4f              = (void(*)(GLfloat,GLfloat,GLfloat,GLfloat))ngx_noop_color4f;
    qglColorPointer         = ngx_route_colorptr;
    qglDisableClientState   = (void(*)(GLenum))ngx_noop_u;
    qglEnableClientState    = (void(*)(GLenum))ngx_noop_u;
    qglLoadIdentity         = (void(*)(void))ngx_noop_void;
    qglLoadMatrixf          = (void(*)(const GLfloat*))ngx_noop_loadmatrix;
    qglMatrixMode           = (void(*)(GLenum))ngx_noop_u;
    qglPopMatrix            = (void(*)(void))ngx_noop_void;
    qglPushMatrix           = (void(*)(void))ngx_noop_void;
    qglShadeModel           = (void(*)(GLenum))ngx_noop_shademodel;
    qglTexCoordPointer      = ngx_route_texcoordptr;
    qglTexEnvf              = (void(*)(GLenum,GLenum,GLfloat))ngx_noop_texenvf;
    qglTranslatef           = (void(*)(GLfloat,GLfloat,GLfloat))ngx_noop_translatef;
    qglVertexPointer        = ngx_route_vertexptr;

    qglClearDepth           = ngx_noop_d;
    qglDepthRange           = ngx_route_depthrange;
    qglDrawBuffer           = noop_DrawBuffer;
    qglPolygonMode          = noop_PolygonMode;

    qglArrayElement         = (void(*)(GLint))ngx_noop_arrayelement;
    qglBegin                = (void(*)(GLenum))ngx_noop_begin;
    qglClipPlane            = (void(*)(GLenum,const GLdouble*))ngx_noop_clipplane;
    qglColor3f              = (void(*)(GLfloat,GLfloat,GLfloat))ngx_noop_color3f;
    qglColor4ubv            = (void(*)(const GLubyte*))ngx_noop_color4ubv;
    qglEnd                  = (void(*)(void))ngx_noop_void;
    qglFrustum              = (void(*)(GLdouble,GLdouble,GLdouble,GLdouble,GLdouble,GLdouble))ngx_noop_frustum;
    qglOrtho                = (void(*)(GLdouble,GLdouble,GLdouble,GLdouble,GLdouble,GLdouble))ngx_noop_ortho;
    qglTexCoord2f           = (void(*)(GLfloat,GLfloat))ngx_noop_texcoord2f;
    qglTexCoord2fv          = (void(*)(const GLfloat*))ngx_noop_texcoord2fv;
    qglVertex2f             = (void(*)(GLfloat,GLfloat))ngx_noop_vertex2f;
    qglVertex3f             = (void(*)(GLfloat,GLfloat,GLfloat))ngx_noop_vertex3f;
    qglVertex3fv            = (void(*)(const GLfloat*))ngx_noop_vertex3fv;

    qglActiveTextureARB       = (void(*)(GLenum))ngx_noop_multitex;
    qglClientActiveTextureARB = (void(*)(GLenum))ngx_noop_multitex;
    qglMultiTexCoord2fARB     = (void(*)(GLenum,GLfloat,GLfloat))ngx_noop_multitexcoord;

    qglLockArraysEXT   = noop_LockArrays;
    qglUnlockArraysEXT = noop_UnlockArrays;

#else  /* !WII_NATIVE_GX — original OpenGX wiring */

    qglBindTexture        = glBindTexture;
    qglBlendFunc          = glBlendFunc;
    qglClear              = glClear;
    qglClearColor         = glClearColor;
    qglClearStencil       = glClearStencil;
    qglColorMask          = glColorMask;
    qglCopyTexSubImage2D  = noop_CopyTexSubImage2D;
    qglCullFace           = glCullFace;
    qglDeleteTextures     = glDeleteTextures;
    qglDepthFunc          = glDepthFunc;
    qglDepthMask          = glDepthMask;
    qglDisable            = glDisable;
    qglDrawArrays         = glDrawArrays;
    qglDrawElements       = glDrawElements;
    qglEnable             = glEnable;
    qglFinish             = glFinish;
    qglFlush              = glFlush;
    qglGenTextures        = glGenTextures;
    qglGetBooleanv        = glGetBooleanv;
    qglGetError           = glGetError;
    qglGetIntegerv        = glGetIntegerv;
    qglGetString          = glGetString;
    qglLineWidth          = glLineWidth;
    qglPolygonOffset      = glPolygonOffset;
    qglReadPixels         = glReadPixels;
    qglScissor            = glScissor;
    qglStencilFunc        = glStencilFunc;
    qglStencilMask        = glStencilMask;
    qglStencilOp          = glStencilOp;
    qglTexImage2D         = glTexImage2D;
    qglTexParameterf      = glTexParameterf;
    qglTexParameteri      = glTexParameteri;
    qglTexSubImage2D      = glTexSubImage2D;
#ifndef WII_DEBUG
    qglViewport           = glViewport;
#endif

    qglAlphaFunc          = glAlphaFunc;
    qglColor4f            = glColor4f;
    qglColorPointer       = glColorPointer;
    qglDisableClientState = glDisableClientState;
    qglEnableClientState  = glEnableClientState;
    qglLoadIdentity       = glLoadIdentity;
    qglLoadMatrixf        = glLoadMatrixf;
    qglMatrixMode         = glMatrixMode;
    qglPopMatrix          = glPopMatrix;
    qglPushMatrix         = glPushMatrix;
    qglShadeModel         = glShadeModel;
    qglTexCoordPointer    = glTexCoordPointer;
    qglTexEnvf            = glTexEnvf;
    qglTranslatef         = glTranslatef;
    qglVertexPointer      = glVertexPointer;

    qglClearDepth         = glClearDepth;
    qglDepthRange         = glDepthRange;
    qglDrawBuffer         = noop_DrawBuffer;
    qglPolygonMode        = noop_PolygonMode;

    qglArrayElement       = glArrayElement;
    qglBegin              = glBegin;
    qglClipPlane          = glClipPlane;
    qglColor3f            = glColor3f;
    qglColor4ubv          = glColor4ubv;
    qglEnd                = glEnd;
    qglFrustum            = glFrustum;
    qglOrtho              = glOrtho;
    qglTexCoord2f         = glTexCoord2f;
    qglTexCoord2fv        = glTexCoord2fv;
    qglVertex2f           = glVertex2f;
    qglVertex3f           = glVertex3f;
    qglVertex3fv          = glVertex3fv;

    qglActiveTextureARB       = (void(*)(GLenum))glActiveTexture;
    qglClientActiveTextureARB = (void(*)(GLenum))glClientActiveTexture;
    qglMultiTexCoord2fARB     = (void(*)(GLenum,GLfloat,GLfloat))glMultiTexCoord2f;

    /* CVA no-ops force primitives=2 (glDrawElements) — glArrayElement path unreliable in OpenGX. */
    qglLockArraysEXT   = noop_LockArrays;
    qglUnlockArraysEXT = noop_UnlockArrays;

#ifdef WII_DEBUG
    /* Diagnostic shims — last so they override the assignments above. */
    qglViewport = diag_Viewport;
    qglScissor  = diag_Scissor;
    qglOrtho    = diag_Ortho;
#endif

#endif /* !WII_NATIVE_GX */
}

void QGL_Shutdown(void)
{
}
