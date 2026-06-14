package com.winlator.cmod.core;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;

import java.io.File;
import java.io.FileOutputStream;
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;


public class ExeIconExtractor {

    private static final String TAG = "ExeIconExtractor";

    private static final ExecutorService executor = Executors.newSingleThreadExecutor();

    public static final int ICON_SIZE    = 256;
    public static final int COVER_WIDTH  = 600;
    public static final int COVER_HEIGHT = 900;


    public static boolean extractIcon(File exeFile, File destinationFile) {
        return extractAndSave(exeFile, destinationFile, false);
    }

    public static boolean extractCover(File exeFile, File destinationFile) {
        return extractAndSave(exeFile, destinationFile, true);
    }

    public static void extractAsync(File exeFile, File destinationFile, boolean isCover, Runnable onComplete) {
        executor.submit(() -> {
            boolean ok = extractAndSave(exeFile, destinationFile, isCover);
            if (ok && onComplete != null) onComplete.run();
        });
    }

    public static Bitmap extractBitmap(File exeFile) {
        try {
            return PeIconExtractor.extract(exeFile);
        } catch (Exception e) {
            return null;
        }
    }


    private static boolean extractAndSave(File exeFile, File destinationFile, boolean isCover) {
        String label = isCover ? "cover" : "icon";

        if (exeFile == null) {
            return false;
        }
        if (!exeFile.exists()) {
            return false;
        }
        if (!exeFile.canRead()) {
            return false;
        }


        Bitmap raw;
        try {
            raw = PeIconExtractor.extract(exeFile);
        } catch (Exception e) {
            return false;
        }

        if (raw == null) {
            return false;
        }

        Bitmap result;
        try {
            if (isCover) {
                result = buildCover(raw);
            } else {
                result = Bitmap.createScaledBitmap(raw, ICON_SIZE, ICON_SIZE, true);
            }
        } catch (Exception e) {
            if (!raw.isRecycled()) raw.recycle();
            return false;
        }

        File parentDir = destinationFile.getParentFile();
        if (parentDir != null && !parentDir.exists()) {
            boolean made = parentDir.mkdirs();
            if (!made) {
                if (!raw.isRecycled()) raw.recycle();
                if (result != raw && !result.isRecycled()) result.recycle();
                return false;
            }
        }

        try (FileOutputStream out = new FileOutputStream(destinationFile)) {
            boolean compressed = result.compress(Bitmap.CompressFormat.PNG, 100, out);
            if (!compressed) {
                return false;
            }
        } catch (Exception e) {
            return false;
        } finally {
            if (!raw.isRecycled()) raw.recycle();
            if (result != raw && !result.isRecycled()) result.recycle();
        }

        return true;
    }


