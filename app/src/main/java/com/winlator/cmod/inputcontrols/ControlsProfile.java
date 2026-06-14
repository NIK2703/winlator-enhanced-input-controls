package com.winlator.cmod.inputcontrols;

import android.content.Context;

import androidx.annotation.NonNull;

import com.winlator.cmod.core.FileUtils;
import com.winlator.cmod.widget.InputControlsView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

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
import java.util.Locale;
import java.util.Map;

public class ControlsProfile implements Comparable<ControlsProfile> {
    public final int id;
    private String name;
    private float cursorSpeed = 1.0f;
    private final ArrayList<ControlElement> elements = new ArrayList<>();
    private final ArrayList<ExternalController> controllers = new ArrayList<>();
    private final List<ControlElement> immutableElements = Collections.unmodifiableList(elements);
    private boolean elementsLoaded = false;
    private boolean controllersLoaded = false;
    private boolean virtualGamepad = false;
    private final Context context;
    private GamepadState gamepadState;

    // Touchscreen gesture settings (kept for backward compat)
    private MouseMode mouseMode = MouseMode.TOUCHPAD;
    private InputMode inputMode = InputMode.ABSOLUTE;
    private DragMode dragMode = DragMode.AUTO;
    private int doubleTapTimeout = 150;
    private int longPressTimeout = 200;
    private int bindingDelay;
    private int longPressDelay = 200;
    private int buttonLongPressHaptic = 1;
    private int buttonGestureHaptic = 5;
    private int gestureLongPressHaptic = 3;
    private int scrollBindHaptic = 3;
    private int scrollHoldHaptic = 1;
    private int scrollHoldThreshold = 100;
    private int dragThreshold = 10;
    private int gestureThreshold = 20;
    private int doubleTapDistance = 50;
    private int singleTapDelay;
    private float strokeWidth = 0.15f;
    private int fillAlphaInactive = 0;
    private float cornerRadius = 0.8f;
    private TouchActivationMode touchActivationMode = TouchActivationMode.LOCK;


    // Gesture type indices (mirrors C GestureType enum)
    public static final int GESTURE_SINGLE_TAP = GestureType.SINGLE_TAP.id;
    public static final int GESTURE_LONG_PRESS = GestureType.LONG_PRESS.id;
    public static final int GESTURE_DOUBLE_TAP = GestureType.DOUBLE_TAP.id;
    public static final int GESTURE_SINGLE_TAP_DRAG = GestureType.SINGLE_TAP_DRAG.id;
    public static final int GESTURE_LONG_PRESS_DRAG = GestureType.LONG_PRESS_DRAG.id;
    public static final int GESTURE_DOUBLE_TAP_DRAG = GestureType.DOUBLE_TAP_DRAG.id;
    public static final int GESTURE_SINGLE_2ND = GestureType.SINGLE_2ND.id;
    public static final int GESTURE_DOUBLE_2ND = GestureType.DOUBLE_2ND.id;
    public static final int GESTURE_SINGLE_DRAG_2ND = GestureType.SINGLE_DRAG_2ND.id;
    public static final int GESTURE_DOUBLE_DRAG_2ND = GestureType.DOUBLE_DRAG_2ND.id;
    public static final int GESTURE_COUNT = GestureType.COUNT;

    // === Unified gesture bindings (primary storage) ===
    private BindPackage gestureSingleTapAction = BindPackage.fromSingle(Bind.MOUSE_LEFT_BUTTON);
    private BindPackage gestureLongPressAction = new BindPackage();
    private BindPackage gestureDoubleTapAction = new BindPackage();
    private BindPackage gestureSingleTap2ndFingerAction = BindPackage.fromSingle(Bind.MOUSE_RIGHT_BUTTON);
    private BindPackage gestureDoubleTap2ndFingerAction = new BindPackage();
    private BindPackage gestureSingleTapDragAction = new BindPackage();
    private BindPackage gestureLongPressDragAction = new BindPackage();
    private BindPackage gestureDoubleTapDragAction = new BindPackage();
    private BindPackage gestureSingleTap2ndFingerDragAction = BindPackage.fromSingle(Bind.MOUSE_LEFT_BUTTON);
    private BindPackage gestureDoubleTap2ndFingerDragAction = new BindPackage();

    // Per-gesture scroll mode bindings (up/down/left/right for each drag gesture)
    // Index: 0=Sd, 1=Ld, 2=Dd, 3=Sd2, 4=Dd2
    private final BindPackage[] gestureScrollUp = new BindPackage[5];
    private final BindPackage[] gestureScrollDown = new BindPackage[5];
    private final BindPackage[] gestureScrollLeft = new BindPackage[5];
    private final BindPackage[] gestureScrollRight = new BindPackage[5];
    private int scrollThreshold = 30;
    private final boolean[] scrollHoldV = new boolean[5];
    private final boolean[] scrollHoldH = new boolean[5];

    private boolean gestureSettingsLoaded = false;

