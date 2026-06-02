package com.winlator.cmod.inputcontrols;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
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
import android.graphics.Xfermode;
import android.os.SystemClock;
import android.util.Base64;

import androidx.core.graphics.ColorUtils;
import com.winlator.cmod.core.CubicBezierInterpolator;
import com.winlator.cmod.inputcontrols.InputMode;
import com.winlator.cmod.inputcontrols.InputControlsManager;
import com.winlator.cmod.math.Mathf;
import com.winlator.cmod.widget.InputControlsView;
import com.winlator.cmod.widget.TouchpadView;
import com.winlator.cmod.winhandler.MouseEventFlags;
import com.winlator.cmod.xserver.XServer;

import java.io.File;
import java.io.FileOutputStream;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;

public class ControlElement {
    public static final float STICK_DEAD_ZONE = 0.15f;
    public static final float DPAD_DEAD_ZONE = 0.3f;
    public static final float STICK_SENSITIVITY = 2.0f;
    public static final float TRACKPAD_MIN_SPEED = 0.8f;
    public static final float TRACKPAD_MAX_SPEED = 20.0f;
    public static final byte TRACKPAD_ACCELERATION_THRESHOLD = 4;
    public static final short BUTTON_MIN_TIME_TO_KEEP_PRESSED = 300;
    public enum Type {
        BUTTON, D_PAD, RANGE_BUTTON, STICK, TRACKPAD;

        public static String[] names() {
            Type[] types = values();
            String[] names = new String[types.length];
            for (int i = 0; i < types.length; i++) names[i] = types[i].name().replace("_", "-");
            return names;
        }
    }
    public enum Shape {
        CIRCLE, RECT;

        public static String[] names() {
            Shape[] shapes = values();
            String[] names = new String[shapes.length];
            for (int i = 0; i < shapes.length; i++) names[i] = shapes[i].name().replace("_", " ");
            return names;
        }
    }
    public enum Range {
        FROM_A_TO_Z(26), FROM_0_TO_9(10), FROM_F1_TO_F12(12), FROM_NP0_TO_NP9(10);
        public final byte max;
        public final String[] texts;

        Range(int max) {
            this.max = (byte)max;
            this.texts = new String[max];
            for (int i = 0; i < max; i++) {
                switch (ordinal()) {
                    case 0: texts[i] = String.valueOf((char)(65 + i)); break;
                    case 1: texts[i] = String.valueOf((i + 1) % 10); break;
                    case 2: texts[i] = "F" + (i + 1); break;
                    case 3: texts[i] = "NP" + ((i + 1) % 10); break;
                }
            }
        }

        public static String[] names() {
            Range[] ranges = values();
            String[] names = new String[ranges.length];
            for (int i = 0; i < ranges.length; i++) names[i] = ranges[i].name().replace("_", " ");
            return names;
        }
    }
    private static final HashMap<String, Bitmap> sharedPool = new HashMap<>();
    private static final Xfermode XFERMODE_CLEAR = new PorterDuffXfermode(PorterDuff.Mode.CLEAR);
    private static final Xfermode XFERMODE_DST_OUT = new PorterDuffXfermode(PorterDuff.Mode.DST_OUT);
    private final InputControlsView inputControlsView;
    private Type type = Type.BUTTON;
    private Shape shape = Shape.CIRCLE;
    private float elementWidth = 8f;
    private float elementHeight = 4f;
    private float cornerRadius = 0.6f;
    private float dpadCornerRadius = 0.6f;
    private List<List<Binding>> bindings = new ArrayList<>();
    private float scale = 1.0f;
    private short x;
    private short y;
    private boolean selected = false;
    private boolean toggleSwitch = false;
    private int currentPointerId = -1;
    private final Rect boundingBox = new Rect();
    private boolean[] states = new boolean[4];
    private boolean boundingBoxNeedsUpdate = true;
    private static final int LAYER_FILL = 0;
    private static final int LAYER_COMBINED = 1;

    private Bitmap cacheCombined;
    private Bitmap cacheFill;
    private boolean cacheCombinedDirty = true;
    private boolean cacheFillDirty = true;
    private Bitmap dpadPetalStroke;
    private Bitmap dpadPetalFill;
    private boolean dpadCacheDirty = true;
    private Bitmap cacheThumbFillActive;
    private Bitmap cacheThumbFillInactive;
    private Bitmap cacheThumbStrokePrimary;
    private Bitmap cacheThumbStrokeSelected;
    private boolean cacheThumbDirty = true;
    private String cacheCombinedKey = "";
    private String text = "";
    private byte iconId;
    private String customIconData = "";
    private Bitmap customIcon;
    private String cachedDisplayText;
    private boolean displayTextDirty = true;
    private List<Binding> heldBindings;
    private List<Binding> longPressBindings = new ArrayList<>();
    private boolean longPressTriggered;
    private List<Binding> gestureBindings = new ArrayList<>();
    private boolean gestureTriggered;
    private List<Binding> doubleTapBindings = new ArrayList<>();
    private boolean doubleTapTriggered;
    private boolean doubleTapWaiting;
    private float doubleTapScale = 1.0f;
    private float doubleTapDownX;
    private float doubleTapDownY;
    private android.os.Handler doubleTapHandler;

    private float gestureDownX;

    private float gestureDownY;
    private boolean buildingCache;
    private boolean active;
    private android.os.Handler longPressHandler;
    private Range range;
    private byte orientation;
    private PointF currentPosition;
    private RangeScroller scroller;
    private CubicBezierInterpolator interpolator;
    private long touchTime;
    private final boolean[] scratchStates = new boolean[4];
    private final Rect drawIconSrcRect = new Rect();
    private final Rect drawIconDstRect = new Rect();
    private PathEffect dpadPathEffect;
    private PorterDuffColorFilter colorFilterPrimary;
    private PorterDuffColorFilter colorFilterSecondary;

    private float cachedStrokeWidth;
    private int cachedFillAlphaInactive;
    private int cachedButtonLongPressHaptic = 1;
    private int cachedButtonGestureHaptic = 1;

    private void refreshProfileCache() {
        ControlsProfile p = inputControlsView.getProfile();
        cachedStrokeWidth = p != null ? p.getStrokeWidth() : 0.2f;
        cachedFillAlphaInactive = p != null ? p.getFillAlphaInactive() : 50;
        cachedButtonLongPressHaptic = p != null ? p.getButtonLongPressHaptic() : 1;
        cachedButtonGestureHaptic = p != null ? p.getButtonGestureHaptic() : 1;
    }

    public ControlElement(InputControlsView inputControlsView) {
        this.inputControlsView = inputControlsView;
        for (int i = 0; i < 4; i++) {
            bindings.add(new ArrayList<Binding>());
        }
        refreshProfileCache();
    }

    private void reset() {
        for (List<Binding> seq : bindings) {
            seq.clear();
        }
        scroller = null;

        if (type == Type.STICK) {
            bindings.get(0).add(Binding.KEY_W);
            bindings.get(1).add(Binding.KEY_D);
            bindings.get(2).add(Binding.KEY_S);
            bindings.get(3).add(Binding.KEY_A);
        }
        else if(type == Type.D_PAD){
            bindings.get(0).add(Binding.GAMEPAD_DPAD_UP);
            bindings.get(1).add(Binding.GAMEPAD_DPAD_RIGHT);
            bindings.get(2).add(Binding.GAMEPAD_DPAD_DOWN);
            bindings.get(3).add(Binding.GAMEPAD_DPAD_LEFT);
        }
        else if (type == Type.TRACKPAD) {
            bindings.get(0).add(Binding.GAMEPAD_RIGHT_THUMB_UP);
            bindings.get(1).add(Binding.GAMEPAD_RIGHT_THUMB_RIGHT);
            bindings.get(2).add(Binding.GAMEPAD_RIGHT_THUMB_DOWN);
            bindings.get(3).add(Binding.GAMEPAD_RIGHT_THUMB_LEFT);
        }
        else if (type == Type.RANGE_BUTTON) {
            scroller = new RangeScroller(inputControlsView, this);
        }

        text = "";
        iconId = 0;
        customIconData = "";
        customIcon = null;
        range = null;
        boundingBoxNeedsUpdate = true;
        refreshProfileCache();
        invalidateElementCache();
    }

    public Type getType() {
        return type;
    }

    public void setType(Type type) {
        if (this.type == type) return;
        this.type = type;
        reset();
    }

    public int getBindingCount() {
        return bindings.size();
    }

    public void setBindingCount(int bindingCount) {
        while (bindings.size() < bindingCount) {
            List<Binding> seq = new ArrayList<>();
            seq.add(Binding.NONE);
            bindings.add(seq);
        }
        while (bindings.size() > bindingCount) {
            bindings.remove(bindings.size() - 1);
        }
        states = new boolean[bindingCount];
        boundingBoxNeedsUpdate = true;
        invalidateElementCache();
    }

    public Shape getShape() {
        return shape;
    }

    public void setShape(Shape shape) {
        this.shape = shape;
        boundingBoxNeedsUpdate = true;
        invalidateElementCache();
    }

    public float getElementWidth() {
        return elementWidth;
    }

    public void setElementWidth(float elementWidth) {
        this.elementWidth = elementWidth;
        boundingBoxNeedsUpdate = true;
        invalidateElementCache();
    }

    public float getElementHeight() {
        return elementHeight;
    }

    public void setElementHeight(float elementHeight) {
        this.elementHeight = elementHeight;
        boundingBoxNeedsUpdate = true;
        invalidateElementCache();
    }

    public float getCornerRadius() {
        return cornerRadius;
    }

    public void setCornerRadius(float cornerRadius) {
        this.cornerRadius = cornerRadius;
        invalidateElementCache();
    }

    public float getDpadCornerRadius() {
        return dpadCornerRadius;
    }

    public void setDpadCornerRadius(float dpadCornerRadius) {
        this.dpadCornerRadius = dpadCornerRadius;
        invalidateElementCache();
    }

    public Range getRange() {
        return range != null ? range : Range.FROM_A_TO_Z;
    }

