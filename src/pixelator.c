/*
 * pixelator.c  —  Pixel Image Generator
 * ========================================
 * Core image-processing algorithms compiled to WebAssembly via Emscripten.
 *
 * PIPELINE (pixelate_advanced):
 *   [1] Optional contrast stretch
 *   [2] Median-cut colour quantisation  → palette of N colours
 *   [3] Dithering  (none / Bayer 4×4 / Floyd-Steinberg)
 *   [4] Edge-aware darkening of silhouettes
 *
 * The pixel buffer is always RGBA (4 bytes per pixel).
 * Alpha is never modified.
 *
 * All functions operate on an already-small "pixel grid" image that
 * JavaScript produced by down-sampling the original.  After C processing
 * the tiny image is enlarged in JavaScript with nearest-neighbor scaling,
 * producing hard square pixels with no blur.
 */

#include <emscripten.h>
#include <stdlib.h>
#include <string.h>


/* ====================================================================
   HELPERS
   ==================================================================== */

/* Clamp an int to [0, 255] and return it as unsigned char */
static inline unsigned char clamp_byte(int v) {
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return (unsigned char)v;
}

/* Compute luminance (integer, range 0-255) using the BT.601 formula
   scaled by 1000 to avoid floats: 299*R + 587*G + 114*B */
static inline int luminance(unsigned char r, unsigned char g, unsigned char b) {
    return (299 * r + 587 * g + 114 * b) / 1000;
}

/*
 * Perceptual weighted colour distance (squared).
 * Weights: R=2, G=4, B=3  (human eyes are most sensitive to green).
 * Returns an integer – no sqrt needed for comparisons.
 */
static inline long colour_dist_sq(
    int r1, int g1, int b1,
    int r2, int g2, int b2)
{
    int dr = r1 - r2;
    int dg = g1 - g2;
    int db = b1 - b2;
    return 2L*dr*dr + 4L*dg*dg + 3L*db*db;
}


/* ====================================================================
   CONTRAST STRETCH
   ====================================================================
   Stretches each colour channel so that the darkest pixel maps to 0
   and the brightest maps to 255.  Controlled by a 0-100 strength value.
   At strength 0 nothing changes; at strength 100 the full stretch is
   applied.  Intermediate values blend between the original and stretched.
*/
static void contrast_stretch(
    unsigned char *pixels, int n_pixels, int strength)
{
    if (strength <= 0) return;

    /* Find min and max per channel */
    int minR = 255, maxR = 0;
    int minG = 255, maxG = 0;
    int minB = 255, maxB = 0;

    for (int i = 0; i < n_pixels; i++) {
        int r = pixels[i*4+0];
        int g = pixels[i*4+1];
        int b = pixels[i*4+2];
        if (r < minR) minR = r;  if (r > maxR) maxR = r;
        if (g < minG) minG = g;  if (g > maxG) maxG = g;
        if (b < minB) minB = b;  if (b > maxB) maxB = b;
    }

    /* Guard against flat channels */
    if (maxR == minR) { minR = 0; maxR = 255; }
    if (maxG == minG) { minG = 0; maxG = 255; }
    if (maxB == minB) { minB = 0; maxB = 255; }

    int rangeR = maxR - minR;
    int rangeG = maxG - minG;
    int rangeB = maxB - minB;

    /* Apply stretch blended by strength */
    for (int i = 0; i < n_pixels; i++) {
        int r = pixels[i*4+0];
        int g = pixels[i*4+1];
        int b = pixels[i*4+2];

        int sr = (r - minR) * 255 / rangeR;
        int sg = (g - minG) * 255 / rangeG;
        int sb = (b - minB) * 255 / rangeB;

        /* Blend: original + (stretched - original) * strength/100 */
        pixels[i*4+0] = clamp_byte(r + (sr - r) * strength / 100);
        pixels[i*4+1] = clamp_byte(g + (sg - g) * strength / 100);
        pixels[i*4+2] = clamp_byte(b + (sb - b) * strength / 100);
    }
}


/* ====================================================================
   GRAYSCALE CONVERSION
   ==================================================================== */
