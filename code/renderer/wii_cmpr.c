/*
 * GX_TF_CMPR encoder / decoder. See wii_cmpr.h for design constraints and API.
 *
 * GX_TF_CMPR memory layout
 * ─────────────────────────
 * The texture is divided into (W/8) × (H/8) "super-tiles".
 * Each super-tile covers 8×8 pixels and holds four 4×4 DXT1 blocks:
 *
 *   ┌──────────┬──────────┐
 *   │ block 0  │ block 1  │   ← top row: rows 0-3 of super-tile
 *   │ (tx*8+0, │ (tx*8+4, │
 *   │  ty*8+0) │  ty*8+0) │
 *   ├──────────┼──────────┤
 *   │ block 2  │ block 3  │   ← bottom row: rows 4-7 of super-tile
 *   └──────────┴──────────┘
 *
 * Storage order within one super-tile: block0, block1, block2, block3 (32 B).
 * Super-tiles are stored left-to-right, top-to-bottom.
 *
 * Each 8-byte DXT1 block
 * ───────────────────────
 *   bytes 0-1 : endpoint e0, RGB565, big-endian  ← GX is PowerPC / big-endian
 *   bytes 2-3 : endpoint e1, RGB565, big-endian
 *   bytes 4-7 : 16 × 2-bit palette indices, 4 pixels per byte
 *               byte 4 = row 0 of the 4×4 block
 *               within each byte: bits [1:0] = column 0, bits [7:6] = column 3
 *
 * Palette construction (e0 > e1 → 4-colour mode)
 * ─────────────────────────────────────────────────
 *   c0 = expand(e0)
 *   c1 = expand(e1)
 *   c2 = (5*c0 + 3*c1) >> 3   ← 5/8 c0 + 3/8 c1  (Wii GX shift-based decode)
 *   c3 = (3*c0 + 5*c1) >> 3   ← 3/8 c0 + 5/8 c1
 *
 * Standard S3TC uses 2/3 + 1/3 (requires division by 3).  Wii GX hardware
 * approximates this with the shift-based 5/8 + 3/8 form.  The encoder uses
 * the same weights so palette reconstruction in the decoder matches exactly.
 */

#include "wii_cmpr.h"

#include <string.h>
#include <limits.h>

/*
 * pack_rgb565 — quantise RGB888 to RGB565 with round-to-nearest.
 *
 * Formula:  rN = round(r * N / 255) = (r*N + 127) / 255
 * This is within ½ LSB of the true floating-point value for all inputs.
 */
static uint16_t pack_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t r5 = (uint16_t)((r * 31u + 127u) / 255u);
    uint16_t g6 = (uint16_t)((g * 63u + 127u) / 255u);
    uint16_t b5 = (uint16_t)((b * 31u + 127u) / 255u);
    return (r5 << 11) | (g6 << 5) | b5;
}

/*
 * expand_rgb565 — convert RGB565 → RGB888, replicating the top bits
 * into the vacant low bits (the standard "bit replication" expansion).
 *
 *   r5 → r8 : (r5 << 3) | (r5 >> 2)   maps 0→0, 31→255
 *   g6 → g8 : (g6 << 2) | (g6 >> 4)   maps 0→0, 63→255
 *   b5 → b8 : (b5 << 3) | (b5 >> 2)   maps 0→0, 31→255
 */
static void expand_rgb565(uint16_t c, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t r5 = (uint8_t)((c >> 11) & 0x1Fu);
    uint8_t g6 = (uint8_t)((c >>  5) & 0x3Fu);
    uint8_t b5 = (uint8_t)( c        & 0x1Fu);
    *r = (uint8_t)((r5 << 3) | (r5 >> 2));
    *g = (uint8_t)((g6 << 2) | (g6 >> 4));
    *b = (uint8_t)((b5 << 3) | (b5 >> 2));
}

/* Big-endian I/O — GX expects big-endian RGB565 words */