    public ControlsProfile(Context context, int id) {
        this.context = context;
        this.id = id;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public float getCursorSpeed() {
        return cursorSpeed;
    }

    public void setCursorSpeed(float cursorSpeed) {
        this.cursorSpeed = cursorSpeed;
    }

    public boolean isVirtualGamepad() {
        return virtualGamepad;
    }

    public GamepadState getGamepadState() {
        if (gamepadState == null) gamepadState = new GamepadState();
        return gamepadState;
    }

    public ExternalController addController(String id) {
        ExternalController controller = getController(id);
        if (controller == null) {
            controller = ExternalController.getController(id);
            if (controller != null) {
                controllers.add(controller);
            }
        }
        controllersLoaded = true;
        return controller;
    }

    public void removeController(ExternalController controller) {
        if (!controllersLoaded) loadControllers();
        controllers.remove(controller);
    }

    public ExternalController getController(String id) {
        if (!controllersLoaded) loadControllers();
        for (ExternalController controller : controllers) if (controller.getId().equals(id)) return controller;
        return null;
    }

    public ExternalController getController(int deviceId) {
        if (!controllersLoaded) loadControllers();
        
        for (ExternalController controller : controllers) {
            if (controller.getDeviceId() == deviceId) return controller;
        }
        
        android.view.InputDevice device = android.view.InputDevice.getDevice(deviceId);
        if (device != null) {
            String descriptor = device.getDescriptor();
            for (ExternalController controller : controllers) {
                if (controller.getId().equals(descriptor)) {
                    return controller;
                }
            }
        }
        return null;
    }

    @NonNull
    @Override
    public String toString() {
        return name;
    }

    @Override
    public int compareTo(ControlsProfile o) {
        if (this.name.equals("Default")) return -1;
        if (o.name.equals("Default")) return 1;
        int cmp = Integer.compare(id, o.id);
        if (cmp != 0) return cmp;
        return name.compareTo(o.name);
    }

    // ---- Touchscreen gesture settings ----

    public MouseMode getMouseMode() {
        ensureGestureSettingsLoaded();
        return mouseMode;
    }

    public void setMouseMode(MouseMode mouseMode) {
        this.mouseMode = mouseMode;
    }

    public InputMode getInputMode() {
        ensureGestureSettingsLoaded();
        return inputMode;
    }

    public void setInputMode(InputMode inputMode) {
        this.inputMode = inputMode;
    }

    public DragMode getDragMode() {
        ensureGestureSettingsLoaded();
        return dragMode;
    }

    public void setDragMode(DragMode dragMode) {
        this.dragMode = dragMode;
    }

    public List<Bind> getSingleTapAction() { return getGestureSingleTapAction().getBindings(); }
    public void setSingleTapAction(Bind binding) { setGestureSingleTapAction(BindPackage.fromSingle(binding)); }
    public void setSingleTapAction(List<Bind> bindings) { setGestureSingleTapAction(new BindPackage(bindings)); }

    public List<Bind> getLongPressAction() { return getGestureLongPressAction().getBindings(); }
    public void setLongPressAction(Bind binding) { setGestureLongPressAction(BindPackage.fromSingle(binding)); }
    public void setLongPressAction(List<Bind> bindings) { setGestureLongPressAction(new BindPackage(bindings)); }

    public List<Bind> getDoubleTapAction() { return getGestureDoubleTapAction().getBindings(); }
    public void setDoubleTapAction(Bind binding) { setGestureDoubleTapAction(BindPackage.fromSingle(binding)); }
    public void setDoubleTapAction(List<Bind> bindings) { setGestureDoubleTapAction(new BindPackage(bindings)); }

    public List<Bind> getSingleTap2ndFingerAction() { return getGestureSingleTap2ndFingerAction().getBindings(); }
    public void setSingleTap2ndFingerAction(Bind binding) { setGestureSingleTap2ndFingerAction(BindPackage.fromSingle(binding)); }
    public void setSingleTap2ndFingerAction(List<Bind> bindings) { setGestureSingleTap2ndFingerAction(new BindPackage(bindings)); }

    public List<Bind> getDoubleTap2ndFingerAction() { return getGestureDoubleTap2ndFingerAction().getBindings(); }
    public void setDoubleTap2ndFingerAction(Bind binding) { setGestureDoubleTap2ndFingerAction(BindPackage.fromSingle(binding)); }
    public void setDoubleTap2ndFingerAction(List<Bind> bindings) { setGestureDoubleTap2ndFingerAction(new BindPackage(bindings)); }

    public int getDoubleTapTimeout() {
        ensureGestureSettingsLoaded();
        return doubleTapTimeout;
    }

    public void setDoubleTapTimeout(int timeout) {
        this.doubleTapTimeout = clamp(timeout, 50, 500);
    }

    public int getLongPressTimeout() {
        ensureGestureSettingsLoaded();
        return longPressTimeout;
    }

    public void setLongPressTimeout(int timeout) {
        this.longPressTimeout = clamp(timeout, 50, 1000);
    }

    public int getBindingDelay() {
        ensureGestureSettingsLoaded();
        return bindingDelay;
    }

    public void setBindingDelay(int bindingDelay) {
        this.bindingDelay = Math.max(0, Math.min(10, bindingDelay));
    }

    public int getLongPressDelay() {
        ensureGestureSettingsLoaded();
        return longPressDelay;
    }

    public void setLongPressDelay(int longPressDelay) {
        ensureGestureSettingsLoaded();
        this.longPressDelay = clamp(longPressDelay, 50, 1000);
    }

    public int getButtonLongPressHaptic() {
        ensureGestureSettingsLoaded();
        return buttonLongPressHaptic;
    }

    public void setButtonLongPressHaptic(int value) {
        this.buttonLongPressHaptic = clamp(value, 0, 5);
    }

    public int getButtonGestureHaptic() {
        ensureGestureSettingsLoaded();
        return buttonGestureHaptic;
    }

    public void setButtonGestureHaptic(int value) {
        this.buttonGestureHaptic = clamp(value, 0, 5);
    }

    public int getGestureLongPressHaptic() {
        ensureGestureSettingsLoaded();
        return gestureLongPressHaptic;
    }

    public void setGestureLongPressHaptic(int value) {
        this.gestureLongPressHaptic = clamp(value, 0, 5);
    }

    public int getScrollBindHaptic() {
        ensureGestureSettingsLoaded();
        return scrollBindHaptic;
    }

    public void setScrollBindHaptic(int value) {
        this.scrollBindHaptic = clamp(value, 0, 5);
    }

    public int getScrollHoldHaptic() {
        ensureGestureSettingsLoaded();
        return scrollHoldHaptic;
    }

    public void setScrollHoldHaptic(int value) {
        this.scrollHoldHaptic = clamp(value, 0, 5);
    }

    public int getScrollHoldThreshold() {
        ensureGestureSettingsLoaded();
        return scrollHoldThreshold;
    }

    public void setScrollHoldThreshold(int threshold) {
        this.scrollHoldThreshold = clamp(threshold, 5, 100);
    }

    public int getDragThreshold() {
        ensureGestureSettingsLoaded();
        return dragThreshold;
    }

    public void setDragThreshold(int dragThreshold) {
        ensureGestureSettingsLoaded();
        this.dragThreshold = clamp(dragThreshold, 0, 30);
    }

    public int getDoubleTapDistance() {
        ensureGestureSettingsLoaded();
        return doubleTapDistance;
    }

    public void setDoubleTapDistance(int distance) {
        ensureGestureSettingsLoaded();
        this.doubleTapDistance = clamp(distance, 10, 100);
    }

    public int getSingleTapDelay() {
        ensureGestureSettingsLoaded();
        return singleTapDelay;
    }

    public void setSingleTapDelay(int delay) {
        this.singleTapDelay = clamp(delay, 0, 10);
    }

    public int getGestureThreshold() {
        ensureGestureSettingsLoaded();
        return gestureThreshold;
    }

    public void setGestureThreshold(int gestureThreshold) {
        ensureGestureSettingsLoaded();
        this.gestureThreshold = clamp(gestureThreshold, 10, 50);
    }

    public float getStrokeWidth() {
        ensureGestureSettingsLoaded();
        return strokeWidth;
    }

    public void setStrokeWidth(float strokeWidth) {
        this.strokeWidth = strokeWidth;
    }

    public int getFillAlphaInactive() {
        ensureGestureSettingsLoaded();
        return fillAlphaInactive;
    }

    public void setFillAlphaInactive(int fillAlphaInactive) {
        this.fillAlphaInactive = fillAlphaInactive;
    }

    public float getCornerRadius() {
        return cornerRadius;
    }

    public void setCornerRadius(float cornerRadius) {
        this.cornerRadius = cornerRadius;
    }

    public TouchActivationMode getTouchActivationMode() {
        ensureGestureSettingsLoaded();
        return touchActivationMode;
    }

    public void setTouchActivationMode(TouchActivationMode mode) {
        this.touchActivationMode = mode;
    }

    public List<Bind> getSingleTapDragAction() { return getGestureSingleTapDragAction().getBindings(); }
    public void setSingleTapDragAction(Bind binding) { setGestureSingleTapDragAction(BindPackage.fromSingle(binding)); }
    public void setSingleTapDragAction(List<Bind> bindings) { setGestureSingleTapDragAction(new BindPackage(bindings)); }

    public List<Bind> getLongPressDragAction() { return getGestureLongPressDragAction().getBindings(); }
    public void setLongPressDragAction(Bind binding) { setGestureLongPressDragAction(BindPackage.fromSingle(binding)); }
    public void setLongPressDragAction(List<Bind> bindings) { setGestureLongPressDragAction(new BindPackage(bindings)); }

    public List<Bind> getDoubleTapDragAction() { return getGestureDoubleTapDragAction().getBindings(); }
    public void setDoubleTapDragAction(Bind binding) { setGestureDoubleTapDragAction(BindPackage.fromSingle(binding)); }
    public void setDoubleTapDragAction(List<Bind> bindings) { setGestureDoubleTapDragAction(new BindPackage(bindings)); }

    public List<Bind> getSingleTap2ndFingerDragAction() { return getGestureSingleTap2ndFingerDragAction().getBindings(); }
    public void setSingleTap2ndFingerDragAction(Bind binding) { setGestureSingleTap2ndFingerDragAction(BindPackage.fromSingle(binding)); }
    public void setSingleTap2ndFingerDragAction(List<Bind> bindings) { setGestureSingleTap2ndFingerDragAction(new BindPackage(bindings)); }

    public List<Bind> getDoubleTap2ndFingerDragAction() { return getGestureDoubleTap2ndFingerDragAction().getBindings(); }
    public void setDoubleTap2ndFingerDragAction(Bind binding) { setGestureDoubleTap2ndFingerDragAction(BindPackage.fromSingle(binding)); }
    public void setDoubleTap2ndFingerDragAction(List<Bind> bindings) { setGestureDoubleTap2ndFingerDragAction(new BindPackage(bindings)); }

    // === Unified gesture binding getters/setters (primary storage) ===

    public BindPackage getGestureSingleTapAction() { return getGestureAction(GESTURE_SINGLE_TAP); }
    public void setGestureSingleTapAction(BindPackage bp) { setGestureAction(GESTURE_SINGLE_TAP, bp); }
    public void setGestureSingleTapAction(List<Bind> bindings) { setGestureAction(GESTURE_SINGLE_TAP, new BindPackage(bindings)); }

    public BindPackage getGestureLongPressAction() { return getGestureAction(GESTURE_LONG_PRESS); }
    public void setGestureLongPressAction(BindPackage bp) { setGestureAction(GESTURE_LONG_PRESS, bp); }
    public void setGestureLongPressAction(List<Bind> bindings) { setGestureAction(GESTURE_LONG_PRESS, new BindPackage(bindings)); }

    public BindPackage getGestureDoubleTapAction() { return getGestureAction(GESTURE_DOUBLE_TAP); }
    public void setGestureDoubleTapAction(BindPackage bp) { setGestureAction(GESTURE_DOUBLE_TAP, bp); }
    public void setGestureDoubleTapAction(List<Bind> bindings) { setGestureAction(GESTURE_DOUBLE_TAP, new BindPackage(bindings)); }

    public BindPackage getGestureSingleTap2ndFingerAction() { return getGestureAction(GESTURE_SINGLE_2ND); }
    public void setGestureSingleTap2ndFingerAction(BindPackage bp) { setGestureAction(GESTURE_SINGLE_2ND, bp); }
    public void setGestureSingleTap2ndFingerAction(List<Bind> bindings) { setGestureAction(GESTURE_SINGLE_2ND, new BindPackage(bindings)); }

    public BindPackage getGestureDoubleTap2ndFingerAction() { return getGestureAction(GESTURE_DOUBLE_2ND); }
    public void setGestureDoubleTap2ndFingerAction(BindPackage bp) { setGestureAction(GESTURE_DOUBLE_2ND, bp); }
    public void setGestureDoubleTap2ndFingerAction(List<Bind> bindings) { setGestureAction(GESTURE_DOUBLE_2ND, new BindPackage(bindings)); }

    public BindPackage getGestureSingleTapDragAction() { return getGestureAction(GESTURE_SINGLE_TAP_DRAG); }
    public void setGestureSingleTapDragAction(BindPackage bp) { setGestureAction(GESTURE_SINGLE_TAP_DRAG, bp); }
    public void setGestureSingleTapDragAction(List<Bind> bindings) { setGestureAction(GESTURE_SINGLE_TAP_DRAG, new BindPackage(bindings)); }

    public BindPackage getGestureLongPressDragAction() { return getGestureAction(GESTURE_LONG_PRESS_DRAG); }
    public void setGestureLongPressDragAction(BindPackage bp) { setGestureAction(GESTURE_LONG_PRESS_DRAG, bp); }
    public void setGestureLongPressDragAction(List<Bind> bindings) { setGestureAction(GESTURE_LONG_PRESS_DRAG, new BindPackage(bindings)); }

    public BindPackage getGestureDoubleTapDragAction() { return getGestureAction(GESTURE_DOUBLE_TAP_DRAG); }
    public void setGestureDoubleTapDragAction(BindPackage bp) { setGestureAction(GESTURE_DOUBLE_TAP_DRAG, bp); }
    public void setGestureDoubleTapDragAction(List<Bind> bindings) { setGestureAction(GESTURE_DOUBLE_TAP_DRAG, new BindPackage(bindings)); }

    public BindPackage getGestureSingleTap2ndFingerDragAction() { return getGestureAction(GESTURE_SINGLE_DRAG_2ND); }
    public void setGestureSingleTap2ndFingerDragAction(BindPackage bp) { setGestureAction(GESTURE_SINGLE_DRAG_2ND, bp); }
    public void setGestureSingleTap2ndFingerDragAction(List<Bind> bindings) { setGestureAction(GESTURE_SINGLE_DRAG_2ND, new BindPackage(bindings)); }

    public BindPackage getGestureDoubleTap2ndFingerDragAction() { return getGestureAction(GESTURE_DOUBLE_DRAG_2ND); }
    public void setGestureDoubleTap2ndFingerDragAction(BindPackage bp) { setGestureAction(GESTURE_DOUBLE_DRAG_2ND, bp); }
    public void setGestureDoubleTap2ndFingerDragAction(List<Bind> bindings) { setGestureAction(GESTURE_DOUBLE_DRAG_2ND, new BindPackage(bindings)); }

    // Scroll mode binding accessors
    // slotIndex: 0=Sd, 1=Ld, 2=Dd, 3=Sd2, 4=Dd2
    // dirIndex: 0=UP, 1=DOWN, 2=LEFT, 3=RIGHT
    private static final int SCROLL_SLOT_SD = 0;
    private static final int SCROLL_SLOT_LD = 1;
    private static final int SCROLL_SLOT_DD = 2;
    private static final int SCROLL_SLOT_SD2 = 3;
    private static final int SCROLL_SLOT_DD2 = 4;
    private static final int SCROLL_DIR_UP = 0;
    private static final int SCROLL_DIR_DOWN = 1;
    private static final int SCROLL_DIR_LEFT = 2;
    private static final int SCROLL_DIR_RIGHT = 3;

    public BindPackage getScrollBinding(int slotIndex, int dirIndex) {
        ensureGestureSettingsLoaded();
        BindPackage[][] arrays = { gestureScrollUp, gestureScrollDown, gestureScrollLeft, gestureScrollRight };
        BindPackage bp = arrays[dirIndex][slotIndex];
        return bp != null ? bp : new BindPackage();
    }

    public void setScrollBinding(int slotIndex, int dirIndex, BindPackage bp) {
        BindPackage[][] arrays = { gestureScrollUp, gestureScrollDown, gestureScrollLeft, gestureScrollRight };
        arrays[dirIndex][slotIndex] = bp != null ? new BindPackage(bp) : new BindPackage();
    }

    public void setScrollBinding(int slotIndex, int dirIndex, Bind binding) {
        setScrollBinding(slotIndex, dirIndex, BindPackage.fromSingle(binding));
    }

    public int getScrollThreshold() {
        ensureGestureSettingsLoaded();
        return scrollThreshold;
    }

    public void setScrollThreshold(int threshold) {
        this.scrollThreshold = clamp(threshold, 5, 100);
    }

    public boolean getScrollHoldV(int slot) {
        ensureGestureSettingsLoaded();
        return scrollHoldV[slot];
    }

    public void setScrollHoldV(int slot, boolean value) {
        scrollHoldV[slot] = value;
    }

    public boolean getScrollHoldH(int slot) {
        ensureGestureSettingsLoaded();
        return scrollHoldH[slot];
    }

    public void setScrollHoldH(int slot, boolean value) {
        scrollHoldH[slot] = value;
    }

    public static int scrollGestureSlotForDragGesture(int gestureType) {
        if (gestureType == GESTURE_SINGLE_TAP_DRAG) return 0;
        if (gestureType == GESTURE_LONG_PRESS_DRAG) return 1;
        if (gestureType == GESTURE_DOUBLE_TAP_DRAG) return 2;
        if (gestureType == GESTURE_SINGLE_DRAG_2ND) return 3;
        if (gestureType == GESTURE_DOUBLE_DRAG_2ND) return 4;
        return -1;
    }

    public BindPackage getGestureAction(int gestureIndex) {
        ensureGestureSettingsLoaded();
        if (gestureIndex == GESTURE_SINGLE_TAP) return gestureSingleTapAction;
        if (gestureIndex == GESTURE_LONG_PRESS) return gestureLongPressAction;
        if (gestureIndex == GESTURE_DOUBLE_TAP) return gestureDoubleTapAction;
        if (gestureIndex == GESTURE_SINGLE_TAP_DRAG) return gestureSingleTapDragAction;
        if (gestureIndex == GESTURE_LONG_PRESS_DRAG) return gestureLongPressDragAction;
        if (gestureIndex == GESTURE_DOUBLE_TAP_DRAG) return gestureDoubleTapDragAction;
        if (gestureIndex == GESTURE_SINGLE_2ND) return gestureSingleTap2ndFingerAction;
        if (gestureIndex == GESTURE_DOUBLE_2ND) return gestureDoubleTap2ndFingerAction;
        if (gestureIndex == GESTURE_SINGLE_DRAG_2ND) return gestureSingleTap2ndFingerDragAction;
        if (gestureIndex == GESTURE_DOUBLE_DRAG_2ND) return gestureDoubleTap2ndFingerDragAction;
        return new BindPackage();
    }

    public void setGestureAction(int gestureIndex, BindPackage bp) {
        BindPackage pkg = new BindPackage(bp);
        if (gestureIndex == GESTURE_SINGLE_TAP) gestureSingleTapAction = pkg;
        else if (gestureIndex == GESTURE_LONG_PRESS) gestureLongPressAction = pkg;
        else if (gestureIndex == GESTURE_DOUBLE_TAP) gestureDoubleTapAction = pkg;
        else if (gestureIndex == GESTURE_SINGLE_TAP_DRAG) gestureSingleTapDragAction = pkg;
        else if (gestureIndex == GESTURE_LONG_PRESS_DRAG) gestureLongPressDragAction = pkg;
        else if (gestureIndex == GESTURE_DOUBLE_TAP_DRAG) gestureDoubleTapDragAction = pkg;
        else if (gestureIndex == GESTURE_SINGLE_2ND) gestureSingleTap2ndFingerAction = pkg;
        else if (gestureIndex == GESTURE_DOUBLE_2ND) gestureDoubleTap2ndFingerAction = pkg;
        else if (gestureIndex == GESTURE_SINGLE_DRAG_2ND) gestureSingleTap2ndFingerDragAction = pkg;
        else if (gestureIndex == GESTURE_DOUBLE_DRAG_2ND) gestureDoubleTap2ndFingerDragAction = pkg;
    }

    private void ensureGestureSettingsLoaded() {
        if (gestureSettingsLoaded) return;
        File file = getProfileFile(context, id);
        if (!file.isFile()) {
            gestureSettingsLoaded = true;
            return;
        }
        try {
            JSONObject data = new JSONObject(FileUtils.readString(file));
            if (data.has("cursorSpeed")) cursorSpeed = (float)data.getDouble("cursorSpeed");
            if (data.has("bindingDelay")) bindingDelay = data.getInt("bindingDelay");
            if (data.has("longPressDelay")) longPressDelay = data.getInt("longPressDelay");
            if (data.has("buttonLongPressHaptic")) buttonLongPressHaptic = clamp(data.getInt("buttonLongPressHaptic"), 0, 5);
            if (data.has("buttonGestureHaptic")) buttonGestureHaptic = clamp(data.getInt("buttonGestureHaptic"), 0, 5);
            if (data.has("gestureLongPressHaptic")) gestureLongPressHaptic = clamp(data.getInt("gestureLongPressHaptic"), 0, 5);
            if (data.has("scrollBindHaptic")) scrollBindHaptic = clamp(data.getInt("scrollBindHaptic"), 0, 5);
            if (data.has("scrollHoldHaptic")) scrollHoldHaptic = clamp(data.getInt("scrollHoldHaptic"), 0, 5);
            if (data.has("scrollHoldThreshold")) scrollHoldThreshold = clamp(data.getInt("scrollHoldThreshold"), 5, 100);
            if (data.has("dragThreshold")) dragThreshold = clamp(data.getInt("dragThreshold"), 0, 30);
            if (data.has("doubleTapDistance")) doubleTapDistance = clamp(data.getInt("doubleTapDistance"), 10, 100);
            if (data.has("gestureThreshold")) gestureThreshold = clamp(data.getInt("gestureThreshold"), 10, 50);
            if (data.has("strokeWidth")) strokeWidth = (float)data.getDouble("strokeWidth");
            if (data.has("fillAlphaInactive")) fillAlphaInactive = data.getInt("fillAlphaInactive");
            if (data.has("gestureSettings")) {
                loadUnifiedGestureSettingsFromJson(data.getJSONObject("gestureSettings"));
            }
            else if (data.has("touchscreenGestures")) {
                loadLegacyTouchscreenGesturesFromJson(data.getJSONObject("touchscreenGestures"));
            }
        }
        catch (JSONException e) {

        }
        gestureSettingsLoaded = true;
    }



    public void markGestureSettingsLoaded() {
        this.gestureSettingsLoaded = true;
    }

    public void forceReloadGestureSettings() {
        this.gestureSettingsLoaded = false;
        ensureGestureSettingsLoaded();
    }

    public void loadUnifiedGestureSettingsFromJson(JSONObject gestureData) {
        if (gestureData == null) return;
        try {
            if (gestureData.has("mouseMode"))
                mouseMode = parseEnum(MouseMode.class, gestureData.getString("mouseMode"), MouseMode.TOUCHPAD);
            if (gestureData.has("inputMode"))
                inputMode = parseEnum(InputMode.class, gestureData.getString("inputMode"), InputMode.ABSOLUTE);
            if (gestureData.has("dragMode"))
                dragMode = parseEnum(DragMode.class, gestureData.getString("dragMode"), DragMode.AUTO);

            String[] gestureKeys = {"singleTapAction", "longPressAction", "doubleTapAction",
                "singleTapDragAction", "longPressDragAction", "doubleTapDragAction",
                "singleTap2ndFingerAction", "doubleTap2ndFingerAction",
                "singleTap2ndFingerDragAction", "doubleTap2ndFingerDragAction"};
            Bind[] gestureDefaults = {Bind.MOUSE_LEFT_BUTTON, Bind.NONE, Bind.NONE,
                Bind.NONE, Bind.NONE, Bind.NONE,
                Bind.MOUSE_RIGHT_BUTTON, Bind.NONE,
                Bind.MOUSE_LEFT_BUTTON, Bind.NONE};

            for (int i = 0; i < gestureKeys.length; i++) {
                if (gestureData.has(gestureKeys[i])) {
                    setGestureAction(i, BindPackage.fromJSON(gestureData.getJSONObject(gestureKeys[i]), gestureDefaults[i]));
                }
            }



            if (gestureData.has("doubleTapTimeout"))
                doubleTapTimeout = clamp(gestureData.getInt("doubleTapTimeout"), 50, 500);
            if (gestureData.has("longPressTimeout"))
                longPressTimeout = clamp(gestureData.getInt("longPressTimeout"), 50, 1000);
            if (gestureData.has("dragThreshold"))
                dragThreshold = clamp(gestureData.getInt("dragThreshold"), 0, 30);
            if (gestureData.has("singleTapDelay"))
                singleTapDelay = clamp(gestureData.getInt("singleTapDelay"), 0, 10);
            if (gestureData.has("cursorSpeed"))
                cursorSpeed = (float)gestureData.getDouble("cursorSpeed");
            if (gestureData.has("touchActivationMode"))
                touchActivationMode = parseEnum(TouchActivationMode.class, gestureData.getString("touchActivationMode"), TouchActivationMode.LOCK);

            // Load scroll mode bindings
            if (gestureData.has("scrollBindings")) {
                JSONObject scrollData = gestureData.getJSONObject("scrollBindings");
                String[] slotKeys = {"sd", "ld", "dd", "sd2", "dd2"};
                String[] dirKeys = {"up", "down", "left", "right"};
                BindPackage[][] arrays = { gestureScrollUp, gestureScrollDown, gestureScrollLeft, gestureScrollRight };
                for (int s = 0; s < 5; s++) {
                    if (!scrollData.has(slotKeys[s])) continue;
                    JSONObject slotObj = scrollData.getJSONObject(slotKeys[s]);
                    for (int d = 0; d < 4; d++) {
                        if (slotObj.has(dirKeys[d])) {
                            arrays[d][s] = BindPackage.fromJSON(slotObj.getJSONObject(dirKeys[d]), Bind.NONE);
                        }
                    }
                }
            }
            if (gestureData.has("scrollThreshold"))
                scrollThreshold = clamp(gestureData.getInt("scrollThreshold"), 5, 100);

            if (gestureData.has("scrollHoldV")) {
                JSONArray arr = gestureData.getJSONArray("scrollHoldV");
                for (int i = 0; i < Math.min(arr.length(), 5); i++)
                    scrollHoldV[i] = arr.getBoolean(i);
            }
            if (gestureData.has("scrollHoldH")) {
                JSONArray arr = gestureData.getJSONArray("scrollHoldH");
                for (int i = 0; i < Math.min(arr.length(), 5); i++)
                    scrollHoldH[i] = arr.getBoolean(i);
            }
        }
        catch (JSONException e) {

        }
    }

    private void loadLegacyTouchscreenGesturesFromJson(JSONObject gestureData) {
        if (gestureData == null) return;
        try {
            if (gestureData.has("mouseMode"))
                mouseMode = parseEnum(MouseMode.class, gestureData.getString("mouseMode"), MouseMode.TOUCHPAD);
            if (gestureData.has("inputMode"))
                inputMode = parseEnum(InputMode.class, gestureData.getString("inputMode"), InputMode.ABSOLUTE);
            if (gestureData.has("dragMode"))
                dragMode = parseEnum(DragMode.class, gestureData.getString("dragMode"), DragMode.AUTO);

            String[] gestureKeys = {"singleTapAction", "longPressAction", "doubleTapAction",
                "singleTapDragAction", "longPressDragAction", "doubleTapDragAction",
                "singleTap2ndFingerAction", "doubleTap2ndFingerAction",
                "singleTap2ndFingerDragAction", "doubleTap2ndFingerDragAction"};
            Bind[] gestureDefaults = {Bind.MOUSE_LEFT_BUTTON, Bind.NONE, Bind.NONE,
                Bind.NONE, Bind.NONE, Bind.NONE,
                Bind.MOUSE_RIGHT_BUTTON, Bind.NONE,
                Bind.MOUSE_LEFT_BUTTON, Bind.NONE};

            for (int i = 0; i < gestureKeys.length; i++) {
                if (gestureData.has(gestureKeys[i])) {
                    JSONArray arr = gestureData.getJSONArray(gestureKeys[i]);
                    setGestureAction(i, BindPackage.fromJSONArray(arr, gestureDefaults[i]));
                }
            }

            if (gestureData.has("doubleTapTimeout"))
                doubleTapTimeout = clamp(gestureData.getInt("doubleTapTimeout"), 50, 500);
            if (gestureData.has("longPressTimeout"))
                longPressTimeout = clamp(gestureData.getInt("longPressTimeout"), 50, 1000);
            if (gestureData.has("touchActivationMode"))
                touchActivationMode = parseEnum(TouchActivationMode.class, gestureData.getString("touchActivationMode"), TouchActivationMode.LOCK);
        }
        catch (JSONException e) {

        }
    }

    public static MouseMode parseMouseMode(String value) {
        return parseEnum(MouseMode.class, value, MouseMode.TOUCHPAD);
    }

    public static InputMode parseInputMode(String value) {
        return parseEnum(InputMode.class, value, InputMode.ABSOLUTE);
    }

    public static DragMode parseDragMode(String value) {
        return parseEnum(DragMode.class, value, DragMode.AUTO);
    }

    static <T extends Enum<T>> T parseEnum(Class<T> enumType, String value, T defaultValue) {
        if (value == null) return defaultValue;
        for (T constant : enumType.getEnumConstants()) {
            if (constant.name().equalsIgnoreCase(value)) return constant;
        }
        return defaultValue;
    }

    public static Bind parseBinding(String value, Bind defaultValue) {
        if (value == null) return defaultValue;
        try {
            return Bind.valueOf(value);
        }
        catch (IllegalArgumentException e) {
            return defaultValue;
        }
    }

    public static int clamp(int value, int min, int max) {
        return Math.max(min, Math.min(max, value));
    }

    // ---- End of touchscreen gesture settings ----

    public boolean isElementsLoaded() {
        return elementsLoaded;
    }

    public void save() {
        ensureGestureSettingsLoaded();
        File file = getProfileFile(context, id);

        try {
            JSONObject data = new JSONObject();
            data.put("id", id);
            data.put("name", name);
            data.put("cursorSpeed", Float.valueOf(cursorSpeed));

            if (bindingDelay > 0) data.put("bindingDelay", bindingDelay);
            if (longPressDelay < 50) longPressDelay = 200;
            data.put("longPressDelay", longPressDelay);
            if (buttonLongPressHaptic != 1) data.put("buttonLongPressHaptic", buttonLongPressHaptic);
            if (buttonGestureHaptic != 5) data.put("buttonGestureHaptic", buttonGestureHaptic);
            if (gestureLongPressHaptic != 3) data.put("gestureLongPressHaptic", gestureLongPressHaptic);
            if (scrollBindHaptic != 3) data.put("scrollBindHaptic", scrollBindHaptic);
            if (scrollHoldHaptic != 1) data.put("scrollHoldHaptic", scrollHoldHaptic);
            if (scrollHoldThreshold != 100) data.put("scrollHoldThreshold", scrollHoldThreshold);
            if (dragThreshold != 10) data.put("dragThreshold", dragThreshold);
            if (doubleTapDistance != 50) data.put("doubleTapDistance", doubleTapDistance);
            if (gestureThreshold != 20) data.put("gestureThreshold", gestureThreshold);
            if (strokeWidth != 0.15f) data.put("strokeWidth", strokeWidth);
            data.put("cornerRadius", cornerRadius);
            data.put("fillAlphaInactive", fillAlphaInactive);

            JSONArray elementsJSONArray = new JSONArray();
            if (!elementsLoaded && file.isFile()) {
                JSONObject profileJSONObject = new JSONObject(FileUtils.readString(file));
                if (profileJSONObject.has("longPressDelay")) longPressDelay = profileJSONObject.getInt("longPressDelay");
                if (profileJSONObject.has("gestureThreshold")) gestureThreshold = profileJSONObject.getInt("gestureThreshold");
                if (profileJSONObject.has("doubleTapDistance")) doubleTapDistance = profileJSONObject.getInt("doubleTapDistance");
                elementsJSONArray = profileJSONObject.getJSONArray("elements");
            }
            else for (ControlElement element : elements) elementsJSONArray.put(element.toJSONObject());
            data.put("elements", elementsJSONArray);

            JSONArray controllersJSONArray = new JSONArray();
            if (!controllersLoaded && file.isFile()) {
                JSONObject profileJSONObject = new JSONObject(FileUtils.readString(file));
                if (profileJSONObject.has("controllers")) controllersJSONArray = profileJSONObject.getJSONArray("controllers");
            }
            else {
                for (ExternalController controller : controllers) {
                    JSONObject controllerJSONObject = controller.toJSONObject();
                    if (controllerJSONObject != null) controllersJSONArray.put(controllerJSONObject);
                }
            }
            if (controllersJSONArray.length() > 0) data.put("controllers", controllersJSONArray);

            // Unified gesture settings (new primary format)
            JSONObject unifiedGestureData = new JSONObject();
            unifiedGestureData.put("mouseMode", mouseMode.name());
            unifiedGestureData.put("inputMode", inputMode.name());
            unifiedGestureData.put("dragMode", dragMode.name());
            String[] gestureKeys = {"singleTapAction", "longPressAction", "doubleTapAction",
                "singleTapDragAction", "longPressDragAction", "doubleTapDragAction",
                "singleTap2ndFingerAction", "doubleTap2ndFingerAction",
                "singleTap2ndFingerDragAction", "doubleTap2ndFingerDragAction"};
            for (int i = 0; i < gestureKeys.length; i++) {
                unifiedGestureData.put(gestureKeys[i], getGestureAction(i).toJSON());
            }
            unifiedGestureData.put("doubleTapTimeout", doubleTapTimeout);
            unifiedGestureData.put("longPressTimeout", longPressTimeout);
            unifiedGestureData.put("dragThreshold", dragThreshold);
            unifiedGestureData.put("singleTapDelay", singleTapDelay);
            unifiedGestureData.put("cursorSpeed", Float.valueOf(cursorSpeed));
            unifiedGestureData.put("touchActivationMode", touchActivationMode.name());

            // Save scroll mode bindings
            JSONObject scrollData = new JSONObject();
            String[] slotKeys = {"sd", "ld", "dd", "sd2", "dd2"};
            String[] dirKeys = {"up", "down", "left", "right"};
            BindPackage[][] arrays = { gestureScrollUp, gestureScrollDown, gestureScrollLeft, gestureScrollRight };
            boolean hasAnyScroll = false;
            for (int s = 0; s < 5; s++) {
                JSONObject slotObj = new JSONObject();
                boolean hasSlot = false;
                for (int d = 0; d < 4; d++) {
                    if (arrays[d][s] != null && arrays[d][s].size() > 0) {
                        slotObj.put(dirKeys[d], arrays[d][s].toJSON());
                        hasSlot = true;
                    }
                }
                if (hasSlot) {
                    scrollData.put(slotKeys[s], slotObj);
                    hasAnyScroll = true;
                }
            }
            if (hasAnyScroll) unifiedGestureData.put("scrollBindings", scrollData);
            if (scrollThreshold != 30) unifiedGestureData.put("scrollThreshold", scrollThreshold);

            boolean hasHoldV = false, hasHoldH = false;
            for (int i = 0; i < 5; i++) {
                if (scrollHoldV[i]) hasHoldV = true;
                if (scrollHoldH[i]) hasHoldH = true;
            }
            if (hasHoldV) {
                JSONArray arr = new JSONArray();
                for (int i = 0; i < 5; i++) arr.put(scrollHoldV[i]);
                unifiedGestureData.put("scrollHoldV", arr);
            }
            if (hasHoldH) {
                JSONArray arr = new JSONArray();
                for (int i = 0; i < 5; i++) arr.put(scrollHoldH[i]);
                unifiedGestureData.put("scrollHoldH", arr);
            }

            data.put("gestureSettings", unifiedGestureData);

            String jsonStr = data.toString();
            FileUtils.writeString(file, jsonStr);
        }
        catch (JSONException e) {}
    }

    public void exportOriginalFormat(File dest) {
        try {
            JSONObject data = new JSONObject();
            data.put("id", id);
            data.put("name", name);
            data.put("cursorSpeed", Float.valueOf(cursorSpeed));

            JSONArray elementsJSONArray = new JSONArray();
            if (!elementsLoaded) {
                File file = getProfileFile(context, id);
                if (file.isFile()) {
                    try {
                        JSONObject profileJSONObject = new JSONObject(FileUtils.readString(file));
                        elementsJSONArray = profileJSONObject.getJSONArray("elements");
                    }
                    catch (JSONException e) {
                        for (ControlElement element : elements) elementsJSONArray.put(element.toOriginalJSONObject());
                    }
                }
            }
            else {
                for (ControlElement element : elements) elementsJSONArray.put(element.toOriginalJSONObject());
            }
            data.put("elements", elementsJSONArray);

            JSONArray controllersJSONArray = new JSONArray();
            if (!controllersLoaded) {
                File file = getProfileFile(context, id);
                if (file.isFile()) {
                    try {
                        JSONObject profileJSONObject = new JSONObject(FileUtils.readString(file));
                        if (profileJSONObject.has("controllers")) controllersJSONArray = profileJSONObject.getJSONArray("controllers");
                    }
                    catch (JSONException e) {
                        for (ExternalController controller : controllers) {
                            JSONObject c = controller.toJSONObject();
                            if (c != null) controllersJSONArray.put(c);
                        }
                    }
                }
            }
            else {
                for (ExternalController controller : controllers) {
                    JSONObject c = controller.toJSONObject();
                    if (c != null) controllersJSONArray.put(c);
                }
            }
            if (controllersJSONArray.length() > 0) data.put("controllers", controllersJSONArray);

            FileUtils.writeString(dest, data.toString());
        }
        catch (JSONException e) {
        }
    }

    public static File getProfileFile(Context context, int id) {
        return new File(InputControlsManager.getProfilesDir(context), "controls-"+id+".icp");
    }

    public void addElement(ControlElement element) {
        elements.add(element);
        elementsLoaded = true;
    }

    public void removeElement(ControlElement element) {
        elements.remove(element);
        elementsLoaded = true;
    }

    public List<ControlElement> getElements() {
        return immutableElements;
    }

    public boolean isTemplate() {
        return name.toLowerCase(Locale.ENGLISH).contains("template");
    }

    public ArrayList<ExternalController> loadControllers() {
        controllers.clear();
        controllersLoaded = false;

        File file = getProfileFile(context, id);
        if (!file.isFile()) return controllers;

        try {
            JSONObject profileJSONObject = new JSONObject(FileUtils.readString(file));
            if (!profileJSONObject.has("controllers")) return controllers;
            JSONArray controllersJSONArray = profileJSONObject.getJSONArray("controllers");
            for (int i = 0; i < controllersJSONArray.length(); i++) {
                JSONObject controllerJSONObject = controllersJSONArray.getJSONObject(i);
                String id = controllerJSONObject.getString("id");
                ExternalController controller = new ExternalController();
                controller.setId(id);
                controller.setName(controllerJSONObject.getString("name"));

                JSONArray controllerBindingsJSONArray = controllerJSONObject.getJSONArray("controllerBindings");
                for (int j = 0; j < controllerBindingsJSONArray.length(); j++) {
                    JSONObject controllerBindingJSONObject = controllerBindingsJSONArray.getJSONObject(j);
                    ExternalControllerBinding controllerBinding = new ExternalControllerBinding();
                    controllerBinding.setKeyCode(controllerBindingJSONObject.getInt("keyCode"));
                    controllerBinding.setBinding(Bind.fromString(controllerBindingJSONObject.getString("binding")));
                    controller.addControllerBinding(controllerBinding);
                }
                controllers.add(controller);
            }
            controllersLoaded = true;
        }
        catch (JSONException e) {
            e.printStackTrace();
        }
        return controllers;
    }

    public void loadElements(InputControlsView inputControlsView) {
        elements.clear();
        elementsLoaded = false;
        virtualGamepad = false;

        File file = getProfileFile(context, id);
        if (!file.isFile()) {
            return;
        }

        try {
            JSONObject profileJSONObject = new JSONObject(FileUtils.readString(file));


            if (profileJSONObject.has("bindingDelay")) bindingDelay = profileJSONObject.getInt("bindingDelay");
            if (profileJSONObject.has("buttonLongPressHaptic")) buttonLongPressHaptic = clamp(profileJSONObject.getInt("buttonLongPressHaptic"), 0, 5);
            if (profileJSONObject.has("buttonGestureHaptic")) buttonGestureHaptic = clamp(profileJSONObject.getInt("buttonGestureHaptic"), 0, 5);
            if (profileJSONObject.has("gestureLongPressHaptic")) gestureLongPressHaptic = clamp(profileJSONObject.getInt("gestureLongPressHaptic"), 0, 5);
            if (profileJSONObject.has("scrollBindHaptic")) scrollBindHaptic = clamp(profileJSONObject.getInt("scrollBindHaptic"), 0, 5);
            if (profileJSONObject.has("scrollHoldHaptic")) scrollHoldHaptic = clamp(profileJSONObject.getInt("scrollHoldHaptic"), 0, 5);
            if (profileJSONObject.has("scrollHoldThreshold")) scrollHoldThreshold = clamp(profileJSONObject.getInt("scrollHoldThreshold"), 5, 100);
            if (profileJSONObject.has("dragThreshold")) dragThreshold = clamp(profileJSONObject.getInt("dragThreshold"), 0, 30);
            if (profileJSONObject.has("strokeWidth")) strokeWidth = (float)profileJSONObject.getDouble("strokeWidth");
            if (profileJSONObject.has("fillAlphaInactive")) fillAlphaInactive = profileJSONObject.getInt("fillAlphaInactive");
            if (profileJSONObject.has("cornerRadius")) setCornerRadius((float)profileJSONObject.getDouble("cornerRadius"));
            if (profileJSONObject.has("gestureSettings")) {
                loadUnifiedGestureSettingsFromJson(profileJSONObject.getJSONObject("gestureSettings"));
            }
            else if (profileJSONObject.has("touchscreenGestures")) {
                loadLegacyTouchscreenGesturesFromJson(profileJSONObject.getJSONObject("touchscreenGestures"));
            }
            gestureSettingsLoaded = true;

            JSONArray elementsJSONArray = profileJSONObject.getJSONArray("elements");
            for (int i = 0; i < elementsJSONArray.length(); i++) {
                JSONObject elementJSONObject = elementsJSONArray.getJSONObject(i);
                ControlElement element = new ControlElement(inputControlsView);
                element.setType(ControlElement.Type.valueOf(elementJSONObject.getString("type")));

                String shapeStr = elementJSONObject.getString("shape");
                if ("RECT".equals(shapeStr) || "SQUARE".equals(shapeStr) || "ROUND_RECT".equals(shapeStr)) {
                    element.setShape(ControlElement.Shape.RECT);
                    switch (shapeStr) {
                        case "SQUARE":
                            element.setElementWidth(5f);
                            element.setElementHeight(5f);
                            element.setCornerRadius(0.75f);
                            break;
                        case "ROUND_RECT":
                            element.setElementWidth(8f);
                            element.setElementHeight(4f);
                            element.setCornerRadius(2f);
                            break;
                        default:
                            element.setElementWidth(8f);
                            element.setElementHeight(4f);
                            break;
                    }
                } else {
                    element.setShape(ControlElement.Shape.valueOf(shapeStr));
                }

                if (elementJSONObject.has("elementWidth")) element.setElementWidth((float)elementJSONObject.getDouble("elementWidth"));
                if (elementJSONObject.has("elementHeight")) element.setElementHeight((float)elementJSONObject.getDouble("elementHeight"));
                if (elementJSONObject.has("cornerRadius")) element.setCornerRadius((float)elementJSONObject.getDouble("cornerRadius"));
                if (elementJSONObject.has("dpadCornerRadius")) element.setDpadCornerRadius((float)elementJSONObject.getDouble("dpadCornerRadius"));
                if (elementJSONObject.has("passthroughTouch")) element.setPassthroughTouch(elementJSONObject.getBoolean("passthroughTouch"));
                if (elementJSONObject.has("opacity")) element.setOpacity((float)elementJSONObject.getDouble("opacity"));
                element.setX((int)(elementJSONObject.getDouble("x") * inputControlsView.getMaxWidth()));
                element.setY((int)(elementJSONObject.getDouble("y") * inputControlsView.getMaxHeight()));
                element.setScale((float)elementJSONObject.getDouble("scale"));
                element.setText(elementJSONObject.getString("text"));
                element.setIconId(elementJSONObject.getInt("iconId"));
                if (elementJSONObject.has("customIconData")) element.setCustomIconData(elementJSONObject.getString("customIconData"));
                if (elementJSONObject.has("range")) element.setRange(ControlElement.Range.valueOf(elementJSONObject.getString("range")));
                if (elementJSONObject.has("orientation")) element.setOrientation((byte)elementJSONObject.getInt("orientation"));

                boolean hasGamepadBinding = true;
                // Try new simple format first (non-BUTTON)
                JSONArray bindsArray = elementJSONObject.optJSONArray("binds");
                if (bindsArray != null && element.getType() != ControlElement.Type.BUTTON) {
                    for (int j = 0; j < bindsArray.length() && j < 4; j++) {
                        String bName = bindsArray.optString(j, "NONE");
                        Bind b = parseBinding(bName, Bind.NONE);
                        element.setBind(j, b);
                        if (b != null && !b.isGamepad()) hasGamepadBinding = false;
                    }
                }
                else if (element.getType() != ControlElement.Type.BUTTON) {
                    // Migration: read first bind from each slotPackages and store as binds
                    boolean migrated = false;
                    JSONArray slotPackagesArray = elementJSONObject.optJSONArray("slotPackages");
                    if (slotPackagesArray != null) {
                        for (int j = 0; j < slotPackagesArray.length() && j < 4; j++) {
                            JSONObject pkgObj = slotPackagesArray.getJSONObject(j);
                            BindPackage pkg = BindPackage.fromJSON(pkgObj, Bind.NONE);
                            Bind b = pkg.size() > 0 ? pkg.get(0) : Bind.NONE;
                            element.setBind(j, b);
                            if (b != null && !b.isGamepad()) hasGamepadBinding = false;
                            migrated = true;
                        }
                    }
                    if (!migrated) {
                        JSONArray oldBindingsArray = elementJSONObject.optJSONArray("bindings");
                        if (oldBindingsArray != null && oldBindingsArray.length() > 0) {
                            try {
                                boolean isFlat = oldBindingsArray.get(0) instanceof String;
                                boolean oldToggleSwitch = elementJSONObject.optBoolean("toggleSwitch", false);
                                if (isFlat) {
                                    for (int j = 0; j < oldBindingsArray.length() && j < 4; j++) {
                                        String bName = oldBindingsArray.optString(j, "NONE");
                                        Bind b = parseBinding(bName, Bind.NONE);
                                        element.setBind(j, b);
                                        if (b != null && !b.isGamepad()) hasGamepadBinding = false;
                                    }
                                }
                                else {
                                    for (int j = 0; j < oldBindingsArray.length() && j < 4; j++) {
                                        JSONArray seqArray = oldBindingsArray.getJSONArray(j);
                                        Bind b = seqArray.length() > 0 ? parseBinding(seqArray.optString(0, "NONE"), Bind.NONE) : Bind.NONE;
                                        element.setBind(j, b);
                                        if (b != null && !b.isGamepad()) hasGamepadBinding = false;
                                    }
                                }
                            }
                            catch (JSONException e2) {
                            }
                        }
                    }
                    // Remove old slotPackages/bindings from element data to avoid re-migration
                    elementJSONObject.remove("slotPackages");
                    elementJSONObject.remove("bindings");
                }
                else {
                    // Existing slotPackages / legacy bindings loading (for BUTTON and backward compat)
                    JSONArray slotPackagesArray = elementJSONObject.optJSONArray("slotPackages");
                    if (slotPackagesArray != null) {
                        for (int j = 0; j < slotPackagesArray.length(); j++) {
                            JSONObject pkgObj = slotPackagesArray.getJSONObject(j);
                            BindPackage pkg = BindPackage.fromJSON(pkgObj, Bind.NONE);
                            element.setSlotPackage(j, pkg);
                            for (int k = 0; k < pkg.size(); k++) {
                                Bind b = pkg.get(k);
                                if (b != null && !b.isGamepad()) hasGamepadBinding = false;
                            }
                        }
                    }
                    else {
                        JSONArray oldBindingsArray = elementJSONObject.optJSONArray("bindings");
                        if (oldBindingsArray != null && oldBindingsArray.length() > 0) {
                            try {
                                boolean isFlat = oldBindingsArray.get(0) instanceof String;
                                boolean oldToggleSwitch = elementJSONObject.optBoolean("toggleSwitch", false);
                                if (isFlat) {
                                    for (int j = 0; j < oldBindingsArray.length(); j++) {
                                        String bName = oldBindingsArray.optString(j, "NONE");
                                        BindPackage pkg = BindPackage.fromSingle(parseBinding(bName, Bind.NONE));
                                        pkg.setToggleSwitch(oldToggleSwitch);
                                        element.setSlotPackage(j, pkg);
                                        Bind b = pkg.get(0);
                                        if (b != null && !b.isGamepad()) hasGamepadBinding = false;
                                    }
                                }
                                else {
                                    for (int j = 0; j < oldBindingsArray.length(); j++) {
                                        JSONArray seqArray = oldBindingsArray.getJSONArray(j);
                                        BindPackage pkg = BindPackage.fromJSONArray(seqArray, Bind.NONE);
                                        pkg.setToggleSwitch(oldToggleSwitch);
                                        element.setSlotPackage(j, pkg);
                                        for (int k = 0; k < pkg.size(); k++) {
                                            Bind b = pkg.get(k);
                                            if (b != null && !b.isGamepad()) hasGamepadBinding = false;
                                        }
                                    }
                                }
                            }
                            catch (JSONException e2) {
                            }
                        }
                    }
                }

                if (element.getType() == ControlElement.Type.BUTTON) {
                    if (elementJSONObject.has("longPressPackage")) {
                        JSONObject lpObj = elementJSONObject.getJSONObject("longPressPackage");
                        element.setBindingPackage(ControlElement.BindingSection.LONG_PRESS, BindPackage.fromJSON(lpObj, Bind.NONE));
                    }

                    if (elementJSONObject.has("gesturePackage")) {
                        JSONObject gObj = elementJSONObject.getJSONObject("gesturePackage");
                        element.setBindingPackage(ControlElement.BindingSection.GESTURE, BindPackage.fromJSON(gObj, Bind.NONE));
                    }
                }

                if (!virtualGamepad && hasGamepadBinding) virtualGamepad = true;
                elements.add(element);
            }
            elementsLoaded = true;
        }
        catch (JSONException e) {
            e.printStackTrace();
        }
    }
}
