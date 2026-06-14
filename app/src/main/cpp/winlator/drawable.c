#include <jni.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <android/bitmap.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#define WHITE 0xffffff
#define BLACK 0x000000
#define FILL_ROW_STACK_SIZE 4096
#define BYTES_PER_PIXEL 4

enum GCFunction {GCF_CLEAR, GCF_AND, GCF_AND_REVERSE, GCF_COPY, GCF_AND_INVERTED, GCF_NO_OP, GCF_XOR, GCF_OR, GCF_NOR, GCF_EQUIV, GCF_INVERT, GCF_OR_REVERSE, GCF_COPY_INVERTED, GCF_OR_INVERTED, GCF_NAND, GCF_SET};

static bool alloc_fill_row(uint8_t stack_row[FILL_ROW_STACK_SIZE * BYTES_PER_PIXEL], uint8_t** row_out, int width) {
    if (width <= 0) return false;
    *row_out = stack_row;
    if (width > FILL_ROW_STACK_SIZE) {
        *row_out = (uint8_t*)malloc((size_t)width * BYTES_PER_PIXEL);
        if (!*row_out) return false;
    }
    return true;
}

static void free_fill_row(uint8_t* row, uint8_t stack_row[FILL_ROW_STACK_SIZE * BYTES_PER_PIXEL]) {
    if (row != stack_row) free(row);
}

static void fill_row_with_color(uint8_t* row, int rowSize, uint32_t color32) {
    uint32_t* row32 = (uint32_t*)row;
    int rowPixels = rowSize / BYTES_PER_PIXEL;
    for (int i = 0; i < rowPixels; i++) row32[i] = color32;
}

static inline uint32_t packColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | a;
}

static void unpackColor(int color, uint8_t *rgba) {
    rgba[2] = (color >> 16) & 255;
    rgba[1] = (color >> 8) & 255;
    rgba[0] = color & 255;
    rgba[3] = 255;
}

static inline int unpack_pixel(const uint8_t *data, int offset) {
    return (data[offset] << 16) | (data[offset + 1] << 8) | data[offset + 2];
}

static inline void pack_pixel(uint8_t *data, int offset, int color) {
    data[offset] = (color >> 16) & 0xff;
    data[offset + 1] = (color >> 8) & 0xff;
    data[offset + 2] = color & 0xff;
}

static inline uint32_t pack_pixel32(const uint8_t *rgba) {
    return ((uint32_t)rgba[3] << 24) | ((uint32_t)rgba[2] << 16) | ((uint32_t)rgba[1] << 8) | rgba[0];
}

static int8_t getBit(uint8_t *line, int x) {
    uint8_t mask = (1 << (x & 7));
    line += (x >> 3);
    return (*line & mask) ? 1 : 0;
}

static int getBitmapBytePad(int width) {
    return ((width + 32 - 1) >> 5) << 2;
}

