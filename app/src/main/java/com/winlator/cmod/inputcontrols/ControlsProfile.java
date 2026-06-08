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
    private int longPressTimeout = 150;
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
    private float cornerRadius = 0.8f;
    private TouchActivationMode touchActivationMode = TouchActivationMode.LOCK;


    // === Unified gesture bindings (primary storage) ===
    private BindPackage gestureSingleTapAction = BindPackage.fromSingle(Binding.MOUSE_LEFT_BUTTON);
    private BindPackage gestureLongPressAction = BindPackage.fromSingle(Binding.MOUSE_RIGHT_BUTTON);
    private BindPackage gestureDoubleTapAction = new BindPackage();
    private BindPackage gestureSingleTap2ndFingerAction = BindPackage.fromSingle(Binding.MOUSE_RIGHT_BUTTON);
    private BindPackage gestureDoubleTap2ndFingerAction = new BindPackage();
    private BindPackage gestureSingleTapDragAction = new BindPackage();
    private BindPackage gestureLongPressDragAction = BindPackage.fromSingle(Binding.MOUSE_LEFT_BUTTON);
    private BindPackage gestureDoubleTapDragAction = new BindPackage();
    private BindPackage gestureSingleTap2ndFingerDragAction = new BindPackage();
    private BindPackage gestureDoubleTap2ndFingerDragAction = new BindPackage();

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

    public List<Binding> getSingleTapAction() {
        ensureGestureSettingsLoaded();
        return gestureSingleTapAction.getBindings();
    }

    public void setSingleTapAction(Binding binding) {
        this.gestureSingleTapAction = BindPackage.fromSingle(binding);
    }

    public void setSingleTapAction(List<Binding> bindings) {
        this.gestureSingleTapAction = new BindPackage(bindings);
    }

    public List<Binding> getLongPressAction() {
        ensureGestureSettingsLoaded();
        return gestureLongPressAction.getBindings();
    }

    public void setLongPressAction(Binding binding) {
        this.gestureLongPressAction = BindPackage.fromSingle(binding);
    }

    public void setLongPressAction(List<Binding> bindings) {
        this.gestureLongPressAction = new BindPackage(bindings);
    }

    public List<Binding> getDoubleTapAction() {
        ensureGestureSettingsLoaded();
        return gestureDoubleTapAction.getBindings();
    }

    public void setDoubleTapAction(Binding binding) {
        this.gestureDoubleTapAction = BindPackage.fromSingle(binding);
    }

    public void setDoubleTapAction(List<Binding> bindings) {
        this.gestureDoubleTapAction = new BindPackage(bindings);
    }

    public List<Binding> getSingleTap2ndFingerAction() {
        ensureGestureSettingsLoaded();
        return gestureSingleTap2ndFingerAction.getBindings();
    }

    public void setSingleTap2ndFingerAction(Binding binding) {
        this.gestureSingleTap2ndFingerAction = BindPackage.fromSingle(binding);
    }

    public void setSingleTap2ndFingerAction(List<Binding> bindings) {
        this.gestureSingleTap2ndFingerAction = new BindPackage(bindings);
    }

    public List<Binding> getDoubleTap2ndFingerAction() {
        ensureGestureSettingsLoaded();
        return gestureDoubleTap2ndFingerAction.getBindings();
    }

    public void setDoubleTap2ndFingerAction(Binding binding) {
        this.gestureDoubleTap2ndFingerAction = BindPackage.fromSingle(binding);
    }

    public void setDoubleTap2ndFingerAction(List<Binding> bindings) {
        this.gestureDoubleTap2ndFingerAction = new BindPackage(bindings);
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

    public List<Binding> getSingleTapDragAction() {
        ensureGestureSettingsLoaded();
        return gestureSingleTapDragAction.getBindings();
    }

    public void setSingleTapDragAction(Binding binding) {
        this.gestureSingleTapDragAction = BindPackage.fromSingle(binding);
    }

    public void setSingleTapDragAction(List<Binding> bindings) {
        this.gestureSingleTapDragAction = new BindPackage(bindings);
    }

    public List<Binding> getLongPressDragAction() {
        ensureGestureSettingsLoaded();
        return gestureLongPressDragAction.getBindings();
    }

    public void setLongPressDragAction(Binding binding) {
        this.gestureLongPressDragAction = BindPackage.fromSingle(binding);
    }

    public void setLongPressDragAction(List<Binding> bindings) {
        this.gestureLongPressDragAction = new BindPackage(bindings);
    }

    public List<Binding> getDoubleTapDragAction() {
        ensureGestureSettingsLoaded();
        return gestureDoubleTapDragAction.getBindings();
    }

    public void setDoubleTapDragAction(Binding binding) {
        this.gestureDoubleTapDragAction = BindPackage.fromSingle(binding);
    }

    public void setDoubleTapDragAction(List<Binding> bindings) {
        this.gestureDoubleTapDragAction = new BindPackage(bindings);
    }

    public List<Binding> getSingleTap2ndFingerDragAction() {
        ensureGestureSettingsLoaded();
        return gestureSingleTap2ndFingerDragAction.getBindings();
    }

    public void setSingleTap2ndFingerDragAction(Binding binding) {
        this.gestureSingleTap2ndFingerDragAction = BindPackage.fromSingle(binding);
    }

    public void setSingleTap2ndFingerDragAction(List<Binding> bindings) {
        this.gestureSingleTap2ndFingerDragAction = new BindPackage(bindings);
    }

    public List<Binding> getDoubleTap2ndFingerDragAction() {
        ensureGestureSettingsLoaded();
        return gestureDoubleTap2ndFingerDragAction.getBindings();
    }

    public void setDoubleTap2ndFingerDragAction(Binding binding) {
        this.gestureDoubleTap2ndFingerDragAction = BindPackage.fromSingle(binding);
    }

    public void setDoubleTap2ndFingerDragAction(List<Binding> bindings) {
        this.gestureDoubleTap2ndFingerDragAction = new BindPackage(bindings);
    }

    // === Unified gesture binding getters/setters (primary storage) ===

    public BindPackage getGestureSingleTapAction() { ensureGestureSettingsLoaded(); return gestureSingleTapAction; }
    public void setGestureSingleTapAction(BindPackage bp) { this.gestureSingleTapAction = new BindPackage(bp); }
    public void setGestureSingleTapAction(List<Binding> bindings) { this.gestureSingleTapAction = new BindPackage(bindings); }

    public BindPackage getGestureLongPressAction() { ensureGestureSettingsLoaded(); return gestureLongPressAction; }
    public void setGestureLongPressAction(BindPackage bp) { this.gestureLongPressAction = new BindPackage(bp); }
    public void setGestureLongPressAction(List<Binding> bindings) { this.gestureLongPressAction = new BindPackage(bindings); }

    public BindPackage getGestureDoubleTapAction() { ensureGestureSettingsLoaded(); return gestureDoubleTapAction; }
    public void setGestureDoubleTapAction(BindPackage bp) { this.gestureDoubleTapAction = new BindPackage(bp); }
    public void setGestureDoubleTapAction(List<Binding> bindings) { this.gestureDoubleTapAction = new BindPackage(bindings); }

    public BindPackage getGestureSingleTap2ndFingerAction() { ensureGestureSettingsLoaded(); return gestureSingleTap2ndFingerAction; }
    public void setGestureSingleTap2ndFingerAction(BindPackage bp) { this.gestureSingleTap2ndFingerAction = new BindPackage(bp); }
    public void setGestureSingleTap2ndFingerAction(List<Binding> bindings) { this.gestureSingleTap2ndFingerAction = new BindPackage(bindings); }

    public BindPackage getGestureDoubleTap2ndFingerAction() { ensureGestureSettingsLoaded(); return gestureDoubleTap2ndFingerAction; }
    public void setGestureDoubleTap2ndFingerAction(BindPackage bp) { this.gestureDoubleTap2ndFingerAction = new BindPackage(bp); }
    public void setGestureDoubleTap2ndFingerAction(List<Binding> bindings) { this.gestureDoubleTap2ndFingerAction = new BindPackage(bindings); }

    public BindPackage getGestureSingleTapDragAction() { ensureGestureSettingsLoaded(); return gestureSingleTapDragAction; }
    public void setGestureSingleTapDragAction(BindPackage bp) { this.gestureSingleTapDragAction = new BindPackage(bp); }
    public void setGestureSingleTapDragAction(List<Binding> bindings) { this.gestureSingleTapDragAction = new BindPackage(bindings); }

    public BindPackage getGestureLongPressDragAction() { ensureGestureSettingsLoaded(); return gestureLongPressDragAction; }
    public void setGestureLongPressDragAction(BindPackage bp) { this.gestureLongPressDragAction = new BindPackage(bp); }
    public void setGestureLongPressDragAction(List<Binding> bindings) { this.gestureLongPressDragAction = new BindPackage(bindings); }

    public BindPackage getGestureDoubleTapDragAction() { ensureGestureSettingsLoaded(); return gestureDoubleTapDragAction; }
    public void setGestureDoubleTapDragAction(BindPackage bp) { this.gestureDoubleTapDragAction = new BindPackage(bp); }
    public void setGestureDoubleTapDragAction(List<Binding> bindings) { this.gestureDoubleTapDragAction = new BindPackage(bindings); }

    public BindPackage getGestureSingleTap2ndFingerDragAction() { ensureGestureSettingsLoaded(); return gestureSingleTap2ndFingerDragAction; }
    public void setGestureSingleTap2ndFingerDragAction(BindPackage bp) { this.gestureSingleTap2ndFingerDragAction = new BindPackage(bp); }
    public void setGestureSingleTap2ndFingerDragAction(List<Binding> bindings) { this.gestureSingleTap2ndFingerDragAction = new BindPackage(bindings); }

    public BindPackage getGestureDoubleTap2ndFingerDragAction() { ensureGestureSettingsLoaded(); return gestureDoubleTap2ndFingerDragAction; }
    public void setGestureDoubleTap2ndFingerDragAction(BindPackage bp) { this.gestureDoubleTap2ndFingerDragAction = new BindPackage(bp); }
    public void setGestureDoubleTap2ndFingerDragAction(List<Binding> bindings) { this.gestureDoubleTap2ndFingerDragAction = new BindPackage(bindings); }

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
            if (data.has("dragThreshold")) dragThreshold = clamp(data.getInt("dragThreshold"), 0, 30);
            if (data.has("doubleTapDistance")) doubleTapDistance = clamp(data.getInt("doubleTapDistance"), 10, 100);
            if (data.has("gestureThreshold")) gestureThreshold = clamp(data.getInt("gestureThreshold"), 10, 50);
            if (data.has("strokeWidth")) strokeWidth = (float)data.getDouble("strokeWidth");
            if (data.has("fillAlphaInactive")) fillAlphaInactive = data.getInt("fillAlphaInactive");
            if (data.has("gestureSettings")) {
                loadUnifiedGestureSettingsFromJson(data.getJSONObject("gestureSettings"));
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

            if (gestureData.has("singleTapAction"))
                gestureSingleTapAction = BindPackage.fromJSON(gestureData.getJSONObject("singleTapAction"), Binding.MOUSE_LEFT_BUTTON);
            if (gestureData.has("longPressAction"))
                gestureLongPressAction = BindPackage.fromJSON(gestureData.getJSONObject("longPressAction"), Binding.MOUSE_RIGHT_BUTTON);
            if (gestureData.has("doubleTapAction"))
                gestureDoubleTapAction = BindPackage.fromJSON(gestureData.getJSONObject("doubleTapAction"), Binding.NONE);
            if (gestureData.has("singleTap2ndFingerAction"))
                gestureSingleTap2ndFingerAction = BindPackage.fromJSON(gestureData.getJSONObject("singleTap2ndFingerAction"), Binding.MOUSE_RIGHT_BUTTON);
            if (gestureData.has("doubleTap2ndFingerAction"))
                gestureDoubleTap2ndFingerAction = BindPackage.fromJSON(gestureData.getJSONObject("doubleTap2ndFingerAction"), Binding.NONE);
            if (gestureData.has("singleTapDragAction"))
                gestureSingleTapDragAction = BindPackage.fromJSON(gestureData.getJSONObject("singleTapDragAction"), Binding.NONE);
            if (gestureData.has("longPressDragAction"))
                gestureLongPressDragAction = BindPackage.fromJSON(gestureData.getJSONObject("longPressDragAction"), Binding.MOUSE_LEFT_BUTTON);
            if (gestureData.has("doubleTapDragAction"))
                gestureDoubleTapDragAction = BindPackage.fromJSON(gestureData.getJSONObject("doubleTapDragAction"), Binding.NONE);
            if (gestureData.has("singleTap2ndFingerDragAction"))
                gestureSingleTap2ndFingerDragAction = BindPackage.fromJSON(gestureData.getJSONObject("singleTap2ndFingerDragAction"), Binding.NONE);
            if (gestureData.has("doubleTap2ndFingerDragAction"))
                gestureDoubleTap2ndFingerDragAction = BindPackage.fromJSON(gestureData.getJSONObject("doubleTap2ndFingerDragAction"), Binding.NONE);



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
            data.put("longPressDelay", longPressDelay);
            if (buttonLongPressHaptic != 1) data.put("buttonLongPressHaptic", buttonLongPressHaptic);
            if (buttonGestureHaptic != 5) data.put("buttonGestureHaptic", buttonGestureHaptic);
            if (gestureLongPressHaptic != 3) data.put("gestureLongPressHaptic", gestureLongPressHaptic);
            if (dragThreshold != 10) data.put("dragThreshold", dragThreshold);
            if (doubleTapDistance != 50) data.put("doubleTapDistance", doubleTapDistance);
            if (gestureThreshold != 20) data.put("gestureThreshold", gestureThreshold);
            if (strokeWidth != 0.15f) data.put("strokeWidth", strokeWidth);
            data.put("cornerRadius", cornerRadius);
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
            unifiedGestureData.put("singleTapAction", gestureSingleTapAction.toJSON());
            unifiedGestureData.put("longPressAction", gestureLongPressAction.toJSON());
            unifiedGestureData.put("doubleTapAction", gestureDoubleTapAction.toJSON());
            unifiedGestureData.put("singleTap2ndFingerAction", gestureSingleTap2ndFingerAction.toJSON());
            unifiedGestureData.put("doubleTap2ndFingerAction", gestureDoubleTap2ndFingerAction.toJSON());
            unifiedGestureData.put("singleTapDragAction", gestureSingleTapDragAction.toJSON());
            unifiedGestureData.put("longPressDragAction", gestureLongPressDragAction.toJSON());
            unifiedGestureData.put("doubleTapDragAction", gestureDoubleTapDragAction.toJSON());
            unifiedGestureData.put("singleTap2ndFingerDragAction", gestureSingleTap2ndFingerDragAction.toJSON());
            unifiedGestureData.put("doubleTap2ndFingerDragAction", gestureDoubleTap2ndFingerDragAction.toJSON());
            unifiedGestureData.put("doubleTapTimeout", doubleTapTimeout);
            unifiedGestureData.put("longPressTimeout", longPressTimeout);
            unifiedGestureData.put("dragThreshold", dragThreshold);
            unifiedGestureData.put("singleTapDelay", singleTapDelay);
            unifiedGestureData.put("cursorSpeed", Float.valueOf(cursorSpeed));
            unifiedGestureData.put("touchActivationMode", touchActivationMode.name());
            data.put("gestureSettings", unifiedGestureData);

            String jsonStr = data.toString();
            if (gestureSingleTapAction.isToggleSwitch())
                android.util.Log.w("Winlator_Gesture", "SAVE: singleTapAction has toggleSwitch=true JSON="+jsonStr.substring(0, Math.min(500, jsonStr.length())));
            FileUtils.writeString(file, jsonStr);
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
        Log.w("Winlator_Controls", "loadElements: loading profile from " + file.getAbsolutePath());
        if (!file.isFile()) {
            Log.w("Winlator_Controls", "loadElements: file not found, returning");
            return;
        }

        try {
            JSONObject profileJSONObject = new JSONObject(FileUtils.readString(file));


            if (profileJSONObject.has("bindingDelay")) bindingDelay = profileJSONObject.getInt("bindingDelay");
            if (profileJSONObject.has("buttonLongPressHaptic")) buttonLongPressHaptic = clamp(profileJSONObject.getInt("buttonLongPressHaptic"), 0, 5);
            if (profileJSONObject.has("buttonGestureHaptic")) buttonGestureHaptic = clamp(profileJSONObject.getInt("buttonGestureHaptic"), 0, 5);
            if (profileJSONObject.has("gestureLongPressHaptic")) gestureLongPressHaptic = clamp(profileJSONObject.getInt("gestureLongPressHaptic"), 0, 5);
            if (profileJSONObject.has("dragThreshold")) dragThreshold = clamp(profileJSONObject.getInt("dragThreshold"), 0, 30);
            if (profileJSONObject.has("strokeWidth")) strokeWidth = (float)profileJSONObject.getDouble("strokeWidth");
            if (profileJSONObject.has("fillAlphaInactive")) fillAlphaInactive = profileJSONObject.getInt("fillAlphaInactive");
            if (profileJSONObject.has("cornerRadius")) setCornerRadius((float)profileJSONObject.getDouble("cornerRadius"));
            if (profileJSONObject.has("gestureSettings")) {
                loadUnifiedGestureSettingsFromJson(profileJSONObject.getJSONObject("gestureSettings"));
            }
            gestureSettingsLoaded = true;

            JSONArray elementsJSONArray = profileJSONObject.getJSONArray("elements");
            Log.w("Winlator_Controls", "loadElements: found " + elementsJSONArray.length() + " elements in JSON");
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
                Log.w("Winlator_Controls", "loadElements: element[" + i + "] type=" + element.getType() + " text=" + element.getText());
                element.setIconId(elementJSONObject.getInt("iconId"));
                if (elementJSONObject.has("customIconData")) element.setCustomIconData(elementJSONObject.getString("customIconData"));
                if (elementJSONObject.has("range")) element.setRange(ControlElement.Range.valueOf(elementJSONObject.getString("range")));
                if (elementJSONObject.has("orientation")) element.setOrientation((byte)elementJSONObject.getInt("orientation"));

                boolean hasGamepadBinding = true;
                JSONArray slotPackagesArray = elementJSONObject.getJSONArray("slotPackages");
                for (int j = 0; j < slotPackagesArray.length(); j++) {
                    JSONObject pkgObj = slotPackagesArray.getJSONObject(j);
                    BindPackage pkg = BindPackage.fromJSON(pkgObj, Binding.NONE);
                    element.setSlotPackage(j, pkg);
                    for (int k = 0; k < pkg.size(); k++) {
                        Binding b = pkg.get(k);
                        if (b != null && !b.isGamepad()) hasGamepadBinding = false;
                    }
                }

                if (elementJSONObject.has("longPressPackage")) {
                    JSONObject lpObj = elementJSONObject.getJSONObject("longPressPackage");
                    element.setLongPressPackage(BindPackage.fromJSON(lpObj, Binding.NONE));
                }

                if (elementJSONObject.has("gesturePackage")) {
                    JSONObject gObj = elementJSONObject.getJSONObject("gesturePackage");
                    element.setGesturePackage(BindPackage.fromJSON(gObj, Binding.NONE));
                }

                if (!virtualGamepad && hasGamepadBinding) virtualGamepad = true;
                elements.add(element);
            }
            elementsLoaded = true;
        }
        catch (JSONException e) {
            Log.w("Winlator_Controls", "loadElements: JSONException: " + e.getMessage());
            e.printStackTrace();
        }
    }
}
