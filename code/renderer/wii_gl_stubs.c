/* qgl* function-pointer definitions and platform GL stubs. */

#include <gccore.h>
#include <string.h>
#include "tr_local.h"

#define GLE(ret, name, ...) name##proc * qgl##name = NULL;
QGL_1_1_PROCS;
QGL_1_1_FIXED_FUNCTION_PROCS;
QGL_DESKTOP_1_1_PROCS;
QGL_DESKTOP_1_1_FIXED_FUNCTION_PROCS;
QGL_3_0_PROCS;
#undef GLE

void (APIENTRYP qglActiveTextureARB)      (GLenum texture) = NULL;
void (APIENTRYP qglClientActiveTextureARB)(GLenum texture) = NULL;
void (APIENTRYP qglMultiTexCoord2fARB)    (GLenum target, GLfloat s, GLfloat t) = NULL;
void (APIENTRYP qglLockArraysEXT)         (GLint first, GLsizei count) = NULL;
void (APIENTRYP qglUnlockArraysEXT)       (void) = NULL;

int qglMajorVersion = 1;
int qglMinorVersion = 1;
int qglesMajorVersion = 0;
int qglesMinorVersion = 0;

void GLimp_LogComment(char *comment)
{
    (void)comment;
}

void GLimp_Minimize(void)
{
}
