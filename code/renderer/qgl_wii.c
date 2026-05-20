/* qgl_wii.c — Wire ioQ3's qgl* function pointers to OpenGX. */

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

void QGL_Init(void)
{
    /* ---- QGL_1_1_PROCS ---- */
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

    /* ---- QGL_1_1_FIXED_FUNCTION_PROCS ---- */
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

    /* ---- QGL_DESKTOP_1_1_PROCS ---- */
    qglClearDepth         = glClearDepth;
    qglDepthRange         = glDepthRange;
    qglDrawBuffer         = noop_DrawBuffer;
    qglPolygonMode        = noop_PolygonMode;

    /* ---- QGL_DESKTOP_1_1_FIXED_FUNCTION_PROCS ---- */
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

    /* ---- ARB multitexture ---- */
    qglActiveTextureARB       = (void(*)(GLenum))glActiveTexture;
    qglClientActiveTextureARB = (void(*)(GLenum))glClientActiveTexture;
    qglMultiTexCoord2fARB     = (void(*)(GLenum,GLfloat,GLfloat))glMultiTexCoord2f;

    /* CVA — no-op stubs force primitives=2 (glDrawElements) instead of the
     * glBegin/glEnd/glArrayElement path, which OpenGX doesn't handle reliably
     * for MD3 entity models. Mirrors the PS3 port's approach. */
    qglLockArraysEXT   = noop_LockArrays;
    qglUnlockArraysEXT = noop_UnlockArrays;

#ifdef WII_DEBUG
    /* Override with diagnostic shims — must be last to win over all above assignments */
    qglViewport = diag_Viewport;
    qglScissor  = diag_Scissor;
    qglOrtho    = diag_Ortho;
#endif
}

void QGL_Shutdown(void)
{
}