static void to_grayscale(unsigned char *pixels, int n_pixels)
{
    for (int i = 0; i < n_pixels; i++) {
        int gray = luminance(
            pixels[i*4+0],
            pixels[i*4+1],
            pixels[i*4+2]);
        pixels[i*4+0] = (unsigned char)gray;
        pixels[i*4+1] = (unsigned char)gray;
        pixels[i*4+2] = (unsigned char)gray;
    }
}


/* ====================================================================
   MEDIAN-CUT COLOUR QUANTISATION
   ====================================================================
   Builds a palette of `paletteSize` colours (must be a power of 2, max 64)
   by recursively splitting the colour space along its widest axis.

   After building the palette every pixel is mapped to the nearest
   palette entry using perceptual weighted distance.

   References: Paul Heckbert 1982 – "Color Image Quantisation for Frame
   Buffer Display", SIGGRAPH Proceedings.
*/

#define MAX_PALETTE  64
#define MAX_DEPTH     6   /* 2^6 = 64 leaves */

/* One entry in our colour palette */
typedef struct {
    unsigned char r, g, b;
} PaletteEntry;

/* A bucket of pixel indices used during median-cut */
typedef struct {
    int *indices;      /* pointer into a shared array of pixel indices */
    int  count;
    int  minR, maxR;
    int  minG, maxG;
    int  minB, maxB;
} Bucket;

/* Comparators for qsort */
static unsigned char *g_sort_pixels; /* set before each qsort call */

static int cmp_R(const void *a, const void *b) {
    int ia = *(const int*)a;
    int ib = *(const int*)b;
    return (int)g_sort_pixels[ia*4+0] - (int)g_sort_pixels[ib*4+0];
}
static int cmp_G(const void *a, const void *b) {
    int ia = *(const int*)a;
    int ib = *(const int*)b;
    return (int)g_sort_pixels[ia*4+1] - (int)g_sort_pixels[ib*4+1];
}
static int cmp_B(const void *a, const void *b) {
    int ia = *(const int*)a;
    int ib = *(const int*)b;
    return (int)g_sort_pixels[ia*4+2] - (int)g_sort_pixels[ib*4+2];
}

/*
 * Compute the bounding box (min/max RGB) for a bucket.
 */
static void bucket_bounds(
    Bucket *bk, unsigned char *pixels)
{
    int minR=255,maxR=0,minG=255,maxG=0,minB=255,maxB=0;
    for (int k = 0; k < bk->count; k++) {
        int i = bk->indices[k];
        int r = pixels[i*4+0];
        int g = pixels[i*4+1];
        int b = pixels[i*4+2];
        if (r<minR) minR=r; if (r>maxR) maxR=r;
        if (g<minG) minG=g; if (g>maxG) maxG=g;
        if (b<minB) minB=b; if (b>maxB) maxB=b;
    }
    bk->minR=minR; bk->maxR=maxR;
    bk->minG=minG; bk->maxG=maxG;
    bk->minB=minB; bk->maxB=maxB;
}

/*
 * median_cut()
 * Recursively splits buckets.  On return `buckets[0..numBuckets-1]`
 * are the final leaves; their count of leaves == paletteSize.
 */
static void median_cut(
    Bucket *buckets, int *numBuckets, int targetCount,
    unsigned char *pixels)
{
    while (*numBuckets < targetCount) {

        /* Find the bucket with the largest range on any axis */
        int bestIdx  = 0;
        int bestRange = 0;
        int bestAxis  = 0;   /* 0=R 1=G 2=B */

        for (int b = 0; b < *numBuckets; b++) {
            Bucket *bk = &buckets[b];
            int rR = bk->maxR - bk->minR;
            int rG = bk->maxG - bk->minG;
            int rB = bk->maxB - bk->minB;
            int mx = rR;
            int ax = 0;
            if (rG > mx) { mx = rG; ax = 1; }
            if (rB > mx) { mx = rB; ax = 2; }
            if (mx > bestRange && bk->count > 1) {
                bestRange = mx;
                bestIdx   = b;
                bestAxis  = ax;
            }
        }

        /* Cannot split further */
        if (bestRange == 0) break;

        Bucket *bk = &buckets[bestIdx];

        /* Sort bucket indices by the chosen axis */
        g_sort_pixels = pixels;
        if      (bestAxis == 0) qsort(bk->indices, bk->count, sizeof(int), cmp_R);
        else if (bestAxis == 1) qsort(bk->indices, bk->count, sizeof(int), cmp_G);
        else                    qsort(bk->indices, bk->count, sizeof(int), cmp_B);

        /* Split at median */
        int half = bk->count / 2;

        /* New bucket B (second half) — we reuse the tail of bk->indices */
        Bucket newBucket;
        newBucket.indices = bk->indices + half;
        newBucket.count   = bk->count - half;
        bucket_bounds(&newBucket, pixels);

        /* Shrink original bucket to first half */
        bk->count = half;
        bucket_bounds(bk, pixels);

        buckets[*numBuckets] = newBucket;
        (*numBuckets)++;
    }
}