    private static Bitmap buildCover(Bitmap icon) {
        Bitmap cover = Bitmap.createBitmap(COVER_WIDTH, COVER_HEIGHT, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(cover);

        Bitmap tiny   = Bitmap.createScaledBitmap(icon, 6, 6, true);
        Bitmap bgFill = Bitmap.createScaledBitmap(tiny, COVER_WIDTH, COVER_HEIGHT, true);
        tiny.recycle();
        canvas.drawBitmap(bgFill, 0, 0, null);
        bgFill.recycle();

        canvas.drawColor(0x99000000);

        android.graphics.RadialGradient vignette = new android.graphics.RadialGradient(
                COVER_WIDTH  / 2f,
                COVER_HEIGHT / 2f,
                Math.max(COVER_WIDTH, COVER_HEIGHT) * 0.72f,
                new int[]{ 0x00000000, 0x99000000 },
                new float[]{ 0.35f, 1.0f },
                android.graphics.Shader.TileMode.CLAMP);
        Paint vignettePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        vignettePaint.setShader(vignette);
        canvas.drawRect(0, 0, COVER_WIDTH, COVER_HEIGHT, vignettePaint);

        int idealDraw = (int) (Math.min(COVER_WIDTH, COVER_HEIGHT) * 0.72f);
        int srcSize   = Math.max(icon.getWidth(), icon.getHeight());
        int iconDraw  = (srcSize <= 32)
                ? Math.max(idealDraw / 2, (int) (Math.min(COVER_WIDTH, COVER_HEIGHT) * 0.40f))
                : idealDraw;

        int left = (COVER_WIDTH  - iconDraw) / 2;
        int top  = (COVER_HEIGHT - iconDraw) / 2;

        Bitmap drawIcon = icon;
        if (srcSize < iconDraw) {
            int cur = srcSize;
            Bitmap stepped = icon;
            while (cur * 2 < iconDraw) {
                cur *= 2;
                Bitmap next = Bitmap.createScaledBitmap(stepped, cur, cur, true);
                if (stepped != icon) stepped.recycle();
                stepped = next;
            }
            drawIcon = stepped;
        }

        int shadowOff = Math.max(4, iconDraw / 18);
        Paint shadowPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        shadowPaint.setColorFilter(new android.graphics.PorterDuffColorFilter(
                0xFF000000, android.graphics.PorterDuff.Mode.SRC_ATOP));
        shadowPaint.setAlpha(110);
        canvas.drawBitmap(drawIcon, null,
                new Rect(left + shadowOff, top + shadowOff,
                         left + iconDraw + shadowOff, top + iconDraw + shadowOff),
                shadowPaint);

        Paint iconPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        canvas.drawBitmap(drawIcon, null,
                new Rect(left, top, left + iconDraw, top + iconDraw),
                iconPaint);

        if (drawIcon != icon) drawIcon.recycle();

        return cover;
    }

    private static int getDominantColor(Bitmap bitmap) {
        Bitmap small = Bitmap.createScaledBitmap(bitmap, 16, 16, false);
        long r = 0, g = 0, b = 0;
        int count = 0;
        for (int y = 0; y < small.getHeight(); y++) {
            for (int x = 0; x < small.getWidth(); x++) {
                int pixel = small.getPixel(x, y);
                if (((pixel >> 24) & 0xFF) < 128) continue;
                r += (pixel >> 16) & 0xFF;
                g += (pixel >>  8) & 0xFF;
                b +=  pixel        & 0xFF;
                count++;
            }
        }
        small.recycle();
        if (count == 0) return 0xFF1A1A2E;
        return 0xFF000000
             | (((int)(r / count)) << 16)
             | (((int)(g / count)) <<  8)
             |  ((int)(b / count));
    }

    private static int darkenColor(int color, float factor) {
        int r = (int) (((color >> 16) & 0xFF) * factor);
        int g = (int) (((color >>  8) & 0xFF) * factor);
        int b = (int) ( (color        & 0xFF) * factor);
        return 0xFF000000 | (r << 16) | (g << 8) | b;
    }

    static class PeIconExtractor {

        static Bitmap extract(File exeFile) {
            try (RandomAccessFile raf = new RandomAccessFile(exeFile, "r")) {

                int b0 = raf.read(), b1 = raf.read();
                if (b0 != 0x4D || b1 != 0x5A) {
                    return null;
                }

                raf.seek(0x3C);
                int peOffset = readLE32(raf);

                if (peOffset <= 0 || peOffset >= exeFile.length()) {
                    return null;
                }

                raf.seek(peOffset);
                int p0 = raf.read(), p1 = raf.read(), p2 = raf.read(), p3 = raf.read();
                if (p0 != 0x50 || p1 != 0x45 || p2 != 0 || p3 != 0) {
                    return null;
                }

                raf.skipBytes(2); // Machine
                int numSections   = readLE16(raf);
                raf.skipBytes(12); // TimeDateStamp + PointerToSymbolTable + NumberOfSymbols
                int optHeaderSize = readLE16(raf);
                raf.skipBytes(2); // Characteristics

                long optHeaderStart = raf.getFilePointer();

                int magic = readLE16(raf);

                if (magic != 0x10B && magic != 0x20B) {
                    return null;
                }

                int ddOffset = (magic == 0x20B) ? 112 : 96;
                raf.seek(optHeaderStart + ddOffset);
                raf.skipBytes(16);

                long rsrcRVA  = readLE32(raf) & 0xFFFFFFFFL;
                int  rsrcSize = readLE32(raf);

                if (rsrcRVA == 0) {
                    return null;
                }

                long sectionsStart = optHeaderStart + optHeaderSize;
                raf.seek(sectionsStart);
                long rsrcOffset = 0;

                for (int i = 0; i < numSections; i++) {
                    byte[] nm = new byte[8];
                    raf.readFully(nm);
                    String secName = new String(nm).trim().replace("\0", "");
                    raf.skipBytes(4); // VirtualSize
                    long vAddr  = readLE32(raf) & 0xFFFFFFFFL;
                    raf.skipBytes(4); // SizeOfRawData
                    long rawOff = readLE32(raf) & 0xFFFFFFFFL;
                    raf.skipBytes(16);


                    if (vAddr == rsrcRVA) {
                        rsrcOffset = rawOff;
                    }
                }

                if (rsrcOffset == 0) {
                    return null;
                }

                return extractBestIcon(raf, rsrcOffset, rsrcRVA, exeFile.getName());

            } catch (Exception e) {
                return null;
            }
        }


        private static Bitmap extractBestIcon(RandomAccessFile raf, long rsrcBase,
                                              long rsrcRVA, String exeName) throws Exception {

            raf.seek(rsrcBase + 12);
            int namedL1   = readLE16(raf);
            int idCountL1 = readLE16(raf);

            raf.skipBytes(namedL1 * 8);

            boolean foundGroupIcon = false;
            for (int i = 0; i < idCountL1; i++) {
                raf.seek(rsrcBase + 16 + namedL1 * 8 + i * 8);
                int typeId = readLE16(raf);
                readLE16(raf); // high word
                int off = readLE32(raf);

                String typeName = typeId == 3  ? " (RT_ICON)"
                                : typeId == 14 ? " (RT_GROUP_ICON)"
                                : "";

                if (typeId != 14) continue; // RT_GROUP_ICON = 14
                foundGroupIcon = true;

                long subDir = rsrcBase + (off & 0x7FFFFFFF);
                raf.seek(subDir + 12);
                int namedL2   = readLE16(raf);
                int idCountL2 = readLE16(raf);

                if (namedL2 + idCountL2 == 0) {
                    return null;
                }

                raf.seek(subDir + 16);
                readLE16(raf); readLE16(raf); // entry name/id
                int off2 = readLE32(raf);

                long subDir2 = rsrcBase + (off2 & 0x7FFFFFFF);
                raf.seek(subDir2 + 12);
                int namedL3   = readLE16(raf);
                int idCountL3 = readLE16(raf);

                if (namedL3 + idCountL3 == 0) {
                    continue;
                }

                raf.seek(subDir2 + 16);
                readLE16(raf); readLE16(raf);
                int off3 = readLE32(raf);

                long dataEntry = rsrcBase + (off3 & 0x7FFFFFFF);
                raf.seek(dataEntry);
                long dataRVA  = readLE32(raf) & 0xFFFFFFFFL;
                int  dataSize = readLE32(raf);

                long grpDataOffset = rsrcBase + (dataRVA - rsrcRVA);
                raf.seek(grpDataOffset);
                raf.skipBytes(4); // idReserved + idType
                int iconCount = readLE16(raf);

                if (iconCount == 0) {
                    return null;
                }

                List<int[]> entries = new ArrayList<>();
                for (int j = 0; j < iconCount; j++) {
                    int w = raf.read() & 0xFF;
                    int h = raf.read() & 0xFF;
                    raf.skipBytes(2); // colorCount + reserved
                    raf.skipBytes(4); // planes + bitCount
                    raf.skipBytes(4); // bytesInRes
                    int iconId = readLE16(raf);
                    if (w == 0) w = 256;
                    if (h == 0) h = 256;
                    entries.add(new int[]{w, h, iconId});
                }

                Collections.sort(entries, (a, b) -> (b[0] * b[1]) - (a[0] * a[1]));

                for (int[] entry : entries) {
                    Bitmap bmp = extractRtIcon(raf, rsrcBase, rsrcRVA, entry[2]);
                    if (bmp != null) {
                        return bmp;
                    }
                }

                return null;
            }

            if (!foundGroupIcon) {
            }
            return null;
        }


        private static Bitmap extractRtIcon(RandomAccessFile raf, long rsrcBase,
                                            long rsrcRVA, int iconId) throws Exception {
            raf.seek(rsrcBase + 12);
            int namedL1   = readLE16(raf);
            int idCountL1 = readLE16(raf);

            for (int i = 0; i < idCountL1; i++) {
                raf.seek(rsrcBase + 16 + namedL1 * 8 + i * 8);
                int typeId = readLE16(raf);
                readLE16(raf);
                int off = readLE32(raf);

                if (typeId != 3) continue; // RT_ICON = 3

                long subDir = rsrcBase + (off & 0x7FFFFFFF);
                raf.seek(subDir + 12);
                int namedL2   = readLE16(raf);
                int idCountL2 = readLE16(raf);

                for (int j = 0; j < idCountL2; j++) {
                    raf.seek(subDir + 16 + namedL2 * 8 + j * 8);
                    int entryId = readLE16(raf);
                    readLE16(raf);
                    int off2 = readLE32(raf);

                    if (entryId != iconId) continue;

                    long subDir2 = rsrcBase + (off2 & 0x7FFFFFFF);
                    raf.seek(subDir2 + 12);
                    int namedL3   = readLE16(raf);
                    int idCountL3 = readLE16(raf);
                    if (namedL3 + idCountL3 == 0) {
                        continue;
                    }

                    raf.seek(subDir2 + 16 + namedL3 * 8);
                    readLE16(raf); readLE16(raf);
                    int off3 = readLE32(raf);

                    long dataEntry = rsrcBase + (off3 & 0x7FFFFFFF);
                    raf.seek(dataEntry);
                    long dataRVA  = readLE32(raf) & 0xFFFFFFFFL;
                    int  dataSize = readLE32(raf);


                    if (dataSize <= 0 || dataSize > 4 * 1024 * 1024) {
                        continue;
                    }

                    long iconDataOffset = rsrcBase + (dataRVA - rsrcRVA);

                    raf.seek(iconDataOffset);
                    byte[] iconData = new byte[dataSize];
                    raf.readFully(iconData);

                    Bitmap bmp = BitmapFactory.decodeByteArray(iconData, 0, iconData.length);
                    if (bmp != null) {
                        return bmp;
                    }


                    bmp = decodeDIB(iconData, iconId);
                    if (bmp != null) {
                        return bmp;
                    }

                }
            }

            return null;
        }


        /**
         * Decode a raw DIB (Device-Independent Bitmap) as stored inside ICO/RT_ICON.
         *
         * Differences from a standalone BMP:
         *  - No BITMAPFILEHEADER prefix
         *  - Height = 2x real height (XOR mask + AND mask stacked)
         *  - 32bpp pre-Vista icons often have alpha=0 everywhere; fall back to AND mask
         */
        private static Bitmap decodeDIB(byte[] data, int iconId) {
            try {
                if (data.length < 40) {
                    return null;
                }

                int headerSize  = readLE32(data, 0);
                int width       = readLE32(data, 4);
                int rawHeight   = readLE32(data, 8);
                int height      = rawHeight / 2; // stored as 2x (XOR mask + AND mask)
                int bpp         = readLE16(data, 14);
                int compression = readLE32(data, 16);


                if (width <= 0 || height <= 0) {
                    return null;
                }
                if (width > 1024 || height > 1024) {
                    return null;
                }
                if (compression != 0) {
                    return null;
                }

                Bitmap bmp = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);

                if (bpp == 32) {

                    int    pixelDataOffset = headerSize;
                    int[]  pixels          = new int[width * height];
                    boolean hasAlpha       = false;

                    for (int y = height - 1; y >= 0; y--) {
                        for (int x = 0; x < width; x++) {
                            int idx = pixelDataOffset + ((height - 1 - y) * width + x) * 4;
                            if (idx + 3 >= data.length) break;
                            int b = data[idx]     & 0xFF;
                            int g = data[idx + 1] & 0xFF;
                            int r = data[idx + 2] & 0xFF;
                            int a = data[idx + 3] & 0xFF;
                            if (a > 0) hasAlpha = true;
                            pixels[y * width + x] = (a << 24) | (r << 16) | (g << 8) | b;
                        }
                    }

                    if (!hasAlpha) {
                        for (int idx2 = 0; idx2 < pixels.length; idx2++) {
                            pixels[idx2] |= 0xFF000000;
                        }
                        int andMaskOffset = headerSize + width * height * 4;
                        int maskRowBytes  = ((width + 31) / 32) * 4;
                        if (andMaskOffset + maskRowBytes * height <= data.length) {
                            for (int y = height - 1; y >= 0; y--) {
                                int maskRow = andMaskOffset + (height - 1 - y) * maskRowBytes;
                                for (int x = 0; x < width; x++) {
                                    int byteIdx = maskRow + x / 8;
                                    int bit     = 7 - (x % 8);
                                    if (byteIdx < data.length && ((data[byteIdx] >> bit) & 1) == 1) {
                                        pixels[y * width + x] = 0x00000000;
                                    }
                                }
                            }
                        } else {
                        }
                    }

                    bmp.setPixels(pixels, 0, width, 0, 0, width, height);

                } else if (bpp == 24) {

                    int rowBytes = ((width * 3 + 3) / 4) * 4;
                    int[] pixels = new int[width * height];
                    for (int y = height - 1; y >= 0; y--) {
                        int rowStart = headerSize + (height - 1 - y) * rowBytes;
                        for (int x = 0; x < width; x++) {
                            int idx = rowStart + x * 3;
                            if (idx + 2 >= data.length) break;
                            int b = data[idx]     & 0xFF;
                            int g = data[idx + 1] & 0xFF;
                            int r = data[idx + 2] & 0xFF;
                            pixels[y * width + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
                        }
                    }
                    int maskOffset   = headerSize + rowBytes * height;
                    int maskRowBytes = ((width + 31) / 32) * 4;
                    if (maskOffset + maskRowBytes * height <= data.length) {
                        for (int y = height - 1; y >= 0; y--) {
                            int maskRow = maskOffset + (height - 1 - y) * maskRowBytes;
                            for (int x = 0; x < width; x++) {
                                int byteIdx = maskRow + x / 8;
                                int bit     = 7 - (x % 8);
                                if (byteIdx < data.length && ((data[byteIdx] >> bit) & 1) == 1) {
                                    pixels[y * width + x] = 0x00000000;
                                }
                            }
                        }
                    } else {
                    }
                    bmp.setPixels(pixels, 0, width, 0, 0, width, height);

                } else if (bpp == 8) {

                    int paletteSize = 256;
                    int[] palette   = new int[paletteSize];
                    int palOffset   = headerSize;
                    for (int i = 0; i < paletteSize; i++) {
                        if (palOffset + 3 >= data.length) break;
                        int b = data[palOffset++] & 0xFF;
                        int g = data[palOffset++] & 0xFF;
                        int r = data[palOffset++] & 0xFF;
                        palOffset++;
                        palette[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
                    }
                    int pixelOffset = headerSize + paletteSize * 4;
                    int rowBytes8   = ((width + 3) / 4) * 4;
                    int[] pixels    = new int[width * height];
                    for (int y = height - 1; y >= 0; y--) {
                        int rowStart = pixelOffset + (height - 1 - y) * rowBytes8;
                        for (int x = 0; x < width; x++) {
                            int idx = rowStart + x;
                            if (idx >= data.length) break;
                            pixels[y * width + x] = palette[data[idx] & 0xFF];
                        }
                    }
                    int maskOffset   = pixelOffset + rowBytes8 * height;
                    int maskRowBytes = ((width + 31) / 32) * 4;
                    if (maskOffset + maskRowBytes * height <= data.length) {
                        for (int y = height - 1; y >= 0; y--) {
                            int maskRow = maskOffset + (height - 1 - y) * maskRowBytes;
                            for (int x = 0; x < width; x++) {
                                int byteIdx = maskRow + x / 8;
                                int bit     = 7 - (x % 8);
                                if (byteIdx < data.length && ((data[byteIdx] >> bit) & 1) == 1) {
                                    pixels[y * width + x] = 0x00000000;
                                }
                            }
                        }
                    } else {
                    }
                    bmp.setPixels(pixels, 0, width, 0, 0, width, height);

                } else if (bpp == 4) {

                    int paletteSize = 16;
                    int[] palette   = new int[paletteSize];
                    int palOffset   = headerSize;
                    for (int i = 0; i < paletteSize; i++) {
                        if (palOffset + 3 >= data.length) break;
                        int b = data[palOffset++] & 0xFF;
                        int g = data[palOffset++] & 0xFF;
                        int r = data[palOffset++] & 0xFF;
                        palOffset++;
                        palette[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
                    }
                    int pixelOffset = headerSize + paletteSize * 4;
                    int rowBytes4   = ((width + 7) / 8) * 4;
                    int[] pixels    = new int[width * height];
                    for (int y = height - 1; y >= 0; y--) {
                        int rowStart = pixelOffset + (height - 1 - y) * rowBytes4;
                        for (int x = 0; x < width; x++) {
                            int idx = rowStart + x / 2;
                            if (idx >= data.length) break;
                            int nibble = (x % 2 == 0)
                                    ? ((data[idx] >> 4) & 0x0F)
                                    :  (data[idx]       & 0x0F);
                            pixels[y * width + x] = palette[nibble];
                        }
                    }
                    int maskOffset   = pixelOffset + rowBytes4 * height;
                    int maskRowBytes = ((width + 31) / 32) * 4;
                    if (maskOffset + maskRowBytes * height <= data.length) {
                        for (int y = height - 1; y >= 0; y--) {
                            int maskRow = maskOffset + (height - 1 - y) * maskRowBytes;
                            for (int x = 0; x < width; x++) {
                                int byteIdx = maskRow + x / 8;
                                int bit     = 7 - (x % 8);
                                if (byteIdx < data.length && ((data[byteIdx] >> bit) & 1) == 1) {
                                    pixels[y * width + x] = 0x00000000;
                                }
                            }
                        }
                    } else {
                    }
                    bmp.setPixels(pixels, 0, width, 0, 0, width, height);

                } else {
                    bmp.recycle();
                    return null;
                }

                return bmp;

            } catch (Exception e) {
                return null;
            }
        }


        private static int readLE16(RandomAccessFile r) throws Exception {
            return (r.read() & 0xFF) | ((r.read() & 0xFF) << 8);
        }

        private static int readLE32(RandomAccessFile r) throws Exception {
            return  (r.read() & 0xFF)
                 | ((r.read() & 0xFF) <<  8)
                 | ((r.read() & 0xFF) << 16)
                 | ((r.read() & 0xFF) << 24);
        }

        private static int readLE16(byte[] data, int offset) {
            return (data[offset] & 0xFF) | ((data[offset + 1] & 0xFF) << 8);
        }

        private static int readLE32(byte[] data, int offset) {
            return  (data[offset]     & 0xFF)
                 | ((data[offset + 1] & 0xFF) <<  8)
                 | ((data[offset + 2] & 0xFF) << 16)
                 | ((data[offset + 3] & 0xFF) << 24);
        }
    }
}
