package com.winlator.cmod.inputcontrols;

import android.content.Context;
import android.util.Log;

import androidx.annotation.NonNull;

import com.winlator.cmod.core.FileUtils;
import com.winlator.cmod.widget.InputControlsView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

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
    private List<Binding> singleTapAction = new ArrayList<>(Collections.singletonList(Binding.MOUSE_LEFT_BUTTON));
    private List<Binding> longPressAction = new ArrayList<>(Collections.singletonList(Binding.MOUSE_RIGHT_BUTTON));
    private List<Binding> doubleTapAction = new ArrayList<>();
    private List<Binding> singleTap2ndFingerAction = new ArrayList<>(Collections.singletonList(Binding.MOUSE_RIGHT_BUTTON));
    private List<Binding> doubleTap2ndFingerAction = new ArrayList<>();
    private List<Binding> singleTapDragAction = new ArrayList<>();
    private List<Binding> longPressDragAction = new ArrayList<>(Collections.singletonList(Binding.MOUSE_LEFT_BUTTON));
    private List<Binding> doubleTapDragAction = new ArrayList<>();
    private List<Binding> singleTap2ndFingerDragAction = new ArrayList<>();
    private List<Binding> doubleTap2ndFingerDragAction = new ArrayList<>();
    private int doubleTapTimeout = 300;
    private int longPressTimeout = 200;
    private int bindingDelay;
    private int longPressDelay = 200;
    private int buttonLongPressHaptic = 1;
    private int buttonGestureHaptic = 5;
    private int gestureLongPressHaptic = 3;
    private int dragThreshold = 10;
    private int gestureThreshold = 20;
    private int doubleTapDistance = 50;
    private int singleTapDelay;
    private float strokeWidth = 0.15f;
    private int fillAlphaInactive = 0;
    private TouchActivationMode touchActivationMode = TouchActivationMode.LOCK;

    // Touchpad gesture settings (separate from touchscreen, kept for backward compat)
    private List<Binding> touchpadSingleTapAction = new ArrayList<>(Collections.singletonList(Binding.MOUSE_LEFT_BUTTON));
    private List<Binding> touchpadLongPressAction = new ArrayList<>(Collections.singletonList(Binding.MOUSE_RIGHT_BUTTON));
    private List<Binding> touchpadDoubleTapAction = new ArrayList<>();
    private List<Binding> touchpadSingleTap2ndFingerAction = new ArrayList<>(Collections.singletonList(Binding.MOUSE_RIGHT_BUTTON));
    private List<Binding> touchpadDoubleTap2ndFingerAction = new ArrayList<>();
    private List<Binding> touchpadSingleTapDragAction = new ArrayList<>();
    private List<Binding> touchpadLongPressDragAction = new ArrayList<>(Collections.singletonList(Binding.MOUSE_LEFT_BUTTON));
    private List<Binding> touchpadDoubleTapDragAction = new ArrayList<>();
    private List<Binding> touchpadSingleTap2ndFingerDragAction = new ArrayList<>();
    private List<Binding> touchpadDoubleTap2ndFingerDragAction = new ArrayList<>();


    // === Unified gesture bindings (replace both ts and tp) ===
    // These are the primary storage. Old ts/tp fields delegate to these.
    private List<Binding> gestureSingleTapAction = new ArrayList<>();
    private List<Binding> gestureLongPressAction = new ArrayList<>();
    private List<Binding> gestureDoubleTapAction = new ArrayList<>();
    private List<Binding> gestureSingleTap2ndFingerAction = new ArrayList<>();
    private List<Binding> gestureDoubleTap2ndFingerAction = new ArrayList<>();
    private List<Binding> gestureSingleTapDragAction = new ArrayList<>();
    private List<Binding> gestureLongPressDragAction = new ArrayList<>();
    private List<Binding> gestureDoubleTapDragAction = new ArrayList<>();
    private List<Binding> gestureSingleTap2ndFingerDragAction = new ArrayList<>();
    private List<Binding> gestureDoubleTap2ndFingerDragAction = new ArrayList<>();

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
        if (controller == null) controllers.add(controller = ExternalController.getController(id));
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
        return Integer.compare(id, o.id);
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

    public List<Binding> getSingleTapAction() {
        ensureGestureSettingsLoaded();
        return singleTapAction;
    }

    public void setSingleTapAction(Binding binding) {
        this.singleTapAction.clear();
        this.singleTapAction.add(binding);
    }

    public void setSingleTapAction(List<Binding> bindings) {
        this.singleTapAction.clear();
        this.singleTapAction.addAll(bindings);
    }

    public List<Binding> getLongPressAction() {
        ensureGestureSettingsLoaded();
        return longPressAction;
    }

    public void setLongPressAction(Binding binding) {
        this.longPressAction.clear();
        this.longPressAction.add(binding);
    }

    public void setLongPressAction(List<Binding> bindings) {
        this.longPressAction.clear();
        this.longPressAction.addAll(bindings);
    }

    public List<Binding> getDoubleTapAction() {
        ensureGestureSettingsLoaded();
        return doubleTapAction;
    }

    public void setDoubleTapAction(Binding binding) {
        this.doubleTapAction.clear();
        this.doubleTapAction.add(binding);
    }

    public void setDoubleTapAction(List<Binding> bindings) {
        this.doubleTapAction.clear();
        this.doubleTapAction.addAll(bindings);
    }

    public List<Binding> getSingleTap2ndFingerAction() {
        ensureGestureSettingsLoaded();
        return singleTap2ndFingerAction;
    }

    public void setSingleTap2ndFingerAction(Binding binding) {
        this.singleTap2ndFingerAction.clear();
        this.singleTap2ndFingerAction.add(binding);
    }

    public void setSingleTap2ndFingerAction(List<Binding> bindings) {
        this.singleTap2ndFingerAction.clear();
        this.singleTap2ndFingerAction.addAll(bindings);
    }

    public List<Binding> getDoubleTap2ndFingerAction() {
        ensureGestureSettingsLoaded();
        return doubleTap2ndFingerAction;
    }

    public void setDoubleTap2ndFingerAction(Binding binding) {
        this.doubleTap2ndFingerAction.clear();
        this.doubleTap2ndFingerAction.add(binding);
    }

    public void setDoubleTap2ndFingerAction(List<Binding> bindings) {
        this.doubleTap2ndFingerAction.clear();
        this.doubleTap2ndFingerAction.addAll(bindings);
    }

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
        this.longPressDelay = longPressDelay;
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

    public TouchActivationMode getTouchActivationMode() {
        ensureGestureSettingsLoaded();
        return touchActivationMode;
    }

    public void setTouchActivationMode(TouchActivationMode mode) {
        this.touchActivationMode = mode;
    }

    public List<Binding> getSingleTapDragAction() {
        ensureGestureSettingsLoaded();
        return singleTapDragAction;
    }

    public void setSingleTapDragAction(Binding binding) {
        this.singleTapDragAction.clear();
        this.singleTapDragAction.add(binding);
    }

    public void setSingleTapDragAction(List<Binding> bindings) {
        this.singleTapDragAction.clear();
        this.singleTapDragAction.addAll(bindings);
    }

    public List<Binding> getLongPressDragAction() {
        ensureGestureSettingsLoaded();
        return longPressDragAction;
    }

    public void setLongPressDragAction(Binding binding) {
        this.longPressDragAction.clear();
        this.longPressDragAction.add(binding);
    }

    public void setLongPressDragAction(List<Binding> bindings) {
        this.longPressDragAction.clear();
        this.longPressDragAction.addAll(bindings);
    }

    public List<Binding> getDoubleTapDragAction() {
        ensureGestureSettingsLoaded();
        return doubleTapDragAction;
    }

    public void setDoubleTapDragAction(Binding binding) {
        this.doubleTapDragAction.clear();
        this.doubleTapDragAction.add(binding);
    }

    public void setDoubleTapDragAction(List<Binding> bindings) {
        this.doubleTapDragAction.clear();
        this.doubleTapDragAction.addAll(bindings);
    }

    public List<Binding> getSingleTap2ndFingerDragAction() {
        ensureGestureSettingsLoaded();
        return singleTap2ndFingerDragAction;
    }

    public void setSingleTap2ndFingerDragAction(Binding binding) {
        this.singleTap2ndFingerDragAction.clear();
        this.singleTap2ndFingerDragAction.add(binding);
    }

    public void setSingleTap2ndFingerDragAction(List<Binding> bindings) {
        this.singleTap2ndFingerDragAction.clear();
        this.singleTap2ndFingerDragAction.addAll(bindings);
    }

    public List<Binding> getDoubleTap2ndFingerDragAction() {
        ensureGestureSettingsLoaded();
        return doubleTap2ndFingerDragAction;
    }

    public void setDoubleTap2ndFingerDragAction(Binding binding) {
        this.doubleTap2ndFingerDragAction.clear();
        this.doubleTap2ndFingerDragAction.add(binding);
    }

    public void setDoubleTap2ndFingerDragAction(List<Binding> bindings) {
        this.doubleTap2ndFingerDragAction.clear();
        this.doubleTap2ndFingerDragAction.addAll(bindings);
    }

    // ---- Touchpad gesture getters/setters ----

    public List<Binding> getTouchpadSingleTapAction() { ensureGestureSettingsLoaded(); return touchpadSingleTapAction; }
    public void setTouchpadSingleTapAction(Binding binding) { this.touchpadSingleTapAction.clear(); this.touchpadSingleTapAction.add(binding); }
    public void setTouchpadSingleTapAction(List<Binding> bindings) { this.touchpadSingleTapAction.clear(); this.touchpadSingleTapAction.addAll(bindings); }

    public List<Binding> getTouchpadLongPressAction() { ensureGestureSettingsLoaded(); return touchpadLongPressAction; }
    public void setTouchpadLongPressAction(Binding binding) { this.touchpadLongPressAction.clear(); this.touchpadLongPressAction.add(binding); }
    public void setTouchpadLongPressAction(List<Binding> bindings) { this.touchpadLongPressAction.clear(); this.touchpadLongPressAction.addAll(bindings); }

    public List<Binding> getTouchpadDoubleTapAction() { ensureGestureSettingsLoaded(); return touchpadDoubleTapAction; }
    public void setTouchpadDoubleTapAction(Binding binding) { this.touchpadDoubleTapAction.clear(); this.touchpadDoubleTapAction.add(binding); }
    public void setTouchpadDoubleTapAction(List<Binding> bindings) { this.touchpadDoubleTapAction.clear(); this.touchpadDoubleTapAction.addAll(bindings); }

    public List<Binding> getTouchpadSingleTap2ndFingerAction() { ensureGestureSettingsLoaded(); return touchpadSingleTap2ndFingerAction; }
    public void setTouchpadSingleTap2ndFingerAction(Binding binding) { this.touchpadSingleTap2ndFingerAction.clear(); this.touchpadSingleTap2ndFingerAction.add(binding); }
    public void setTouchpadSingleTap2ndFingerAction(List<Binding> bindings) { this.touchpadSingleTap2ndFingerAction.clear(); this.touchpadSingleTap2ndFingerAction.addAll(bindings); }

    public List<Binding> getTouchpadDoubleTap2ndFingerAction() { ensureGestureSettingsLoaded(); return touchpadDoubleTap2ndFingerAction; }
    public void setTouchpadDoubleTap2ndFingerAction(Binding binding) { this.touchpadDoubleTap2ndFingerAction.clear(); this.touchpadDoubleTap2ndFingerAction.add(binding); }
    public void setTouchpadDoubleTap2ndFingerAction(List<Binding> bindings) { this.touchpadDoubleTap2ndFingerAction.clear(); this.touchpadDoubleTap2ndFingerAction.addAll(bindings); }

    public List<Binding> getTouchpadSingleTapDragAction() { ensureGestureSettingsLoaded(); return touchpadSingleTapDragAction; }
    public void setTouchpadSingleTapDragAction(Binding binding) { this.touchpadSingleTapDragAction.clear(); this.touchpadSingleTapDragAction.add(binding); }
    public void setTouchpadSingleTapDragAction(List<Binding> bindings) { this.touchpadSingleTapDragAction.clear(); this.touchpadSingleTapDragAction.addAll(bindings); }

    public List<Binding> getTouchpadLongPressDragAction() { ensureGestureSettingsLoaded(); return touchpadLongPressDragAction; }
    public void setTouchpadLongPressDragAction(Binding binding) { this.touchpadLongPressDragAction.clear(); this.touchpadLongPressDragAction.add(binding); }
    public void setTouchpadLongPressDragAction(List<Binding> bindings) { this.touchpadLongPressDragAction.clear(); this.touchpadLongPressDragAction.addAll(bindings); }

    public List<Binding> getTouchpadDoubleTapDragAction() { ensureGestureSettingsLoaded(); return touchpadDoubleTapDragAction; }
    public void setTouchpadDoubleTapDragAction(Binding binding) { this.touchpadDoubleTapDragAction.clear(); this.touchpadDoubleTapDragAction.add(binding); }
    public void setTouchpadDoubleTapDragAction(List<Binding> bindings) { this.touchpadDoubleTapDragAction.clear(); this.touchpadDoubleTapDragAction.addAll(bindings); }

    public List<Binding> getTouchpadSingleTap2ndFingerDragAction() { ensureGestureSettingsLoaded(); return touchpadSingleTap2ndFingerDragAction; }
    public void setTouchpadSingleTap2ndFingerDragAction(Binding binding) { this.touchpadSingleTap2ndFingerDragAction.clear(); this.touchpadSingleTap2ndFingerDragAction.add(binding); }
    public void setTouchpadSingleTap2ndFingerDragAction(List<Binding> bindings) { this.touchpadSingleTap2ndFingerDragAction.clear(); this.touchpadSingleTap2ndFingerDragAction.addAll(bindings); }

    public List<Binding> getTouchpadDoubleTap2ndFingerDragAction() { ensureGestureSettingsLoaded(); return touchpadDoubleTap2ndFingerDragAction; }
    public void setTouchpadDoubleTap2ndFingerDragAction(Binding binding) { this.touchpadDoubleTap2ndFingerDragAction.clear(); this.touchpadDoubleTap2ndFingerDragAction.add(binding); }
    public void setTouchpadDoubleTap2ndFingerDragAction(List<Binding> bindings) { this.touchpadDoubleTap2ndFingerDragAction.clear(); this.touchpadDoubleTap2ndFingerDragAction.addAll(bindings); }

    // === Unified gesture binding getters/setters (replaces separate ts/tp) ===

    private void initGestureFromBinding(List<Binding> dst, List<Binding> src) {
        dst.clear();
        dst.addAll(src);
    }

    public List<Binding> getGestureSingleTapAction() { ensureGestureSettingsLoaded(); return gestureSingleTapAction; }
    public void setGestureSingleTapAction(List<Binding> bindings) { initGestureFromBinding(this.gestureSingleTapAction, bindings); }

    public List<Binding> getGestureLongPressAction() { ensureGestureSettingsLoaded(); return gestureLongPressAction; }
    public void setGestureLongPressAction(List<Binding> bindings) { initGestureFromBinding(this.gestureLongPressAction, bindings); }

    public List<Binding> getGestureDoubleTapAction() { ensureGestureSettingsLoaded(); return gestureDoubleTapAction; }
    public void setGestureDoubleTapAction(List<Binding> bindings) { initGestureFromBinding(this.gestureDoubleTapAction, bindings); }

    public List<Binding> getGestureSingleTap2ndFingerAction() { ensureGestureSettingsLoaded(); return gestureSingleTap2ndFingerAction; }
    public void setGestureSingleTap2ndFingerAction(List<Binding> bindings) { initGestureFromBinding(this.gestureSingleTap2ndFingerAction, bindings); }

    public List<Binding> getGestureDoubleTap2ndFingerAction() { ensureGestureSettingsLoaded(); return gestureDoubleTap2ndFingerAction; }
    public void setGestureDoubleTap2ndFingerAction(List<Binding> bindings) { initGestureFromBinding(this.gestureDoubleTap2ndFingerAction, bindings); }

    public List<Binding> getGestureSingleTapDragAction() { ensureGestureSettingsLoaded(); return gestureSingleTapDragAction; }
    public void setGestureSingleTapDragAction(List<Binding> bindings) { initGestureFromBinding(this.gestureSingleTapDragAction, bindings); }

    public List<Binding> getGestureLongPressDragAction() { ensureGestureSettingsLoaded(); return gestureLongPressDragAction; }
    public void setGestureLongPressDragAction(List<Binding> bindings) { initGestureFromBinding(this.gestureLongPressDragAction, bindings); }

    public List<Binding> getGestureDoubleTapDragAction() { ensureGestureSettingsLoaded(); return gestureDoubleTapDragAction; }
    public void setGestureDoubleTapDragAction(List<Binding> bindings) { initGestureFromBinding(this.gestureDoubleTapDragAction, bindings); }

    public List<Binding> getGestureSingleTap2ndFingerDragAction() { ensureGestureSettingsLoaded(); return gestureSingleTap2ndFingerDragAction; }
    public void setGestureSingleTap2ndFingerDragAction(List<Binding> bindings) { initGestureFromBinding(this.gestureSingleTap2ndFingerDragAction, bindings); }

    public List<Binding> getGestureDoubleTap2ndFingerDragAction() { ensureGestureSettingsLoaded(); return gestureDoubleTap2ndFingerDragAction; }
    public void setGestureDoubleTap2ndFingerDragAction(List<Binding> bindings) { initGestureFromBinding(this.gestureDoubleTap2ndFingerDragAction, bindings); }

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
            else if (data.has("hapticFeedbackEnabled")) {
                boolean oldVal = data.getBoolean("hapticFeedbackEnabled");
                if (!oldVal) {
                    buttonLongPressHaptic = 0;
                    buttonGestureHaptic = 0;
                    gestureLongPressHaptic = 0;
                }
            }
            else if (data.has("longPressHapticEnabled")) {
                boolean oldVal = data.getBoolean("longPressHapticEnabled");
                if (!oldVal) {
                    buttonLongPressHaptic = 0;
                    buttonGestureHaptic = 0;
                    gestureLongPressHaptic = 0;
                }
            }
            if (data.has("dragThreshold")) dragThreshold = clamp(data.getInt("dragThreshold"), 0, 30);
            if (data.has("doubleTapDistance")) doubleTapDistance = clamp(data.getInt("doubleTapDistance"), 10, 100);
            if (data.has("gestureThreshold")) gestureThreshold = clamp(data.getInt("gestureThreshold"), 10, 50);
            if (data.has("strokeWidth")) strokeWidth = (float)data.getDouble("strokeWidth");
            if (data.has("fillAlphaInactive")) fillAlphaInactive = data.getInt("fillAlphaInactive");
            if (data.has("gestureSettings")) {
                loadUnifiedGestureSettingsFromJson(data.getJSONObject("gestureSettings"));
            }
            else {
                if (data.has("touchscreenGestures")) {
                    loadGestureSettingsFromJson(data.getJSONObject("touchscreenGestures"));
                }
                if (data.has("touchpadGestures")) {
                    loadTouchpadGestureSettingsFromJson(data.getJSONObject("touchpadGestures"));
                }
                else if (data.has("touchscreenGestures")) {
                    loadTouchpadGestureSettingsFromJson(data.getJSONObject("touchscreenGestures"));
                }
            }
        }
        catch (JSONException e) {
            Log.w("ControlsProfile", "Failed to load touchscreenGestures", e);
        }
        gestureSettingsLoaded = true;
    }

    public void loadGestureSettingsFromJson(JSONObject gestureData) {
        if (gestureData == null) return;
        try {
            if (gestureData.has("mouseMode"))
                mouseMode = parseEnum(MouseMode.class, gestureData.getString("mouseMode"), MouseMode.TOUCHPAD);
            if (gestureData.has("inputMode"))
                inputMode = parseEnum(InputMode.class, gestureData.getString("inputMode"), InputMode.ABSOLUTE);
            if (gestureData.has("dragMode"))
                dragMode = parseEnum(DragMode.class, gestureData.getString("dragMode"), DragMode.AUTO);

            // Read touchscreen bindings and populate BOTH old ts fields AND unified fields
            if (gestureData.has("singleTapAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "singleTapAction", Binding.MOUSE_LEFT_BUTTON);
                setSingleTapAction(v); setGestureSingleTapAction(v);
            }
            if (gestureData.has("longPressAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "longPressAction", Binding.MOUSE_LEFT_BUTTON);
                setLongPressAction(v); setGestureLongPressAction(v);
            }
            if (gestureData.has("doubleTapAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "doubleTapAction", Binding.NONE);
                setDoubleTapAction(v); setGestureDoubleTapAction(v);
            }
            if (gestureData.has("singleTap2ndFingerAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "singleTap2ndFingerAction", Binding.MOUSE_RIGHT_BUTTON);
                setSingleTap2ndFingerAction(v); setGestureSingleTap2ndFingerAction(v);
            }
            if (gestureData.has("doubleTap2ndFingerAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "doubleTap2ndFingerAction", Binding.NONE);
                setDoubleTap2ndFingerAction(v); setGestureDoubleTap2ndFingerAction(v);
            }
            if (gestureData.has("singleTapDragAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "singleTapDragAction", Binding.NONE);
                setSingleTapDragAction(v); setGestureSingleTapDragAction(v);
            }
            if (gestureData.has("longPressDragAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "longPressDragAction", Binding.NONE);
                setLongPressDragAction(v); setGestureLongPressDragAction(v);
            }
            if (gestureData.has("doubleTapDragAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "doubleTapDragAction", Binding.NONE);
                setDoubleTapDragAction(v); setGestureDoubleTapDragAction(v);
            }
            if (gestureData.has("singleTap2ndFingerDragAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "singleTap2ndFingerDragAction", Binding.NONE);
                setSingleTap2ndFingerDragAction(v); setGestureSingleTap2ndFingerDragAction(v);
            }
            if (gestureData.has("doubleTap2ndFingerDragAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "doubleTap2ndFingerDragAction", Binding.NONE);
                setDoubleTap2ndFingerDragAction(v); setGestureDoubleTap2ndFingerDragAction(v);
            }
            if (gestureData.has("doubleTapTimeout"))
                doubleTapTimeout = clamp(gestureData.getInt("doubleTapTimeout"), 50, 500);
            if (gestureData.has("longPressTimeout"))
                longPressTimeout = clamp(gestureData.getInt("longPressTimeout"), 50, 1000);
            if (gestureData.has("touchActivationMode"))
                touchActivationMode = parseEnum(TouchActivationMode.class, gestureData.getString("touchActivationMode"), TouchActivationMode.LOCK);
            if (gestureData.has("singleTapDelay"))
                singleTapDelay = clamp(gestureData.getInt("singleTapDelay"), 0, 10);
        }
        catch (JSONException e) {
            Log.w("ControlsProfile", "Failed to parse gesture field in touchscreenGestures", e);
        }
    }

    public void markGestureSettingsLoaded() {
        this.gestureSettingsLoaded = true;
    }

    private void loadTouchpadGestureSettingsFromJson(JSONObject gestureData) {
        if (gestureData == null) return;
        try {
            if (gestureData.has("singleTapAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "singleTapAction", Binding.MOUSE_LEFT_BUTTON);
                setTouchpadSingleTapAction(v); setGestureSingleTapAction(v);
            }
            if (gestureData.has("longPressAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "longPressAction", Binding.MOUSE_LEFT_BUTTON);
                setTouchpadLongPressAction(v); setGestureLongPressAction(v);
            }
            if (gestureData.has("doubleTapAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "doubleTapAction", Binding.NONE);
                setTouchpadDoubleTapAction(v); setGestureDoubleTapAction(v);
            }
            if (gestureData.has("singleTap2ndFingerAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "singleTap2ndFingerAction", Binding.MOUSE_RIGHT_BUTTON);
                setTouchpadSingleTap2ndFingerAction(v); setGestureSingleTap2ndFingerAction(v);
            }
            if (gestureData.has("doubleTap2ndFingerAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "doubleTap2ndFingerAction", Binding.NONE);
                setTouchpadDoubleTap2ndFingerAction(v); setGestureDoubleTap2ndFingerAction(v);
            }
            if (gestureData.has("singleTapDragAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "singleTapDragAction", Binding.NONE);
                setTouchpadSingleTapDragAction(v); setGestureSingleTapDragAction(v);
            }
            if (gestureData.has("longPressDragAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "longPressDragAction", Binding.NONE);
                setTouchpadLongPressDragAction(v); setGestureLongPressDragAction(v);
            }
            if (gestureData.has("doubleTapDragAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "doubleTapDragAction", Binding.NONE);
                setTouchpadDoubleTapDragAction(v); setGestureDoubleTapDragAction(v);
            }
            if (gestureData.has("singleTap2ndFingerDragAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "singleTap2ndFingerDragAction", Binding.NONE);
                setTouchpadSingleTap2ndFingerDragAction(v); setGestureSingleTap2ndFingerDragAction(v);
            }
            if (gestureData.has("doubleTap2ndFingerDragAction")) {
                List<Binding> v = parseGestureBindingList(gestureData, "doubleTap2ndFingerDragAction", Binding.NONE);
                setTouchpadDoubleTap2ndFingerDragAction(v); setGestureDoubleTap2ndFingerDragAction(v);
            }
            if (gestureData.has("doubleTapTimeout"))
                doubleTapTimeout = clamp(gestureData.getInt("doubleTapTimeout"), 50, 500);
            if (gestureData.has("longPressTimeout"))
                longPressTimeout = clamp(gestureData.getInt("longPressTimeout"), 50, 1000);
            if (gestureData.has("dragThreshold"))
                dragThreshold = clamp(gestureData.getInt("dragThreshold"), 0, 30);
            if (gestureData.has("cursorSpeed"))
                cursorSpeed = (float)gestureData.getDouble("cursorSpeed");
        }
        catch (JSONException e) {
            Log.w("ControlsProfile", "Failed to parse touchpad gesture field", e);
        }
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

            // Unified gesture bindings (primary)
            if (gestureData.has("singleTapAction")) setGestureSingleTapAction(parseGestureBindingList(gestureData, "singleTapAction", Binding.MOUSE_LEFT_BUTTON));
            if (gestureData.has("longPressAction")) setGestureLongPressAction(parseGestureBindingList(gestureData, "longPressAction", Binding.MOUSE_RIGHT_BUTTON));
            if (gestureData.has("doubleTapAction")) setGestureDoubleTapAction(parseGestureBindingList(gestureData, "doubleTapAction", Binding.NONE));
            if (gestureData.has("singleTap2ndFingerAction")) setGestureSingleTap2ndFingerAction(parseGestureBindingList(gestureData, "singleTap2ndFingerAction", Binding.MOUSE_RIGHT_BUTTON));
            if (gestureData.has("doubleTap2ndFingerAction")) setGestureDoubleTap2ndFingerAction(parseGestureBindingList(gestureData, "doubleTap2ndFingerAction", Binding.NONE));
            if (gestureData.has("singleTapDragAction")) setGestureSingleTapDragAction(parseGestureBindingList(gestureData, "singleTapDragAction", Binding.NONE));
            if (gestureData.has("longPressDragAction")) setGestureLongPressDragAction(parseGestureBindingList(gestureData, "longPressDragAction", Binding.NONE));
            if (gestureData.has("doubleTapDragAction")) setGestureDoubleTapDragAction(parseGestureBindingList(gestureData, "doubleTapDragAction", Binding.NONE));
            if (gestureData.has("singleTap2ndFingerDragAction")) setGestureSingleTap2ndFingerDragAction(parseGestureBindingList(gestureData, "singleTap2ndFingerDragAction", Binding.NONE));
            if (gestureData.has("doubleTap2ndFingerDragAction")) setGestureDoubleTap2ndFingerDragAction(parseGestureBindingList(gestureData, "doubleTap2ndFingerDragAction", Binding.NONE));

            // Also populate old ts fields for backward compat
            if (gestureData.has("singleTapAction")) setSingleTapAction(parseGestureBindingList(gestureData, "singleTapAction", Binding.MOUSE_LEFT_BUTTON));
            if (gestureData.has("longPressAction")) setLongPressAction(parseGestureBindingList(gestureData, "longPressAction", Binding.MOUSE_RIGHT_BUTTON));
            if (gestureData.has("doubleTapAction")) setDoubleTapAction(parseGestureBindingList(gestureData, "doubleTapAction", Binding.NONE));
            if (gestureData.has("singleTap2ndFingerAction")) setSingleTap2ndFingerAction(parseGestureBindingList(gestureData, "singleTap2ndFingerAction", Binding.MOUSE_RIGHT_BUTTON));
            if (gestureData.has("doubleTap2ndFingerAction")) setDoubleTap2ndFingerAction(parseGestureBindingList(gestureData, "doubleTap2ndFingerAction", Binding.NONE));
            if (gestureData.has("singleTapDragAction")) setSingleTapDragAction(parseGestureBindingList(gestureData, "singleTapDragAction", Binding.NONE));
            if (gestureData.has("longPressDragAction")) setLongPressDragAction(parseGestureBindingList(gestureData, "longPressDragAction", Binding.NONE));
            if (gestureData.has("doubleTapDragAction")) setDoubleTapDragAction(parseGestureBindingList(gestureData, "doubleTapDragAction", Binding.NONE));
            if (gestureData.has("singleTap2ndFingerDragAction")) setSingleTap2ndFingerDragAction(parseGestureBindingList(gestureData, "singleTap2ndFingerDragAction", Binding.NONE));
            if (gestureData.has("doubleTap2ndFingerDragAction")) setDoubleTap2ndFingerDragAction(parseGestureBindingList(gestureData, "doubleTap2ndFingerDragAction", Binding.NONE));

            // Also populate old tp fields for backward compat
            if (gestureData.has("singleTapAction")) setTouchpadSingleTapAction(parseGestureBindingList(gestureData, "singleTapAction", Binding.MOUSE_LEFT_BUTTON));
            if (gestureData.has("longPressAction")) setTouchpadLongPressAction(parseGestureBindingList(gestureData, "longPressAction", Binding.MOUSE_RIGHT_BUTTON));
            if (gestureData.has("doubleTapAction")) setTouchpadDoubleTapAction(parseGestureBindingList(gestureData, "doubleTapAction", Binding.NONE));
            if (gestureData.has("singleTap2ndFingerAction")) setTouchpadSingleTap2ndFingerAction(parseGestureBindingList(gestureData, "singleTap2ndFingerAction", Binding.MOUSE_RIGHT_BUTTON));
            if (gestureData.has("doubleTap2ndFingerAction")) setTouchpadDoubleTap2ndFingerAction(parseGestureBindingList(gestureData, "doubleTap2ndFingerAction", Binding.NONE));
            if (gestureData.has("singleTapDragAction")) setTouchpadSingleTapDragAction(parseGestureBindingList(gestureData, "singleTapDragAction", Binding.NONE));
            if (gestureData.has("longPressDragAction")) setTouchpadLongPressDragAction(parseGestureBindingList(gestureData, "longPressDragAction", Binding.NONE));
            if (gestureData.has("doubleTapDragAction")) setTouchpadDoubleTapDragAction(parseGestureBindingList(gestureData, "doubleTapDragAction", Binding.NONE));
            if (gestureData.has("singleTap2ndFingerDragAction")) setTouchpadSingleTap2ndFingerDragAction(parseGestureBindingList(gestureData, "singleTap2ndFingerDragAction", Binding.NONE));
            if (gestureData.has("doubleTap2ndFingerDragAction")) setTouchpadDoubleTap2ndFingerDragAction(parseGestureBindingList(gestureData, "doubleTap2ndFingerDragAction", Binding.NONE));

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
        }
        catch (JSONException e) {
            Log.w("ControlsProfile", "Failed to parse unified gesture field", e);
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

    public static Binding parseBinding(String value, Binding defaultValue) {
        if (value == null) return defaultValue;
        try {
            return Binding.valueOf(value);
        }
        catch (IllegalArgumentException e) {
            return defaultValue;
        }
    }

    private static List<Binding> parseGestureBindingList(JSONObject obj, String key, Binding defaultBinding) {
        try {
            JSONArray arr = obj.optJSONArray(key);
            if (arr != null) {
                List<Binding> result = new ArrayList<>();
                for (int i = 0; i < arr.length(); i++) {
                    result.add(parseBinding(arr.getString(i), defaultBinding));
                }
                return result;
            }
        } catch (JSONException ignored) {}
        Binding single = parseBinding(obj.optString(key, null), defaultBinding);
        return new ArrayList<>(Collections.singletonList(single));
    }

    private static JSONArray bindingListToJSONArray(List<Binding> bindings) {
        JSONArray arr = new JSONArray();
        for (Binding b : bindings) {
            if (b != null && b != Binding.NONE) arr.put(b.name());
        }
        return arr;
    }

    public static int clamp(int value, int min, int max) {
        return Math.max(min, Math.min(max, value));
    }

    // ---- End of touchscreen gesture settings ----

    public boolean isElementsLoaded() {
        return elementsLoaded;
    }

    public void save() {
        File file = getProfileFile(context, id);

        try {
            JSONObject data = new JSONObject();
            data.put("id", id);
            data.put("name", name);
            data.put("cursorSpeed", Float.valueOf(cursorSpeed));

            if (bindingDelay > 0) data.put("bindingDelay", bindingDelay);
            data.put("longPressDelay", longPressDelay);
            if (buttonLongPressHaptic != 1) data.put("buttonLongPressHaptic", buttonLongPressHaptic);
            if (buttonGestureHaptic != 5) data.put("buttonGestureHaptic", buttonGestureHaptic);
            if (gestureLongPressHaptic != 3) data.put("gestureLongPressHaptic", gestureLongPressHaptic);
            if (dragThreshold != 10) data.put("dragThreshold", dragThreshold);
            if (doubleTapDistance != 50) data.put("doubleTapDistance", doubleTapDistance);
            if (gestureThreshold != 20) data.put("gestureThreshold", gestureThreshold);
            if (strokeWidth != 0.2f) data.put("strokeWidth", strokeWidth);
            data.put("fillAlphaInactive", fillAlphaInactive);

            JSONArray elementsJSONArray = new JSONArray();
            if (!elementsLoaded && file.isFile()) {
                JSONObject profileJSONObject = new JSONObject(FileUtils.readString(file));
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
            unifiedGestureData.put("singleTapAction", bindingListToJSONArray(gestureSingleTapAction));
            unifiedGestureData.put("longPressAction", bindingListToJSONArray(gestureLongPressAction));
            unifiedGestureData.put("doubleTapAction", bindingListToJSONArray(gestureDoubleTapAction));
            unifiedGestureData.put("singleTap2ndFingerAction", bindingListToJSONArray(gestureSingleTap2ndFingerAction));
            unifiedGestureData.put("doubleTap2ndFingerAction", bindingListToJSONArray(gestureDoubleTap2ndFingerAction));
            unifiedGestureData.put("singleTapDragAction", bindingListToJSONArray(gestureSingleTapDragAction));
            unifiedGestureData.put("longPressDragAction", bindingListToJSONArray(gestureLongPressDragAction));
            unifiedGestureData.put("doubleTapDragAction", bindingListToJSONArray(gestureDoubleTapDragAction));
            unifiedGestureData.put("singleTap2ndFingerDragAction", bindingListToJSONArray(gestureSingleTap2ndFingerDragAction));
            unifiedGestureData.put("doubleTap2ndFingerDragAction", bindingListToJSONArray(gestureDoubleTap2ndFingerDragAction));
            unifiedGestureData.put("doubleTapTimeout", doubleTapTimeout);
            unifiedGestureData.put("longPressTimeout", longPressTimeout);
            unifiedGestureData.put("dragThreshold", dragThreshold);
            unifiedGestureData.put("singleTapDelay", singleTapDelay);
            unifiedGestureData.put("cursorSpeed", Float.valueOf(cursorSpeed));
            unifiedGestureData.put("touchActivationMode", touchActivationMode.name());
            data.put("gestureSettings", unifiedGestureData);

            // Legacy format: touchscreenGestures (populated from unified)
            {
                JSONObject gestureData = new JSONObject();
                gestureData.put("mouseMode", mouseMode.name());
                gestureData.put("inputMode", inputMode.name());
                gestureData.put("dragMode", dragMode.name());
                gestureData.put("singleTapAction", bindingListToJSONArray(gestureSingleTapAction));
                gestureData.put("longPressAction", bindingListToJSONArray(gestureLongPressAction));
                gestureData.put("doubleTapAction", bindingListToJSONArray(gestureDoubleTapAction));
                gestureData.put("singleTap2ndFingerAction", bindingListToJSONArray(gestureSingleTap2ndFingerAction));
                gestureData.put("doubleTap2ndFingerAction", bindingListToJSONArray(gestureDoubleTap2ndFingerAction));
                gestureData.put("singleTapDragAction", bindingListToJSONArray(gestureSingleTapDragAction));
                gestureData.put("longPressDragAction", bindingListToJSONArray(gestureLongPressDragAction));
                gestureData.put("doubleTapDragAction", bindingListToJSONArray(gestureDoubleTapDragAction));
                gestureData.put("singleTap2ndFingerDragAction", bindingListToJSONArray(gestureSingleTap2ndFingerDragAction));
                gestureData.put("doubleTap2ndFingerDragAction", bindingListToJSONArray(gestureDoubleTap2ndFingerDragAction));
                gestureData.put("doubleTapTimeout", doubleTapTimeout);
                gestureData.put("longPressTimeout", longPressTimeout);
                gestureData.put("touchActivationMode", touchActivationMode.name());
                if (singleTapDelay > 0) gestureData.put("singleTapDelay", singleTapDelay);
                data.put("touchscreenGestures", gestureData);
            }

            // Legacy format: touchpadGestures (also populated from unified)
            {
                JSONObject touchpadGestureData = new JSONObject();
                touchpadGestureData.put("singleTapAction", bindingListToJSONArray(gestureSingleTapAction));
                touchpadGestureData.put("longPressAction", bindingListToJSONArray(gestureLongPressAction));
                touchpadGestureData.put("doubleTapAction", bindingListToJSONArray(gestureDoubleTapAction));
                touchpadGestureData.put("singleTap2ndFingerAction", bindingListToJSONArray(gestureSingleTap2ndFingerAction));
                touchpadGestureData.put("doubleTap2ndFingerAction", bindingListToJSONArray(gestureDoubleTap2ndFingerAction));
                touchpadGestureData.put("singleTapDragAction", bindingListToJSONArray(gestureSingleTapDragAction));
                touchpadGestureData.put("longPressDragAction", bindingListToJSONArray(gestureLongPressDragAction));
                touchpadGestureData.put("doubleTapDragAction", bindingListToJSONArray(gestureDoubleTapDragAction));
                touchpadGestureData.put("singleTap2ndFingerDragAction", bindingListToJSONArray(gestureSingleTap2ndFingerDragAction));
                touchpadGestureData.put("doubleTap2ndFingerDragAction", bindingListToJSONArray(gestureDoubleTap2ndFingerDragAction));
                touchpadGestureData.put("doubleTapTimeout", doubleTapTimeout);
                touchpadGestureData.put("longPressTimeout", longPressTimeout);
                touchpadGestureData.put("dragThreshold", dragThreshold);
                touchpadGestureData.put("cursorSpeed", Float.valueOf(cursorSpeed));
                data.put("touchpadGestures", touchpadGestureData);
            }

            FileUtils.writeString(file, data.toString());
        }
        catch (JSONException e) {}
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
                    controllerBinding.setBinding(Binding.fromString(controllerBindingJSONObject.getString("binding")));
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
        if (!file.isFile()) return;

        try {
            JSONObject profileJSONObject = new JSONObject(FileUtils.readString(file));


            if (profileJSONObject.has("bindingDelay")) bindingDelay = profileJSONObject.getInt("bindingDelay");
            if (profileJSONObject.has("buttonLongPressHaptic")) buttonLongPressHaptic = clamp(profileJSONObject.getInt("buttonLongPressHaptic"), 0, 5);
            if (profileJSONObject.has("buttonGestureHaptic")) buttonGestureHaptic = clamp(profileJSONObject.getInt("buttonGestureHaptic"), 0, 5);
            if (profileJSONObject.has("gestureLongPressHaptic")) gestureLongPressHaptic = clamp(profileJSONObject.getInt("gestureLongPressHaptic"), 0, 5);
            else if (profileJSONObject.has("hapticFeedbackEnabled")) {
                if (!profileJSONObject.getBoolean("hapticFeedbackEnabled")) {
                    buttonLongPressHaptic = 0;
                    buttonGestureHaptic = 0;
                    gestureLongPressHaptic = 0;
                }
            }
            else if (profileJSONObject.has("longPressHapticEnabled")) {
                if (!profileJSONObject.getBoolean("longPressHapticEnabled")) {
                    buttonLongPressHaptic = 0;
                    buttonGestureHaptic = 0;
                    gestureLongPressHaptic = 0;
                }
            }
            if (profileJSONObject.has("dragThreshold")) dragThreshold = clamp(profileJSONObject.getInt("dragThreshold"), 0, 30);
            if (profileJSONObject.has("strokeWidth")) strokeWidth = (float)profileJSONObject.getDouble("strokeWidth");
            if (profileJSONObject.has("fillAlphaInactive")) fillAlphaInactive = profileJSONObject.getInt("fillAlphaInactive");
            if (profileJSONObject.has("touchscreenGestures")) {
                loadGestureSettingsFromJson(profileJSONObject.getJSONObject("touchscreenGestures"));
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
                            element.setCornerRadius(0f);
                            break;
                    }
                } else {
                    element.setShape(ControlElement.Shape.valueOf(shapeStr));
                }

                if (elementJSONObject.has("elementWidth")) element.setElementWidth((float)elementJSONObject.getDouble("elementWidth"));
                if (elementJSONObject.has("elementHeight")) element.setElementHeight((float)elementJSONObject.getDouble("elementHeight"));
                if (elementJSONObject.has("cornerRadius")) element.setCornerRadius((float)elementJSONObject.getDouble("cornerRadius"));
                if (elementJSONObject.has("dpadCornerRadius")) element.setDpadCornerRadius((float)elementJSONObject.getDouble("dpadCornerRadius"));
                element.setToggleSwitch(elementJSONObject.getBoolean("toggleSwitch"));
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
                JSONArray bindingsJSONArray = elementJSONObject.getJSONArray("bindings");
                JSONArray firstSeq = bindingsJSONArray.optJSONArray(0);
                if (firstSeq != null) {
                    for (int j = 0; j < bindingsJSONArray.length(); j++) {
                        JSONArray seqArray = bindingsJSONArray.getJSONArray(j);
                        List<Binding> seqList = new ArrayList<>();
                        for (int k = 0; k < seqArray.length(); k++) {
                            Binding binding = Binding.fromString(seqArray.getString(k));
                            seqList.add(binding);
                            if (!binding.isGamepad()) hasGamepadBinding = false;
                        }
                        element.setBindingSequence(j, seqList);
                    }
                } else {
                    for (int j = 0; j < bindingsJSONArray.length(); j++) {
                        Binding binding = Binding.fromString(bindingsJSONArray.getString(j));
                        element.setBindingAt(j, binding);
                        if (!binding.isGamepad()) hasGamepadBinding = false;
                    }
                }

                if (elementJSONObject.has("longPressBindings")) {
                    JSONArray lpArray = elementJSONObject.getJSONArray("longPressBindings");
                    List<Binding> lpList = new ArrayList<>();
                    for (int k = 0; k < lpArray.length(); k++) {
                        lpList.add(Binding.fromString(lpArray.getString(k)));
                    }
                    element.setLongPressBindings(lpList);
                }

                if (elementJSONObject.has("gestureBindings")) {
                    JSONArray gArray = elementJSONObject.getJSONArray("gestureBindings");
                    List<Binding> gList = new ArrayList<>();
                    for (int k = 0; k < gArray.length(); k++) {
                        gList.add(Binding.fromString(gArray.getString(k)));
                    }
                    element.setGestureBindings(gList);
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
