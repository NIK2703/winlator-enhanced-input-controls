package com.winlator.cmod.inputcontrols;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.BlurMaskFilter;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.CornerPathEffect;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PathEffect;
import android.graphics.PointF;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.PorterDuffXfermode;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Xfermode;

import androidx.core.graphics.ColorUtils;
import com.winlator.cmod.inputcontrols.InputMode;
import com.winlator.cmod.inputcontrols.InputControlsManager;
import com.winlator.cmod.widget.InputControlsView;

import java.io.File;
import java.io.FileOutputStream;

import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.Map;

class ElementRenderer {
    static final int LAYER_FILL = 0;
    static final int LAYER_COMBINED = 1;
    static final int LAYER_GLOW = 2;
    static final int LAYER_STROKE_TEXT = 3;
    static final int LAYER_OUTER_STROKE = 4;

    static final int VL_FILL        = 1;
    static final int VL_STROKE_TEXT  = 2;
    static final int VL_GLOW        = 4;
    static final int VL_OUTER_STROKE = 8;

    static final int SHARED_POOL_MAX_SIZE = 64;
    static final LinkedHashMap<String, Bitmap> sharedPool = new LinkedHashMap<String, Bitmap>(SHARED_POOL_MAX_SIZE + 1, 0.75f, true) {
        @Override
        protected boolean removeEldestEntry(Map.Entry<String, Bitmap> eldest) {
            return size() > SHARED_POOL_MAX_SIZE;
        }
    };
    static final Xfermode XFERMODE_CLEAR = new PorterDuffXfermode(PorterDuff.Mode.CLEAR);
    static final Xfermode XFERMODE_DST_OUT = new PorterDuffXfermode(PorterDuff.Mode.DST_OUT);

    final ControlElement element;
    Bitmap cacheCombined;
    Bitmap cacheFill;
    Bitmap cacheGlow;
    Bitmap cacheStrokeText;
    Bitmap cacheOuterStroke;
    boolean cacheCombinedDirty = true;
    boolean cacheFillDirty = true;
    boolean cacheGlowDirty = true;
    boolean cacheStrokeTextDirty = true;
    boolean cacheOuterStrokeDirty = true;
    Bitmap dpadPetalActive;
    Bitmap dpadPetalInactive;
    boolean dpadCacheDirty = true;
    Bitmap cacheThumbFillActive;
    Bitmap cacheThumbFillInactive;
    Bitmap cacheThumbStrokePrimary;
    Bitmap cacheThumbStrokeSelected;
    boolean cacheThumbDirty = true;
    String cacheCombinedKey = "";
    PathEffect dpadPathEffect;
    PorterDuffColorFilter colorFilterPrimary;
    PorterDuffColorFilter colorFilterSecondary;
    final Rect drawIconSrcRect = new Rect();
    final Rect drawIconDstRect = new Rect();

    ElementRenderer(ControlElement element) {
        this.element = element;
    }

    // === Cache invalidation ===

    void invalidateAll() {
        element.cachedDisplayText = null;
        element.displayTextDirty = true;
        dpadPathEffect = null;
        colorFilterPrimary = null;
        colorFilterSecondary = null;
        cacheCombined = null;
        cacheFill = null;
        cacheGlow = null;
        cacheStrokeText = null;
        cacheOuterStroke = null;
        dpadPetalActive = null;
        dpadPetalInactive = null;
        cacheThumbFillActive = null;
        cacheThumbFillInactive = null;
        cacheThumbStrokePrimary = null;
        cacheThumbStrokeSelected = null;
        cacheCombinedDirty = true;
        cacheFillDirty = true;
        cacheGlowDirty = true;
        cacheStrokeTextDirty = true;
        cacheOuterStrokeDirty = true;
        dpadCacheDirty = true;
        cacheThumbDirty = true;
        if (element.inputControlsView != null) {
            File dir = cacheDir();
            String combinedKey = diskKey(LAYER_COMBINED);
            String fillKey = diskKey(LAYER_FILL);
            String strokeTextKey = diskKey(LAYER_STROKE_TEXT);
            for (String name : new String[]{combinedKey, fillKey, strokeTextKey, fillKey + "_dpad_active", fillKey + "_dpad_inactive"}) {
                File f = new File(dir, name + ".png");
                if (f.exists()) f.delete();
            }
        }
    }

    void invalidateKeepDisk() {
        element.refreshProfileCache();
        dpadPathEffect = null;
        cacheCombined = null;
        cacheFill = null;
        cacheGlow = null;
        cacheStrokeText = null;
        cacheOuterStroke = null;
        dpadPetalActive = null;
        dpadPetalInactive = null;
        cacheThumbFillActive = null;
        cacheThumbFillInactive = null;
        cacheThumbStrokePrimary = null;
        cacheThumbStrokeSelected = null;
        cacheCombinedDirty = true;
        cacheFillDirty = true;
        cacheGlowDirty = true;
        cacheStrokeTextDirty = true;
        cacheOuterStrokeDirty = true;
        dpadCacheDirty = true;
        cacheThumbDirty = true;
    }

    static void clearSharedPool() {
        sharedPool.clear();
    }

    static void deleteProfileCache(Context context, int profileId) {
        String prefix = profileId + "_";
        File dir = new File(InputControlsManager.getProfilesDir(context), "cache");
        if (dir.isDirectory()) {
            File[] files = dir.listFiles((d, n) -> n.startsWith(prefix));
            if (files != null) for (File f : files) f.delete();
        }
    }

    static void deleteAllCaches(Context context) {
        File dir = new File(InputControlsManager.getProfilesDir(context), "cache");
        if (dir.isDirectory()) {
            File[] files = dir.listFiles();
            if (files != null) for (File f : files) f.delete();
            dir.delete();
        }
    }

    // === Cache building ===

    void buildCache() {
        if (!isCachingEnabled()) return;
        if (element.getBoundingBox().width() <= 0) return;
        if (element.type == ControlElement.Type.D_PAD) {
            ensureDPadCaches(element.inputControlsView.getSnappingSize());
        } else {
            ensureCombinedCache();
            ensureFillCache();
            if (element.type == ControlElement.Type.BUTTON || element.type == ControlElement.Type.TRACKPAD) {
                ensureStrokeTextCache();
                ensureGlowCache();
            }
            if (element.type == ControlElement.Type.STICK) ensureThumbCaches();
        }
    }

    // === Private helpers ===

    private static final char[] HEX = "0123456789abcdef".toCharArray();

    private static String md5(String input) {
        try {
            java.security.MessageDigest md = java.security.MessageDigest.getInstance("MD5");
            byte[] digest = md.digest(input.getBytes("UTF-8"));
            char[] hexChars = new char[digest.length * 2];
            for (int i = 0; i < digest.length; i++) {
                int v = digest[i] & 0xFF;
                hexChars[i * 2] = HEX[v >>> 4];
                hexChars[i * 2 + 1] = HEX[v & 0x0F];
            }
            return new String(hexChars);
        } catch (Exception e) {
            return input.replaceAll("[^a-zA-Z0-9]", "_");
        }
    }

    File cacheDir() {
        File dir = new File(InputControlsManager.getProfilesDir(element.inputControlsView.getContext()), "cache");
        if (!dir.exists()) dir.mkdirs();
        return dir;
    }

    String diskKey(int layer) {
        ControlsProfile p = element.inputControlsView.getProfile();
        int pid = p != null ? p.id : 0;
        String prefix;
        switch (layer) {
            case LAYER_FILL: prefix = "f_"; break;
            case LAYER_STROKE_TEXT: prefix = "s_"; break;
            default: prefix = "c_"; break;
        }
        String raw = pid + "|" + element.type.ordinal() + "|" + element.shape.ordinal() + "|" + element.elementWidth + "|" + element.elementHeight
            + "|" + element.getEffectiveCornerRadius() + "|" + element.scale
            + "|" + element.cachedStrokeWidth + "|" + element.cachedFillAlphaInactive;
        if (layer == LAYER_FILL || layer == LAYER_COMBINED || layer == LAYER_STROKE_TEXT) {
            raw += "|" + element.getDisplayText() + "|" + element.iconId;
            if (element.hasCustomIcon()) raw += "|" + element.customIconData;
        }
        return pid + "_" + prefix + md5(raw);
    }

    String visualKey(int layer) {
        float effOp = element.getEffectiveOpacity();
        String base = element.type.ordinal() + "_" + element.shape.ordinal() + "_" + element.elementWidth + "_" + element.elementHeight + "_" + element.getEffectiveCornerRadius() + "_" + element.scale + "_" + (int)(effOp * 255) + "_" + element.cachedStrokeWidth + "_" + element.cachedFillAlphaInactive;
        String customSuffix = element.hasCustomIcon() ? "_" + element.customIconData.hashCode() : "";
        switch (layer) {
            case 0: return base + "_" + element.getDisplayText() + "_" + element.iconId + customSuffix + "_fill";
            case 1: return base + "_" + element.getDisplayText() + "_" + element.iconId + customSuffix + "_combined";
            case 3: return base + "_" + element.getDisplayText() + "_" + element.iconId + customSuffix + "_stroketext";
            default: return base;
        }
    }