    public void setRange(Range range) {
        this.range = range;
        invalidateElementCache();
    }

    public byte getOrientation() {
        return orientation;
    }

    public void setOrientation(byte orientation) {
        this.orientation = orientation;
        boundingBoxNeedsUpdate = true;
        invalidateElementCache();
    }

    public boolean isToggleSwitch() {
        return toggleSwitch;
    }

    public void setToggleSwitch(boolean toggleSwitch) {
        this.toggleSwitch = toggleSwitch;
    }

    public Binding getBindingAt(int index) {
        if (index >= bindings.size()) return Binding.NONE;
        List<Binding> seq = bindings.get(index);
        return seq.isEmpty() ? Binding.NONE : seq.get(0);
    }

    public void setBindingAt(int index, Binding binding) {
        while (index >= bindings.size()) {
            bindings.add(new ArrayList<Binding>());
        }
        List<Binding> seq = bindings.get(index);
        seq.clear();
        seq.add(binding);
        markDisplayTextDirty();
        invalidateElementCache();
    }

    public void setBinding(Binding binding) {
        for (List<Binding> seq : bindings) {
            seq.clear();
            seq.add(binding);
        }
        markDisplayTextDirty();
        invalidateElementCache();
    }

    public List<Binding> getBindingSequence(int index) {
        if (index >= bindings.size()) return Collections.emptyList();
        return Collections.unmodifiableList(bindings.get(index));
    }

    public void setBindingSequence(int index, List<Binding> sequence) {
        while (index >= bindings.size()) {
            bindings.add(new ArrayList<Binding>());
        }
        List<Binding> seq = bindings.get(index);
        seq.clear();
        seq.addAll(sequence);
        invalidateElementCache();
    }

    public void addBindingToSequence(int index, Binding binding) {
        while (index >= bindings.size()) {
            bindings.add(new ArrayList<Binding>());
        }
        bindings.get(index).add(binding);
        invalidateElementCache();
    }

    public void removeBindingFromSequence(int index, int seqIndex) {
        if (index < bindings.size()) {
            List<Binding> seq = bindings.get(index);
            if (seqIndex >= 0 && seqIndex < seq.size()) {
                seq.remove(seqIndex);
                invalidateElementCache();
            }
        }
    }

    public void setBindingAtSequenceIndex(int slotIndex, int seqIndex, Binding binding) {
        if (slotIndex < bindings.size()) {
            List<Binding> seq = bindings.get(slotIndex);
            if (seqIndex >= 0 && seqIndex < seq.size()) {
                seq.set(seqIndex, binding);
                invalidateElementCache();
            }
        }
    }

    public List<Binding> getLongPressBindings() {
        return longPressBindings;
    }

    public void setLongPressBindings(List<Binding> bindings) {
        longPressBindings.clear();
        longPressBindings.addAll(bindings);
    }

    public void addLongPressBinding(Binding binding) {
        longPressBindings.add(binding);
    }

    public void removeLongPressBinding(int index) {
        if (index >= 0 && index < longPressBindings.size()) {
            longPressBindings.remove(index);
        }
    }

    public boolean hasLongPressBinding() {
        if (longPressBindings == null || longPressBindings.isEmpty()) return false;
        if (longPressBindings.size() == 1 && longPressBindings.get(0) == Binding.NONE) return false;
        return true;
    }

    public List<Binding> getGestureBindings() {
        return gestureBindings;
    }

    public void setGestureBindings(List<Binding> bindings) {
        gestureBindings.clear();
        gestureBindings.addAll(bindings);
    }

    public void addGestureBinding(Binding binding) {
        gestureBindings.add(binding);
    }

    public void removeGestureBinding(int index) {
        if (index >= 0 && index < gestureBindings.size()) {
            gestureBindings.remove(index);
        }
    }

    public boolean hasGestureBinding() {
        if (gestureBindings == null || gestureBindings.isEmpty()) return false;
        if (gestureBindings.size() == 1 && gestureBindings.get(0) == Binding.NONE) return false;
        return true;
    }

    public List<Binding> getDoubleTapBindings() {
        return doubleTapBindings;
    }

    public void setDoubleTapBindings(List<Binding> bindings) {
        doubleTapBindings.clear();
        doubleTapBindings.addAll(bindings);
    }

    public void addDoubleTapBinding(Binding binding) {
        doubleTapBindings.add(binding);
    }

    public void removeDoubleTapBinding(int index) {
        if (index >= 0 && index < doubleTapBindings.size()) {
            doubleTapBindings.remove(index);
        }
    }

    public boolean hasDoubleTapBinding() {
        if (doubleTapBindings == null || doubleTapBindings.isEmpty()) return false;
        if (doubleTapBindings.size() == 1 && doubleTapBindings.get(0) == Binding.NONE) return false;
        return true;
    }

    public void setGestureDownPosition(float x, float y) {
        gestureDownX = x;
        gestureDownY = y;
    }

    public boolean isGestureTriggered() {
        return gestureTriggered;
    }

    public boolean isLongPressTriggered() {
        return longPressTriggered;
    }

    public boolean isDoubleTapTriggered() {
        return doubleTapTriggered;
    }

    public float getScale() {
        return scale;
    }

    public void setScale(float scale) {
        this.scale = scale;
        boundingBoxNeedsUpdate = true;
        invalidateElementCache();
    }

    public short getX() {
        return x;
    }

    public void setX(int x) {
        this.x = (short)x;
        if (currentPosition != null) currentPosition.x = x;
        boundingBoxNeedsUpdate = true;
        invalidateElementCache();
    }

    public short getY() {
        return y;
    }

    public void setY(int y) {
        this.y = (short)y;
        if (currentPosition != null) currentPosition.y = y;
        boundingBoxNeedsUpdate = true;
        invalidateElementCache();
    }

    public boolean isSelected() {
        return selected;
    }

    public void setSelected(boolean selected) {
        this.selected = selected;
    }

    public String getText() {
        return text;
    }

    public void setText(String text) {
        this.text = text != null ? text : "";
        invalidateElementCache();
    }

    public byte getIconId() {
        return iconId;
    }

    public void setIconId(int iconId) {
        this.iconId = (byte)iconId;
        invalidateElementCache();
    }

    public boolean hasCustomIcon() {
        return customIconData != null && !customIconData.isEmpty();
    }

    public String getCustomIconData() {
        return customIconData;
    }

    public void setCustomIconData(String customIconData) {
        String oldFillKey = visualKey(LAYER_FILL);
        String oldCombinedKey = visualKey(LAYER_COMBINED);
        String oldDiskFillKey = diskKey(LAYER_FILL);
        String oldDiskCombinedKey = diskKey(LAYER_COMBINED);
        this.customIconData = customIconData != null ? customIconData : "";
        customIcon = null;
        invalidateElementCache();
        sharedPool.remove(oldFillKey);
        sharedPool.remove(oldCombinedKey);
        if (inputControlsView != null) {
            File dir = cacheDir();
            for (String key : new String[]{oldDiskCombinedKey, oldDiskFillKey}) {
                File f = new File(dir, key + ".png");
                if (f.exists()) f.delete();
            }
        }
    }

    public Bitmap getCustomIcon() {
        if (customIcon == null && hasCustomIcon()) {
            try {
                byte[] data = Base64.decode(customIconData, Base64.DEFAULT);
                customIcon = BitmapFactory.decodeByteArray(data, 0, data.length);
            }
            catch (IllegalArgumentException e) {
                customIconData = "";
            }
        }
        return customIcon;
    }

    public Rect getBoundingBox() {
        if (boundingBoxNeedsUpdate) computeBoundingBox();
        return boundingBox;
    }

    private Rect computeBoundingBox() {
        int snappingSize = inputControlsView.getSnappingSize();
        int halfWidth = 0;
        int halfHeight = 0;

        switch (type) {
            case BUTTON:
                switch (shape) {
                    case RECT:
                        halfWidth = (int)((elementWidth / 2.0) * snappingSize);
                        halfHeight = (int)((elementHeight / 2.0) * snappingSize);
                        break;
                    case CIRCLE:
                        halfWidth = snappingSize * 3;
                        halfHeight = snappingSize * 3;
                        break;
                }
                break;
            case D_PAD: {
                halfWidth = snappingSize * 7;
                halfHeight = snappingSize * 7;
                break;
            }
            case TRACKPAD:
            case STICK: {
                halfWidth = snappingSize * 6;
                halfHeight = snappingSize * 6;
                break;
            }
            case RANGE_BUTTON: {
                halfWidth = snappingSize * ((bindings.size() * 4) / 2);
                halfHeight = snappingSize * 2;

                if (orientation == 1) {
                    int tmp = halfWidth;
                    halfWidth = halfHeight;
                    halfHeight = tmp;
                }
                break;
            }
        }

        halfWidth *= scale;
        halfHeight *= scale;
        boundingBox.set(x - halfWidth, y - halfHeight, x + halfWidth, y + halfHeight);
        boundingBoxNeedsUpdate = false;
        return boundingBox;
    }



    private String getDisplayText() {
        if (text != null && !text.isEmpty()) {
            return text;
        }
        if (!displayTextDirty && cachedDisplayText != null) return cachedDisplayText;
        displayTextDirty = false;
        Binding binding = getBindingAt(0);
        String display = binding.toString().replace("NUMPAD ", "NP").replace("BUTTON ", "");
        if (display.length() > 7) {
            String[] parts = display.split(" ");
            StringBuilder sb = new StringBuilder();
            for (String part : parts) sb.append(part.charAt(0));
            cachedDisplayText = (binding.isMouse() ? "M" : "")+ sb;
        } else {
            cachedDisplayText = display;
        }
        return cachedDisplayText;
    }

    public void markDisplayTextDirty() {
        displayTextDirty = true;
    }

    private static float getTextSizeForWidth(Paint paint, String text, float desiredWidth) {
        final byte testTextSize = 48;
        paint.setTextSize(testTextSize);
        return testTextSize * desiredWidth / paint.measureText(text);
    }

    private static String getRangeTextForIndex(Range range, int index) {
        return range.texts[index % range.max];
    }