/*
 * build_palette()
 * Runs median-cut and fills `palette[0..paletteSize-1]`.
 * Returns the number of palette entries actually produced.
 */
static int build_palette(
    unsigned char *pixels, int n_pixels,
    int paletteSize, PaletteEntry *palette)
{
    /* Clamp palette size */
    if (paletteSize <  2) paletteSize =  2;
    if (paletteSize > MAX_PALETTE) paletteSize = MAX_PALETTE;

    /* Build initial index array [0, 1, 2, ..., n_pixels-1] */
    int *all_indices = (int*)malloc(n_pixels * sizeof(int));
    if (!all_indices) return 0;
    for (int i = 0; i < n_pixels; i++) all_indices[i] = i;

    /* Allocate bucket array (max paletteSize buckets) */
    Bucket *buckets = (Bucket*)malloc(paletteSize * sizeof(Bucket));
    if (!buckets) { free(all_indices); return 0; }

    /* Initial single bucket = all pixels */
    buckets[0].indices = all_indices;
    buckets[0].count   = n_pixels;
    bucket_bounds(&buckets[0], pixels);

    int numBuckets = 1;

    /* Run median-cut to produce paletteSize buckets */
    median_cut(buckets, &numBuckets, paletteSize, pixels);

    /* For each leaf bucket, compute average colour → palette entry */
    for (int b = 0; b < numBuckets; b++) {
        Bucket *bk = &buckets[b];
        long sumR = 0, sumG = 0, sumB = 0;
        for (int k = 0; k < bk->count; k++) {
            int i = bk->indices[k];
            sumR += pixels[i*4+0];
            sumG += pixels[i*4+1];
            sumB += pixels[i*4+2];
        }
        palette[b].r = (unsigned char)(sumR / bk->count);
        palette[b].g = (unsigned char)(sumG / bk->count);
        palette[b].b = (unsigned char)(sumB / bk->count);
    }

    free(buckets);
    free(all_indices);

    return numBuckets;
}

/*
 * nearest_palette()
 * Finds the index of the palette entry closest to (r,g,b)
 * using perceptual weighted distance.
 */
static int nearest_palette(
    int r, int g, int b,
    PaletteEntry *palette, int paletteCount)
{
    int   best  = 0;
    long  bestD = colour_dist_sq(
        r, g, b,
        palette[0].r, palette[0].g, palette[0].b);

    for (int p = 1; p < paletteCount; p++) {
        long d = colour_dist_sq(
            r, g, b,
            palette[p].r, palette[p].g, palette[p].b);
        if (d < bestD) {
            bestD = d;
            best  = p;
        }
    }
    return best;
}


/* ====================================================================
   BAYER 4×4 ORDERED DITHERING
   ====================================================================
   The 4×4 Bayer matrix normalised to [-1, +1] range.
   We threshold-perturb each pixel colour before palette mapping.
   The perturbation is scaled by the "spread" factor (max 32 units per
   channel), which is a good middle-ground for pixel art.
*/
static const int BAYER_4[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 }
};

/*
 * apply_bayer_dithering()
 * Modifies pixels in-place so that each pixel is perturbed before
 * palette quantisation.  Spread controls the dither intensity (0-50).
 */
