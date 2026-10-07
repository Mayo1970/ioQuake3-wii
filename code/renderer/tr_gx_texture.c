/* Swizzles RGBA into tiled RGB565/RGB5A3/CMPR, fed Upload32's SCALED buffer
 * (full RGBA8 ate the whole heap once). memalign/free only, never the bump. */
#if defined(WII_NATIVE_GX)

#include "tr_local.h"
#include "tr_gx.h"
#include "wii_glimp.h" /* Wii_GX_GetEFBHeight */
#include <malloc.h>    /* memalign, free */
#include <math.h>


GXTexObj  s_gx_texobjs[GX_MAX_TEXOBJS];
qboolean  s_gx_texobj_valid[GX_MAX_TEXOBJS];
void     *s_gx_texbufs[GX_MAX_TEXOBJS];
int       s_gx_next_texnum = 0;

/* GX texture format per slot, needed by GXBE_TexSubImage2D to re-encode the same way. */
static u8 s_gx_texfmt[GX_MAX_TEXOBJS];

/* Set by R_CreateImage for the next GXBE_Upload32 only; cinematic re-uploads leave it unset. */
static char     s_gx_hintName[MAX_QPATH];
static int      s_gx_hintFlags;
static qboolean s_gx_hintValid;

/* 16-bit swizzles: 4x4-texel tiles of 32 bytes; u16 per texel, native big-endian. */

/* Nearest rounding, clamped: a plain >> biases every channel dark and costs RGB5A3's
 * 3-bit alpha up to ~6% of its range. */
static inline u32 gx_round_bits(u8 v, int bits)
{
    int shift = 8 - bits;
    int half  = 1 << (shift - 1);
    int max   = (1 << bits) - 1;
    int q     = (v + half) >> shift;
    return (u32)((q > max) ? max : q);
}

static void gx_swizzle_rgb565(void *dst, const void *src, int w, int h)
{
    const u8 *s = (const u8 *)src;
    u16      *d = (u16 *)dst;
    int bw = (w + 3) >> 2;
    int bh = (h + 3) >> 2;
    int bx, by, py, px;

    for (by = 0; by < bh; by++) {
        for (bx = 0; bx < bw; bx++) {
            for (py = 0; py < 4; py++) {
                for (px = 0; px < 4; px++) {
                    int sx = bx * 4 + px;
                    int sy = by * 4 + py;
                    u16 v = 0;
                    if (sx < w && sy < h) {
                        const u8 *p = s + (sy * w + sx) * 4;
                        v = (u16)((gx_round_bits(p[0], 5) << 11) |
                                  (gx_round_bits(p[1], 6) << 5)  |
                                   gx_round_bits(p[2], 5));
                    }
                    *d++ = v;
                }
            }
        }
    }
}

/* RGB5A3: MSB set   -> 0x8000 | RGB555 (opaque texel)
 *         MSB clear -> 3-bit alpha + RGB444 */
static void gx_swizzle_rgb5a3(void *dst, const void *src, int w, int h)
{
    const u8 *s = (const u8 *)src;
    u16      *d = (u16 *)dst;
    int bw = (w + 3) >> 2;
    int bh = (h + 3) >> 2;
    int bx, by, py, px;

    for (by = 0; by < bh; by++) {
        for (bx = 0; bx < bw; bx++) {
            for (py = 0; py < 4; py++) {
                for (px = 0; px < 4; px++) {
                    int sx = bx * 4 + px;
                    int sy = by * 4 + py;
                    u16 v = 0;
                    if (sx < w && sy < h) {
                        const u8 *p = s + (sy * w + sx) * 4;
                        if (p[3] >= 224) {
                            v = (u16)(0x8000 |
                                      (gx_round_bits(p[0], 5) << 10) |
                                      (gx_round_bits(p[1], 5) << 5)  |
                                       gx_round_bits(p[2], 5));
                        } else {
                            v = (u16)((gx_round_bits(p[3], 3) << 12) |
                                      (gx_round_bits(p[0], 4) << 8)  |
                                      (gx_round_bits(p[1], 4) << 4)  |
                                       gx_round_bits(p[2], 4));
                        }
                    }
                    *d++ = v;
                }
            }
        }
    }
}

