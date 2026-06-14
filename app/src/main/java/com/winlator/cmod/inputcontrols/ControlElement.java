package com.winlator.cmod.inputcontrols;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.PointF;
import android.graphics.Rect;

import com.winlator.cmod.widget.InputControlsView;

import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

public class ControlElement {
    public static final float STICK_DEAD_ZONE = 0.15f;
    public static final float DPAD_DEAD_ZONE = 0.3f;
    public static final float STICK_SENSITIVITY = 2.0f;
    public static final float TRACKPAD_MIN_SPEED = 0.8f;
    public static final float TRACKPAD_MAX_SPEED = 20.0f;
    public static final byte TRACKPAD_ACCELERATION_THRESHOLD = 4;

    public static final int VF_TAP      = 1;
    public static final int VF_LONG_TAP = 2;
    public static final int VF_GESTURE  = 4;

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
        FROM_A_TO_Z(26, 38), FROM_0_TO_9(10, 10), FROM_F1_TO_F12(12, 67), FROM_NP0_TO_NP9(10, 87);
        public final byte max;
        public final String[] texts;
        public final int initialKc;
        Range(int max, int initialKc) {
            this.max = (byte)max;
            this.initialKc = initialKc;
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
    public enum BindingSection {
        SLOT_0, SLOT_1, SLOT_2, SLOT_3,
        LONG_PRESS,
        GESTURE
    }

    // Package-private model fields (accessible by ElementRenderer/ElementBindings/ElementSerializer)
    final InputControlsView inputControlsView;
    Type type = Type.BUTTON;
    Shape shape = Shape.CIRCLE;
    float elementWidth = 8f;
    float elementHeight = 4f;
    float cornerRadius = -1f;
    float dpadCornerRadius = 0.6f;
    float scale = 1.0f;
    short x;
    short y;
    boolean selected = false;
    boolean passthroughTouch;
    float opacity = -1f;
    final Rect boundingBox = new Rect();
    boolean boundingBoxNeedsUpdate = true;
    String text = "";
    byte iconId;
    String customIconData = "";
    Bitmap customIcon;
    String cachedDisplayText;
    boolean displayTextDirty = true;
    Range range;
    byte orientation;
    PointF currentPosition;
    RangeScroller scroller;
    boolean buildingCache;
    boolean tapActive;
    boolean longTapActive;
    boolean gestureActive;
    int visualLayers;
    int visualAlphas;
    float cachedStrokeWidth;
    int cachedFillAlphaInactive;
    float cachedProfileCornerRadius = 0.8f;

    // Sub-components
    final ElementBindings bindings;
    final ElementRenderer renderer;

    public ControlElement(InputControlsView inputControlsView) {
        this.inputControlsView = inputControlsView;
        this.bindings = new ElementBindings(this);
        this.renderer = new ElementRenderer(this);
        currentPosition = new PointF();
        refreshProfileCache();
    }

    void refreshProfileCache() {
        ControlsProfile p = inputControlsView.getProfile();
        cachedStrokeWidth = p != null ? p.getStrokeWidth() : 0.2f;
        cachedFillAlphaInactive = p != null ? p.getFillAlphaInactive() : 50;
        cachedProfileCornerRadius = p != null ? p.getCornerRadius() : 0.8f;
    }

    // === Type / Shape ===

    public Type getType() { return type; }

    public void setType(Type type) {
        if (this.type == type) return;
        this.type = type;
        bindings.reset(type);
        scroller = null;
        text = "";
        iconId = 0;
        customIconData = "";
        customIcon = null;
        range = null;
        cornerRadius = -1f;
        opacity = -1f;
        boundingBoxNeedsUpdate = true;
        if (type == Type.RANGE_BUTTON) {
            scroller = new RangeScroller(inputControlsView, this);
        }
        refreshProfileCache();
        renderer.invalidateAll();
    }

    public Shape getShape() { return shape; }
    public void setShape(Shape shape) { this.shape = shape; boundingBoxNeedsUpdate = true; renderer.invalidateAll(); }

    // === Element geometry ===

    public float getElementWidth() { return elementWidth; }
    public void setElementWidth(float elementWidth) { this.elementWidth = elementWidth; boundingBoxNeedsUpdate = true; renderer.invalidateAll(); }
    public float getElementHeight() { return elementHeight; }
    public void setElementHeight(float elementHeight) { this.elementHeight = elementHeight; boundingBoxNeedsUpdate = true; renderer.invalidateAll(); }
    public float getCornerRadius() { return cornerRadius; }
    public void setCornerRadius(float cornerRadius) { this.cornerRadius = cornerRadius; renderer.invalidateAll(); }
    public float getDpadCornerRadius() { return dpadCornerRadius; }
    public void setDpadCornerRadius(float dpadCornerRadius) { this.dpadCornerRadius = dpadCornerRadius; renderer.invalidateAll(); }
    public Range getRange() { return range != null ? range : Range.FROM_A_TO_Z; }
    public void setRange(Range range) { this.range = range; renderer.invalidateAll(); }
    public byte getOrientation() { return orientation; }
    public void setOrientation(byte orientation) { this.orientation = orientation; boundingBoxNeedsUpdate = true; renderer.invalidateAll(); }
    public boolean isPassthroughTouch() { return passthroughTouch; }
    public void setPassthroughTouch(boolean passthroughTouch) { this.passthroughTouch = passthroughTouch; }

    // === Scale ===

    public float getScale() { return scale; }
    public void setScale(float scale) { this.scale = scale; boundingBoxNeedsUpdate = true; renderer.invalidateAll(); }

    // === Opacity ===

    public float getOpacity() { return opacity; }
    public void setOpacity(float opacity) { this.opacity = opacity < 0 ? -1f : Math.max(0.1f, Math.min(opacity, 1.0f)); renderer.invalidateKeepDisk(); }
    public float getEffectiveOpacity() { return opacity >= 0 ? opacity : inputControlsView.getOverlayOpacity(); }
    public float getEffectiveCornerRadius() {
        if (cornerRadius >= 0) return cornerRadius;
        ControlsProfile p = inputControlsView.getProfile();
        return p != null ? p.getCornerRadius() : 0.8f;
    }
    public int getEffectiveAlphaInt() {
        if (buildingCache) return 255;
        return (int)(getEffectiveOpacity() * 255);
    }

    // === Position ===

    public short getX() { return x; }
    public void setX(int x) { this.x = (short)x; if (currentPosition != null) currentPosition.x = x; boundingBoxNeedsUpdate = true; renderer.invalidateAll(); }
    public short getY() { return y; }
    public void setY(int y) { this.y = (short)y; if (currentPosition != null) currentPosition.y = y; boundingBoxNeedsUpdate = true; renderer.invalidateAll(); }
    public boolean isSelected() { return selected; }
    public void setSelected(boolean selected) { this.selected = selected; }

    public PointF getCurrentPosition() { return currentPosition; }
    public void setCurrentPosition(float x, float y) {
        if (currentPosition == null) currentPosition = new PointF();
        currentPosition.set(x, y);
        inputControlsView.invalidate();
    }

    // === Text / Icon ===

    public String getText() { return text; }
    public void setText(String text) { this.text = text != null ? text : ""; renderer.invalidateAll(); }
    public byte getIconId() { return iconId; }
    public void setIconId(int iconId) { this.iconId = (byte)iconId; renderer.invalidateAll(); }
    public boolean hasCustomIcon() { return customIconData != null && !customIconData.isEmpty(); }
    public String getCustomIconData() { return customIconData; }
    public void setCustomIconData(String customIconData) {
        String oldFillKey = renderer.visualKey(ElementRenderer.LAYER_FILL);
        String oldCombinedKey = renderer.visualKey(ElementRenderer.LAYER_COMBINED);
        String oldStrokeTextKey = renderer.visualKey(ElementRenderer.LAYER_STROKE_TEXT);
        String oldDiskFillKey = renderer.diskKey(ElementRenderer.LAYER_FILL);
        String oldDiskCombinedKey = renderer.diskKey(ElementRenderer.LAYER_COMBINED);
        String oldDiskStrokeTextKey = renderer.diskKey(ElementRenderer.LAYER_STROKE_TEXT);
        this.customIconData = customIconData != null ? customIconData : "";
        customIcon = null;
        renderer.invalidateAll();
        ElementRenderer.sharedPool.remove(oldFillKey);
        ElementRenderer.sharedPool.remove(oldCombinedKey);
        ElementRenderer.sharedPool.remove(oldStrokeTextKey);
        if (inputControlsView != null) {
            java.io.File dir = renderer.cacheDir();
            for (String key : new String[]{oldDiskCombinedKey, oldDiskFillKey, oldDiskStrokeTextKey}) {
                java.io.File f = new java.io.File(dir, key + ".png");
                if (f.exists()) f.delete();
            }
        }
    }
    public Bitmap getCustomIcon() {
        if (customIcon == null && hasCustomIcon()) {
            try {
                byte[] data = android.util.Base64.decode(customIconData, android.util.Base64.DEFAULT);
                customIcon = BitmapFactory.decodeByteArray(data, 0, data.length);
            } catch (IllegalArgumentException e) { customIconData = ""; }
        }
        return customIcon;
    }

    // === Display text ===

    String getDisplayText() {
        if (text != null && !text.isEmpty()) return text;
        if (!displayTextDirty && cachedDisplayText != null) return cachedDisplayText;
        displayTextDirty = false;
        Bind binding = getBindingAt(0);
        if (binding == Bind.NONE && bindings.slotPackages != null && bindings.slotPackages.length > 0 && bindings.slotPackages[0] != null && !bindings.slotPackages[0].isEmpty()) {
            binding = bindings.slotPackages[0].get(0);
        }
        if (binding == null) binding = Bind.NONE;
        String display = binding.toString().replace("NUMPAD ", "NP").replace("BUTTON ", "");
        if (display.length() > 7) {
            String[] parts = display.split(" ");
            StringBuilder sb = new StringBuilder();
            for (String part : parts) sb.append(part.charAt(0));
            cachedDisplayText = (binding.isMouse() ? "M" : "") + sb;
        } else {
            cachedDisplayText = display;
        }
        return cachedDisplayText;
    }

    public void markDisplayTextDirty() { displayTextDirty = true; }

    // === Bounding box ===

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
            case D_PAD:
                halfWidth = snappingSize * 7;
                halfHeight = snappingSize * 7;
                break;
            case TRACKPAD:
            case STICK:
                halfWidth = snappingSize * 6;
                halfHeight = snappingSize * 6;
                break;
            case RANGE_BUTTON:
                halfWidth = snappingSize * ((bindings.getBindingCount() * 4) / 2);
                halfHeight = snappingSize * 2;
                if (orientation != 0) { int tmp = halfWidth; halfWidth = halfHeight; halfHeight = tmp; }
                break;
        }
        halfWidth *= scale;
        halfHeight *= scale;
        boundingBox.set(x - halfWidth, y - halfHeight, x + halfWidth, y + halfHeight);
        boundingBoxNeedsUpdate = false;
        return boundingBox;
    }

    // === State ===

    public boolean hasAnyAction() {
        for (int i = 0; i < bindings.bindings.size(); i++) {
            List<Bind> seq = bindings.bindings.get(i);
            if (seq != null) {
                for (Bind b : seq) {
                    if (b != null && b != Bind.NONE) return true;
                }
            }
        }
        if (type == Type.BUTTON) {
            if (bindings.slotPackages != null) {
                for (BindPackage pkg : bindings.slotPackages) {
                    if (pkg != null && !pkg.isEmpty()) return true;
                }
            }
        } else {
            for (int i = 0; i < 4; i++) {
                if (bindings.binds[i] != null && bindings.binds[i] != Bind.NONE) return true;
            }
        }
        if (bindings.hasLongPressBinding()) return true;
        if (bindings.hasGestureBinding()) return true;
        return false;
    }

    public boolean isEngaged() {
        return hasAnyAction() && (tapActive || visualLayers != 0);
    }

    public boolean containsPoint(float x, float y) {
        Rect box = getBoundingBox();
        return x >= box.left - 0.5f && x <= box.right + 0.5f &&
               y >= box.top - 0.5f && y <= box.bottom + 0.5f;
    }

    // === Sync visual state from C ===

    public void syncVisualState(int visFlags, float posX, float posY,
                         boolean petalUp, boolean petalRight,
                         boolean petalDown, boolean petalLeft,
                         float rangeScrollOffset,
                         int visualLayers, int visualAlphas) {
        this.tapActive = (visFlags & VF_TAP) != 0;
        this.longTapActive = (visFlags & VF_LONG_TAP) != 0;
        this.gestureActive = (visFlags & VF_GESTURE) != 0;
        this.visualLayers = visualLayers;
        this.visualAlphas = visualAlphas;
        if (type == Type.D_PAD) {
            bindings.states[0] = petalUp;
            bindings.states[1] = petalRight;
            bindings.states[2] = petalDown;
            bindings.states[3] = petalLeft;
        }
        currentPosition.set(posX, posY);
        if (type == Type.RANGE_BUTTON && scroller != null) {
            scroller.updateVisualState(tapActive, posX, posY, rangeScrollOffset);
        }
    }

    // === Delegation: Bindings ===

    public int getBindingCount() { return bindings.getBindingCount(); }
    public void setBindingCount(int bindingCount) { bindings.setBindingCount(bindingCount); }
    public Bind getBindingAt(int index) { return bindings.getBindingAt(index); }
    public void setBindingAt(int index, Bind binding) { bindings.setBindingAt(index, binding); }
    public void setBinding(Bind binding) { bindings.setBinding(binding); }
    public List<Bind> getBindingSequence(int index) { return bindings.getBindingSequence(index); }
    public void setBindingSequence(int index, List<Bind> sequence) { bindings.setBindingSequence(index, sequence); }
    public void addBindingToSequence(int index, Bind binding) { bindings.addBindingToSequence(index, binding); }
    public void removeBindingFromSequence(int index, int seqIndex) { bindings.removeBindingFromSequence(index, seqIndex); }
    public boolean isBindingSticky(int slot, int index) { return bindings.isBindingSticky(slot, index); }
    public void setBindingSticky(int slot, int index, boolean sticky) { bindings.setBindingSticky(slot, index, sticky); }
    public List<Boolean> getBindingSticky(int slot) { return bindings.getBindingSticky(slot); }
    public void setBindingAtSequenceIndex(int slotIndex, int seqIndex, Bind binding) { bindings.setBindingAtSequenceIndex(slotIndex, seqIndex, binding); }
    public List<Bind> getLongPressBindings() { return bindings.getLongPressBindings(); }
    public void setLongPressBindings(List<Bind> b) { bindings.setLongPressBindings(b); }
    public void addLongPressBinding(Bind binding) { bindings.addLongPressBinding(binding); }
    public void removeLongPressBinding(int index) { bindings.removeLongPressBinding(index); }
    public boolean hasLongPressBinding() { return bindings.hasLongPressBinding(); }
    public List<Bind> getGestureBindings() { return bindings.getGestureBindings(); }
    public void setGestureBindings(List<Bind> b) { bindings.setGestureBindings(b); }
    public void addGestureBinding(Bind binding) { bindings.addGestureBinding(binding); }
    public void removeGestureBinding(int index) { bindings.removeGestureBinding(index); }
    public boolean hasGestureBinding() { return bindings.hasGestureBinding(); }
    public BindPackage getSlotPackage(int index) { return bindings.getSlotPackage(index); }
    public void setSlotPackage(int index, BindPackage pkg) { bindings.setSlotPackage(index, pkg); }
    public BindPackage getLongPressPackage() { return bindings.getLongPressPackage(); }
    public void setLongPressPackage(BindPackage pkg) { bindings.setLongPressPackage(pkg); }
    public BindPackage getGesturePackage() { return bindings.getGesturePackage(); }
    public void setGesturePackage(BindPackage pkg) { bindings.setGesturePackage(pkg); }
    public Bind getBind(int index) { return bindings.getBind(index); }
    public void setBind(int index, Bind binding) { bindings.setBind(index, binding); }
    public boolean hasAnySlotToggle() { return bindings.hasAnySlotToggle(); }
    public boolean isLongPressToggle() { return bindings.isLongPressToggle(); }
    public boolean isGestureToggle() { return bindings.isGestureToggle(); }
    public BindPackage getBindingPackage(BindingSection section) { return bindings.getBindingPackage(section); }
    public void setBindingPackage(BindingSection section, BindPackage pkg) { bindings.setBindingPackage(section, pkg); }
    public List<Bind> getBindingsList(BindingSection section) { return bindings.getBindingsList(section); }
    public boolean isBindingToggle(BindingSection section) { return bindings.isBindingToggle(section); }
    public boolean getBindingAutoRepeat(BindingSection section) { return bindings.getBindingAutoRepeat(section); }
    public boolean hasBinding(BindingSection section) { return bindings.hasBinding(section); }
    public int computeToggleBitmask(BindingSection section) { return bindings.computeToggleBitmask(section); }
    public boolean getSlotAutoRepeat(int slot) { return bindings.getSlotAutoRepeat(slot); }
    public int getSlotAutoRepeatIntervalMs(int slot) { return bindings.getSlotAutoRepeatIntervalMs(slot); }

    // === Delegation: Renderer ===

    public void invalidateElementCache() { renderer.invalidateAll(); }
    public void invalidateElementCachesKeepDisk() { renderer.invalidateKeepDisk(); }
    public void buildCache() { renderer.buildCache(); }
    public void draw(Canvas canvas) { renderer.draw(canvas); }
    public void drawCached(Canvas canvas) { renderer.drawCached(canvas); }
    public static void clearSharedPool() { ElementRenderer.clearSharedPool(); }
    public static void deleteProfileCache(Context context, int profileId) { ElementRenderer.deleteProfileCache(context, profileId); }
    public static void deleteAllCaches(Context context) { ElementRenderer.deleteAllCaches(context); }

    // === Delegation: Serializer ===

    public JSONObject toJSONObject() { return ElementSerializer.toJSONObject(this); }
    public JSONObject toOriginalJSONObject() { return ElementSerializer.toOriginalJSONObject(this); }
}