static void apply_bayer_dithering(
    unsigned char *pixels, int width, int height,
    PaletteEntry *palette, int paletteCount, int spread)
{
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 4;

            /*
             * Bayer threshold for this cell.
             * Matrix values 0-15; map to range [-spread, +spread].
             * threshold = (value / 15 - 0.5) * 2 * spread
             *           = (value * 2 * spread / 15) - spread
             */
            int bv = BAYER_4[y & 3][x & 3];
            int threshold = (bv * 2 * spread / 15) - spread;

            int r = (int)pixels[idx+0] + threshold;
            int g = (int)pixels[idx+1] + threshold;
            int b = (int)pixels[idx+2] + threshold;

            /* Clamp to [0,255] then snap to nearest palette colour */
            r = (r < 0) ? 0 : (r > 255) ? 255 : r;
            g = (g < 0) ? 0 : (g > 255) ? 255 : g;
            b = (b < 0) ? 0 : (b > 255) ? 255 : b;

            int pi = nearest_palette(r, g, b, palette, paletteCount);
            pixels[idx+0] = palette[pi].r;
            pixels[idx+1] = palette[pi].g;
            pixels[idx+2] = palette[pi].b;
        }
    }
}


/* ====================================================================
   FLOYD-STEINBERG ERROR DIFFUSION DITHERING
   ====================================================================
   Error kernel (fractions of 1):
              *  7/16
      3/16  5/16  1/16

   We use an integer error buffer (signed, per channel) to avoid floats.
   Error is stored × 16 and divided when applied for precision.
*/
static void apply_floyd_steinberg(
    unsigned char *pixels, int width, int height,
    PaletteEntry *palette, int paletteCount)
{
    /*
     * Allocate a signed error buffer: one int per channel per pixel.
     * Initialised to zero.  We accumulate errors × 16.
     */
    int *errR = (int*)calloc(width * height, sizeof(int));
    int *errG = (int*)calloc(width * height, sizeof(int));
    int *errB = (int*)calloc(width * height, sizeof(int));

    if (!errR || !errG || !errB) {
        /* Fallback: no dithering if allocation fails */
        free(errR); free(errG); free(errB);
        for (int i = 0; i < width * height; i++) {
            int idx = i * 4;
            int pi = nearest_palette(
                pixels[idx+0], pixels[idx+1], pixels[idx+2],
                palette, paletteCount);
            pixels[idx+0] = palette[pi].r;
            pixels[idx+1] = palette[pi].g;
            pixels[idx+2] = palette[pi].b;
        }
        return;
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int i   = y * width + x;
            int idx = i * 4;

            /*
             * Adjusted pixel value = original + accumulated error.
             * Error is stored ×16, so divide before using.
             */
            int r = (int)pixels[idx+0] + errR[i] / 16;
            int g = (int)pixels[idx+1] + errG[i] / 16;
            int b = (int)pixels[idx+2] + errB[i] / 16;

            r = (r < 0) ? 0 : (r > 255) ? 255 : r;
            g = (g < 0) ? 0 : (g > 255) ? 255 : g;
            b = (b < 0) ? 0 : (b > 255) ? 255 : b;

            /* Find nearest palette colour */
            int pi = nearest_palette(r, g, b, palette, paletteCount);
            unsigned char pr = palette[pi].r;
            unsigned char pg = palette[pi].g;
            unsigned char pb = palette[pi].b;

            /* Write quantised colour */
            pixels[idx+0] = pr;
            pixels[idx+1] = pg;
            pixels[idx+2] = pb;

            /* Quantisation error (× 16 for integer arithmetic) */
            int qeR = (r - pr) * 16;
            int qeG = (g - pg) * 16;
            int qeB = (b - pb) * 16;

            /*
             * Distribute error to four neighbours:
             *   right:       7/16
             *   below-left:  3/16
             *   below:       5/16
             *   below-right: 1/16
             */
            if (x + 1 < width) {
                int j = i + 1;
                errR[j] += qeR * 7 / 16;
                errG[j] += qeG * 7 / 16;
                errB[j] += qeB * 7 / 16;
            }
            if (y + 1 < height) {
                if (x - 1 >= 0) {
                    int j = (y+1)*width + (x-1);
                    errR[j] += qeR * 3 / 16;
                    errG[j] += qeG * 3 / 16;
                    errB[j] += qeB * 3 / 16;
                }
                {
                    int j = (y+1)*width + x;
                    errR[j] += qeR * 5 / 16;
                    errG[j] += qeG * 5 / 16;
                    errB[j] += qeB * 5 / 16;
                }
                if (x + 1 < width) {
                    int j = (y+1)*width + (x+1);
                    errR[j] += qeR * 1 / 16;
                    errG[j] += qeG * 1 / 16;
                    errB[j] += qeB * 1 / 16;
                }
            }
        }
    }

    free(errR);
    free(errG);
    free(errB);
}