/* Size in bytes of a swizzled 16-bit mip level. */
static int gx_tex16_size(int w, int h)
{
    int bw = (w + 3) >> 2;
    int bh = (h + 3) >> 2;
    return bw * bh * 32; /* 32 bytes per 4x4 tile */
}

/* CMPR is DXT1 in 8x8 tiles of four 4x4 blocks (TL, TR, BL, BR): big-endian RGB565
 * endpoints, MSB-first 2-bit indices, and GX blends at 5/8-3/8 instead of 2/3-1/3. */
#define GX_CMPR_POWER_ITERATIONS 4
/* Below this the block has no usable principal axis; stb_dxt then uses luminance weights. */
#define GX_CMPR_MIN_AXIS 4.0f

static int gx_cmpr_size(int w, int h)
{
    return ((w + 7) >> 3) * ((h + 7) >> 3) * 32;
}

static u16 gx_cmpr_pack(const float *rgb)
{
    static const float scale[3] = { 31.0f / 255.0f, 63.0f / 255.0f, 31.0f / 255.0f };
    static const int   top[3]   = { 31, 63, 31 };
    int value[3];
    int k;

    for (k = 0; k < 3; k++) {
        value[k] = (int)(rgb[k] * scale[k] + 0.5f);
        if (value[k] < 0)
            value[k] = 0;
        else if (value[k] > top[k])
            value[k] = top[k];
    }
    return (u16)((value[0] << 11) | (value[1] << 5) | value[2]);
}

static void gx_cmpr_unpack(u16 color, int *rgb)
{
    int r = (color >> 11) & 31;
    int g = (color >> 5) & 63;
    int b = color & 31;

    rgb[0] = (r << 3) | (r >> 2);
    rgb[1] = (g << 2) | (g >> 4);
    rgb[2] = (b << 3) | (b >> 2);
}

/* Palette index per texel as if c0 > c1, matching GX's blend; returns the squared error. */
static int gx_cmpr_indexes(const byte (*texels)[4], u16 c0, u16 c1, byte *indexes)
{
    int palette[4][3];
    int axis[3];
    int lengthSq;
    int error = 0;
    int i, k;

    gx_cmpr_unpack(c0, palette[0]);
    gx_cmpr_unpack(c1, palette[1]);
    for (k = 0; k < 3; k++) {
        palette[2][k] = (palette[0][k] * 5 + palette[1][k] * 3) >> 3;
        palette[3][k] = (palette[0][k] * 3 + palette[1][k] * 5) >> 3;
        axis[k] = palette[0][k] - palette[1][k];
    }
    lengthSq = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
    for (i = 0; i < 16; i++) {
        int p = 0;

        if (lengthSq) {
            /* 16x the position on the line; entries 1, 3, 2, 0 sit at 0, 6, 10 and 16. */
            int step = 16 * ((texels[i][0] - palette[1][0]) * axis[0] +
                             (texels[i][1] - palette[1][1]) * axis[1] +
                             (texels[i][2] - palette[1][2]) * axis[2]);

            p = step < 3 * lengthSq ? 1 : step < 8 * lengthSq ? 3 :
                step < 13 * lengthSq ? 2 : 0;
        }
        for (k = 0; k < 3; k++) {
            int d = texels[i][k] - palette[p][k];

            error += d * d;
        }
        indexes[i] = (byte)p;
    }
    return error;
}