static void write_u16be(uint8_t *dst, uint16_t v)
{
    dst[0] = (uint8_t)(v >> 8);
    dst[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t read_u16be(const uint8_t *src)
{
    return ((uint16_t)src[0] << 8) | (uint16_t)src[1];
}

/*
 * encode_dxt1_block — encode one 4×4 RGB888 region into 8 bytes of DXT1.
 *
 *   rgb    pointer to the top-left pixel of the block
 *   stride bytes between the start of consecutive rows in the source image
 *   out    8-byte output buffer
 *
 * Algorithm
 * ─────────
 * 1. Find the axis-aligned bounding box of the 16 pixel colours.
 * 2. Quantise the max corner → e0, min corner → e1 (round-to-nearest RGB565).
 * 3. Guarantee e0 > e1 so GX uses 4-colour mode (bump apart if equal).
 * 4. Expand both endpoints to RGB888 and compute the 3/8+5/8 palette.
 * 5. Assign each pixel to its nearest palette entry (min squared RGB error).
 * 6. Pack: [e0 BE][e1 BE][row0 indices][row1][row2][row3].
 */
static void encode_dxt1_block(const uint8_t *rgb, int stride, uint8_t *out)
{
    /* 1. Bounding box */
    uint8_t rmin = 255u, gmin = 255u, bmin = 255u;
    uint8_t rmax =   0u, gmax =   0u, bmax =   0u;

    for (int y = 0; y < 4; ++y) {
        const uint8_t *row = rgb + y * stride;
        for (int x = 0; x < 4; ++x) {
            const uint8_t *p = row + x * 3;
            if (p[0] < rmin) rmin = p[0];
            if (p[0] > rmax) rmax = p[0];
            if (p[1] < gmin) gmin = p[1];
            if (p[1] > gmax) gmax = p[1];
            if (p[2] < bmin) bmin = p[2];
            if (p[2] > bmax) bmax = p[2];
        }
    }

    /* 2. Quantise endpoints to RGB565 (round-to-nearest) */
    uint16_t e0 = pack_rgb565(rmax, gmax, bmax);
    uint16_t e1 = pack_rgb565(rmin, gmin, bmin);

    /* 3. Guarantee 4-colour mode: e0 must be strictly greater than e1 */
    if (e0 < e1) { uint16_t t = e0; e0 = e1; e1 = t; }
    if (e0 == e1) {
        if (e0 < 0xFFFFu) { e0++; } else { e1--; }
    }

    /* 4. Build 4-colour palette in RGB888 (3/8 + 5/8 interpolation) */
    uint8_t pr[4], pg[4], pb[4];
    expand_rgb565(e0, &pr[0], &pg[0], &pb[0]);   /* c0 */
    expand_rgb565(e1, &pr[1], &pg[1], &pb[1]);   /* c1 */

    /* c2 = 5/8 * c0 + 3/8 * c1  (matches Wii GX hardware decode) */
    pr[2] = (uint8_t)((5u * pr[0] + 3u * pr[1]) >> 3);
    pg[2] = (uint8_t)((5u * pg[0] + 3u * pg[1]) >> 3);
    pb[2] = (uint8_t)((5u * pb[0] + 3u * pb[1]) >> 3);

    /* c3 = 3/8 * c0 + 5/8 * c1 */
    pr[3] = (uint8_t)((3u * pr[0] + 5u * pr[1]) >> 3);
    pg[3] = (uint8_t)((3u * pg[0] + 5u * pg[1]) >> 3);
    pb[3] = (uint8_t)((3u * pb[0] + 5u * pb[1]) >> 3);

    /* 5. Assign pixels to nearest palette entry */
    uint8_t idx_bytes[4] = {0u, 0u, 0u, 0u};

    for (int y = 0; y < 4; ++y) {
        const uint8_t *row = rgb + y * stride;
        uint8_t row_bits = 0u;
        for (int x = 0; x < 4; ++x) {
            const uint8_t *p = row + x * 3;
            int best = 0, best_err = INT_MAX;
            for (int k = 0; k < 4; ++k) {
                int dr = (int)p[0] - (int)pr[k];
                int dg = (int)p[1] - (int)pg[k];
                int db = (int)p[2] - (int)pb[k];
                int err = dr*dr + dg*dg + db*db;
                if (err < best_err) { best_err = err; best = k; }
            }
            /* bits [x*2+1 : x*2] of this byte hold the index for column x */
            row_bits |= (uint8_t)(best << (x * 2));
        }
        idx_bytes[y] = row_bits;
    }

    /* 6. Write 8-byte block */
    write_u16be(out,     e0);
    write_u16be(out + 2, e1);
    out[4] = idx_bytes[0];
    out[5] = idx_bytes[1];
    out[6] = idx_bytes[2];
    out[7] = idx_bytes[3];
}

void CMPR_Encode(const uint8_t *rgb, int width, int height, uint8_t *out)
{
    int stx        = width  / 8;   /* super-tiles across */
    int sty        = height / 8;   /* super-tiles down   */
    int row_stride = width  * 3;   /* bytes per source row */

    for (int ty = 0; ty < sty; ++ty) {
        for (int tx = 0; tx < stx; ++tx) {
            /*
             * Four 4×4 blocks per 8×8 super-tile, row-major:
             *   (bx=0,by=0) TL  →  (bx=1,by=0) TR
             *   (bx=0,by=1) BL  →  (bx=1,by=1) BR
             */
            for (int by = 0; by < 2; ++by) {
                for (int bx = 0; bx < 2; ++bx) {
                    int px = tx * 8 + bx * 4;
                    int py = ty * 8 + by * 4;
                    const uint8_t *origin = rgb + py * row_stride + px * 3;
                    encode_dxt1_block(origin, row_stride, out);
                    out += 8;
                }
            }
        }
    }
}

/* Decoder used by smoke test; not called on Wii at runtime */
void CMPR_Decode(const uint8_t *cmpr, int width, int height, uint8_t *out_rgb)
{
    int stx        = width  / 8;
    int sty        = height / 8;
    int row_stride = width  * 3;

    for (int ty = 0; ty < sty; ++ty) {
        for (int tx = 0; tx < stx; ++tx) {
            for (int by = 0; by < 2; ++by) {
                for (int bx = 0; bx < 2; ++bx) {
                    uint16_t e0 = read_u16be(cmpr);
                    uint16_t e1 = read_u16be(cmpr + 2);

                    /* Build palette — same weights as encoder */
                    uint8_t pr[4], pg[4], pb[4];
                    expand_rgb565(e0, &pr[0], &pg[0], &pb[0]);
                    expand_rgb565(e1, &pr[1], &pg[1], &pb[1]);

                    if (e0 > e1) {
                        /* 4-colour mode */
                        pr[2] = (uint8_t)((5u * pr[0] + 3u * pr[1]) >> 3);
                        pg[2] = (uint8_t)((5u * pg[0] + 3u * pg[1]) >> 3);
                        pb[2] = (uint8_t)((5u * pb[0] + 3u * pb[1]) >> 3);
                        pr[3] = (uint8_t)((3u * pr[0] + 5u * pr[1]) >> 3);
                        pg[3] = (uint8_t)((3u * pg[0] + 5u * pg[1]) >> 3);
                        pb[3] = (uint8_t)((3u * pb[0] + 5u * pb[1]) >> 3);
                    } else {
                        /* 3-colour + transparent mode (should not occur for
                         * opaque textures, but handle defensively) */
                        pr[2] = (uint8_t)((pr[0] + pr[1]) / 2u);
                        pg[2] = (uint8_t)((pg[0] + pg[1]) / 2u);
                        pb[2] = (uint8_t)((pb[0] + pb[1]) / 2u);
                        pr[3] = pg[3] = pb[3] = 0u;
                    }

                    int ox = tx * 8 + bx * 4;
                    int oy = ty * 8 + by * 4;
                    for (int y = 0; y < 4; ++y) {
                        uint8_t ib = cmpr[4 + y];
                        for (int x = 0; x < 4; ++x) {
                            int idx = (ib >> (x * 2)) & 0x3;
                            uint8_t *dst = out_rgb + (oy + y) * row_stride + (ox + x) * 3;
                            dst[0] = pr[idx];
                            dst[1] = pg[idx];
                            dst[2] = pb[idx];
                        }
                    }

                    cmpr += 8;
                }
            }
        }
    }
}