/* ====================================================================
   SIMPLE QUANTISATION (no dithering)
   ==================================================================== */
static void apply_no_dither(
    unsigned char *pixels, int n_pixels,
    PaletteEntry *palette, int paletteCount)
{
    for (int i = 0; i < n_pixels; i++) {
        int idx = i * 4;
        int pi = nearest_palette(
            pixels[idx+0], pixels[idx+1], pixels[idx+2],
            palette, paletteCount);
        pixels[idx+0] = palette[pi].r;
        pixels[idx+1] = palette[pi].g;
        pixels[idx+2] = palette[pi].b;
    }
}


/* ====================================================================
   EDGE-AWARE DARKENING
   ====================================================================
   After quantisation, scan each pixel.  If any of its 4-connected
   neighbours differs in luminance by more than `threshold`, darken the
   current pixel proportionally to the edgeStrength parameter.
   This sharpens silhouettes without drawing hard black outlines
   on every boundary.
*/
static void apply_edge_darkening(
    unsigned char *pixels, int width, int height,
    int edgeStrength)
{
    if (edgeStrength <= 0) return;

    /*
     * Scale: edgeStrength 0-100 maps to darkening factor 0-80%.
     * A fully white edge pixel at strength 100 becomes 0.20×255 ≈ 51.
     * We keep it conservative so it doesn't over-posterise.
     */
    int factor = edgeStrength * 80 / 100;   /* max 80 % darkening */
    int threshold = 40;  /* luminance difference to call it an edge */

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 4;

            int lumC = luminance(
                pixels[idx+0], pixels[idx+1], pixels[idx+2]);

            int isEdge = 0;

            /* Check 4 neighbours */
            if (x > 0) {
                int n = (y*width + (x-1)) * 4;
                if (abs(lumC - luminance(pixels[n], pixels[n+1], pixels[n+2])) > threshold)
                    isEdge = 1;
            }
            if (!isEdge && x < width-1) {
                int n = (y*width + (x+1)) * 4;
                if (abs(lumC - luminance(pixels[n], pixels[n+1], pixels[n+2])) > threshold)
                    isEdge = 1;
            }
            if (!isEdge && y > 0) {
                int n = ((y-1)*width + x) * 4;
                if (abs(lumC - luminance(pixels[n], pixels[n+1], pixels[n+2])) > threshold)
                    isEdge = 1;
            }
            if (!isEdge && y < height-1) {
                int n = ((y+1)*width + x) * 4;
                if (abs(lumC - luminance(pixels[n], pixels[n+1], pixels[n+2])) > threshold)
                    isEdge = 1;
            }

            /*
             * Only darken the DARKER side of an edge.
             * This prevents brightening darker edges while also avoiding
             * artificially darkening highlights.
             * Threshold: if pixel luminance < 180, it's a "darker" side.
             */
            if (isEdge && lumC < 180) {
                /* Darken: newVal = val * (100 - factor) / 100 */
                pixels[idx+0] = clamp_byte(
                    pixels[idx+0] * (100 - factor) / 100);
                pixels[idx+1] = clamp_byte(
                    pixels[idx+1] * (100 - factor) / 100);
                pixels[idx+2] = clamp_byte(
                    pixels[idx+2] * (100 - factor) / 100);
            }
        }
    }
}


/* ====================================================================
   PUBLIC API
   ==================================================================== */