    public boolean isEngaged() {
        return currentPointerId != -1 || active || (toggleSwitch && selected);
    }

    public void invalidateElementCache() {
        cachedDisplayText = null;
        displayTextDirty = true;
        dpadPathEffect = null;
        colorFilterPrimary = null;
        colorFilterSecondary = null;
        cacheCombined = null;
        cacheFill = null;
        dpadPetalStroke = null;
        dpadPetalFill = null;
        cacheThumbFillActive = null;
        cacheThumbFillInactive = null;
        cacheThumbStrokePrimary = null;
        cacheThumbStrokeSelected = null;
        cacheCombinedDirty = true;
        cacheFillDirty = true;
        dpadCacheDirty = true;
        cacheThumbDirty = true;
        if (inputControlsView != null) {
            File dir = cacheDir();
            String combinedKey = diskKey(LAYER_COMBINED);
            String fillKey = diskKey(LAYER_FILL);
            for (String name : new String[]{combinedKey, fillKey, fillKey + "_dpad_stroke", fillKey + "_dpad_fill"}) {
                File f = new File(dir, name + ".png");
                if (f.exists()) f.delete();
            }
        }
    }

    public void invalidateElementCachesKeepDisk() {
        cacheCombined = null;
        cacheFill = null;
        dpadPetalStroke = null;
        dpadPetalFill = null;
        cacheThumbFillActive = null;
        cacheThumbFillInactive = null;
        cacheThumbStrokePrimary = null;
        cacheThumbStrokeSelected = null;
        cacheCombinedDirty = true;
        cacheFillDirty = true;
        dpadCacheDirty = true;
        cacheThumbDirty = true;
    }

    public void buildCache() {
        if (!isCachingEnabled()) return;
        if (getBoundingBox().width() <= 0) return;
        if (type == Type.D_PAD) {
            ensureDPadCaches(inputControlsView.getSnappingSize());
        } else {
            ensureCombinedCache();
            ensureFillCache();
            if (type == Type.STICK) ensureThumbCaches();
        }
    }

    public static void clearSharedPool() {
        for (Bitmap bmp : sharedPool.values()) {
            if (bmp != null && !bmp.isRecycled()) bmp.recycle();
        }
        sharedPool.clear();
    }

    public static void deleteProfileCache(Context context, int profileId) {
        String prefix = profileId + "_";
        File dir = new File(InputControlsManager.getProfilesDir(context), "cache");
        if (dir.isDirectory()) {
            File[] files = dir.listFiles((d, n) -> n.startsWith(prefix));
            if (files != null) for (File f : files) f.delete();
        }
    }

    public static void deleteAllCaches(Context context) {
        File dir = new File(InputControlsManager.getProfilesDir(context), "cache");
        if (dir.isDirectory()) {
            File[] files = dir.listFiles();
            if (files != null) for (File f : files) f.delete();
            dir.delete();
        }
    }

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

    private File cacheDir() {
        File dir = new File(InputControlsManager.getProfilesDir(inputControlsView.getContext()), "cache");
        if (!dir.exists()) dir.mkdirs();
        return dir;
    }

    private String diskKey(int layer) {
        ControlsProfile p = inputControlsView.getProfile();
        int pid = p != null ? p.id : 0;
        String prefix = layer == LAYER_FILL ? "f_" : "c_";
        String raw = pid + "|" + type.ordinal() + "|" + shape.ordinal() + "|" + elementWidth + "|" + elementHeight
            + "|" + cornerRadius + "|" + scale;
        if (layer == LAYER_FILL || layer == LAYER_COMBINED) {
            raw += "|" + getDisplayText() + "|" + iconId;
            if (hasCustomIcon()) raw += "|" + customIconData;
        }
        return pid + "_" + prefix + md5(raw);
    }

    private String visualKey(int layer) {
        String base = type.ordinal() + "_" + shape.ordinal() + "_" + elementWidth + "_" + elementHeight + "_" + cornerRadius + "_" + scale;
        String customSuffix = hasCustomIcon() ? "_" + customIconData.hashCode() : "";
        switch (layer) {
            case 0: return base + "_" + getDisplayText() + "_" + iconId + customSuffix + "_fill";
            case 1: return base + "_" + getDisplayText() + "_" + iconId + customSuffix + "_combined";
            default: return base;
        }
    }

    private boolean isCachingEnabled() {
        return inputControlsView.isCachingEnabled();
    }

    private int strokePad() {
        return Math.max(1, (int)Math.ceil(inputControlsView.getSnappingSize() * strokeWidthMultiplier() / 2) + 1);
    }

    private float strokeWidthMultiplier() {
        return cachedStrokeWidth;
    }

    private int fillAlphaInactive() {
        return cachedFillAlphaInactive;
    }

    private Bitmap loadFromDisk(String key) {
        File file = new File(cacheDir(), key + ".png");
        if (!file.isFile()) return null;
        return BitmapFactory.decodeFile(file.getAbsolutePath());
    }

    private void saveToDisk(String key, Bitmap bitmap) {
        try (FileOutputStream out = new FileOutputStream(new File(cacheDir(), key + ".png"))) {
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, out);
        } catch (Exception e) {
            android.util.Log.w("ControlElement", "save cache " + key + " failed", e);
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
        Paint p = inputControlsView.getPaint();
        int savedAlpha = p.getAlpha();
        p.setAlpha((int)(opacity * 255));
        c.drawBitmap(src, 0, 0, p);
        p.setAlpha(savedAlpha);
        src.recycle();
        return result;
    }