static int setPixelOp(int srcColor, int dstColor, enum GCFunction gcFunction) {
    switch (gcFunction) {
        case GCF_CLEAR:
            return BLACK;
        case GCF_AND:
            return srcColor & dstColor;
        case GCF_AND_REVERSE:
            return srcColor & ~dstColor;
        case GCF_COPY:
            return srcColor;
        case GCF_AND_INVERTED:
            return ~srcColor & dstColor;
        case GCF_XOR:
            return srcColor ^ dstColor;
        case GCF_OR:
            return srcColor | dstColor;
        case GCF_NOR:
            return ~srcColor & ~dstColor;
        case GCF_EQUIV:
            return ~srcColor ^ dstColor;
        case GCF_INVERT:
            return ~dstColor;
        case GCF_OR_REVERSE:
            return srcColor | ~dstColor;
        case GCF_COPY_INVERTED:
            return ~srcColor;
        case GCF_OR_INVERTED:
            return ~srcColor | dstColor;
        case GCF_NAND:
            return ~srcColor | ~dstColor;
        case GCF_SET:
            return WHITE;
        case GCF_NO_OP:
        default:
            return dstColor;
    }
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_xserver_Drawable_drawBitmap(JNIEnv *env, jclass obj,
                                              jshort width, jshort height, jobject srcData,
                                              jobject dstData) {
    uint8_t *srcDataAddr = (*env)->GetDirectBufferAddress(env, srcData);
    int *dstDataAddr = (*env)->GetDirectBufferAddress(env, dstData);

    if (!srcDataAddr || !dstDataAddr) {
        return;
    }

    if (width <= 0 || height <= 0) return;

    int stride = getBitmapBytePad(width);
    for (int16_t y = 0, x; y < height; y++) {
        for (x = 0; x < width; x++) {
            *dstDataAddr++ = getBit(srcDataAddr, x) ? WHITE : BLACK;
        }
        srcDataAddr += stride;
    }
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_xserver_Drawable_copyArea(JNIEnv *env, jclass obj, jshort srcX,
                                            jshort srcY, jshort dstX, jshort dstY,
                                            jshort width, jshort height, jshort srcStride,
                                            jshort dstStride, jobject srcData,
                                            jobject dstData) {
    uint8_t *srcDataAddr = (*env)->GetDirectBufferAddress(env, srcData);
    uint8_t *dstDataAddr = (*env)->GetDirectBufferAddress(env, dstData);

    if (!srcDataAddr || !dstDataAddr) {
        return;
    }

    jlong srcLength = (*env)->GetDirectBufferCapacity(env, srcData);
    jlong dstLength = (*env)->GetDirectBufferCapacity(env, dstData);

    if (srcX != 0 || srcY != 0 || dstX != 0 || dstY != 0 || srcLength != dstLength) {
        if (width <= 0 || height <= 0) return;
        jlong maxSrcOff = ((jlong)(srcX + width - 1) + (jlong)(srcY + height - 1) * srcStride) * BYTES_PER_PIXEL;
        jlong maxDstOff = ((jlong)(dstX + width - 1) + (jlong)(dstY + height - 1) * dstStride) * BYTES_PER_PIXEL;
        if (maxSrcOff >= srcLength || maxDstOff >= dstLength) return;
        int copyAmount = width * BYTES_PER_PIXEL;
        for (int16_t y = 0; y < height; y++) {
            memcpy(dstDataAddr + (dstX + (y + dstY) * dstStride) * BYTES_PER_PIXEL,
                   srcDataAddr + (srcX + (y + srcY) * srcStride) * BYTES_PER_PIXEL, copyAmount);
        }
    } else {
        memcpy(dstDataAddr, srcDataAddr, dstLength);
    }
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_xserver_Drawable_copyAreaOp(JNIEnv *env, jclass obj, jshort srcX,
                                              jshort srcY, jshort dstX, jshort dstY,
                                              jshort width, jshort height, jshort srcStride,
                                              jshort dstStride, jobject srcData,
                                              jobject dstData, int gcFunction) {
    uint8_t *srcDataAddr = (*env)->GetDirectBufferAddress(env, srcData);
    uint8_t *dstDataAddr = (*env)->GetDirectBufferAddress(env, dstData);

    if (!srcDataAddr || !dstDataAddr) {
        return;
    }

    jlong srcLength = (*env)->GetDirectBufferCapacity(env, srcData);
    jlong dstLength = (*env)->GetDirectBufferCapacity(env, dstData);
    jlong maxSrcOffset = (jlong)(srcX + width - 1 + (jlong)(srcY + height - 1) * srcStride) * BYTES_PER_PIXEL + (BYTES_PER_PIXEL - 1);
    jlong maxDstOffset = (jlong)(dstX + width - 1 + (jlong)(dstY + height - 1) * dstStride) * BYTES_PER_PIXEL + (BYTES_PER_PIXEL - 1);
    if (maxSrcOffset >= srcLength || maxDstOffset >= dstLength) {
        return;
    }

    for (int16_t y = 0; y < height; y++) {
        for (int16_t x = 0; x < width; x++) {
            int i = (x + srcX + (y + srcY) * srcStride) * BYTES_PER_PIXEL;
            int j = (x + dstX + (y + dstY) * dstStride) * BYTES_PER_PIXEL;
            int srcColor = unpack_pixel(srcDataAddr, i);
            int dstColor = unpack_pixel(dstDataAddr, j);

            dstColor = setPixelOp(srcColor, dstColor, gcFunction);

            pack_pixel(dstDataAddr, j, dstColor);
        }
    }
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_xserver_Drawable_fillRect(JNIEnv *env, jclass obj, jshort x, jshort y,
                                            jshort width, jshort height, jint color, jshort stride,
                                            jobject data) {
    uint8_t *dataAddr = (*env)->GetDirectBufferAddress(env, data);

    if (!dataAddr) {
        return;
    }

    jlong bufLength = (*env)->GetDirectBufferCapacity(env, data);
    jlong maxOffset = (jlong)(x + width - 1 + (jlong)(y + height - 1) * stride) * BYTES_PER_PIXEL + (BYTES_PER_PIXEL - 1);
    if (maxOffset >= bufLength) {
        return;
    }

    uint8_t rgba[4];
    unpackColor(color, rgba);

    uint8_t stackRow[FILL_ROW_STACK_SIZE * BYTES_PER_PIXEL];
    uint8_t *row;
    if (!alloc_fill_row(stackRow, &row, width)) {
        return;
    }

    int rowSize = width * BYTES_PER_PIXEL;
    uint32_t color32 = pack_pixel32(rgba);
    fill_row_with_color(row, rowSize, color32);
    for (int16_t i = 0; i < height; i++) {
        memcpy(dataAddr + (x + (i + y) * stride) * BYTES_PER_PIXEL, row, rowSize);
    }

    free_fill_row(row, stackRow);
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_xserver_Drawable_drawLine(JNIEnv *env, jclass obj, jshort x0, jshort y0,
                                            jshort x1, jshort y1, jint color, jshort lineWidth,
                                            jshort stride, jobject data) {
    uint8_t *dataAddr = (*env)->GetDirectBufferAddress(env, data);

    if (!dataAddr) {
        return;
    }

    int dx =  abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int8_t sx = x0 < x1 ? 1 : -1;
    int8_t sy = y0 < y1 ? 1 : -1;
    int e1 = dx + dy, e2;

    uint8_t rgba[4];
    unpackColor(color, rgba);

    uint8_t stackRow[FILL_ROW_STACK_SIZE * BYTES_PER_PIXEL];
    uint8_t *row;
    if (!alloc_fill_row(stackRow, &row, lineWidth)) {
        return;
    }

    jlong bufLength = (*env)->GetDirectBufferCapacity(env, data);
    if (stride == 0) {
        free_fill_row(row, stackRow);
        return;
    }
    int height = bufLength / ((jlong)stride * BYTES_PER_PIXEL);
    if (x0 < 0 || x0 >= stride || y0 < 0 || y0 >= height) {
        free_fill_row(row, stackRow);
        return;
    }
    if (x1 < 0 || x1 >= stride || y1 < 0 || y1 >= height) {
        free_fill_row(row, stackRow);
        return;
    }

    int rowSize = lineWidth * BYTES_PER_PIXEL;
    uint32_t color32 = pack_pixel32(rgba);
    fill_row_with_color(row, rowSize, color32);

    int max_x = (x0 > x1 ? x0 : x1) + lineWidth;
    int max_y = (y0 > y1 ? y0 : y1) + lineWidth;
    if (max_x > stride || max_y > height) {
        free_fill_row(row, stackRow);
        return;
    }

    while (true) {
        if (abs(x1 - x0) >= abs(y1 - y0)) {
            for (int16_t i = 0; i < lineWidth; i++) {
                memcpy(dataAddr + (x0 + (i + y0) * stride) * BYTES_PER_PIXEL, row, rowSize);
            }
        } else {
            for (int16_t i = 0; i < lineWidth; i++) {
                ((uint32_t *)dataAddr)[(x0 + i) + y0 * stride] = color32;
            }
        }
        if (x0 == x1 && y0 == y1) break;

        e2 = e1 * 2;
        if (e2 >= dy) {
            e1 += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            e1 += dx;
            y0 += sy;
        }
    }

    free_fill_row(row, stackRow);
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_xserver_Drawable_drawAlphaMaskedBitmap(JNIEnv *env, jclass obj,
                                                         jbyte foreRed, jbyte foreGreen,
                                                         jbyte foreBlue, jbyte backRed,
                                                         jbyte backGreen, jbyte backBlue,
                                                         jobject srcData, jobject maskData,
                                                         jobject dstData) {
    uint32_t *srcDataAddr = (*env)->GetDirectBufferAddress(env, srcData);
    uint32_t *maskDataAddr = (*env)->GetDirectBufferAddress(env, maskData);
    uint32_t *dstDataAddr = (*env)->GetDirectBufferAddress(env, dstData);

    if (!srcDataAddr || !maskDataAddr || !dstDataAddr) {
        return;
    }

    uint32_t foreColor = packColor(foreRed & 0xff, foreGreen & 0xff, foreBlue & 0xff, 0xff);
    uint32_t backColor = packColor(backRed & 0xff, backGreen & 0xff, backBlue & 0xff, 0xff);

    jlong dstLength = (*env)->GetDirectBufferCapacity(env, dstData) / BYTES_PER_PIXEL;
    const uint32_t whiteMask = (uint32_t)WHITE;
#ifdef __ARM_NEON
    uint32x4_t vFore = vdupq_n_u32(foreColor);
    uint32x4_t vBack = vdupq_n_u32(backColor);
    uint32x4_t vWhite = vdupq_n_u32(whiteMask);
    uint32x4_t vZero = vdupq_n_u32(0u);
    jlong i = 0;
    for (; i + 3 < dstLength; i += 4) {
        uint32x4_t vMask = vld1q_u32(maskDataAddr + i);
        uint32x4_t vSrc = vld1q_u32(srcDataAddr + i);
        uint32x4_t maskIsWhite = vceqq_u32(vMask, vWhite);
        uint32x4_t srcIsWhite = vceqq_u32(vSrc, vWhite);
        uint32x4_t color = vbslq_u32(srcIsWhite, vFore, vBack);
        uint32x4_t result = vbslq_u32(maskIsWhite, color, vZero);
        vst1q_u32(dstDataAddr + i, result);
    }
    for (; i < dstLength; i++) {
        dstDataAddr[i] = maskDataAddr[i] == whiteMask
            ? (srcDataAddr[i] == whiteMask ? foreColor : backColor)
            : 0u;
    }
#else
    for (jlong i = 0; i < dstLength; i++) {
        dstDataAddr[i] = maskDataAddr[i] == whiteMask
            ? (srcDataAddr[i] == whiteMask ? foreColor : backColor)
            : 0u;
    }
#endif
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_xserver_Drawable_fromBitmap(JNIEnv *env, jclass obj, jobject bitmap,
                                              jobject data) {
    char *dataAddr = (*env)->GetDirectBufferAddress(env, data);

    if (!dataAddr) {
        return;
    }

    AndroidBitmapInfo info;
    uint8_t *pixels;

    if (AndroidBitmap_getInfo(env, bitmap, &info) < 0) {
        return;
    }
    if (AndroidBitmap_lockPixels(env, bitmap, (void**)&pixels) < 0) {
        return;
    }

    size_t size = (size_t)info.width * (size_t)info.height * BYTES_PER_PIXEL;
    jlong capacity = (*env)->GetDirectBufferCapacity(env, data);
    if (capacity < (jlong)size) {
        AndroidBitmap_unlockPixels(env, bitmap);
        return;
    }
    memcpy(dataAddr, pixels, size);

    AndroidBitmap_unlockPixels(env, bitmap);
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_xserver_Pixmap_toBitmap(JNIEnv *env, jclass obj, jobject colorData,
                                          jobject maskData, jobject bitmap) {
    char *colorDataAddr = (*env)->GetDirectBufferAddress(env, colorData);
    char *maskDataAddr = maskData ? (*env)->GetDirectBufferAddress(env, maskData) : NULL;

    if (!colorDataAddr) {
        return;
    }

    AndroidBitmapInfo info;
    uint8_t *pixels;

    if (AndroidBitmap_getInfo(env, bitmap, &info) < 0) {
        return;
    }
    if (AndroidBitmap_lockPixels(env, bitmap, (void**)&pixels) < 0) {
        return;
    }

    jlong size = (jlong)info.width * (jlong)info.height * BYTES_PER_PIXEL;
    jlong colorCap = (*env)->GetDirectBufferCapacity(env, colorData);
    if (colorCap < size) {
        AndroidBitmap_unlockPixels(env, bitmap);
        return;
    }
    if (maskData) {
        jlong maskCap = (*env)->GetDirectBufferCapacity(env, maskData);
        if (maskCap < size) {
            AndroidBitmap_unlockPixels(env, bitmap);
            return;
        }
    }

    for (jlong i = 0; i < size; i += BYTES_PER_PIXEL) {
        pixels[i+2] = colorDataAddr[i+0];
        pixels[i+1] = colorDataAddr[i+1];
        pixels[i+0] = colorDataAddr[i+2];
        pixels[i+3] = maskDataAddr ? maskDataAddr[i+0] : colorDataAddr[i+3];
    }

    AndroidBitmap_unlockPixels(env, bitmap);
}