/* The two texels farthest apart along the colour covariance's principal axis (stb_dxt). */
static void gx_cmpr_endpoints(const byte (*texels)[4], float *high, float *low)
{
    float mean[3] = { 0.0f, 0.0f, 0.0f };
    float cov[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float axis[3];
    int   minColor[3] = { 255, 255, 255 };
    int   maxColor[3] = { 0, 0, 0 };
    float minDot = 1e30f, maxDot = -1e30f;
    int   minTexel = 0, maxTexel = 0;
    int   i, k;

    for (i = 0; i < 16; i++) {
        for (k = 0; k < 3; k++) {
            mean[k] += texels[i][k];
            if (texels[i][k] < minColor[k])
                minColor[k] = texels[i][k];
            if (texels[i][k] > maxColor[k])
                maxColor[k] = texels[i][k];
        }
    }
    for (k = 0; k < 3; k++)
        mean[k] *= 1.0f / 16.0f;
    for (i = 0; i < 16; i++) {
        float r = texels[i][0] - mean[0];
        float g = texels[i][1] - mean[1];
        float b = texels[i][2] - mean[2];

        cov[0] += r * r;
        cov[1] += r * g;
        cov[2] += r * b;
        cov[3] += g * g;
        cov[4] += g * b;
        cov[5] += b * b;
    }
    for (k = 0; k < 3; k++)
        axis[k] = (float)(maxColor[k] - minColor[k]);
    for (i = 0; i < GX_CMPR_POWER_ITERATIONS; i++) {
        float x = axis[0] * cov[0] + axis[1] * cov[1] + axis[2] * cov[2];
        float y = axis[0] * cov[1] + axis[1] * cov[3] + axis[2] * cov[4];
        float z = axis[0] * cov[2] + axis[1] * cov[4] + axis[2] * cov[5];
        float largest = fabsf(x);

        if (fabsf(y) > largest)
            largest = fabsf(y);
        if (fabsf(z) > largest)
            largest = fabsf(z);
        if (largest < GX_CMPR_MIN_AXIS) {
            axis[0] = 0.299f;
            axis[1] = 0.587f;
            axis[2] = 0.114f;
            break;
        }
        axis[0] = x / largest;
        axis[1] = y / largest;
        axis[2] = z / largest;
    }
    for (i = 0; i < 16; i++) {
        float d = texels[i][0] * axis[0] + texels[i][1] * axis[1] + texels[i][2] * axis[2];

        if (d < minDot) {
            minDot = d;
            minTexel = i;
        }
        if (d > maxDot) {
            maxDot = d;
            maxTexel = i;
        }
    }
    for (k = 0; k < 3; k++) {
        high[k] = texels[maxTexel][k];
        low[k] = texels[minTexel][k];
    }
}

/* Least-squares endpoints for fixed indexes (stb_dxt RefineBlock); qfalse when degenerate. */
static qboolean gx_cmpr_refine(const byte (*texels)[4], const byte *indexes,
                               float *high, float *low)
{
    static const float weights[4] = { 1.0f, 0.0f, 5.0f / 8.0f, 3.0f / 8.0f };
    float aa = 0.0f, ab = 0.0f, bb = 0.0f;
    float ax[3] = { 0.0f, 0.0f, 0.0f };
    float bx[3] = { 0.0f, 0.0f, 0.0f };
    float det;
    int   i, k;

    for (i = 0; i < 16; i++) {
        float a = weights[indexes[i]];
        float b = 1.0f - a;

        aa += a * a;
        ab += a * b;
        bb += b * b;
        for (k = 0; k < 3; k++) {
            ax[k] += a * texels[i][k];
            bx[k] += b * texels[i][k];
        }
    }
    det = aa * bb - ab * ab;
    if (fabsf(det) < 1e-6f)
        return qfalse;
    det = 1.0f / det;
    for (k = 0; k < 3; k++) {
        high[k] = (bb * ax[k] - ab * bx[k]) * det;
        low[k]  = (aa * bx[k] - ab * ax[k]) * det;
    }
    return qtrue;
}

static void gx_cmpr_block(const byte (*texels)[4], u8 *out)
{
    float high[3], low[3];
    byte  indexes[16], refined[16];
    u16   c0, c1;
    int   error, row, i;

    gx_cmpr_endpoints(texels, high, low);
    c0 = gx_cmpr_pack(high);
    c1 = gx_cmpr_pack(low);
    error = gx_cmpr_indexes(texels, c0, c1, indexes);
    if (error && gx_cmpr_refine(texels, indexes, high, low)) {
        u16 r0 = gx_cmpr_pack(high);
        u16 r1 = gx_cmpr_pack(low);

        if (gx_cmpr_indexes(texels, r0, r1, refined) < error) {
            c0 = r0;
            c1 = r1;
            Com_Memcpy(indexes, refined, sizeof(indexes));
        }
    }
    /* Four-colour mode needs c0 > c1; with equal endpoints index 3 would be transparent. */
    if (c0 < c1) {
        u16 swap = c0;

        c0 = c1;
        c1 = swap;
        for (i = 0; i < 16; i++)
            indexes[i] ^= 1;
    } else if (c0 == c1) {
        Com_Memset(indexes, 0, sizeof(indexes));
    }
    out[0] = (u8)(c0 >> 8);
    out[1] = (u8)c0;
    out[2] = (u8)(c1 >> 8);
    out[3] = (u8)c1;
    for (row = 0; row < 4; row++)
        out[4 + row] = (u8)((indexes[row * 4] << 6) | (indexes[row * 4 + 1] << 4) |
                            (indexes[row * 4 + 2] << 2) | indexes[row * 4 + 3]);
}

/* Texels past the image edge (levels under 8x8) repeat the edge; GX never samples them. */
static void gx_cmpr_encode(void *dst, const void *src, int w, int h)
{
    const u8 *s = (const u8 *)src;
    u8       *d = (u8 *)dst;
    byte      texels[16][4];
    int       tx, ty, sub, px, py;

    for (ty = 0; ty < h; ty += 8) {
        for (tx = 0; tx < w; tx += 8) {
            for (sub = 0; sub < 4; sub++) {
                int bx = tx + (sub & 1) * 4;
                int by = ty + (sub >> 1) * 4;

                for (py = 0; py < 4; py++) {
                    int sy = (by + py < h) ? by + py : h - 1;

                    for (px = 0; px < 4; px++) {
                        int sx = (bx + px < w) ? bx + px : w - 1;

                        Com_Memcpy(texels[py * 4 + px], s + (sy * w + sx) * 4, 4);
                    }
                }
                gx_cmpr_block((const byte (*)[4])texels, d);
                d += 8;
            }
        }
    }
}

static int gx_level_size(int w, int h, u8 fmt)
{
    return (fmt == GX_TF_CMPR) ? gx_cmpr_size(w, h) : gx_tex16_size(w, h);
}

static void gx_encode_level(void *dst, const void *src, int w, int h, u8 fmt)
{
    if (fmt == GX_TF_CMPR)
        gx_cmpr_encode(dst, src, w, h);
    else if (fmt == GX_TF_RGB5A3)
        gx_swizzle_rgb5a3(dst, src, w, h);
    else
        gx_swizzle_rgb565(dst, src, w, h);
}

/* TA fonts stay 16-bit: CMPR blocks would step their antialiased glyph edges. */
static qboolean gx_is_text_art(const char *name)
{
    return !Q_stricmpn(name, "fonts/", 6) || !Q_stricmpn(name, "menu/art/font", 13) ||
           !Q_stricmpn(name, "gfx/2d/bigchars", 15);
}

/* The Xbox port's storage policy: picmip'd art and big 2D art compress. GX has no
 * DXT5, so alpha stays RGB5A3; builtins (*lightmap, *scratch...) stay 16-bit too. */
static qboolean gx_want_cmpr(qboolean alpha, qboolean mipmap, int w, int h)
{
    if (!s_gx_hintValid || alpha || w < 8 || h < 8)
        return qfalse;
    if (s_gx_hintName[0] == '*' || (s_gx_hintFlags & IMGFLAG_NO_COMPRESSION))
        return qfalse;
#if defined(STANDALONETA)
    /* TA menu art stays loaded in a match: 2D from 128 px compresses too. */
    if (!mipmap && w >= 128 && h >= 128 && !gx_is_text_art(s_gx_hintName))
        return qtrue;
#endif
    if (s_gx_hintFlags & IMGFLAG_PICMIP)
        return qtrue;
    return !mipmap && w >= 256 && h >= 256;
}

/* Does the GL internal format Upload32 chose carry alpha? */
static qboolean gx_format_has_alpha(int glInternalFormat)
{
    switch (glInternalFormat) {
    case GL_RGBA:
    case GL_RGBA8:
    case GL_RGBA4:
    case GL_LUMINANCE_ALPHA:
    case GL_LUMINANCE8_ALPHA8:
        return qtrue;
    default:
        return qfalse;
    }
}

/* GXBE_CreateTexnum: replaces qglGenTextures with a 0-based counter. */

void GXBE_CreateTexnum(GLuint *texnum)
{
    if (s_gx_next_texnum >= GX_MAX_TEXOBJS) {
        ri.Error(ERR_DROP, "GXBE_CreateTexnum: GX_MAX_TEXOBJS exceeded");
        return;
    }
    *texnum = (GLuint)s_gx_next_texnum++;
}

/* GXBE_DeleteTexnum: frees the swizzled buffer and marks the slot invalid. */

void GXBE_DeleteTexnum(int texnum)
{
    if (texnum < 0 || texnum >= GX_MAX_TEXOBJS)
        return;

    if (s_gx_texbufs[texnum]) {
        free(s_gx_texbufs[texnum]);
        s_gx_texbufs[texnum] = NULL;
    }
    s_gx_texobj_valid[texnum] = qfalse;

    /* Clear cached binding so the next use forces a re-load */
    if (gxState.boundtex[0] == texnum) gxState.boundtex[0] = -1;
    if (gxState.boundtex[1] == texnum) gxState.boundtex[1] = -1;
}

/* Name and IMGFLAG_* of the image the next GXBE_Upload32 stores; picks CMPR vs 16-bit. */
void GXBE_SetUploadImage(const char *name, int flags)
{
    Q_strncpyz(s_gx_hintName, name, sizeof(s_gx_hintName));
    s_gx_hintFlags = flags;
    s_gx_hintValid = qtrue;
}

/* GXBE_Upload32: swizzle Upload32's final scaled buffer into GX tile format.
 * When mipmap=true, data is box-reduced IN PLACE level-by-level (caller owns it). */

void GXBE_Upload32(unsigned *data, int width, int height,
                   int glInternalFormat, int texnum, int wrapClampMode,
                   qboolean mipmap)
{
    int      size, numLevels, lvl, w, h, offset;
    byte    *buf;
    u8       wrap_s, wrap_t, fmt;
    int      tmu;
    qboolean alpha, cmpr;

    alpha = gx_format_has_alpha(glInternalFormat);
    cmpr  = gx_want_cmpr(alpha, mipmap, width, height);
    s_gx_hintValid = qfalse;

    if (texnum < 0 || texnum >= GX_MAX_TEXOBJS)
        return;

    /* Free any previous buffer for this slot (e.g. re-upload) */
    if (s_gx_texbufs[texnum]) {
        free(s_gx_texbufs[texnum]);
        s_gx_texbufs[texnum] = NULL;
        s_gx_texobj_valid[texnum] = qfalse;
    }

    if (cmpr)
        fmt = GX_TF_CMPR;
    else
        fmt = alpha ? GX_TF_RGB5A3 : GX_TF_RGB565;

    /* Count levels and total (tile-padded) buffer size */
    numLevels = 1;
    size = gx_level_size(width, height, fmt);
    if (mipmap) {
        w = width;  h = height;
        while (w > 1 || h > 1) {
            w = (w > 1) ? (w >> 1) : 1;
            h = (h > 1) ? (h >> 1) : 1;
            size += gx_level_size(w, h, fmt);
            numLevels++;
        }
    }

    /* GX texture data must be 32-byte aligned */
    buf = memalign(32, size);
    if (!buf) {
        ri.Printf(PRINT_WARNING, "GXBE_Upload32: memalign(%d) failed for texnum %d\n",
                  size, texnum);
        return;
    }

    /* Level 0, then box-reduce `data` in place for each successive level */
    gx_encode_level(buf, data, width, height, fmt);
    offset = gx_level_size(width, height, fmt);

    w = width;  h = height;
    for (lvl = 1; lvl < numLevels; lvl++) {
        R_MipMap((byte *)data, w, h);
        w = (w > 1) ? (w >> 1) : 1;
        h = (h > 1) ? (h >> 1) : 1;
        gx_encode_level(buf + offset, data, w, h, fmt);
        offset += gx_level_size(w, h, fmt);
    }
    DCFlushRange(buf, (u32)size);

    /* Wrap mode */
    wrap_s = (wrapClampMode == GL_REPEAT) ? GX_REPEAT : GX_CLAMP;
    wrap_t = wrap_s;

    GX_InitTexObj(&s_gx_texobjs[texnum], buf, (u16)width, (u16)height,
                  fmt, wrap_s, wrap_t, mipmap ? GX_TRUE : GX_FALSE);
    if (mipmap) {
        /* Trilinear: matches r_textureMode's GL_LINEAR_MIPMAP_LINEAR default */
        GX_InitTexObjLOD(&s_gx_texobjs[texnum],
                         GX_LIN_MIP_LIN, GX_LINEAR, /* min/mag filter */
                         0.0f, (f32)(numLevels - 1),/* min/max LOD */
                         0.0f,                      /* LOD bias */
                         GX_FALSE, GX_FALSE,        /* bias clamp, edge LOD */
                         GX_ANISO_1);
    } else {
        GX_InitTexObjLOD(&s_gx_texobjs[texnum],
                         GX_LINEAR, GX_LINEAR,   /* min/mag filter */
                         0.0f, 0.0f,             /* min/max LOD */
                         0.0f,                   /* LOD bias */
                         GX_FALSE, GX_FALSE,     /* bias clamp, edge LOD */
                         GX_ANISO_1);
    }

    s_gx_texbufs[texnum]      = buf;
    s_gx_texfmt[texnum]       = fmt;
    s_gx_texobj_valid[texnum] = qtrue;

    /* If this texnum is currently bound, reload the GXTexObj in GX */
    for (tmu = 0; tmu < 2; tmu++) {
        if (gxState.boundtex[tmu] == texnum) {
            GX_LoadTexObj(&s_gx_texobjs[texnum],
                          tmu == 0 ? GX_TEXMAP0 : GX_TEXMAP1);
        }
    }
    /* Always invalidate the texture cache after any upload */
    GX_InvalidateTexAll();
}

/* GXBE_TexSubImage2D: re-swizzles the whole image; used by RE_UploadCinematic. */

void GXBE_TexSubImage2D(int texnum, int fullW, int fullH, const byte *data)
{
    int   size;
    void *buf;
    int   tmu;

    if (texnum < 0 || texnum >= GX_MAX_TEXOBJS || !s_gx_texobj_valid[texnum])
        return;

    size = gx_level_size(fullW, fullH, s_gx_texfmt[texnum]);
    buf  = s_gx_texbufs[texnum];
    if (!buf) {
        buf = memalign(32, size);
        if (!buf) return;
        s_gx_texbufs[texnum] = buf;
    }

    gx_encode_level(buf, data, fullW, fullH, s_gx_texfmt[texnum]);
    DCFlushRange(buf, (u32)size);
    GX_InvalidateTexAll();

    /* Reload if bound */
    for (tmu = 0; tmu < 2; tmu++) {
        if (gxState.boundtex[tmu] == texnum)
            GX_LoadTexObj(&s_gx_texobjs[texnum],
                          tmu == 0 ? GX_TEXMAP0 : GX_TEXMAP1);
    }
}

/* GXBE_ReadPixelsRGB: EFB→RGBA8 tex-copy, then de-tile into GL bottom-up RGB rows.
 * x/y are GL window coords; padlen pad bytes follow each row. Low-frequency path. */

void GXBE_ReadPixelsRGB(int x, int y, int w, int h, int padlen, byte *dst)
{
    int  efbW = glConfig.vidWidth, efbH = Wii_GX_GetEFBHeight();
    int  rx, ry, rw, rh, cx, ctop, cw, ch, dstWd, tilesW;
    int  i, j, size;
    u8  *buf = NULL;

    if (w <= 0 || h <= 0)
        return;

    /* GL bottom-left logical rect -> top-down -> EFB rect; output samples it nearest-neighbour,
     * which is 1:1 when the mapping is identity (no overscan border, 480-line mode). */
    rx = x;
    ry = glConfig.vidHeight - (y + h);
    rw = w;
    rh = h;
    GXBE_MapRect(&rx, &ry, &rw, &rh);

    /* EFB copy requires even coords/dims inside the EFB. */
    cx   = (rx < 0 ? 0 : rx) & ~1;
    ctop = (ry < 0 ? 0 : ry) & ~1;
    cw   = ((rx + rw > efbW ? efbW : rx + rw) - cx + 1) & ~1;
    ch   = ((ry + rh > efbH ? efbH : ry + rh) - ctop + 1) & ~1;
    if (cx + cw > efbW)   cw = (efbW - cx) & ~1;
    if (ctop + ch > efbH) ch = (efbH - ctop) & ~1;

    dstWd  = (cw + 3) & ~3;
    tilesW = dstWd >> 2;
    size   = tilesW * ((ch + 3) >> 2) * 64;

    if (rw > 0 && rh > 0 && cw > 0 && ch > 0) {
        buf = memalign(32, size);
        if (!buf) {
            static qboolean s_warned;
            if (!s_warned) {
                s_warned = qtrue;
                ri.Printf(PRINT_WARNING, "GXBE_ReadPixelsRGB: memalign(%d) failed\n", size);
            }
        }
    }
    if (!buf) {   /* out of memory, or the rect lies outside the EFB */
        for (j = 0; j < h; j++)
            Com_Memset(dst + j * (w * 3 + padlen), 0, w * 3);
        return;
    }

    /* Invalidate before GP writes — CPU never touches buf until after the copy. */
    DCInvalidateRange(buf, (u32)size);

    GX_DrawDone();

    GX_SetTexCopySrc((u16)cx, (u16)ctop, (u16)cw, (u16)ch);
    GX_SetTexCopyDst((u16)dstWd, (u16)ch, GX_TF_RGBA8, GX_FALSE);
    GX_CopyTex(buf, GX_FALSE);   /* GX_FALSE: do not clear the EFB */
    GX_PixModeSync();
    GX_DrawDone();               /* wait for the copy to reach main memory */

    /* De-tile into GL bottom-up RGB rows, sampling each output pixel's centre in the EFB rect. */
    for (j = 0; j < h; j++) {
        int       gy      = ry + (2 * (h - 1 - j) + 1) * rh / (2 * h);   /* EFB row, top-down */
        byte     *out     = dst + j * (w * 3 + padlen);
        const u8 *tileRow;

        gy = (gy < ctop ? ctop : gy > ctop + ch - 1 ? ctop + ch - 1 : gy) - ctop;
        tileRow = buf + (gy >> 2) * tilesW * 64 + (gy & 3) * 8;

        for (i = 0; i < w; i++) {
            int       gx   = rx + (2 * i + 1) * rw / (2 * w);
            const u8 *tile;
            int       t;

            gx   = (gx < cx ? cx : gx > cx + cw - 1 ? cx + cw - 1 : gx) - cx;
            tile = tileRow + (gx >> 2) * 64;
            t    = (gx & 3) * 2;

            *out++ = tile[t + 1];        /* R (from the A,R half-tile) */
            *out++ = tile[32 + t];       /* G (from the G,B half-tile) */
            *out++ = tile[32 + t + 1];   /* B */
        }
    }

    free(buf);
}

#else  /* !WII_NATIVE_GX */

typedef int tr_gx_texture_placeholder;

#endif /* WII_NATIVE_GX */