    private void ensureCombinedCache() {
        if (!cacheCombinedDirty && cacheCombined != null) return;
        Rect box = getBoundingBox();
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
                cacheCombined = applyOpacity(cacheCombined, inputControlsView.getOverlayOpacity());
            }
        }
        if (cacheCombined == null) {
            cacheCombined = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            Canvas c = new Canvas(cacheCombined);
            c.translate(-box.left + pad, -box.top + pad);
            boolean savedSelected = selected;
            int savedPointerId = currentPointerId;
            boolean savedActive = active;
            boolean[] savedStates = scratchStates;
            scratchStates[0] = states[0];
            scratchStates[1] = states[1];
            scratchStates[2] = states[2];
            scratchStates[3] = states[3];
            selected = false;
            currentPointerId = -1;
            active = false;
            Arrays.fill(states, false);
            inputControlsView.setCacheAlphaOverride(1.0f);
            buildingCache = true;
            draw(c);
            buildingCache = false;
            inputControlsView.setCacheAlphaOverride(-1);
            selected = savedSelected;
            currentPointerId = savedPointerId;
            active = savedActive;
            states[0] = savedStates[0];
            states[1] = savedStates[1];
            states[2] = savedStates[2];
            states[3] = savedStates[3];
            saveToDisk(cacheCombinedKey, cacheCombined);
            cacheCombined = applyOpacity(cacheCombined, inputControlsView.getOverlayOpacity());
            sharedPool.put(vKey, cacheCombined);
        }
        cacheCombinedDirty = false;
    }

    private void ensureFillCache() {
        if (!cacheFillDirty && cacheFill != null) return;
        Rect box = getBoundingBox();
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
        }
        if (cacheFill == null) {
            cacheFill = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            Canvas c = new Canvas(cacheFill);
            c.translate(-box.left + pad, -box.top + pad);
            Paint paint = inputControlsView.getPaint();
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(ColorUtils.setAlphaComponent(Color.WHITE, 255));
            switch (type) {
                case BUTTON: {
                    int snappingSize = inputControlsView.getSnappingSize();
                    ControlsProfile prof = inputControlsView.getProfile();
                    float strW = snappingSize * (prof != null ? prof.getStrokeWidth() : 0.2f);
                    // Draw full button shape as fill mask
                    drawButtonShape(c, box, snappingSize, paint);
                    // Punch out text/icon as transparent stencil
                    float cx = box.centerX();
                    float cy = box.centerY();
                    Bitmap customIconBitmap = getCustomIcon();
                    if (customIconBitmap != null) {
                        int margin = (int)(snappingSize * (shape == Shape.CIRCLE ? 2.0f : 1.0f) * scale);
                        int halfSize = (int)((Math.min(box.width(), box.height()) - margin) * 0.5f);
                        drawIconSrcRect.set(0, 0, customIconBitmap.getWidth(), customIconBitmap.getHeight());
                        drawIconDstRect.set((int)(cx - halfSize), (int)(cy - halfSize), (int)(cx + halfSize), (int)(cy + halfSize));
                        paint.setXfermode(XFERMODE_DST_OUT);
                        c.drawBitmap(customIconBitmap, drawIconSrcRect, drawIconDstRect, paint);
                        paint.setXfermode(null);
                    }
                    else if (iconId > 0) {
                        Bitmap icon = inputControlsView.getIcon((byte)iconId);
                        if (icon != null) {
                            int margin = (int)(snappingSize * (shape == Shape.CIRCLE ? 2.0f : 1.0f) * scale);
                            int halfSize = (int)((Math.min(box.width(), box.height()) - margin) * 0.5f);
                            drawIconSrcRect.set(0, 0, icon.getWidth(), icon.getHeight());
                            drawIconDstRect.set((int)(cx - halfSize), (int)(cy - halfSize), (int)(cx + halfSize), (int)(cy + halfSize));
                            paint.setXfermode(XFERMODE_DST_OUT);
                            c.drawBitmap(icon, drawIconSrcRect, drawIconDstRect, paint);
                            paint.setXfermode(null);
                        }
                    } else {
                        String text = getDisplayText();
                        paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, box.width() - strW * 2), snappingSize * 2 * scale));
                        paint.setTextAlign(Paint.Align.CENTER);
                        paint.setStyle(Paint.Style.FILL);
                        paint.setXfermode(XFERMODE_CLEAR);
                        c.drawText(text, x, (y - ((paint.descent() + paint.ascent()) * 0.5f)), paint);
                        paint.setXfermode(null);
                    }
                    break;
                }
                case STICK: {
                    float cx = box.centerX();
                    float cy = box.centerY();
                    short thumbRadius = (short) (inputControlsView.getSnappingSize() * 3.5f * scale);
                    c.drawCircle(cx, cy, thumbRadius, paint);
                    break;
                }
                case RANGE_BUTTON: {
                    int snappingSize = inputControlsView.getSnappingSize();
                    float radius = snappingSize * 0.75f * scale;
                    c.drawRoundRect(box.left, box.top, box.right, box.bottom, radius, radius, paint);
                    break;
                }
                case TRACKPAD: {
                    float radius = box.height() * 0.15f;
                    c.drawRoundRect(box.left, box.top, box.right, box.bottom, radius, radius, paint);
                    break;
                }
                default: break;
            }
            paint.setStyle(Paint.Style.STROKE);
            sharedPool.put(vKey, cacheFill);
            saveToDisk(diskKey(LAYER_FILL), cacheFill);
        }
        cacheFillDirty = false;
    }

    private void ensureThumbCaches() {
        if (type != Type.STICK) return;
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
        int snappingSize = inputControlsView.getSnappingSize();
        short thumbRadius = (short)(snappingSize * 3.5f * scale);
        float strokeWidth = snappingSize * strokeWidthMultiplier();
        int halfSize = (int)(thumbRadius + strokeWidth * 0.5f + 1);
        int size = halfSize * 2;
        if (size <= 0) return;

        int baseAlpha = Color.alpha(inputControlsView.getPrimaryColor());
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

        int primaryColor = inputControlsView.getPrimaryColor();
        int secondaryColor = inputControlsView.getSecondaryColor();

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
        float offsetX = snappingSize * 2 * scale;
        float offsetY = snappingSize * 3 * scale;
        float start = snappingSize * scale;
        path.rewind();
        path.moveTo(cx, cy - start);
        path.lineTo(cx - offsetX, cy - offsetY);
        path.lineTo(cx - offsetX, box.top);
        path.lineTo(cx + offsetX, box.top);
        path.lineTo(cx + offsetX, cy - offsetY);
        path.close();
    }

    private void ensureDPadCaches(int snappingSize) {
        Rect box = getBoundingBox();
        int pad = strokePad();
        int w = box.width() + pad * 2;
        int h = box.height() + pad * 2;
        if (w <= 0 || h <= 0) return;
        if (!dpadCacheDirty && dpadPetalStroke != null &&
            dpadPetalStroke.getWidth() == w && dpadPetalStroke.getHeight() == h) return;
        dpadPetalStroke = null;
        dpadPetalFill = null;
        Paint paint = inputControlsView.getPaint();
        float strokeWidth = snappingSize * strokeWidthMultiplier();
        paint.setPathEffect(null);
        if (dpadCornerRadius > 0) {
            if (dpadPathEffect == null) dpadPathEffect = new CornerPathEffect(dpadCornerRadius * snappingSize * scale);
            paint.setPathEffect(dpadPathEffect);
        }
        String strokeKey = diskKey(LAYER_FILL) + "_dpad_stroke";
        String fillKey = diskKey(LAYER_FILL) + "_dpad_fill";
        dpadPetalStroke = loadFromDisk(strokeKey);
        if (dpadPetalStroke != null && (dpadPetalStroke.getWidth() != w || dpadPetalStroke.getHeight() != h)) {
            dpadPetalStroke.recycle();
            dpadPetalStroke = null;
        }
        if (dpadPetalStroke != null) {
            dpadPetalStroke = applyOpacity(dpadPetalStroke, inputControlsView.getOverlayOpacity());
        }
        if (dpadPetalStroke == null) {
            dpadPetalStroke = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            Canvas c = new Canvas(dpadPetalStroke);
            c.translate(-box.left + pad, -box.top + pad);
            paint.setColor(ColorUtils.setAlphaComponent(Color.WHITE, 255));
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(strokeWidth);
            Path petalPath = inputControlsView.getPath();
            buildUpPetal(box, snappingSize, petalPath);
            c.drawPath(petalPath, paint);
            saveToDisk(strokeKey, dpadPetalStroke);
            dpadPetalStroke = applyOpacity(dpadPetalStroke, inputControlsView.getOverlayOpacity());
        }
        dpadPetalFill = loadFromDisk(fillKey);
        if (dpadPetalFill != null && (dpadPetalFill.getWidth() != w || dpadPetalFill.getHeight() != h)) {
            dpadPetalFill.recycle();
            dpadPetalFill = null;
        }
        if (dpadPetalFill == null) {
            dpadPetalFill = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            Canvas c = new Canvas(dpadPetalFill);
            c.translate(-box.left + pad, -box.top + pad);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(ColorUtils.setAlphaComponent(Color.WHITE, 255));
            Path petalPath = inputControlsView.getPath();
            buildUpPetal(box, snappingSize, petalPath);
            c.drawPath(petalPath, paint);
            paint.setStyle(Paint.Style.STROKE);
            saveToDisk(fillKey, dpadPetalFill);
        }
        paint.setPathEffect(null);
        dpadCacheDirty = false;
    }

    public void drawCached(Canvas canvas) {
        if (!isCachingEnabled()) { draw(canvas); return; }
        Rect box = getBoundingBox();
        if (box.width() <= 0 || box.height() <= 0) return;
        if (selected && !(toggleSwitch && type == Type.BUTTON)) { draw(canvas); return; }

        int pad = strokePad();
        int snappingSize = inputControlsView.getSnappingSize();
        Paint paint = inputControlsView.getPaint();
        int primaryColor = inputControlsView.getPrimaryColor();
        int secondaryColor = inputControlsView.getSecondaryColor();
        int colorAlpha = Color.alpha(primaryColor);
        int inactiveAlpha = colorAlpha * fillAlphaInactive() / 255;

        boolean scaled = false;
        if (type == Type.BUTTON && doubleTapScale > 1.0f) {
            canvas.save();
            canvas.scale(doubleTapScale, doubleTapScale, box.centerX(), box.centerY());
            scaled = true;
        }
        try {
            if (type == Type.D_PAD) {
                ensureDPadCaches(snappingSize);
                if (dpadPetalStroke == null) { draw(canvas); return; }
                float cx = box.centerX();
                float cy = box.centerY();
                float strokeWidth = snappingSize * strokeWidthMultiplier();
                boolean engagedDpad = isEngaged();
                int fillActive = colorAlpha;
                int fillInactive = inactiveAlpha;
                paint.setStyle(Paint.Style.FILL);
                for (int i = 0; i < 4; i++) {
                    canvas.save();
                    canvas.rotate(i * 90, cx, cy);
                    if (engagedDpad && states[i]) {
                        paint.setColor(ColorUtils.setAlphaComponent(primaryColor, fillActive));
                        canvas.drawBitmap(dpadPetalFill, box.left - pad, box.top - pad, paint);
                    } else {
                        canvas.drawBitmap(dpadPetalStroke, box.left - pad, box.top - pad, null);
                        paint.setColor(ColorUtils.setAlphaComponent(primaryColor, fillInactive));
                        canvas.drawBitmap(dpadPetalFill, box.left - pad, box.top - pad, paint);
                    }
                    canvas.restore();
                }
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(strokeWidth);
                paint.setColor(primaryColor);
                return;
            }

            boolean engaged = isEngaged();

            // 1. Engaged: BUTTON/TRACKPAD — fill cache replaces normal rendering
            if (engaged && (type == Type.BUTTON || type == Type.TRACKPAD)) {
                int savedColor = paint.getColor();
                Paint.Style savedStyle = paint.getStyle();
                float savedStrokeWidth = paint.getStrokeWidth();
                ensureFillCache();
                if (cacheFill != null) {
                    int alpha = colorAlpha;
                    paint.setColor(ColorUtils.setAlphaComponent(primaryColor, alpha));
                    paint.setStyle(Paint.Style.FILL);
                    canvas.drawBitmap(cacheFill, box.left - pad, box.top - pad, paint);
                }
                paint.setStyle(savedStyle);
                paint.setColor(savedColor);
                paint.setStrokeWidth(savedStrokeWidth);
                return;
            }

            // 2. Fill under shape (non-engaged, non-BUTTON) — subtle background wash
            if (type != Type.TRACKPAD && type != Type.STICK && type != Type.BUTTON) {
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

            // 3. Combined layer (shape + text/icon, overlay opacity baked in)
            ensureCombinedCache();
            if (cacheCombined != null)
                canvas.drawBitmap(cacheCombined, box.left - pad, box.top - pad, null);
            else {
                draw(canvas);
                return;
            }

        // 4. Dynamic content
        if (type == Type.STICK || type == Type.RANGE_BUTTON) {
            float strokeWidth = snappingSize * strokeWidthMultiplier();
            paint.setColor(selected ? secondaryColor : primaryColor);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(strokeWidth);
            if (type == Type.STICK) {
                float thumbstickX = getCurrentPosition().x;
                float thumbstickY = getCurrentPosition().y;
                ensureThumbCaches();
                if (cacheThumbFillActive != null) {
                    int halfSize = cacheThumbFillActive.getWidth() / 2;
                    if (engaged) {
                        canvas.drawBitmap(cacheThumbFillActive, thumbstickX - halfSize, thumbstickY - halfSize, null);
                    } else {
                        canvas.drawBitmap(cacheThumbFillInactive, thumbstickX - halfSize, thumbstickY - halfSize, null);
                        Bitmap stroke = selected ? cacheThumbStrokeSelected : cacheThumbStrokePrimary;
                        if (stroke != null)
                            canvas.drawBitmap(stroke, thumbstickX - halfSize, thumbstickY - halfSize, null);
                    }
                } else {
                    short thumbRadius = (short) (snappingSize * 3.5f * scale);
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
                float radius = snappingSize * 0.75f * scale;
                float elementSize = scroller.getElementSize();
                float minTextSize = snappingSize * 2 * scale;
                float scrollOffset = scroller.getScrollOffset();
                Range range = getRange();
                Path path = inputControlsView.getPath();
                path.reset();
                if (orientation == 0) {
                    float lineTop = box.top + strokeWidth * 0.5f;
                    float lineBottom = box.bottom - strokeWidth * 0.5f;
                    canvas.save();
                    path.addRoundRect(box.left, box.top, box.right, box.bottom, radius, radius, Path.Direction.CW);
                    canvas.clipPath(path);
                    float startX = box.left - scrollOffset % elementSize;
                    for (int i = scroller.getRangeIndexFrom(); i < scroller.getRangeIndexTo(); i++) {
                        int index = i % range.max;
                        paint.setStyle(Paint.Style.STROKE);
                        paint.setColor(primaryColor);
                        if (startX > box.left && startX < box.right) canvas.drawLine(startX, lineTop, startX, lineBottom, paint);
                        String text = getRangeTextForIndex(range, index);
                        if (startX < box.right && startX + elementSize > box.left) {
                            if (scroller.isActionDown() && scroller.getPressedIndex() == index) {
                                paint.setStyle(Paint.Style.FILL);
                                paint.setColor(ColorUtils.setAlphaComponent(primaryColor, colorAlpha));
                                canvas.drawRect(startX, lineTop, startX + elementSize, lineBottom, paint);
                                paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, elementSize - strokeWidth * 2), minTextSize));
                                paint.setTextAlign(Paint.Align.CENTER);
                                paint.setXfermode(XFERMODE_CLEAR);
                                canvas.drawText(text, startX + elementSize * 0.5f, (y - ((paint.descent() + paint.ascent()) * 0.5f)), paint);
                                paint.setXfermode(null);
                            } else {
                                paint.setStyle(Paint.Style.FILL);
                                paint.setColor(primaryColor);
                                paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, elementSize - strokeWidth * 2), minTextSize));
                                paint.setTextAlign(Paint.Align.CENTER);
                                canvas.drawText(text, startX + elementSize * 0.5f, (y - ((paint.descent() + paint.ascent()) * 0.5f)), paint);
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
                    for (int i = scroller.getRangeIndexFrom(); i < scroller.getRangeIndexTo(); i++) {
                        paint.setStyle(Paint.Style.STROKE);
                        paint.setColor(primaryColor);
                        if (startY > box.top && startY < box.bottom) canvas.drawLine(lineLeft, startY, lineRight, startY, paint);
                        String text = getRangeTextForIndex(range, i);
                        if (startY < box.bottom && startY + elementSize > box.top) {
                            if (scroller.isActionDown() && scroller.getPressedIndex() == (i % range.max)) {
                                paint.setStyle(Paint.Style.FILL);
                                paint.setColor(ColorUtils.setAlphaComponent(primaryColor, colorAlpha));
                                canvas.drawRect(lineLeft, startY, lineRight, startY + elementSize, paint);
                                paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, box.width() - strokeWidth * 2), minTextSize));
                                paint.setTextAlign(Paint.Align.CENTER);
                                paint.setXfermode(XFERMODE_CLEAR);
                                canvas.drawText(text, x, startY + elementSize * 0.5f - ((paint.descent() + paint.ascent()) * 0.5f), paint);
                                paint.setXfermode(null);
                            } else {
                                paint.setStyle(Paint.Style.FILL);
                                paint.setColor(primaryColor);
                                paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, box.width() - strokeWidth * 2), minTextSize));
                                paint.setTextAlign(Paint.Align.CENTER);
                                canvas.drawText(text, x, startY + elementSize * 0.5f - ((paint.descent() + paint.ascent()) * 0.5f), paint);
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
        finally {
            if (scaled) canvas.restore();
        }
    }

    public void draw(Canvas canvas) {
        int snappingSize = inputControlsView.getSnappingSize();
        Paint paint = inputControlsView.getPaint();
        int primaryColor = inputControlsView.getPrimaryColor();
        int secondaryColor = inputControlsView.getSecondaryColor();
        int colorAlpha = Color.alpha(primaryColor);
        int inactiveFillAlpha = colorAlpha * fillAlphaInactive() / 255;

        paint.setColor(selected ? secondaryColor : primaryColor);
        paint.setStyle(Paint.Style.STROKE);
        float strokeWidth = snappingSize * cachedStrokeWidth;
        paint.setStrokeWidth(strokeWidth);
        Rect boundingBox = getBoundingBox();
        boolean engaged = isEngaged();
        int fillAlpha = engaged ? colorAlpha : inactiveFillAlpha;
        int fillColor = ColorUtils.setAlphaComponent(primaryColor, fillAlpha);

        switch (type) {
            case BUTTON: {
                float cx = boundingBox.centerX();
                float cy = boundingBox.centerY();

                if (doubleTapScale > 1.0f && !buildingCache) {
                    canvas.save();
                    canvas.scale(doubleTapScale, doubleTapScale, cx, cy);
                }

                paint.setStyle(Paint.Style.FILL);
                paint.setColor(fillColor);
                drawButtonShape(canvas, boundingBox, snappingSize, paint);
                paint.setStyle(Paint.Style.STROKE);
                paint.setColor(selected ? inputControlsView.getSecondaryColor() : primaryColor);
                paint.setStrokeWidth(strokeWidth);
                drawButtonShape(canvas, boundingBox, snappingSize, paint);

                Bitmap customIconBitmap = getCustomIcon();
                if (customIconBitmap != null) {
                    int margin = (int)(snappingSize * 1.5f * scale);
                    int halfSize = (int)((Math.min(boundingBox.width(), boundingBox.height()) - margin) * 0.5f);
                    drawIconSrcRect.set(0, 0, customIconBitmap.getWidth(), customIconBitmap.getHeight());
                    drawIconDstRect.set((int)(cx - halfSize), (int)(cy - halfSize), (int)(cx + halfSize), (int)(cy + halfSize));
                    canvas.drawBitmap(customIconBitmap, drawIconSrcRect, drawIconDstRect, paint);
                }
                else if (iconId > 0) {
                    drawIcon(canvas, cx, cy, boundingBox.width(), boundingBox.height(), iconId);
                }
                else {
                    String text = getDisplayText();
                    paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, boundingBox.width() - strokeWidth * 2), snappingSize * 2 * scale));
                    paint.setTextAlign(Paint.Align.CENTER);
                    paint.setStyle(Paint.Style.FILL);
                    paint.setColor(primaryColor);
                    canvas.drawText(text, x, (y - ((paint.descent() + paint.ascent()) * 0.5f)), paint);
                }

                if (doubleTapScale > 1.0f && !buildingCache) {
                    canvas.restore();
                }
                break;
            }
            case D_PAD: {
                float cx = boundingBox.centerX();
                float cy = boundingBox.centerY();
                float offsetX = snappingSize * 2 * scale;
                float offsetY = snappingSize * 3 * scale;
                float start = snappingSize * scale;

                Path dpadPath = inputControlsView.getPath();

                paint.setPathEffect(null);
                if (dpadCornerRadius > 0) {
                    if (dpadPathEffect == null) dpadPathEffect = new CornerPathEffect(dpadCornerRadius * snappingSize * scale);
                    paint.setPathEffect(dpadPathEffect);
                }

                // Draw all 4 petal strokes
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

                // Draw all 4 petal fills
                paint.setStyle(Paint.Style.FILL);
                int baseAlpha = Color.alpha(primaryColor);

                dpadPath.rewind();
                dpadPath.moveTo(cx, cy - start);
                dpadPath.lineTo(cx - offsetX, cy - offsetY);
                dpadPath.lineTo(cx - offsetX, boundingBox.top);
                dpadPath.lineTo(cx + offsetX, boundingBox.top);
                dpadPath.lineTo(cx + offsetX, cy - offsetY);
                dpadPath.close();
                paint.setColor(ColorUtils.setAlphaComponent(primaryColor, (engaged && states[0] ? baseAlpha : (baseAlpha * fillAlphaInactive() / 255))));
                canvas.drawPath(dpadPath, paint);

                dpadPath.rewind();
                dpadPath.moveTo(cx + start, cy);
                dpadPath.lineTo(cx + offsetY, cy - offsetX);
                dpadPath.lineTo(boundingBox.right, cy - offsetX);
                dpadPath.lineTo(boundingBox.right, cy + offsetX);
                dpadPath.lineTo(cx + offsetY, cy + offsetX);
                dpadPath.close();
                paint.setColor(ColorUtils.setAlphaComponent(primaryColor, (engaged && states[1] ? baseAlpha : (baseAlpha * fillAlphaInactive() / 255))));
                canvas.drawPath(dpadPath, paint);

                dpadPath.rewind();
                dpadPath.moveTo(cx, cy + start);
                dpadPath.lineTo(cx - offsetX, cy + offsetY);
                dpadPath.lineTo(cx - offsetX, boundingBox.bottom);
                dpadPath.lineTo(cx + offsetX, boundingBox.bottom);
                dpadPath.lineTo(cx + offsetX, cy + offsetY);
                dpadPath.close();
                paint.setColor(ColorUtils.setAlphaComponent(primaryColor, (engaged && states[2] ? baseAlpha : (baseAlpha * fillAlphaInactive() / 255))));
                canvas.drawPath(dpadPath, paint);

                dpadPath.rewind();
                dpadPath.moveTo(cx - start, cy);
                dpadPath.lineTo(cx - offsetY, cy - offsetX);
                dpadPath.lineTo(boundingBox.left, cy - offsetX);
                dpadPath.lineTo(boundingBox.left, cy + offsetX);
                dpadPath.lineTo(cx - offsetY, cy + offsetX);
                dpadPath.close();
                paint.setColor(ColorUtils.setAlphaComponent(primaryColor, (engaged && states[3] ? baseAlpha : (baseAlpha * fillAlphaInactive() / 255))));
                canvas.drawPath(dpadPath, paint);

                paint.setStyle(Paint.Style.STROKE);
                paint.setColor(selected ? inputControlsView.getSecondaryColor() : primaryColor);
                paint.setStrokeWidth(strokeWidth);
                paint.setPathEffect(null);
                break;
            }
            case RANGE_BUTTON: {
                Range range = getRange();
                float radius = snappingSize * 0.75f * scale;
                float elementSize = scroller.getElementSize();
                float minTextSize = snappingSize * 2 * scale;
                float scrollOffset = scroller.getScrollOffset();
                Path path = inputControlsView.getPath();
                path.reset();

                if (orientation == 0) {
                    float lineTop = boundingBox.top + strokeWidth * 0.5f;
                    float lineBottom = boundingBox.bottom - strokeWidth * 0.5f;
                    float startX = boundingBox.left;
                    paint.setStyle(Paint.Style.FILL);
                    paint.setColor(fillColor);
                    canvas.drawRoundRect(startX, boundingBox.top, boundingBox.right, boundingBox.bottom, radius, radius, paint);
                    paint.setStyle(Paint.Style.STROKE);
                    paint.setColor(selected ? inputControlsView.getSecondaryColor() : primaryColor);
                    paint.setStrokeWidth(strokeWidth);
                    canvas.drawRoundRect(startX, boundingBox.top, boundingBox.right, boundingBox.bottom, radius, radius, paint);

                    if (!buildingCache) {
                        canvas.save();
                        path.addRoundRect(startX, boundingBox.top, boundingBox.right, boundingBox.bottom, radius, radius, Path.Direction.CW);
                        canvas.clipPath(path);
                        startX -= scrollOffset % elementSize;

                        for (int i = scroller.getRangeIndexFrom(); i < scroller.getRangeIndexTo(); i++) {
                            int index = i % range.max;
                            paint.setStyle(Paint.Style.STROKE);
                            paint.setColor(primaryColor);

                            if (startX > boundingBox.left && startX  < boundingBox.right) canvas.drawLine(startX, lineTop, startX, lineBottom, paint);
                            String text = getRangeTextForIndex(range, index);

                            if (startX < boundingBox.right && startX + elementSize > boundingBox.left) {
                                if (scroller.isActionDown() && scroller.getPressedIndex() == index) {
                                    paint.setStyle(Paint.Style.FILL);
                                    paint.setColor(ColorUtils.setAlphaComponent(primaryColor, Color.alpha(primaryColor)));
                                    float r = elementSize * 0.2f;
                                    canvas.drawRoundRect(startX, lineTop, startX + elementSize, lineBottom, r, r, paint);
                                }
                                paint.setStyle(Paint.Style.FILL);
                                paint.setColor(primaryColor);
                                paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, elementSize - strokeWidth * 2), minTextSize));
                                paint.setTextAlign(Paint.Align.CENTER);
                                canvas.drawText(text, startX + elementSize * 0.5f, (y - ((paint.descent() + paint.ascent()) * 0.5f)), paint);
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
                    paint.setColor(selected ? inputControlsView.getSecondaryColor() : primaryColor);
                    paint.setStrokeWidth(strokeWidth);
                    canvas.drawRoundRect(boundingBox.left, startY, boundingBox.right, boundingBox.bottom, radius, radius, paint);

                    if (!buildingCache) {
                        canvas.save();
                        path.addRoundRect(boundingBox.left, startY, boundingBox.right, boundingBox.bottom, radius, radius, Path.Direction.CW);
                        canvas.clipPath(path);
                        startY -= scrollOffset % elementSize;

                        for (int i = scroller.getRangeIndexFrom(); i < scroller.getRangeIndexTo(); i++) {
                            paint.setStyle(Paint.Style.STROKE);
                            paint.setColor(primaryColor);

                            if (startY > boundingBox.top && startY < boundingBox.bottom) canvas.drawLine(lineLeft, startY, lineRight, startY, paint);
                            String text = getRangeTextForIndex(range, i);

                            if (startY < boundingBox.bottom && startY + elementSize > boundingBox.top) {
                                if (scroller.isActionDown() && scroller.getPressedIndex() == (i % range.max)) {
                                    paint.setStyle(Paint.Style.FILL);
                                    paint.setColor(ColorUtils.setAlphaComponent(primaryColor, Color.alpha(primaryColor)));
                                    float r = elementSize * 0.2f;
                                    canvas.drawRoundRect(lineLeft, startY, lineRight, startY + elementSize, r, r, paint);
                                }
                                paint.setStyle(Paint.Style.FILL);
                                paint.setColor(primaryColor);
                                paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, boundingBox.width() - strokeWidth * 2), minTextSize));
                                paint.setTextAlign(Paint.Align.CENTER);
                                canvas.drawText(text, x, startY + elementSize * 0.5f - ((paint.descent() + paint.ascent()) * 0.5f), paint);
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
                int cx = boundingBox.centerX();  // Fixed outer circle center
                int cy = boundingBox.centerY();  // Fixed outer circle center
                int oldColor = paint.getColor();

                canvas.drawCircle(cx, cy, boundingBox.height() * 0.5f, paint);

                if (!buildingCache) {
                    float thumbstickX = getCurrentPosition().x;
                    float thumbstickY = getCurrentPosition().y;

                    short thumbRadius = (short) (snappingSize * 3.5f * scale);
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
                float radius = boundingBox.height() * 0.15f;
                paint.setStyle(Paint.Style.FILL);
                paint.setColor(fillColor);
                canvas.drawRoundRect(boundingBox.left, boundingBox.top, boundingBox.right, boundingBox.bottom, radius, radius, paint);
                paint.setStyle(Paint.Style.STROKE);
                paint.setColor(selected ? inputControlsView.getSecondaryColor() : primaryColor);
                paint.setStrokeWidth(strokeWidth);
                canvas.drawRoundRect(boundingBox.left, boundingBox.top, boundingBox.right, boundingBox.bottom, radius, radius, paint);
                float offset = strokeWidth * 2.5f;
                float innerStrokeWidth = strokeWidth * 2;
                float innerHeight = boundingBox.height() - offset * 2;
                radius = (innerHeight / boundingBox.height()) * radius - (innerStrokeWidth * 0.5f + strokeWidth * 0.5f);
                paint.setStrokeWidth(innerStrokeWidth);
                canvas.drawRoundRect(boundingBox.left + offset, boundingBox.top + offset, boundingBox.right - offset, boundingBox.bottom - offset, radius, radius, paint);
                break;
            }
        }
    }

    private void drawButtonShape(Canvas canvas, Rect box, int snappingSize, Paint paint) {
        float cx = box.centerX();
        float cy = box.centerY();
        switch (shape) {
            case CIRCLE:
                canvas.drawCircle(cx, cy, box.width() * 0.5f, paint);
                break;
            case RECT: {
                float r = cornerRadius * snappingSize * scale;
                if (r > 0)
                    canvas.drawRoundRect(box.left, box.top, box.right, box.bottom, r, r, paint);
                else
                    canvas.drawRect(box, paint);
                break;
            }
        }
    }

    private void drawIcon(Canvas canvas, float cx, float cy, float width, float height, int iconId) {
        Paint paint = inputControlsView.getPaint();
        Bitmap icon = inputControlsView.getIcon((byte)iconId);
        if (icon == null) return;
        paint.setColorFilter(inputControlsView.getColorFilter());
        int margin = (int)(inputControlsView.getSnappingSize() * (shape == Shape.CIRCLE ? 2.0f : 1.0f) * scale);
        int halfSize = (int)((Math.min(width, height) - margin) * 0.5f);

        drawIconSrcRect.set(0, 0, icon.getWidth(), icon.getHeight());
        drawIconDstRect.set((int)(cx - halfSize), (int)(cy - halfSize), (int)(cx + halfSize), (int)(cy + halfSize));
        canvas.drawBitmap(icon, drawIconSrcRect, drawIconDstRect, paint);
        paint.setColorFilter(null);
    }

    public JSONObject toJSONObject() {
        try {
            JSONObject elementJSONObject = new JSONObject();
            elementJSONObject.put("type", type.name());
            elementJSONObject.put("shape", shape.name());
            if (type == Type.BUTTON && shape == Shape.RECT) {
                elementJSONObject.put("elementWidth", elementWidth);
                elementJSONObject.put("elementHeight", elementHeight);
                elementJSONObject.put("cornerRadius", cornerRadius);
            }

            JSONArray bindingsJSONArray = new JSONArray();
            for (List<Binding> seq : bindings) {
                JSONArray seqArray = new JSONArray();
                for (Binding b : seq) {
                    if (b != null && b != Binding.NONE) seqArray.put(b.name());
                }
                bindingsJSONArray.put(seqArray);
            }

            elementJSONObject.put("bindings", bindingsJSONArray);
            elementJSONObject.put("scale", Float.valueOf(scale));
            elementJSONObject.put("x", (float)x / inputControlsView.getMaxWidth());
            elementJSONObject.put("y", (float)y / inputControlsView.getMaxHeight());
            elementJSONObject.put("toggleSwitch", toggleSwitch);
            elementJSONObject.put("text", text);
            elementJSONObject.put("iconId", iconId);
            if (hasCustomIcon()) elementJSONObject.put("customIconData", customIconData);

            if (hasLongPressBinding()) {
                JSONArray lpArray = new JSONArray();
                for (Binding b : longPressBindings) {
                    if (b != null && b != Binding.NONE) lpArray.put(b.name());
                }
                elementJSONObject.put("longPressBindings", lpArray);
            }

            if (hasGestureBinding()) {
                JSONArray gArray = new JSONArray();
                for (Binding b : gestureBindings) {
                    if (b != null && b != Binding.NONE) gArray.put(b.name());
                }
                elementJSONObject.put("gestureBindings", gArray);
            }

            if (hasDoubleTapBinding()) {
                JSONArray dtArray = new JSONArray();
                for (Binding b : doubleTapBindings) {
                    if (b != null && b != Binding.NONE) dtArray.put(b.name());
                }
                elementJSONObject.put("doubleTapBindings", dtArray);
            }

            if (type == Type.RANGE_BUTTON && range != null) {
                elementJSONObject.put("range", range.name());
                if (orientation != 0) elementJSONObject.put("orientation", orientation);
            }
            if (type == Type.D_PAD && dpadCornerRadius > 0) {
                elementJSONObject.put("dpadCornerRadius", dpadCornerRadius);
            }
            return elementJSONObject;
        }
        catch (JSONException e) {
            return null;
        }
    }

    public boolean containsPoint(float x, float y) {
        return getBoundingBox().contains((int)(x + 0.5f), (int)(y + 0.5f));
    }

    private boolean isKeepButtonPressedAfterMinTime() {
        Binding binding = getBindingAt(0);
        return !toggleSwitch && (binding == Binding.GAMEPAD_BUTTON_L3 || binding == Binding.GAMEPAD_BUTTON_R3);
    }

    private void pressBindings(List<Binding> seq) {
        ControlsProfile profile = inputControlsView.getProfile();
        int delay = profile != null ? profile.getBindingDelay() : 0;
        int size = seq.size();

        for (int i = 0; i < size; i++) {
            Binding b = seq.get(i);
            if (b == null || b == Binding.NONE) continue;
            Binding kb = b.toKeyboardBinding();
            if (kb != null) inputControlsView.handleInputEvent(kb, true);
        }

        for (int i = 0; i < size; i++) {
            Binding b = seq.get(i);
            if (b == null || b == Binding.NONE) continue;
            if (b.isModifier()) continue;
            inputControlsView.handleInputEvent(b, true);
            if (delay > 0 && i < size - 1) SystemClock.sleep(delay);
        }
    }

    private void releaseHeldBindings() {
        if (heldBindings == null) return;
        int size = heldBindings.size();

        for (int i = size - 1; i >= 0; i--) {
            Binding b = heldBindings.get(i);
            if (b == null || b == Binding.NONE) continue;
            if (b.isModifier()) continue;
            inputControlsView.handleInputEvent(b, false);
        }

        for (int i = size - 1; i >= 0; i--) {
            Binding b = heldBindings.get(i);
            if (b == null || b == Binding.NONE) continue;
            Binding kb = b.toKeyboardBinding();
            if (kb != null) inputControlsView.handleInputEvent(kb, false);
        }

        heldBindings = null;
    }

    public void activate() {
        if (bindings.isEmpty() || bindings.get(0).isEmpty()) return;
        active = true;
        if (toggleSwitch) {
            selected = !selected;
            if (selected) {
                heldBindings = new ArrayList<>(bindings.get(0));
                pressBindings(bindings.get(0));
            } else {
                releaseHeldBindings();
            }
        } else {
            heldBindings = new ArrayList<>(bindings.get(0));
            pressBindings(bindings.get(0));
        }
        inputControlsView.invalidate();
    }

    private void vibrateHaptic(int hapticType, int fallbackMs) {
        if (hapticType <= 0) return;
        android.os.Vibrator vib = (android.os.Vibrator) inputControlsView.getContext().getSystemService(Context.VIBRATOR_SERVICE);
        if (vib == null || !vib.hasVibrator()) return;
        com.winlator.cmod.core.HapticUtils.perform(inputControlsView.getContext(), hapticType);
    }

    public void startLongPressTimer(int delay) {
        if (!hasLongPressBinding() || toggleSwitch || delay <= 0) return;
        if (longPressHandler == null) longPressHandler = new android.os.Handler(android.os.Looper.getMainLooper());
        longPressTriggered = false;
        longPressHandler.postDelayed(() -> {
            if (gestureTriggered) return;
            longPressTriggered = true;
            vibrateHaptic(cachedButtonLongPressHaptic, 50);
            heldBindings = new ArrayList<>(longPressBindings);
            pressBindings(longPressBindings);
            inputControlsView.invalidate();
        }, delay);
    }

    public void deactivate() {
        active = false;
        currentPointerId = -1;
        cancelLongPress();
        if (!(toggleSwitch && selected)) releaseHeldBindings();
        inputControlsView.invalidate();
    }

    public void releaseTapBindings() {
        active = false;
        releaseHeldBindings();
        inputControlsView.invalidate();
    }

    public void cancelLongPress() {
        cancelPendingLongPress();
        longPressTriggered = false;
        gestureTriggered = false;
    }

    public void cancelPendingLongPress() {
        if (longPressHandler != null) {
            longPressHandler.removeCallbacksAndMessages(null);
            longPressHandler = null;
        }
    }

    private void startLongPressTimer() {
        longPressTriggered = false;
        ControlsProfile localProfile = inputControlsView.getProfile();
        int delay = localProfile != null ? localProfile.getLongPressDelay() : 0;
        if (delay > 0) {
            if (longPressHandler == null) longPressHandler = new android.os.Handler(android.os.Looper.getMainLooper());
            longPressHandler.postDelayed(() -> {
                longPressTriggered = true;
                cancelPendingDoubleTap();
                doubleTapWaiting = false;
                vibrateHaptic(cachedButtonLongPressHaptic, 30);
                heldBindings = new ArrayList<>(longPressBindings);
                pressBindings(longPressBindings);
                inputControlsView.invalidate();
            }, delay);
        }
    }

    public void cancelPendingDoubleTap() {
        if (doubleTapHandler != null) {
            doubleTapHandler.removeCallbacksAndMessages(null);
            doubleTapHandler = null;
        }
    }

    private void startDoubleTapTimer() {
        ControlsProfile p = inputControlsView.getProfile();
        int delay = p != null ? p.getButtonDoubleTapTimeout() : 150;
        if (doubleTapHandler == null) doubleTapHandler = new android.os.Handler(android.os.Looper.getMainLooper());
        doubleTapHandler.removeCallbacksAndMessages(null);
        doubleTapHandler.postDelayed(this::onDoubleTapTimeout, delay);
        doubleTapWaiting = true;
    }

    private void onDoubleTapTimeout() {
        if (!doubleTapWaiting) return;
        doubleTapWaiting = false;
        cancelPendingLongPress();
        List<Binding> seq = bindings.get(0);
        heldBindings = new ArrayList<>(seq);
        pressBindings(seq);
        releaseHeldBindings();
        active = false;
        inputControlsView.invalidate();
    }

    private void cancelDoubleTap() {
        cancelPendingDoubleTap();
        cancelPendingLongPress();
        longPressTriggered = false;
        doubleTapWaiting = false;
        active = false;
        List<Binding> seq = bindings.get(0);
        heldBindings = new ArrayList<>(seq);
        pressBindings(seq);
        releaseHeldBindings();
        inputControlsView.invalidate();
    }

    private void handleDoubleTapConfirmed() {
        doubleTapWaiting = false;
        cancelPendingDoubleTap();
        cancelPendingLongPress();
        longPressTriggered = false;
        gestureTriggered = false;
        doubleTapTriggered = true;
        doubleTapScale = 1.5f;
        heldBindings = new ArrayList<>(doubleTapBindings);
        pressBindings(doubleTapBindings);
        inputControlsView.invalidate();
    }

    public boolean handleTouchDown(int pointerId, float x, float y) {
        if (currentPointerId == -1 && containsPoint(x, y)) {
            currentPointerId = pointerId;
            if (type == Type.BUTTON) {
                gestureDownX = x;
                gestureDownY = y;
                if (isKeepButtonPressedAfterMinTime()) touchTime = System.currentTimeMillis();
                if (toggleSwitch && selected) {                    releaseHeldBindings();
                }
                else if (hasDoubleTapBinding()) {
                    doubleTapDownX = x;
                    doubleTapDownY = y;
                    if (doubleTapWaiting) {
                        handleDoubleTapConfirmed();
                    }
                    else {
                        if (hasLongPressBinding() && !toggleSwitch) {
                            startLongPressTimer();
                        }
                    }
                }
                else if (hasLongPressBinding() && !toggleSwitch) {
                    startLongPressTimer();
                }
                else {
                    heldBindings = bindings.get(0);
                    pressBindings(bindings.get(0));
                }
                inputControlsView.invalidate();
                return true;
            }
            else if (type == Type.RANGE_BUTTON) {
                scroller.handleTouchDown(x, y);
                inputControlsView.invalidate();
                return true;
            }
            else {
                if (type == Type.TRACKPAD) {
                    if (currentPosition == null) currentPosition = new PointF();
                    currentPosition.set(x, y);
                }
                return handleTouchMove(pointerId, x, y);
            }
        }
        else return false;
    }

    public boolean handleTouchMove(int pointerId, float x, float y) {
        if (pointerId == currentPointerId) {
            if (type == Type.BUTTON && hasGestureBinding() && !gestureTriggered && !longPressTriggered && !doubleTapTriggered) {
                float dx = x - gestureDownX;
                float dy = y - gestureDownY;
                ControlsProfile p = inputControlsView.getProfile();
                int threshold = p != null ? p.getGestureThreshold() : 20;
                float distSq = dx * dx + dy * dy;
                if (distSq > threshold * threshold) {
                    gestureTriggered = true;
                    if (longPressHandler != null) {
                        longPressHandler.removeCallbacksAndMessages(null);
                        longPressHandler = null;
                    }
                    longPressTriggered = false;
                    cancelPendingDoubleTap();
                    vibrateHaptic(cachedButtonGestureHaptic, 10);
                    heldBindings = new ArrayList<>(gestureBindings);
                    pressBindings(gestureBindings);
                    inputControlsView.invalidate();
                    return true;
                }
            }
            if (type == Type.BUTTON && hasLongPressBinding() && !longPressTriggered && longPressHandler != null && !containsPoint(x, y)) {
                longPressHandler.removeCallbacksAndMessages(null);
                longPressHandler = null;
                longPressTriggered = false;
            }
            if (type == Type.BUTTON && !containsPoint(x, y) && !gestureTriggered && !longPressTriggered && doubleTapWaiting) {
                cancelDoubleTap();
            }
            if (type == Type.D_PAD || type == Type.STICK || type == Type.TRACKPAD) {
            float deltaX, deltaY;
            Rect boundingBox = getBoundingBox();
            float radius = boundingBox.width() * 0.5f;
            TouchpadView touchpadView =  inputControlsView.getTouchpadView();

            if (type == Type.TRACKPAD) {
                if (currentPosition == null) currentPosition = new PointF();
                float[] deltaPoint = touchpadView.computeDeltaPoint(currentPosition.x, currentPosition.y, x, y);
                deltaX = deltaPoint[0];
                deltaY = deltaPoint[1];
                currentPosition.set(x, y);
            }
            else {
                float localX = x - boundingBox.left;
                float localY = y - boundingBox.top;
                float offsetX = localX - radius;
                float offsetY = localY - radius;

                float distance = Mathf.lengthSq(radius - localX, radius - localY);
                if (distance > radius * radius) {
                    float angle = (float)Math.atan2(offsetY, offsetX);
                    offsetX = (float)(Math.cos(angle) * radius);
                    offsetY = (float)(Math.sin(angle) * radius);
                }

                deltaX = Mathf.clamp(offsetX / radius, -1, 1);
                deltaY = Mathf.clamp(offsetY / radius, -1, 1);
            }

            if (type == Type.STICK) {
                if (currentPosition == null) currentPosition = new PointF();
                currentPosition.x = boundingBox.left + deltaX * radius + radius;
                currentPosition.y = boundingBox.top + deltaY * radius + radius;
                
                Binding firstBinding = getBindingAt(0);
                if (firstBinding.isGamepad()) {
                    float magnitude = (float)Math.sqrt(deltaX * deltaX + deltaY * deltaY);
                    
                    float finalX = 0;
                    float finalY = 0;
                    
                    if (magnitude > STICK_DEAD_ZONE) {
                        float normalizedX = deltaX / magnitude;
                        float normalizedY = deltaY / magnitude;
                        
                        float scaledMagnitude = Math.max(0, magnitude - 0.01f) * STICK_SENSITIVITY;
                        scaledMagnitude = Math.min(scaledMagnitude, 1.0f);
                        
                        finalX = normalizedX * scaledMagnitude;
                        finalY = normalizedY * scaledMagnitude;
                    }
                    
                    inputControlsView.handleStickInput(firstBinding, finalX, finalY);
                    
                    for (byte i = 0; i < 4; i++) {
                        this.states[i] = true;
                    }
                } else {
                    scratchStates[0] = deltaY <= -STICK_DEAD_ZONE;
                    scratchStates[1] = deltaX >= STICK_DEAD_ZONE;
                    scratchStates[2] = deltaY >= STICK_DEAD_ZONE;
                    scratchStates[3] = deltaX <= -STICK_DEAD_ZONE;
                    for (byte i = 0; i < 4; i++) {
                        float value = i == 1 || i == 3 ? deltaX : deltaY;
                        List<Binding> seq = bindings.get(i);
                        for (int j = 0, sz = seq.size(); j < sz; j++) {
                            Binding binding = seq.get(j);
                            boolean state = binding.isMouseMove() ? (scratchStates[i] || scratchStates[(i+2)%4]) : scratchStates[i];
                            inputControlsView.handleInputEvent(binding, state, value);
                            this.states[i] = state;
                        }
                    }
                }

                inputControlsView.invalidate();
            }
            else if (type == Type.TRACKPAD) {
                Binding firstBinding = getBindingAt(0);
                if (firstBinding.isGamepad()) {
                    if (interpolator == null) interpolator = new CubicBezierInterpolator();
                    interpolator.set(0.075f, 0.95f, 0.45f, 0.95f);
                    
                    float valueX = deltaX;
                    float valueY = deltaY;
                    if (Math.abs(valueX) > TRACKPAD_ACCELERATION_THRESHOLD) valueX *= STICK_SENSITIVITY;
                    if (Math.abs(valueY) > TRACKPAD_ACCELERATION_THRESHOLD) valueY *= STICK_SENSITIVITY;
                    
                    float interpX = interpolator.getInterpolation(Math.min(1.0f, Math.abs(valueX / TRACKPAD_MAX_SPEED)));
                    float interpY = interpolator.getInterpolation(Math.min(1.0f, Math.abs(valueY / TRACKPAD_MAX_SPEED)));
                    
                    float finalX = Mathf.clamp(interpX * Mathf.sign(valueX), -1, 1);
                    float finalY = Mathf.clamp(interpY * Mathf.sign(valueY), -1, 1);
                    
                    inputControlsView.handleStickInput(firstBinding, finalX, finalY);
                    
                    for (byte i = 0; i < 4; i++) {
                        this.states[i] = true;
                    }
                } else {
                    scratchStates[0] = deltaY <= -TRACKPAD_MIN_SPEED;
                    scratchStates[1] = deltaX >= TRACKPAD_MIN_SPEED;
                    scratchStates[2] = deltaY >= TRACKPAD_MIN_SPEED;
                    scratchStates[3] = deltaX <= -TRACKPAD_MIN_SPEED;
                    int cursorDx = 0;
                    int cursorDy = 0;

                    for (byte i = 0; i < 4; i++) {
                        float value = (i == 1 || i == 3 ? deltaX : deltaY);
                        if (Math.abs(value) > TouchpadView.CURSOR_ACCELERATION_THRESHOLD) value *= TouchpadView.CURSOR_ACCELERATION;
                        List<Binding> seq = bindings.get(i);
                        for (int j = 0, sz = seq.size(); j < sz; j++) {
                            Binding binding = seq.get(j);
                            if (binding == Binding.MOUSE_MOVE_LEFT || binding == Binding.MOUSE_MOVE_RIGHT) {
                                cursorDx = Mathf.roundPoint(value);
                            }
                            else if (binding == Binding.MOUSE_MOVE_UP || binding == Binding.MOUSE_MOVE_DOWN) {
                                cursorDy = Mathf.roundPoint(value);
                            }
                            else {
                                inputControlsView.handleInputEvent(binding, scratchStates[i], value);
                            }
                        }
                        this.states[i] = scratchStates[i];
                    }

                    if (cursorDx != 0 || cursorDy != 0)  {
                        XServer xServer = inputControlsView.getXServer();
                        if (xServer.getInputMode() == InputMode.RELATIVE)
                            xServer.getWinHandler().mouseEvent(MouseEventFlags.MOVE, cursorDx, cursorDy, 0);
                        else
                            inputControlsView.getXServer().injectPointerMoveDelta(cursorDx, cursorDy);
                    }
                }
            }
            else {
                scratchStates[0] = deltaY <= -DPAD_DEAD_ZONE;
                scratchStates[1] = deltaX >= DPAD_DEAD_ZONE;
                scratchStates[2] = deltaY >= DPAD_DEAD_ZONE;
                scratchStates[3] = deltaX <= -DPAD_DEAD_ZONE;

                for (byte i = 0; i < 4; i++) {
                    float value = i == 1 || i == 3 ? deltaX : deltaY;
                    List<Binding> seq = bindings.get(i);
                    for (int j = 0, sz = seq.size(); j < sz; j++) {
                        Binding binding = seq.get(j);
                        boolean state = binding.isMouseMove() ? (scratchStates[i] || scratchStates[(i+2)%4]) : scratchStates[i];
                        inputControlsView.handleInputEvent(binding, state, value);
                        this.states[i] = state;
                    }
                }
                inputControlsView.invalidate();
                return true;
            }
            }
            if (type == Type.RANGE_BUTTON) {
                scroller.handleTouchMove(x, y);
                return true;
            }
            return true;
        }
        return false;
    }

    public boolean handleTouchUp(int pointerId) {
        if (pointerId == currentPointerId) {
            if (type == Type.BUTTON) {
                if (isKeepButtonPressedAfterMinTime() && touchTime != 0) {
                    selected = (System.currentTimeMillis() - touchTime) > BUTTON_MIN_TIME_TO_KEEP_PRESSED;
                    touchTime = 0;
                }

                if (toggleSwitch) {
                    selected = !selected;
                    if (selected) {
                        currentPointerId = -1;
                        return true;
                    }
                }

                if (longPressTriggered) {
                    releaseHeldBindings();
                    longPressTriggered = false;
                }
                else if (gestureTriggered) {
                    releaseHeldBindings();
                    gestureTriggered = false;
                }
                else if (doubleTapTriggered) {
                    releaseHeldBindings();
                    doubleTapScale = 1.0f;
                    doubleTapTriggered = false;
                    active = false;
                }
                else if (hasDoubleTapBinding() && !doubleTapWaiting) {
                    cancelPendingLongPress();
                    longPressTriggered = false;
                    List<Binding> primarySeq = bindings.get(0);
                    boolean hasSingleTap = primarySeq != null && !primarySeq.isEmpty() && primarySeq.get(0) != Binding.NONE;
                    if (hasSingleTap) active = true;
                    startDoubleTapTimer();
                }
                else if (hasLongPressBinding()) {
                    if (longPressHandler != null) longPressHandler.removeCallbacksAndMessages(null);
                    List<Binding> seq = bindings.get(0);
                    heldBindings = new ArrayList<>(seq);
                    pressBindings(seq);
                    releaseHeldBindings();
                }
                else {
                    releaseHeldBindings();
                }
                inputControlsView.invalidate();
            }
            else if (type == Type.RANGE_BUTTON || type == Type.D_PAD || type == Type.STICK || type == Type.TRACKPAD) {
                for (byte i = 0; i < states.length; i++) {
                    if (states[i]) {
                        for (Binding b : bindings.get(i)) {
                            if (b != Binding.NONE) inputControlsView.handleInputEvent(b, false);
                        }
                    }
                    states[i] = false;
                }

                if (type == Type.RANGE_BUTTON) {
                    scroller.handleTouchUp();
                    inputControlsView.invalidate();
                }
                else if (type == Type.STICK || type == Type.D_PAD) {
                    inputControlsView.invalidate();
                }

                if (currentPosition != null) currentPosition = null;
            }
            currentPointerId = -1;
            return true;
        }
        return false;
    }

    public PointF getCurrentPosition() {
        if (currentPosition == null) {
            currentPosition = new PointF(x, y); // Initialize to the center (same as outer circle)
        }
        return currentPosition;
    }

    public void setCurrentPosition(float x, float y) {
        if (currentPosition == null) {
            currentPosition = new PointF();
        }
        currentPosition.set(x, y);
        inputControlsView.invalidate();
    }
}