/*
 * pixelate_advanced()
 * -------------------
 * Main pixel-art conversion function.
 *
 * Parameters:
 *   pixels       - RGBA pixel buffer (modified in-place)
 *   width        - image width  (should be the SMALL pixel grid)
 *   height       - image height (should be the SMALL pixel grid)
 *   paletteSize  - number of colours: 8 / 16 / 32 / 64
 *   ditherMode   - 0 = none, 1 = Bayer 4×4, 2 = Floyd-Steinberg
 *   edgeStrength - 0-100  (0 = off)
 *   contrast     - 0-100  (0 = off, 100 = maximum stretch)
 *   grayscale    - 0 = colour, 1 = grayscale
 */
EMSCRIPTEN_KEEPALIVE
void pixelate_advanced(
    unsigned char *pixels,
    int width,
    int height,
    int paletteSize,
    int ditherMode,
    int edgeStrength,
    int contrast,
    int grayscale)
{
    int n_pixels = width * height;
    if (n_pixels <= 0) return;

    /* ── 1. Contrast stretch ──────────────────────────────────── */
    if (contrast > 0) {
        contrast_stretch(pixels, n_pixels, contrast);
    }

    /* ── 2. Grayscale ────────────────────────────────────────── */
    if (grayscale) {
        to_grayscale(pixels, n_pixels);
    }

    /* ── 3. Build palette (median-cut) ───────────────────────── */
    PaletteEntry palette[MAX_PALETTE];
    int paletteCount = build_palette(
        pixels, n_pixels, paletteSize, palette);

    if (paletteCount <= 0) return;   /* malloc failed */

    /* ── 4. Dithering + quantisation ─────────────────────────── */
    switch (ditherMode) {

        case 1: /* Bayer ordered dithering */
            /*
             * Spread value controls dither intensity.
             * 20 is a good default — strong enough to simulate blends
             * without making the image look noisy.
             */
            apply_bayer_dithering(
                pixels, width, height,
                palette, paletteCount, 20);
            break;

        case 2: /* Floyd-Steinberg error diffusion */
            apply_floyd_steinberg(
                pixels, width, height,
                palette, paletteCount);
            break;

        default: /* No dithering */
            apply_no_dither(pixels, n_pixels, palette, paletteCount);
            break;
    }

    /* ── 5. Edge-aware silhouette sharpening ─────────────────── */
    if (edgeStrength > 0) {
        apply_edge_darkening(pixels, width, height, edgeStrength);
    }
}


/* ====================================================================
   LEGACY FUNCTIONS  (kept for backward compatibility)
   ==================================================================== */

/*
 * pixelate()
 * ----------
 * Original block-averaging function.
 * Operates on the FULL-SIZE image with block-size parameter.
 */
EMSCRIPTEN_KEEPALIVE
void pixelate(
    unsigned char *pixels, int width, int height, int pixelSize)
{
    if (pixelSize < 1) return;

    for (int blockY = 0; blockY < height; blockY += pixelSize) {
        for (int blockX = 0; blockX < width; blockX += pixelSize) {

            int blockW = (blockX + pixelSize < width)  ?
                pixelSize : (width  - blockX);
            int blockH = (blockY + pixelSize < height) ?
                pixelSize : (height - blockY);

            long totalR = 0, totalG = 0, totalB = 0;
            int  count  = blockW * blockH;

            for (int py = 0; py < blockH; py++) {
                for (int px = 0; px < blockW; px++) {
                    int idx = ((blockY+py) * width + (blockX+px)) * 4;
                    totalR += pixels[idx+0];
                    totalG += pixels[idx+1];
                    totalB += pixels[idx+2];
                }
            }

            unsigned char avgR = (unsigned char)(totalR / count);
            unsigned char avgG = (unsigned char)(totalG / count);
            unsigned char avgB = (unsigned char)(totalB / count);

            for (int py = 0; py < blockH; py++) {
                for (int px = 0; px < blockW; px++) {
                    int idx = ((blockY+py) * width + (blockX+px)) * 4;
                    pixels[idx+0] = avgR;
                    pixels[idx+1] = avgG;
                    pixels[idx+2] = avgB;
                }
            }
        }
    }
}


/*
 * pixelate_grayscale()
 * --------------------
 * Block-averaging + luminance-weighted grayscale.
 */
