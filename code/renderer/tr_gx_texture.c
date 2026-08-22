/* Swizzles RGBA into 4x4-tiled RGB565/RGB5A3, fed Upload32's SCALED buffer
 * (full RGBA8 ate the whole heap once). memalign/free only, never the bump. */
#if defined(WII_NATIVE_GX)

#include "tr_local.h"
#include "tr_gx.h"
#include <malloc.h>    /* memalign, free */


GXTexObj  s_gx_texobjs[GX_MAX_TEXOBJS];
qboolean  s_gx_texobj_valid[GX_MAX_TEXOBJS];
void     *s_gx_texbufs[GX_MAX_TEXOBJS];
int       s_gx_next_texnum = 0;

/* GX texture format per slot (GX_TF_RGB565 / GX_TF_RGB5A3), needed by
 * GXBE_TexSubImage2D to re-swizzle with the same format. */
static u8 s_gx_texfmt[GX_MAX_TEXOBJS];

/* 16-bit swizzles: 4x4-texel tiles of 32 bytes; u16 per texel, native big-endian. */

/* Round an 8-bit channel to `bits` bits (nearest, clamped) instead of the
 * truncating shift GX's 16-bit formats would otherwise get from a plain
 * >>. Truncation biases every channel dark and, for RGB5A3's 3-bit alpha,
 * discards up to ~6% of the alpha range on every translucent texel. */
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

/* GXBE_Upload32: swizzle Upload32's final scaled buffer into GX tile format.
 * When mipmap=true, data is box-reduced IN PLACE level-by-level (caller owns it). */

void GXBE_Upload32(unsigned *data, int width, int height,
                   int glInternalFormat, int texnum, int wrapClampMode,
                   qboolean mipmap)
{
    int   size, numLevels, lvl, w, h, offset;
    byte *buf;
    u8    wrap_s, wrap_t, fmt;
    int   tmu;

    if (texnum < 0 || texnum >= GX_MAX_TEXOBJS)
        return;

    /* Free any previous buffer for this slot (e.g. re-upload) */
    if (s_gx_texbufs[texnum]) {
        free(s_gx_texbufs[texnum]);
        s_gx_texbufs[texnum] = NULL;
        s_gx_texobj_valid[texnum] = qfalse;
    }

    fmt = gx_format_has_alpha(glInternalFormat) ? GX_TF_RGB5A3 : GX_TF_RGB565;

    /* Count levels and total (tile-padded) buffer size */
    numLevels = 1;
    size = gx_tex16_size(width, height);
    if (mipmap) {
        w = width;  h = height;
        while (w > 1 || h > 1) {
            w = (w > 1) ? (w >> 1) : 1;
            h = (h > 1) ? (h >> 1) : 1;
            size += gx_tex16_size(w, h);
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
    if (fmt == GX_TF_RGB5A3)
        gx_swizzle_rgb5a3(buf, data, width, height);
    else
        gx_swizzle_rgb565(buf, data, width, height);
    offset = gx_tex16_size(width, height);

    w = width;  h = height;
    for (lvl = 1; lvl < numLevels; lvl++) {
        R_MipMap((byte *)data, w, h);
        w = (w > 1) ? (w >> 1) : 1;
        h = (h > 1) ? (h >> 1) : 1;
        if (fmt == GX_TF_RGB5A3)
            gx_swizzle_rgb5a3(buf + offset, data, w, h);
        else
            gx_swizzle_rgb565(buf + offset, data, w, h);
        offset += gx_tex16_size(w, h);
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

    size = gx_tex16_size(fullW, fullH);
    buf  = s_gx_texbufs[texnum];
    if (!buf) {
        buf = memalign(32, size);
        if (!buf) return;
        s_gx_texbufs[texnum] = buf;
    }

    if (s_gx_texfmt[texnum] == GX_TF_RGB5A3)
        gx_swizzle_rgb5a3(buf, data, fullW, fullH);
    else
        gx_swizzle_rgb565(buf, data, fullW, fullH);
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
    int  top, cx, ctop, cw, ch, dstWd, tilesW;
    int  ox, oy, i, j, size;
    u8  *buf;

    if (w <= 0 || h <= 0)
        return;

    /* GL bottom-left origin -> EFB top-down line of the region's top row */
    top = glConfig.vidHeight - (y + h);

    /* EFB copy requires even coords/dims; widen to satisfy and track offset. */
    cx   = x & ~1;
    ctop = top & ~1;
    cw   = (w + (x - cx) + 1) & ~1;
    ch   = (h + (top - ctop) + 1) & ~1;
    ox   = x - cx;
    oy   = top - ctop;
    if (cx + cw > glConfig.vidWidth)   cw = (glConfig.vidWidth - cx) & ~1;
    if (ctop + ch > glConfig.vidHeight) ch = (glConfig.vidHeight - ctop) & ~1;

    dstWd  = (cw + 3) & ~3;
    tilesW = dstWd >> 2;
    size   = tilesW * ((ch + 3) >> 2) * 64;

    buf = memalign(32, size);
    if (!buf) {
        static qboolean s_warned;
        if (!s_warned) {
            s_warned = qtrue;
            ri.Printf(PRINT_WARNING, "GXBE_ReadPixelsRGB: memalign(%d) failed\n", size);
        }
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

    /* De-tile into GL bottom-up RGB rows */
    for (j = 0; j < h; j++) {
        int       gy      = oy + (h - 1 - j);     /* EFB row, top-down */
        byte     *out     = dst + j * (w * 3 + padlen);
        const u8 *tileRow = buf + (gy >> 2) * tilesW * 64 + (gy & 3) * 8;

        for (i = 0; i < w; i++) {
            int       gx   = ox + i;
            const u8 *tile = tileRow + (gx >> 2) * 64;
            int       t    = (gx & 3) * 2;

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