    private boolean isCachingEnabled() {
        return element.inputControlsView.isCachingEnabled();
    }

    private int strokePad() {
        return Math.max(1, (int)Math.ceil(element.inputControlsView.getSnappingSize() * strokeWidthMultiplier() / 2) + 1);
    }

    private float strokeWidthMultiplier() {
        return element.cachedStrokeWidth;
    }

    private int fillAlphaInactive() {
        return element.cachedFillAlphaInactive;
    }

    private Bitmap loadFromDisk(String key) {
        File file = new File(cacheDir(), key + ".png");
        if (!file.isFile()) return null;
        return BitmapFactory.decodeFile(file.getAbsolutePath());
    }

    private void saveToDisk(String key, Bitmap bitmap) {
        if (com.winlator.cmod.widget.InputControlsView.skipDiskCache) return;
        try (FileOutputStream out = new FileOutputStream(new File(cacheDir(), key + ".png"))) {
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, out);
        } catch (Exception e) {

        }
    }

    private Bitmap obtainPoolBitmap(String vKey, int width, int height) {
        Bitmap bmp = sharedPool.get(vKey);
        if (bmp != null && !bmp.isRecycled() && bmp.getWidth() == width && bmp.getHeight() == height)
            return bmp;
        return null;
    }

    private Bitmap applyOpacity(Bitmap src, float opacity) {
        if (opacity >= 1f) return src;
        int w = src.getWidth();
        int h = src.getHeight();
        Bitmap result = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(result);
        Paint p = element.inputControlsView.getPaint();
        int savedAlpha = p.getAlpha();
        p.setAlpha((int)(opacity * 255));
        c.drawBitmap(src, 0, 0, p);
        p.setAlpha(savedAlpha);
        src.recycle();
        return result;
    }

    // === Ensure cache methods ===

    private void ensureCombinedCache() {
        if (!cacheCombinedDirty && cacheCombined != null) return;
        Rect box = element.getBoundingBox();
        int pad = strokePad();
        int w = box.width() + pad * 2;
        int h = box.height() + pad * 2;
        if (w <= 0 || h <= 0) return;
        cacheCombined = null;
        cacheCombinedKey = diskKey(LAYER_COMBINED);
        String vKey = visualKey(LAYER_COMBINED);
        cacheCombined = obtainPoolBitmap(vKey, w, h);
        if (cacheCombined == null) {
            cacheCombined = loadFromDisk(cacheCombinedKey);
            if (cacheCombined != null && (cacheCombined.getWidth() != w || cacheCombined.getHeight() != h)) {
                cacheCombined.recycle();
                cacheCombined = null;
            }
            if (cacheCombined != null) {
                cacheCombined = applyOpacity(cacheCombined, element.getEffectiveOpacity());
            }
        }
        if (cacheCombined == null) {
            cacheCombined = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            Canvas c = new Canvas(cacheCombined);
            c.translate(-box.left + pad, -box.top + pad);
            boolean savedSelected = element.selected;
            boolean savedTapActive = element.tapActive;
            boolean savedLongTapActive = element.longTapActive;
            boolean savedGestureActive = element.gestureActive;
            boolean[] savedStates = element.bindings.states.clone();
            element.selected = false;
            element.tapActive = false;
            element.longTapActive = false;
            element.gestureActive = false;
            Arrays.fill(element.bindings.states, false);
            element.inputControlsView.setCacheAlphaOverride(1.0f);
            element.buildingCache = true;
            draw(c);
            element.buildingCache = false;
            element.inputControlsView.setCacheAlphaOverride(-1);
            element.selected = savedSelected;
            element.tapActive = savedTapActive;
            element.longTapActive = savedLongTapActive;
            element.gestureActive = savedGestureActive;
            element.bindings.states[0] = savedStates[0];
            element.bindings.states[1] = savedStates[1];
            element.bindings.states[2] = savedStates[2];
            element.bindings.states[3] = savedStates[3];
            saveToDisk(cacheCombinedKey, cacheCombined);
            cacheCombined = applyOpacity(cacheCombined, element.getEffectiveOpacity());
            sharedPool.put(vKey, cacheCombined);
        }
        cacheCombinedDirty = false;
    }

    private void ensureFillCache() {
        if (!cacheFillDirty && cacheFill != null) return;
        Rect box = element.getBoundingBox();
        int pad = strokePad();
        int w = box.width() + pad * 2;
        int h = box.height() + pad * 2;
        if (w <= 0 || h <= 0) return;
        cacheFill = null;
        String vKey = visualKey(LAYER_FILL);
        cacheFill = obtainPoolBitmap(vKey, w, h);
        if (cacheFill == null) {
            cacheFill = loadFromDisk(diskKey(LAYER_FILL));
            if (cacheFill != null && (cacheFill.getWidth() != w || cacheFill.getHeight() != h)) {
                cacheFill.recycle();
                cacheFill = null;
            }
            if (cacheFill != null) {
                cacheFill = applyOpacity(cacheFill, element.getEffectiveOpacity());
            }
        }
        if (cacheFill == null) {
            cacheFill = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            Canvas c = new Canvas(cacheFill);
            c.translate(-box.left + pad, -box.top + pad);
            Paint paint = element.inputControlsView.getPaint();
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(ColorUtils.setAlphaComponent(Color.WHITE, 255));
            switch (element.type) {
                case BUTTON: {
                    int snappingSize = element.inputControlsView.getSnappingSize();
                    ControlsProfile prof = element.inputControlsView.getProfile();
                    float strW = snappingSize * (prof != null ? prof.getStrokeWidth() : 0.2f);
                    float halfStroke = strW * 0.5f;
                    switch (element.shape) {
                        case CIRCLE:
                            c.drawCircle(box.centerX(), box.centerY(), box.width() * 0.5f + halfStroke, paint);
                            break;
                        case RECT: {
                            float r = element.getEffectiveCornerRadius() * snappingSize * element.scale;
                            if (r > 0)
                                c.drawRoundRect(box.left - halfStroke, box.top - halfStroke,
                                    box.right + halfStroke, box.bottom + halfStroke, r, r, paint);
                            else
                                c.drawRect(box.left - halfStroke, box.top - halfStroke,
                                    box.right + halfStroke, box.bottom + halfStroke, paint);
                            break;
                        }
                    }
                    float cx = box.centerX();
                    float cy = box.centerY();
                    Bitmap customIconBitmap = element.getCustomIcon();
                    if (customIconBitmap != null) {
                        int margin = (int)(snappingSize * (element.shape == ControlElement.Shape.CIRCLE ? 2.0f : 1.0f) * element.scale);
                        int halfSize = (int)((Math.min(box.width(), box.height()) - margin) * 0.5f);
                        drawIconSrcRect.set(0, 0, customIconBitmap.getWidth(), customIconBitmap.getHeight());
                        drawIconDstRect.set((int)(cx - halfSize), (int)(cy - halfSize), (int)(cx + halfSize), (int)(cy + halfSize));
                        paint.setXfermode(XFERMODE_DST_OUT);
                        c.drawBitmap(customIconBitmap, drawIconSrcRect, drawIconDstRect, paint);
                        paint.setXfermode(null);
                    }
                    else if (element.iconId > 0) {
                        Bitmap icon = element.inputControlsView.getIcon((byte)element.iconId);
                        if (icon != null) {
                            int margin = (int)(snappingSize * (element.shape == ControlElement.Shape.CIRCLE ? 2.0f : 1.0f) * element.scale);
                            int halfSize = (int)((Math.min(box.width(), box.height()) - margin) * 0.5f);
                            drawIconSrcRect.set(0, 0, icon.getWidth(), icon.getHeight());
                            drawIconDstRect.set((int)(cx - halfSize), (int)(cy - halfSize), (int)(cx + halfSize), (int)(cy + halfSize));
                            paint.setXfermode(XFERMODE_DST_OUT);
                            c.drawBitmap(icon, drawIconSrcRect, drawIconDstRect, paint);
                            paint.setXfermode(null);
                        }
                    } else {
                        String text = element.getDisplayText();
                        paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, box.width() - strW * 2), snappingSize * 2 * element.scale));
                        paint.setTextAlign(Paint.Align.CENTER);
                        paint.setStyle(Paint.Style.FILL);
                        paint.setXfermode(XFERMODE_CLEAR);
                        c.drawText(text, element.x, (element.y - ((paint.descent() + paint.ascent()) * 0.5f)), paint);
                        paint.setXfermode(null);
                    }
                    break;
                }
                case STICK: {
                    float cx2 = box.centerX();
                    float cy2 = box.centerY();
                    short thumbRadius = (short) (element.inputControlsView.getSnappingSize() * 3.5f * element.scale);
                    c.drawCircle(cx2, cy2, thumbRadius, paint);
                    break;
                }
                case RANGE_BUTTON: {
                    int snappingSize = element.inputControlsView.getSnappingSize();
                    float radius = element.getEffectiveCornerRadius() * snappingSize * element.scale;
                    c.drawRoundRect(box.left, box.top, box.right, box.bottom, radius, radius, paint);
                    break;
                }
                case TRACKPAD: {
                    float radius = element.getEffectiveCornerRadius() * element.inputControlsView.getSnappingSize() * element.scale;
                    c.drawRoundRect(box.left, box.top, box.right, box.bottom, radius, radius, paint);
                    break;
                }
                default: break;
            }
            paint.setStyle(Paint.Style.STROKE);
            saveToDisk(diskKey(LAYER_FILL), cacheFill);
            cacheFill = applyOpacity(cacheFill, element.getEffectiveOpacity());
            sharedPool.put(vKey, cacheFill);
        }
        cacheFillDirty = false;
    }

    private void ensureGlowCache() {
        if (!cacheGlowDirty && cacheGlow != null) return;
        Rect box = element.getBoundingBox();
        int snappingSize = element.inputControlsView.getSnappingSize();
        float strokeWidth = snappingSize * element.cachedStrokeWidth;
        float halfStroke = strokeWidth * 0.5f;
        float glowThickness = 3.0f * snappingSize;
        float glowRadius = glowThickness;
        int blurPad = (int)Math.ceil(glowRadius) + 4;
        int pad = strokePad() + (int)Math.ceil(glowRadius) + blurPad;
        int w = box.width() + pad * 2;
        int h = box.height() + pad * 2;
        if (w <= 0 || h <= 0) return;
        cacheGlow = null;
        cacheGlow = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(cacheGlow);
        c.translate(-box.left + pad, -box.top + pad);
        float cx = box.centerX();
        float cy = box.centerY();
        Path fullPath = new Path();
        fullPath.addRect(box.left - pad, box.top - pad, box.right + pad, box.bottom + pad, Path.Direction.CW);
        Path buttonPath = new Path();
        switch (element.shape) {
            case CIRCLE:
                buttonPath.addCircle(cx, cy, box.width() * 0.5f + halfStroke, Path.Direction.CW);
                break;
            case RECT: {
                float r = element.getEffectiveCornerRadius() * snappingSize * element.scale;
                if (r > 0) {
                    buttonPath.addRoundRect(box.left - halfStroke, box.top - halfStroke,
                        box.right + halfStroke, box.bottom + halfStroke, r, r, Path.Direction.CW);
                } else {
                    buttonPath.addRect(box.left - halfStroke, box.top - halfStroke,
                        box.right + halfStroke, box.bottom + halfStroke, Path.Direction.CW);
                }
                break;
            }
        }
        fullPath.op(buttonPath, Path.Op.DIFFERENCE);
        c.clipPath(fullPath);
        float expand = glowRadius * 0.3f;
        Path glowFillPath = new Path();
        switch (element.shape) {
            case CIRCLE:
                glowFillPath.addCircle(cx, cy, box.width() * 0.5f + halfStroke + expand, Path.Direction.CW);
                break;
            case RECT: {
                float r = element.getEffectiveCornerRadius() * snappingSize * element.scale;
                float er = r + expand;
                if (r > 0) {
                    glowFillPath.addRoundRect(
                        box.left - halfStroke - expand, box.top - halfStroke - expand,
                        box.right + halfStroke + expand, box.bottom + halfStroke + expand,
                        er, er, Path.Direction.CW);
                } else {
                    glowFillPath.addRect(
                        box.left - halfStroke - expand, box.top - halfStroke - expand,
                        box.right + halfStroke + expand, box.bottom + halfStroke + expand,
                        Path.Direction.CW);
                }
                break;
            }
        }
        Paint glowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        glowPaint.setStyle(Paint.Style.FILL);
        glowPaint.setColor(Color.argb(96, 255, 255, 255));
        glowPaint.setMaskFilter(new BlurMaskFilter(glowRadius * 0.3f, BlurMaskFilter.Blur.NORMAL));
        c.drawPath(glowFillPath, glowPaint);
        glowPaint.setMaskFilter(null);
        glowPaint.setMaskFilter(new BlurMaskFilter(glowRadius, BlurMaskFilter.Blur.NORMAL));
        c.drawPath(glowFillPath, glowPaint);
        glowPaint.setMaskFilter(null);
        cacheGlow = applyOpacity(cacheGlow, element.getEffectiveOpacity());
        cacheGlowDirty = false;
    }

    private void ensureStrokeTextCache() {
        if (!cacheStrokeTextDirty && cacheStrokeText != null) return;
        Rect box = element.getBoundingBox();
        int snappingSize = element.inputControlsView.getSnappingSize();
        float strokeWidth = snappingSize * element.cachedStrokeWidth;
        int pad = strokePad();
        int w = box.width() + pad * 2;
        int h = box.height() + pad * 2;
        if (w <= 0 || h <= 0) return;
        cacheStrokeText = null;
        String vKey = visualKey(LAYER_STROKE_TEXT);
        cacheStrokeText = obtainPoolBitmap(vKey, w, h);
        if (cacheStrokeText == null) {
            cacheStrokeText = loadFromDisk(diskKey(LAYER_STROKE_TEXT));
            if (cacheStrokeText != null && (cacheStrokeText.getWidth() != w || cacheStrokeText.getHeight() != h)) {
                cacheStrokeText.recycle();
                cacheStrokeText = null;
            }
            if (cacheStrokeText != null) {
                cacheStrokeText = applyOpacity(cacheStrokeText, element.getEffectiveOpacity());
            }
        }
        if (cacheStrokeText == null) {
            cacheStrokeText = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            Canvas c = new Canvas(cacheStrokeText);
            c.translate(-box.left + pad, -box.top + pad);
            Paint paint = element.inputControlsView.getPaint();
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(strokeWidth);
            paint.setColor(ColorUtils.setAlphaComponent(Color.WHITE, 255));
            drawButtonShape(c, box, snappingSize, paint);
            float cx = box.centerX();
            float cy = box.centerY();
            paint.setStyle(Paint.Style.FILL);
            paint.setColorFilter(new PorterDuffColorFilter(Color.WHITE, PorterDuff.Mode.SRC_IN));
            Bitmap customIconBitmap = element.getCustomIcon();
            if (customIconBitmap != null) {
                int margin = (int)(snappingSize * (element.shape == ControlElement.Shape.CIRCLE ? 2.0f : 1.0f) * element.scale);
                int halfSize = (int)((Math.min(box.width(), box.height()) - margin) * 0.5f);
                drawIconSrcRect.set(0, 0, customIconBitmap.getWidth(), customIconBitmap.getHeight());
                drawIconDstRect.set((int)(cx - halfSize), (int)(cy - halfSize), (int)(cx + halfSize), (int)(cy + halfSize));
                c.drawBitmap(customIconBitmap, drawIconSrcRect, drawIconDstRect, paint);
            }
            else if (element.iconId > 0) {
                Bitmap icon = element.inputControlsView.getIcon((byte)element.iconId);
                if (icon != null) {
                    int margin = (int)(snappingSize * (element.shape == ControlElement.Shape.CIRCLE ? 2.0f : 1.0f) * element.scale);
                    int halfSize = (int)((Math.min(box.width(), box.height()) - margin) * 0.5f);
                    drawIconSrcRect.set(0, 0, icon.getWidth(), icon.getHeight());
                    drawIconDstRect.set((int)(cx - halfSize), (int)(cy - halfSize), (int)(cx + halfSize), (int)(cy + halfSize));
                    c.drawBitmap(icon, drawIconSrcRect, drawIconDstRect, paint);
                }
            } else {
                String text = element.getDisplayText();
                paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, box.width() - strokeWidth * 2), snappingSize * 2 * element.scale));
                paint.setTextAlign(Paint.Align.CENTER);
                paint.setColor(ColorUtils.setAlphaComponent(Color.WHITE, 255));
                c.drawText(text, element.x, (element.y - ((paint.descent() + paint.ascent()) * 0.5f)), paint);
            }
            paint.setColorFilter(null);
            saveToDisk(diskKey(LAYER_STROKE_TEXT), cacheStrokeText);
            cacheStrokeText = applyOpacity(cacheStrokeText, element.getEffectiveOpacity());
            sharedPool.put(vKey, cacheStrokeText);
        }
        cacheStrokeTextDirty = false;
    }

    private void ensureOuterStrokeCache() {
        if (!cacheOuterStrokeDirty && cacheOuterStroke != null) return;
        Rect box = element.getBoundingBox();
        int snappingSize = element.inputControlsView.getSnappingSize();
        float strokeWidth = snappingSize * element.cachedStrokeWidth;
        float lpsWidth = strokeWidth;
        float lpsOff = strokeWidth * 0.5f + snappingSize * 0.1f + lpsWidth * 0.5f;
        int pad = (int)Math.ceil(lpsOff + lpsWidth) + 4;
        int w = box.width() + pad * 2;
        int h = box.height() + pad * 2;
        if (w <= 0 || h <= 0) return;
        cacheOuterStroke = null;
        String vKey = visualKey(LAYER_OUTER_STROKE);
        cacheOuterStroke = obtainPoolBitmap(vKey, w, h);
        if (cacheOuterStroke == null) {
            cacheOuterStroke = loadFromDisk(diskKey(LAYER_OUTER_STROKE));
            if (cacheOuterStroke != null && (cacheOuterStroke.getWidth() != w || cacheOuterStroke.getHeight() != h)) {
                cacheOuterStroke.recycle();
                cacheOuterStroke = null;
            }
            if (cacheOuterStroke != null) {
                cacheOuterStroke = applyOpacity(cacheOuterStroke, element.getEffectiveOpacity());
            }
        }
        if (cacheOuterStroke == null) {
            cacheOuterStroke = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            Canvas c = new Canvas(cacheOuterStroke);
            c.translate(-box.left + pad, -box.top + pad);
            Paint paint = element.inputControlsView.getPaint();
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(lpsWidth);
            paint.setColor(ColorUtils.setAlphaComponent(Color.WHITE, 255));
            float cx = box.centerX();
            float cy = box.centerY();
            switch (element.shape) {
                case CIRCLE:
                    c.drawCircle(cx, cy, box.width() * 0.5f + lpsOff, paint);
                    break;
                case RECT: {
                    float r = element.getEffectiveCornerRadius() * snappingSize * element.scale;
                    float er = r + lpsOff;
                    c.drawRoundRect(box.left - lpsOff, box.top - lpsOff,
                        box.right + lpsOff, box.bottom + lpsOff, er, er, paint);
                    break;
                }
            }
            saveToDisk(diskKey(LAYER_OUTER_STROKE), cacheOuterStroke);
            cacheOuterStroke = applyOpacity(cacheOuterStroke, element.getEffectiveOpacity());
            sharedPool.put(vKey, cacheOuterStroke);
        }
        cacheOuterStrokeDirty = false;
    }

    private void ensureThumbCaches() {
        if (element.type != ControlElement.Type.STICK) return;
        if (!cacheThumbDirty &&
            cacheThumbFillActive != null &&
            cacheThumbFillInactive != null &&
            cacheThumbStrokePrimary != null &&
            cacheThumbStrokeSelected != null) return;
        cacheThumbFillActive = null;
        cacheThumbFillInactive = null;
        cacheThumbStrokePrimary = null;
        cacheThumbStrokeSelected = null;
        cacheThumbDirty = false;
        int snappingSize = element.inputControlsView.getSnappingSize();
        short thumbRadius = (short)(snappingSize * 3.5f * element.scale);
        float strokeWidth = snappingSize * strokeWidthMultiplier();
        int halfSize = (int)(thumbRadius + strokeWidth * 0.5f + 1);
        int size = halfSize * 2;
        if (size <= 0) return;

        int baseAlpha = Color.alpha(element.inputControlsView.getPrimaryColor());
        int fillActiveAlpha = baseAlpha;
        int fillInactiveAlpha = baseAlpha * fillAlphaInactive() / 255;

        cacheThumbFillActive = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(cacheThumbFillActive);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setStyle(Paint.Style.FILL);
        p.setColor(ColorUtils.setAlphaComponent(Color.WHITE, fillActiveAlpha));
        c.drawCircle(halfSize, halfSize, thumbRadius, p);

        cacheThumbFillInactive = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        c = new Canvas(cacheThumbFillInactive);
        p.setColor(ColorUtils.setAlphaComponent(Color.WHITE, fillInactiveAlpha));
        c.drawCircle(halfSize, halfSize, thumbRadius, p);

        int primaryColor = element.inputControlsView.getPrimaryColor();
        int secondaryColor = element.inputControlsView.getSecondaryColor();

        cacheThumbStrokePrimary = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        c = new Canvas(cacheThumbStrokePrimary);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(strokeWidth);
        p.setColor(ColorUtils.setAlphaComponent(Color.WHITE, 255));
        if (colorFilterPrimary == null) colorFilterPrimary = new PorterDuffColorFilter(primaryColor, PorterDuff.Mode.SRC_IN);
        p.setColorFilter(colorFilterPrimary);
        c.drawCircle(halfSize, halfSize, thumbRadius, p);

        cacheThumbStrokeSelected = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        c = new Canvas(cacheThumbStrokeSelected);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(strokeWidth);
        p.setColor(ColorUtils.setAlphaComponent(Color.WHITE, 255));
        if (colorFilterSecondary == null) colorFilterSecondary = new PorterDuffColorFilter(secondaryColor, PorterDuff.Mode.SRC_IN);
        p.setColorFilter(colorFilterSecondary);
        p.setColor(Color.WHITE);
        c.drawCircle(halfSize, halfSize, thumbRadius + strokeWidth * 0.5f, p);
    }

    private void buildUpPetal(Rect box, int snappingSize, Path path) {
        float cx = box.centerX();
        float cy = box.centerY();
        float offsetX = snappingSize * 2 * element.scale;
        float offsetY = snappingSize * 3 * element.scale;
        float start = snappingSize * element.scale;
        path.rewind();
        path.moveTo(cx, cy - start);
        path.lineTo(cx - offsetX, cy - offsetY);
        path.lineTo(cx - offsetX, box.top);
        path.lineTo(cx + offsetX, box.top);
        path.lineTo(cx + offsetX, cy - offsetY);
        path.close();
    }

    private void ensureDPadCaches(int snappingSize) {
        Rect box = element.getBoundingBox();
        int pad = strokePad();
        int w = box.width() + pad * 2;
        int h = box.height() + pad * 2;
        if (w <= 0 || h <= 0) return;
        if (!dpadCacheDirty && dpadPetalActive != null && dpadPetalInactive != null &&
            dpadPetalActive.getWidth() == w && dpadPetalActive.getHeight() == h) return;
        dpadPetalActive = null;
        dpadPetalInactive = null;
        Paint paint = element.inputControlsView.getPaint();
        float strokeWidth = snappingSize * strokeWidthMultiplier();
        paint.setPathEffect(null);
        float effCr = element.getEffectiveCornerRadius();
        if (effCr > 0) {
            if (dpadPathEffect == null) dpadPathEffect = new CornerPathEffect(effCr * snappingSize * element.scale);
            paint.setPathEffect(dpadPathEffect);
        }
        int inactiveAlpha = fillAlphaInactive();
        String activeKey = diskKey(LAYER_FILL) + "_dpad_active";
        String inactiveKey = diskKey(LAYER_FILL) + "_dpad_inactive";

        dpadPetalActive = loadFromDisk(activeKey);
        if (dpadPetalActive != null && (dpadPetalActive.getWidth() != w || dpadPetalActive.getHeight() != h)) {
            dpadPetalActive.recycle();
            dpadPetalActive = null;
        }
        if (dpadPetalActive == null) {
            dpadPetalActive = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            Canvas c = new Canvas(dpadPetalActive);
            c.translate(-box.left + pad, -box.top + pad);
            Path petalPath = element.inputControlsView.getPath();
            buildUpPetal(box, snappingSize, petalPath);
            paint.setColor(ColorUtils.setAlphaComponent(Color.WHITE, 255));
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(strokeWidth);
            c.drawPath(petalPath, paint);
            paint.setStyle(Paint.Style.FILL);
            c.drawPath(petalPath, paint);
            saveToDisk(activeKey, dpadPetalActive);
        }

        dpadPetalInactive = loadFromDisk(inactiveKey);
        if (dpadPetalInactive != null && (dpadPetalInactive.getWidth() != w || dpadPetalInactive.getHeight() != h)) {
            dpadPetalInactive.recycle();
            dpadPetalInactive = null;
        }
        if (dpadPetalInactive == null) {
            dpadPetalInactive = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            Canvas c = new Canvas(dpadPetalInactive);
            c.translate(-box.left + pad, -box.top + pad);
            Path petalPath = element.inputControlsView.getPath();
            buildUpPetal(box, snappingSize, petalPath);
            paint.setColor(ColorUtils.setAlphaComponent(Color.WHITE, 255));
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(strokeWidth);
            c.drawPath(petalPath, paint);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(ColorUtils.setAlphaComponent(Color.WHITE, inactiveAlpha));
            c.drawPath(petalPath, paint);
            saveToDisk(inactiveKey, dpadPetalInactive);
        }

        paint.setPathEffect(null);
        dpadCacheDirty = false;
    }

    // === Drawing methods ===

    void drawCached(Canvas canvas) {
        if (!isCachingEnabled()) { draw(canvas); return; }
        Rect box = element.getBoundingBox();
        if (box.width() <= 0 || box.height() <= 0) return;
        if (element.selected) { draw(canvas); return; }

        int pad = strokePad();
        int snappingSize = element.inputControlsView.getSnappingSize();
        Paint paint = element.inputControlsView.getPaint();
        int elementAlpha = element.getEffectiveAlphaInt();
        int primaryColor = Color.argb(elementAlpha, 255, 255, 255);
        int secondaryColor = Color.argb(elementAlpha, 2, 119, 189);
        int colorAlpha = elementAlpha;
        int inactiveAlpha = colorAlpha * fillAlphaInactive() / 255;

        {
            if (element.type == ControlElement.Type.D_PAD) {
                ensureDPadCaches(snappingSize);
                if (dpadPetalActive == null || dpadPetalInactive == null) { draw(canvas); return; }
                float cx = box.centerX();
                float cy = box.centerY();
                float strokeWidth = snappingSize * strokeWidthMultiplier();
                boolean engagedDpad = element.isEngaged();
                paint.setStyle(Paint.Style.FILL);
                for (int i = 0; i < 4; i++) {
                    canvas.save();
                    canvas.rotate(i * 90, cx, cy);
                    Bitmap petal = (engagedDpad && element.bindings.states[i]) ? dpadPetalActive : dpadPetalInactive;
                    paint.setColor(ColorUtils.setAlphaComponent(primaryColor, colorAlpha));
                    canvas.drawBitmap(petal, box.left - pad, box.top - pad, paint);
                    canvas.restore();
                }
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(strokeWidth);
                paint.setColor(primaryColor);
                return;
            }

            boolean engaged = element.isEngaged();

            if (engaged && element.type == ControlElement.Type.BUTTON) {

                int savedColor = paint.getColor();
                Paint.Style savedStyle = paint.getStyle();
                float savedStrokeWidth = paint.getStrokeWidth();
                float lwStroke = snappingSize * element.cachedStrokeWidth;
                float cx = box.centerX();
                float cy = box.centerY();

                if ((element.visualLayers & VL_GLOW) != 0) {
                    ensureGlowCache();
                    if (cacheGlow != null) {
                        int glowPad = (cacheGlow.getWidth() - box.width()) / 2;
                        int glowAlpha = (element.visualAlphas >> 0) & 0xFF;
                        paint.setColor(ColorUtils.setAlphaComponent(primaryColor, glowAlpha));
                        paint.setStyle(Paint.Style.FILL);
                        canvas.drawBitmap(cacheGlow, box.left - glowPad, box.top - glowPad, paint);
                    }
                }

                if ((element.visualLayers & VL_FILL) != 0) {
                    ensureFillCache();
                    if (cacheFill != null) {
                        int fillAlpha = (element.visualAlphas >> 8) & 0xFF;
                        paint.setColor(ColorUtils.setAlphaComponent(primaryColor, fillAlpha));
                        paint.setStyle(Paint.Style.FILL);
                        canvas.drawBitmap(cacheFill, box.left - pad, box.top - pad, paint);
                    }
                }

                if ((element.visualLayers & VL_STROKE_TEXT) != 0) {
                    ensureStrokeTextCache();
                    if (cacheStrokeText != null) {
                        int stAlpha = (element.visualAlphas >> 16) & 0xFF;
                        paint.setColor(ColorUtils.setAlphaComponent(primaryColor, stAlpha));
                        paint.setStyle(Paint.Style.FILL);
                        canvas.drawBitmap(cacheStrokeText, box.left - pad, box.top - pad, paint);
                    }
                }

                if ((element.visualLayers & VL_OUTER_STROKE) != 0) {
                    ensureOuterStrokeCache();
                    if (cacheOuterStroke != null) {
                        int osPad = (cacheOuterStroke.getWidth() - box.width()) / 2;
                        int osAlpha = (element.visualAlphas >> 24) & 0xFF;
                        paint.setColor(ColorUtils.setAlphaComponent(primaryColor, osAlpha));
                        paint.setStyle(Paint.Style.FILL);
                        canvas.drawBitmap(cacheOuterStroke, box.left - osPad, box.top - osPad, paint);
                    }
                }

                paint.setStyle(savedStyle);
                paint.setColor(savedColor);
                paint.setStrokeWidth(savedStrokeWidth);
                return;
            }

            if (element.type != ControlElement.Type.TRACKPAD && element.type != ControlElement.Type.STICK && element.type != ControlElement.Type.BUTTON && element.type != ControlElement.Type.RANGE_BUTTON) {
                int targetAlpha = fillAlphaInactive();
                if (targetAlpha > 0) {
                    ensureFillCache();
                    if (cacheFill != null) {
                        int savedColor = paint.getColor();
                        Paint.Style savedStyle = paint.getStyle();
                        float savedStrokeWidth = paint.getStrokeWidth();
                        int fillAlpha = colorAlpha * targetAlpha / 255;
                        paint.setColor(ColorUtils.setAlphaComponent(primaryColor, fillAlpha));
                        paint.setStyle(Paint.Style.FILL);
                        canvas.drawBitmap(cacheFill, box.left - pad, box.top - pad, paint);
                        paint.setStyle(savedStyle);
                        paint.setColor(savedColor);
                        paint.setStrokeWidth(savedStrokeWidth);
                    }
                }
            }

            if (element.type == ControlElement.Type.BUTTON) {
                ensureFillCache();
                ensureStrokeTextCache();
                if (cacheFill != null && cacheStrokeText != null) {
                    paint.setColor(ColorUtils.setAlphaComponent(Color.WHITE, fillAlphaInactive()));
                    paint.setStyle(Paint.Style.FILL);
                    canvas.drawBitmap(cacheFill, box.left - pad, box.top - pad, paint);
                    paint.setColor(Color.WHITE);
                    canvas.drawBitmap(cacheStrokeText, box.left - pad, box.top - pad, paint);
                } else {
                    draw(canvas);
                    return;
                }
            } else {
                ensureCombinedCache();
                if (cacheCombined != null)
                    canvas.drawBitmap(cacheCombined, box.left - pad, box.top - pad, null);
                else {
                    draw(canvas);
                    return;
                }
            }

        if (element.type == ControlElement.Type.STICK || element.type == ControlElement.Type.RANGE_BUTTON) {
            float strokeWidth = snappingSize * strokeWidthMultiplier();
            paint.setColor(element.selected ? secondaryColor : primaryColor);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(strokeWidth);
            if (element.type == ControlElement.Type.STICK) {
                float thumbstickX = element.getCurrentPosition().x;
                float thumbstickY = element.getCurrentPosition().y;
                ensureThumbCaches();
                if (cacheThumbFillActive != null) {
                    int halfSize = cacheThumbFillActive.getWidth() / 2;
                    if (engaged) {
                        canvas.drawBitmap(cacheThumbFillActive, thumbstickX - halfSize, thumbstickY - halfSize, null);
                    } else {
                        canvas.drawBitmap(cacheThumbFillInactive, thumbstickX - halfSize, thumbstickY - halfSize, null);
                        Bitmap stroke = element.selected ? cacheThumbStrokeSelected : cacheThumbStrokePrimary;
                        if (stroke != null)
                            canvas.drawBitmap(stroke, thumbstickX - halfSize, thumbstickY - halfSize, null);
                    }
                } else {
                    short thumbRadius = (short) (snappingSize * 3.5f * element.scale);
                    int savedColor = paint.getColor();
                    paint.setStyle(Paint.Style.FILL);
                    paint.setColorFilter(null);
                    paint.setColor(ColorUtils.setAlphaComponent(Color.WHITE, (engaged ? colorAlpha : inactiveAlpha)));
                    canvas.drawCircle(thumbstickX, thumbstickY, thumbRadius, paint);
                    if (!engaged) {
                        paint.setStyle(Paint.Style.STROKE);
                        paint.setStrokeWidth(strokeWidth);
                        paint.setColor(savedColor);
                        canvas.drawCircle(thumbstickX, thumbstickY, thumbRadius + strokeWidth * 0.5f, paint);
                    }
                    paint.setColor(savedColor);
                }
            } else {
                float radius = element.getEffectiveCornerRadius() * snappingSize * element.scale;
                float elementSize = element.scroller.getElementSize();
                float minTextSize = snappingSize * 2 * element.scale;
                float scrollOffset = element.scroller.getScrollOffset();
                ControlElement.Range range = element.getRange();
                Path path = element.inputControlsView.getPath();
                path.reset();
                if (element.orientation == 0) {
                    float lineTop = box.top + strokeWidth * 0.5f;
                    float lineBottom = box.bottom - strokeWidth * 0.5f;
                    canvas.save();
                    path.addRoundRect(box.left, box.top, box.right, box.bottom, radius, radius, Path.Direction.CW);
                    canvas.clipPath(path);
                    float startX = box.left - scrollOffset % elementSize;
                    for (int i = element.scroller.getRangeIndexFrom(); i < element.scroller.getRangeIndexTo(); i++) {
                        int index = i % range.max;
                        paint.setStyle(Paint.Style.STROKE);
                        paint.setColor(primaryColor);
                        if (startX > box.left && startX < box.right) canvas.drawLine(startX, lineTop, startX, lineBottom, paint);
                        String text = getRangeTextForIndex(range, index);
                        if (startX < box.right && startX + elementSize > box.left) {
                            if (element.scroller.isActionDown() && element.scroller.getPressedIndex() == index) {
                                paint.setStyle(Paint.Style.FILL);
                                paint.setColor(ColorUtils.setAlphaComponent(primaryColor, colorAlpha));
                                canvas.drawRect(startX, lineTop, startX + elementSize, lineBottom, paint);
                                paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, elementSize - strokeWidth * 2), minTextSize));
                                paint.setTextAlign(Paint.Align.CENTER);
                                paint.setXfermode(XFERMODE_CLEAR);
                                canvas.drawText(text, startX + elementSize * 0.5f, (element.y - ((paint.descent() + paint.ascent()) * 0.5f)), paint);
                                paint.setXfermode(null);
                            } else {
                                paint.setStyle(Paint.Style.FILL);
                                paint.setColor(primaryColor);
                                paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, elementSize - strokeWidth * 2), minTextSize));
                                paint.setTextAlign(Paint.Align.CENTER);
                                canvas.drawText(text, startX + elementSize * 0.5f, (element.y - ((paint.descent() + paint.ascent()) * 0.5f)), paint);
                            }
                        }
                        startX += elementSize;
                    }
                    paint.setStyle(Paint.Style.STROKE);
                    paint.setColor(primaryColor);
                    canvas.restore();
                } else {
                    float lineLeft = box.left + strokeWidth * 0.5f;
                    float lineRight = box.right - strokeWidth * 0.5f;
                    canvas.save();
                    path.addRoundRect(box.left, box.top, box.right, box.bottom, radius, radius, Path.Direction.CW);
                    canvas.clipPath(path);
                    float startY = box.top - scrollOffset % elementSize;
                    for (int i = element.scroller.getRangeIndexFrom(); i < element.scroller.getRangeIndexTo(); i++) {
                        paint.setStyle(Paint.Style.STROKE);
                        paint.setColor(primaryColor);
                        if (startY > box.top && startY < box.bottom) canvas.drawLine(lineLeft, startY, lineRight, startY, paint);
                        String text = getRangeTextForIndex(range, i);
                        if (startY < box.bottom && startY + elementSize > box.top) {
                            if (element.scroller.isActionDown() && element.scroller.getPressedIndex() == (i % range.max)) {
                                paint.setStyle(Paint.Style.FILL);
                                paint.setColor(ColorUtils.setAlphaComponent(primaryColor, colorAlpha));
                                canvas.drawRect(lineLeft, startY, lineRight, startY + elementSize, paint);
                                paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, box.width() - strokeWidth * 2), minTextSize));
                                paint.setTextAlign(Paint.Align.CENTER);
                                paint.setXfermode(XFERMODE_CLEAR);
                                canvas.drawText(text, element.x, startY + elementSize * 0.5f - ((paint.descent() + paint.ascent()) * 0.5f), paint);
                                paint.setXfermode(null);
                            } else {
                                paint.setStyle(Paint.Style.FILL);
                                paint.setColor(primaryColor);
                                paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, box.width() - strokeWidth * 2), minTextSize));
                                paint.setTextAlign(Paint.Align.CENTER);
                                canvas.drawText(text, element.x, startY + elementSize * 0.5f - ((paint.descent() + paint.ascent()) * 0.5f), paint);
                            }
                        }
                        startY += elementSize;
                    }
                    paint.setStyle(Paint.Style.STROKE);
                    paint.setColor(primaryColor);
                    canvas.restore();
                }
            }
        }
        }
    }

    void draw(Canvas canvas) {
        int snappingSize = element.inputControlsView.getSnappingSize();
        Paint paint = element.inputControlsView.getPaint();
        int elementAlpha = element.getEffectiveAlphaInt();
        int primaryColor = Color.argb(elementAlpha, 255, 255, 255);
        int secondaryColor = Color.argb(elementAlpha, 2, 119, 189);
        int colorAlpha = elementAlpha;
        int inactiveFillAlpha = colorAlpha * fillAlphaInactive() / 255;

        paint.setColor(element.selected ? secondaryColor : primaryColor);
        paint.setStyle(Paint.Style.STROKE);
        float strokeWidth = snappingSize * element.cachedStrokeWidth;
        paint.setStrokeWidth(strokeWidth);
        Rect boundingBox = element.getBoundingBox();
        boolean engaged = element.isEngaged();
        int fillAlpha = engaged ? colorAlpha : inactiveFillAlpha;
        int fillColor = ColorUtils.setAlphaComponent(primaryColor, fillAlpha);

        switch (element.type) {
            case BUTTON: {
                float cx = boundingBox.centerX();
                float cy = boundingBox.centerY();

                if ((element.visualLayers & VL_GLOW) != 0) {
                    ensureGlowCache();
                    if (cacheGlow != null) {
                        int glowPad = (cacheGlow.getWidth() - boundingBox.width()) / 2;
                        int glowAlpha = (element.visualAlphas >> 0) & 0xFF;
                        paint.setColor(ColorUtils.setAlphaComponent(primaryColor, glowAlpha));
                        paint.setStyle(Paint.Style.FILL);
                        canvas.drawBitmap(cacheGlow, boundingBox.left - glowPad, boundingBox.top - glowPad, paint);
                    }
                }

                if ((element.visualLayers & VL_FILL) != 0) {
                    float halfStroke = strokeWidth * 0.5f;
                    int fillAlphaVL = (element.visualAlphas >> 8) & 0xFF;
                    paint.setStyle(Paint.Style.FILL);
                    paint.setColor(ColorUtils.setAlphaComponent(primaryColor, fillAlphaVL));
                    switch (element.shape) {
                        case CIRCLE:
                            canvas.drawCircle(cx, cy, boundingBox.width() * 0.5f + halfStroke, paint);
                            break;
                        case RECT: {
                            float r = element.getEffectiveCornerRadius() * snappingSize * element.scale;
                            if (r > 0)
                                canvas.drawRoundRect(boundingBox.left - halfStroke, boundingBox.top - halfStroke,
                                    boundingBox.right + halfStroke, boundingBox.bottom + halfStroke, r, r, paint);
                            else
                                canvas.drawRect(boundingBox.left - halfStroke, boundingBox.top - halfStroke,
                                    boundingBox.right + halfStroke, boundingBox.bottom + halfStroke, paint);
                            break;
                        }
                    }
                }

                if ((element.visualLayers & VL_STROKE_TEXT) != 0) {
                    int stAlpha = (element.visualAlphas >> 16) & 0xFF;
                    paint.setStyle(Paint.Style.STROKE);
                    paint.setColor(ColorUtils.setAlphaComponent(primaryColor, stAlpha));
                    paint.setStrokeWidth(strokeWidth);
                    drawButtonShape(canvas, boundingBox, snappingSize, paint);

                    Bitmap customIconBitmap = element.getCustomIcon();
                    if (customIconBitmap != null) {
                        int margin = (int)(snappingSize * 1.5f * element.scale);
                        int halfSize = (int)((Math.min(boundingBox.width(), boundingBox.height()) - margin) * 0.5f);
                        drawIconSrcRect.set(0, 0, customIconBitmap.getWidth(), customIconBitmap.getHeight());
                        drawIconDstRect.set((int)(cx - halfSize), (int)(cy - halfSize), (int)(cx + halfSize), (int)(cy + halfSize));
                        canvas.drawBitmap(customIconBitmap, drawIconSrcRect, drawIconDstRect, paint);
                    }
                    else if (element.iconId > 0) {
                        drawIcon(canvas, cx, cy, boundingBox.width(), boundingBox.height(), element.iconId);
                    }
                    else {
                        String text = element.getDisplayText();
                        paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, boundingBox.width() - strokeWidth * 2), snappingSize * 2 * element.scale));
                        paint.setTextAlign(Paint.Align.CENTER);
                        paint.setStyle(Paint.Style.FILL);
                        paint.setColor(primaryColor);
                        canvas.drawText(text, element.x, (element.y - ((paint.descent() + paint.ascent()) * 0.5f)), paint);
                    }
                }

                if ((element.visualLayers & VL_OUTER_STROKE) != 0) {
                    ensureOuterStrokeCache();
                    if (cacheOuterStroke != null) {
                        int osPad = (cacheOuterStroke.getWidth() - boundingBox.width()) / 2;
                        int osAlpha = (element.visualAlphas >> 24) & 0xFF;
                        paint.setColor(ColorUtils.setAlphaComponent(primaryColor, osAlpha));
                        paint.setStyle(Paint.Style.FILL);
                        canvas.drawBitmap(cacheOuterStroke, boundingBox.left - osPad, boundingBox.top - osPad, paint);
                    }
                }

                if (element.visualLayers == 0) {
                    float halfStroke = strokeWidth * 0.5f;
                    int fillAlphaFallback = colorAlpha * fillAlphaInactive() / 255;
                    paint.setStyle(Paint.Style.FILL);
                    paint.setColor(ColorUtils.setAlphaComponent(primaryColor, fillAlphaFallback));
                    switch (element.shape) {
                        case CIRCLE:
                            canvas.drawCircle(cx, cy, boundingBox.width() * 0.5f + halfStroke, paint);
                            break;
                        case RECT: {
                            float r = element.getEffectiveCornerRadius() * snappingSize * element.scale;
                            if (r > 0)
                                canvas.drawRoundRect(boundingBox.left - halfStroke, boundingBox.top - halfStroke,
                                    boundingBox.right + halfStroke, boundingBox.bottom + halfStroke, r, r, paint);
                            else
                                canvas.drawRect(boundingBox.left - halfStroke, boundingBox.top - halfStroke,
                                    boundingBox.right + halfStroke, boundingBox.bottom + halfStroke, paint);
                            break;
                        }
                    }
                    paint.setStyle(Paint.Style.STROKE);
                    paint.setColor(element.selected ? secondaryColor : primaryColor);
                    paint.setStrokeWidth(strokeWidth);
                    drawButtonShape(canvas, boundingBox, snappingSize, paint);
                    int btnColor = element.selected ? secondaryColor : primaryColor;
                    Bitmap customIconBitmap = element.getCustomIcon();
                    if (customIconBitmap != null) {
                        int margin = (int)(snappingSize * 1.5f * element.scale);
                        int halfSize = (int)((Math.min(boundingBox.width(), boundingBox.height()) - margin) * 0.5f);
                        drawIconSrcRect.set(0, 0, customIconBitmap.getWidth(), customIconBitmap.getHeight());
                        drawIconDstRect.set((int)(cx - halfSize), (int)(cy - halfSize), (int)(cx + halfSize), (int)(cy + halfSize));
                        paint.setStyle(Paint.Style.FILL);
                        paint.setColor(btnColor);
                        canvas.drawBitmap(customIconBitmap, drawIconSrcRect, drawIconDstRect, paint);
                    }
                    else if (element.iconId > 0) {
                        paint.setColor(btnColor);
                        drawIcon(canvas, cx, cy, boundingBox.width(), boundingBox.height(), element.iconId);
                    }
                    else {
                        String displayText = element.getDisplayText();
                        if (displayText != null && !displayText.isEmpty()) {
                            paint.setTextSize(Math.min(getTextSizeForWidth(paint, displayText, boundingBox.width() - strokeWidth * 2), snappingSize * 2 * element.scale));
                            paint.setTextAlign(Paint.Align.CENTER);
                            paint.setStyle(Paint.Style.FILL);
                            paint.setColor(btnColor);
                            canvas.drawText(displayText, cx, (cy - ((paint.descent() + paint.ascent()) * 0.5f)), paint);
                        }
                    }
                }

                break;
            }
            case D_PAD: {
                float cx = boundingBox.centerX();
                float cy = boundingBox.centerY();
                float offsetX = snappingSize * 2 * element.scale;
                float offsetY = snappingSize * 3 * element.scale;
                float start = snappingSize * element.scale;

                Path dpadPath = element.inputControlsView.getPath();

                paint.setPathEffect(null);
                float effCr = element.getEffectiveCornerRadius();
                if (effCr > 0) {
                    if (dpadPathEffect == null) dpadPathEffect = new CornerPathEffect(effCr * snappingSize * element.scale);
                    paint.setPathEffect(dpadPathEffect);
                }

                dpadPath.rewind();
                dpadPath.moveTo(cx, cy - start);
                dpadPath.lineTo(cx - offsetX, cy - offsetY);
                dpadPath.lineTo(cx - offsetX, boundingBox.top);
                dpadPath.lineTo(cx + offsetX, boundingBox.top);
                dpadPath.lineTo(cx + offsetX, cy - offsetY);
                dpadPath.close();
                canvas.drawPath(dpadPath, paint);

                dpadPath.rewind();
                dpadPath.moveTo(cx + start, cy);
                dpadPath.lineTo(cx + offsetY, cy - offsetX);
                dpadPath.lineTo(boundingBox.right, cy - offsetX);
                dpadPath.lineTo(boundingBox.right, cy + offsetX);
                dpadPath.lineTo(cx + offsetY, cy + offsetX);
                dpadPath.close();
                canvas.drawPath(dpadPath, paint);

                dpadPath.rewind();
                dpadPath.moveTo(cx, cy + start);
                dpadPath.lineTo(cx - offsetX, cy + offsetY);
                dpadPath.lineTo(cx - offsetX, boundingBox.bottom);
                dpadPath.lineTo(cx + offsetX, boundingBox.bottom);
                dpadPath.lineTo(cx + offsetX, cy + offsetY);
                dpadPath.close();
                canvas.drawPath(dpadPath, paint);

                dpadPath.rewind();
                dpadPath.moveTo(cx - start, cy);
                dpadPath.lineTo(cx - offsetY, cy - offsetX);
                dpadPath.lineTo(boundingBox.left, cy - offsetX);
                dpadPath.lineTo(boundingBox.left, cy + offsetX);
                dpadPath.lineTo(cx - offsetY, cy + offsetX);
                dpadPath.close();
                canvas.drawPath(dpadPath, paint);

                paint.setStyle(Paint.Style.FILL);
                int baseAlpha = Color.alpha(primaryColor);

                dpadPath.rewind();
                dpadPath.moveTo(cx, cy - start);
                dpadPath.lineTo(cx - offsetX, cy - offsetY);
                dpadPath.lineTo(cx - offsetX, boundingBox.top);
                dpadPath.lineTo(cx + offsetX, boundingBox.top);
                dpadPath.lineTo(cx + offsetX, cy - offsetY);
                dpadPath.close();
                paint.setColor(ColorUtils.setAlphaComponent(primaryColor, (engaged && element.bindings.states[0] ? baseAlpha : (baseAlpha * fillAlphaInactive() / 255))));
                canvas.drawPath(dpadPath, paint);

                dpadPath.rewind();
                dpadPath.moveTo(cx + start, cy);
                dpadPath.lineTo(cx + offsetY, cy - offsetX);
                dpadPath.lineTo(boundingBox.right, cy - offsetX);
                dpadPath.lineTo(boundingBox.right, cy + offsetX);
                dpadPath.lineTo(cx + offsetY, cy + offsetX);
                dpadPath.close();
                paint.setColor(ColorUtils.setAlphaComponent(primaryColor, (engaged && element.bindings.states[1] ? baseAlpha : (baseAlpha * fillAlphaInactive() / 255))));
                canvas.drawPath(dpadPath, paint);

                dpadPath.rewind();
                dpadPath.moveTo(cx, cy + start);
                dpadPath.lineTo(cx - offsetX, cy + offsetY);
                dpadPath.lineTo(cx - offsetX, boundingBox.bottom);
                dpadPath.lineTo(cx + offsetX, boundingBox.bottom);
                dpadPath.lineTo(cx + offsetX, cy + offsetY);
                dpadPath.close();
                paint.setColor(ColorUtils.setAlphaComponent(primaryColor, (engaged && element.bindings.states[2] ? baseAlpha : (baseAlpha * fillAlphaInactive() / 255))));
                canvas.drawPath(dpadPath, paint);

                dpadPath.rewind();
                dpadPath.moveTo(cx - start, cy);
                dpadPath.lineTo(cx - offsetY, cy - offsetX);
                dpadPath.lineTo(boundingBox.left, cy - offsetX);
                dpadPath.lineTo(boundingBox.left, cy + offsetX);
                dpadPath.lineTo(cx - offsetY, cy + offsetX);
                dpadPath.close();
                paint.setColor(ColorUtils.setAlphaComponent(primaryColor, (engaged && element.bindings.states[3] ? baseAlpha : (baseAlpha * fillAlphaInactive() / 255))));
                canvas.drawPath(dpadPath, paint);

                paint.setStyle(Paint.Style.STROKE);
                paint.setColor(element.selected ? element.inputControlsView.getSecondaryColor() : primaryColor);
                paint.setStrokeWidth(strokeWidth);
                paint.setPathEffect(null);
                break;
            }
            case RANGE_BUTTON: {
                ControlElement.Range range = element.getRange();
                float radius = element.getEffectiveCornerRadius() * snappingSize * element.scale;
                float elementSize = element.scroller.getElementSize();
                float minTextSize = snappingSize * 2 * element.scale;
                float scrollOffset = element.scroller.getScrollOffset();
                Path path = element.inputControlsView.getPath();
                path.reset();

                if (element.orientation == 0) {
                    float lineTop = boundingBox.top + strokeWidth * 0.5f;
                    float lineBottom = boundingBox.bottom - strokeWidth * 0.5f;
                    float startX = boundingBox.left;
                    paint.setStyle(Paint.Style.FILL);
                    paint.setColor(fillColor);
                    canvas.drawRoundRect(startX, boundingBox.top, boundingBox.right, boundingBox.bottom, radius, radius, paint);
                    paint.setStyle(Paint.Style.STROKE);
                    paint.setColor(element.selected ? element.inputControlsView.getSecondaryColor() : primaryColor);
                    paint.setStrokeWidth(strokeWidth);
                    canvas.drawRoundRect(startX, boundingBox.top, boundingBox.right, boundingBox.bottom, radius, radius, paint);

                    if (!element.buildingCache) {
                        canvas.save();
                        path.addRoundRect(startX, boundingBox.top, boundingBox.right, boundingBox.bottom, radius, radius, Path.Direction.CW);
                        canvas.clipPath(path);
                        startX -= scrollOffset % elementSize;

                        for (int i = element.scroller.getRangeIndexFrom(); i < element.scroller.getRangeIndexTo(); i++) {
                            int index = i % range.max;
                            paint.setStyle(Paint.Style.STROKE);
                            paint.setColor(primaryColor);

                            if (startX > boundingBox.left && startX  < boundingBox.right) canvas.drawLine(startX, lineTop, startX, lineBottom, paint);
                            String text = getRangeTextForIndex(range, index);

                            if (startX < boundingBox.right && startX + elementSize > boundingBox.left) {
                                if (element.scroller.isActionDown() && element.scroller.getPressedIndex() == index) {
                                    paint.setStyle(Paint.Style.FILL);
                                    paint.setColor(ColorUtils.setAlphaComponent(primaryColor, Color.alpha(primaryColor)));
                                    float r = elementSize * 0.2f;
                                    canvas.drawRoundRect(startX, lineTop, startX + elementSize, lineBottom, r, r, paint);
                                }
                                paint.setStyle(Paint.Style.FILL);
                                paint.setColor(primaryColor);
                                paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, elementSize - strokeWidth * 2), minTextSize));
                                paint.setTextAlign(Paint.Align.CENTER);
                                canvas.drawText(text, startX + elementSize * 0.5f, (element.y - ((paint.descent() + paint.ascent()) * 0.5f)), paint);
                            }
                            startX += elementSize;
                        }

                        paint.setStyle(Paint.Style.STROKE);
                        paint.setColor(primaryColor);
                        canvas.restore();
                    }
                }
                else {
                    float lineLeft = boundingBox.left + strokeWidth * 0.5f;
                    float lineRight = boundingBox.right - strokeWidth * 0.5f;
                    float startY = boundingBox.top;
                    paint.setStyle(Paint.Style.FILL);
                    paint.setColor(fillColor);
                    canvas.drawRoundRect(boundingBox.left, startY, boundingBox.right, boundingBox.bottom, radius, radius, paint);
                    paint.setStyle(Paint.Style.STROKE);
                    paint.setColor(element.selected ? element.inputControlsView.getSecondaryColor() : primaryColor);
                    paint.setStrokeWidth(strokeWidth);
                    canvas.drawRoundRect(boundingBox.left, startY, boundingBox.right, boundingBox.bottom, radius, radius, paint);

                    if (!element.buildingCache) {
                        canvas.save();
                        path.addRoundRect(boundingBox.left, startY, boundingBox.right, boundingBox.bottom, radius, radius, Path.Direction.CW);
                        canvas.clipPath(path);
                        startY -= scrollOffset % elementSize;

                        for (int i = element.scroller.getRangeIndexFrom(); i < element.scroller.getRangeIndexTo(); i++) {
                            paint.setStyle(Paint.Style.STROKE);
                            paint.setColor(primaryColor);

                            if (startY > boundingBox.top && startY < boundingBox.bottom) canvas.drawLine(lineLeft, startY, lineRight, startY, paint);
                            String text = getRangeTextForIndex(range, i);

                            if (startY < boundingBox.bottom && startY + elementSize > boundingBox.top) {
                                if (element.scroller.isActionDown() && element.scroller.getPressedIndex() == (i % range.max)) {
                                    paint.setStyle(Paint.Style.FILL);
                                    paint.setColor(ColorUtils.setAlphaComponent(primaryColor, Color.alpha(primaryColor)));
                                    float r = elementSize * 0.2f;
                                    canvas.drawRoundRect(lineLeft, startY, lineRight, startY + elementSize, r, r, paint);
                                }
                                paint.setStyle(Paint.Style.FILL);
                                paint.setColor(primaryColor);
                                paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, boundingBox.width() - strokeWidth * 2), minTextSize));
                                paint.setTextAlign(Paint.Align.CENTER);
                                canvas.drawText(text, element.x, startY + elementSize * 0.5f - ((paint.descent() + paint.ascent()) * 0.5f), paint);
                            }
                            startY += elementSize;
                        }

                        paint.setStyle(Paint.Style.STROKE);
                        paint.setColor(primaryColor);
                        canvas.restore();
                    }
                }
                break;
            }
            case STICK: {
                int cx = boundingBox.centerX();
                int cy = boundingBox.centerY();
                int oldColor = paint.getColor();

                canvas.drawCircle(cx, cy, boundingBox.height() * 0.5f, paint);

                if (!element.buildingCache) {
                    float thumbstickX = element.getCurrentPosition().x;
                    float thumbstickY = element.getCurrentPosition().y;
                    short thumbRadius = (short) (snappingSize * 3.5f * element.scale);
                    paint.setStyle(Paint.Style.FILL);
                paint.setColor(ColorUtils.setAlphaComponent(primaryColor, engaged ? colorAlpha : inactiveFillAlpha));
                    canvas.drawCircle(thumbstickX, thumbstickY, thumbRadius, paint);

                    paint.setStyle(Paint.Style.STROKE);
                    paint.setColor(oldColor);
                    canvas.drawCircle(thumbstickX, thumbstickY, thumbRadius + strokeWidth * 0.5f, paint);
                }
                break;
            }

            case TRACKPAD: {
                int snapSize = element.inputControlsView.getSnappingSize();
                float radius = element.getEffectiveCornerRadius() * snapSize * element.scale;
                paint.setStyle(Paint.Style.FILL);
                paint.setColor(fillColor);
                canvas.drawRoundRect(boundingBox.left, boundingBox.top, boundingBox.right, boundingBox.bottom, radius, radius, paint);
                paint.setStyle(Paint.Style.STROKE);
                paint.setColor(element.selected ? element.inputControlsView.getSecondaryColor() : primaryColor);
                paint.setStrokeWidth(strokeWidth);
                canvas.drawRoundRect(boundingBox.left, boundingBox.top, boundingBox.right, boundingBox.bottom, radius, radius, paint);
                float offset = strokeWidth * 2.5f;
                float innerStrokeWidth = strokeWidth * 2;
                float innerHeight = boundingBox.height() - offset * 2;
                float innerRadius = (innerHeight / boundingBox.height()) * radius - (innerStrokeWidth * 0.5f + strokeWidth * 0.5f);
                paint.setStrokeWidth(innerStrokeWidth);
                canvas.drawRoundRect(boundingBox.left + offset, boundingBox.top + offset, boundingBox.right - offset, boundingBox.bottom - offset, innerRadius, innerRadius, paint);

                if (engaged && (element.visualLayers & VL_GLOW) != 0) {
                    ensureGlowCache();
                    if (cacheGlow != null) {
                        int glowPad = (cacheGlow.getWidth() - boundingBox.width()) / 2;
                        int savedColor = paint.getColor();
                        Paint.Style savedStyle = paint.getStyle();
                        float savedSW = paint.getStrokeWidth();
                        paint.setColor(ColorUtils.setAlphaComponent(primaryColor, colorAlpha));
                        paint.setStyle(Paint.Style.FILL);
                        canvas.drawBitmap(cacheGlow, boundingBox.left - glowPad, boundingBox.top - glowPad, paint);
                        paint.setStyle(savedStyle);
                        paint.setColor(savedColor);
                        paint.setStrokeWidth(savedSW);
                    }
                }
                break;
            }
        }
    }

    private void drawButtonShape(Canvas canvas, Rect box, int snappingSize, Paint paint) {
        float cx = box.centerX();
        float cy = box.centerY();
        switch (element.shape) {
            case CIRCLE:
                canvas.drawCircle(cx, cy, box.width() * 0.5f, paint);
                break;
            case RECT: {
                float r = element.getEffectiveCornerRadius() * snappingSize * element.scale;
                if (r > 0)
                    canvas.drawRoundRect(box.left, box.top, box.right, box.bottom, r, r, paint);
                else
                    canvas.drawRect(box, paint);
                break;
            }
        }
    }

    private void drawIcon(Canvas canvas, float cx, float cy, float width, float height, int iconId) {
        Paint paint = element.inputControlsView.getPaint();
        Bitmap icon = element.inputControlsView.getIcon((byte)iconId);
        if (icon == null) return;
        paint.setColorFilter(element.inputControlsView.getColorFilter());
        int margin = (int)(element.inputControlsView.getSnappingSize() * (element.shape == ControlElement.Shape.CIRCLE ? 2.0f : 1.0f) * element.scale);
        int halfSize = (int)((Math.min(width, height) - margin) * 0.5f);

        drawIconSrcRect.set(0, 0, icon.getWidth(), icon.getHeight());
        drawIconDstRect.set((int)(cx - halfSize), (int)(cy - halfSize), (int)(cx + halfSize), (int)(cy + halfSize));
        canvas.drawBitmap(icon, drawIconSrcRect, drawIconDstRect, paint);
        paint.setColorFilter(null);
    }

    private static float getTextSizeForWidth(Paint paint, String text, float desiredWidth) {
        final byte testTextSize = 48;
        paint.setTextSize(testTextSize);
        return testTextSize * desiredWidth / paint.measureText(text);
    }

    private static String getRangeTextForIndex(ControlElement.Range range, int index) {
        return range.texts[index % range.max];
    }
}