EMSCRIPTEN_KEEPALIVE
void pixelate_grayscale(
    unsigned char *pixels, int width, int height, int pixelSize)
{
    if (pixelSize < 1) return;

    for (int blockY = 0; blockY < height; blockY += pixelSize) {
        for (int blockX = 0; blockX < width; blockX += pixelSize) {

            int blockW = (blockX + pixelSize < width)  ?
                pixelSize : (width  - blockX);
            int blockH = (blockY + pixelSize < height) ?
                pixelSize : (height - blockY);

            long totalGray = 0;
            int  count     = blockW * blockH;

            for (int py = 0; py < blockH; py++) {
                for (int px = 0; px < blockW; px++) {
                    int idx = ((blockY+py) * width + (blockX+px)) * 4;
                    totalGray += luminance(
                        pixels[idx+0], pixels[idx+1], pixels[idx+2]);
                }
            }

            unsigned char gray = (unsigned char)(totalGray / count);

            for (int py = 0; py < blockH; py++) {
                for (int px = 0; px < blockW; px++) {
                    int idx = ((blockY+py) * width + (blockX+px)) * 4;
                    pixels[idx+0] = gray;
                    pixels[idx+1] = gray;
                    pixels[idx+2] = gray;
                }
            }
        }
    }
}


/*
 * pixelate_reduced()
 * ------------------
 * Block-averaging + per-channel quantisation (simple rounding).
 */
EMSCRIPTEN_KEEPALIVE
void pixelate_reduced(
    unsigned char *pixels, int width, int height,
    int pixelSize, int colorLevels)
{
    if (pixelSize   < 1)   return;
    if (colorLevels < 2)   colorLevels = 2;
    if (colorLevels > 256) colorLevels = 256;

    int step = 255 / (colorLevels - 1);

    for (int blockY = 0; blockY < height; blockY += pixelSize) {
        for (int blockX = 0; blockX < width; blockX += pixelSize) {

            int blockW = (blockX + pixelSize < width)  ?
                pixelSize : (width  - blockX);
            int blockH = (blockY + pixelSize < height) ?
                pixelSize : (height - blockY);

            long totalR = 0, totalG = 0, totalB = 0;
            int  count  = blockW * blockH;

            for (int py = 0; py < blockH; py++) {
                for (int px = 0; px < blockW; px++) {
                    int idx = ((blockY+py) * width + (blockX+px)) * 4;
                    totalR += pixels[idx+0];
                    totalG += pixels[idx+1];
                    totalB += pixels[idx+2];
                }
            }

            int avgR = (int)(totalR / count);
            int avgG = (int)(totalG / count);
            int avgB = (int)(totalB / count);

            int qR = ((avgR + step/2) / step) * step;
            int qG = ((avgG + step/2) / step) * step;
            int qB = ((avgB + step/2) / step) * step;
            if (qR > 255) qR = 255;
            if (qG > 255) qG = 255;
            if (qB > 255) qB = 255;

            unsigned char fR = (unsigned char)qR;
            unsigned char fG = (unsigned char)qG;
            unsigned char fB = (unsigned char)qB;

            for (int py = 0; py < blockH; py++) {
                for (int px = 0; px < blockW; px++) {
                    int idx = ((blockY+py) * width + (blockX+px)) * 4;
                    pixels[idx+0] = fR;
                    pixels[idx+1] = fG;
                    pixels[idx+2] = fB;
                }
            }
        }
    }
}


/* ====================================================================
   MEMORY MANAGEMENT
   ==================================================================== */

/*
 * get_buffer(size)
 * ----------------
 * Allocates a byte buffer in the WASM heap.
 * JavaScript writes pixel data here before calling C functions.
 */
EMSCRIPTEN_KEEPALIVE
unsigned char* get_buffer(int size) {
    return (unsigned char*)malloc(size);
}

/*
 * free_buffer(ptr)
 * ----------------
 * Frees a buffer previously allocated by get_buffer().
 * Always call this after reading the result back in JavaScript.
 */
EMSCRIPTEN_KEEPALIVE
void free_buffer(unsigned char *ptr) {
    free(ptr);
}
