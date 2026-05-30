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

    // Touchscreen gesture settings
    private MouseMode mouseMode = MouseMode.TOUCHPAD;
    private InputMode inputMode = InputMode.ABSOLUTE;
    private DragMode dragMode = DragMode.AUTO;
    private List<Binding> singleTapAction = new ArrayList<>(Collections.singletonList(Binding.MOUSE_LEFT_BUTTON));
    private List<Binding> longPressAction = new ArrayList<>(Collections.singletonList(Binding.MOUSE_LEFT_BUTTON));
    private List<Binding> doubleTapAction = new ArrayList<>(Collections.singletonList(Binding.NONE));
    private List<Binding> singleTap2ndFingerAction = new ArrayList<>(Collections.singletonList(Binding.MOUSE_RIGHT_BUTTON));
    private List<Binding> longPress2ndFingerAction = new ArrayList<>(Collections.singletonList(Binding.MOUSE_RIGHT_BUTTON));
    private List<Binding> doubleTap2ndFingerAction = new ArrayList<>(Collections.singletonList(Binding.NONE));
    private List<Binding> singleTapDragAction = new ArrayList<>(Collections.singletonList(Binding.NONE));
    private List<Binding> longPressDragAction = new ArrayList<>(Collections.singletonList(Binding.NONE));
    private List<Binding> doubleTapDragAction = new ArrayList<>(Collections.singletonList(Binding.NONE));
    private List<Binding> singleTap2ndFingerDragAction = new ArrayList<>(Collections.singletonList(Binding.NONE));
    private List<Binding> longPress2ndFingerDragAction = new ArrayList<>(Collections.singletonList(Binding.NONE));
    private List<Binding> doubleTap2ndFingerDragAction = new ArrayList<>(Collections.singletonList(Binding.NONE));
    private int doubleTapTimeout = 200;
    private int longPressTimeout = 400;
    private SecondFingerMode secondFingerMode = SecondFingerMode.SECOND_TAP_ACTIONS;
    private int bindingDelay;
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
        if (this.singleTapAction.isEmpty()) this.singleTapAction.add(Binding.NONE);
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
        if (this.longPressAction.isEmpty()) this.longPressAction.add(Binding.NONE);
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
        if (this.doubleTapAction.isEmpty()) this.doubleTapAction.add(Binding.NONE);
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
        if (this.singleTap2ndFingerAction.isEmpty()) this.singleTap2ndFingerAction.add(Binding.NONE);
    }

    public List<Binding> getLongPress2ndFingerAction() {
        ensureGestureSettingsLoaded();
        return longPress2ndFingerAction;
    }

    public void setLongPress2ndFingerAction(Binding binding) {
        this.longPress2ndFingerAction.clear();
        this.longPress2ndFingerAction.add(binding);
    }

    public void setLongPress2ndFingerAction(List<Binding> bindings) {
        this.longPress2ndFingerAction.clear();
        this.longPress2ndFingerAction.addAll(bindings);
        if (this.longPress2ndFingerAction.isEmpty()) this.longPress2ndFingerAction.add(Binding.NONE);
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
        if (this.doubleTap2ndFingerAction.isEmpty()) this.doubleTap2ndFingerAction.add(Binding.NONE);
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
        return bindingDelay;
    }

    public void setBindingDelay(int bindingDelay) {
        this.bindingDelay = Math.max(0, Math.min(10, bindingDelay));
    }

    public SecondFingerMode getSecondFingerMode() {
        ensureGestureSettingsLoaded();
        return secondFingerMode;
    }

    public void setSecondFingerMode(SecondFingerMode mode) {
        this.secondFingerMode = mode;
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
        if (this.singleTapDragAction.isEmpty()) this.singleTapDragAction.add(Binding.NONE);
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
        if (this.longPressDragAction.isEmpty()) this.longPressDragAction.add(Binding.NONE);
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
        if (this.doubleTapDragAction.isEmpty()) this.doubleTapDragAction.add(Binding.NONE);
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
        if (this.singleTap2ndFingerDragAction.isEmpty()) this.singleTap2ndFingerDragAction.add(Binding.NONE);
    }

    public List<Binding> getLongPress2ndFingerDragAction() {
        ensureGestureSettingsLoaded();
        return longPress2ndFingerDragAction;
    }

    public void setLongPress2ndFingerDragAction(Binding binding) {
        this.longPress2ndFingerDragAction.clear();
        this.longPress2ndFingerDragAction.add(binding);
    }

    public void setLongPress2ndFingerDragAction(List<Binding> bindings) {
        this.longPress2ndFingerDragAction.clear();
        this.longPress2ndFingerDragAction.addAll(bindings);
        if (this.longPress2ndFingerDragAction.isEmpty()) this.longPress2ndFingerDragAction.add(Binding.NONE);
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
        if (this.doubleTap2ndFingerDragAction.isEmpty()) this.doubleTap2ndFingerDragAction.add(Binding.NONE);
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
            if (data.has("bindingDelay")) bindingDelay = data.getInt("bindingDelay");
            if (data.has("touchscreenGestures")) {
                loadGestureSettingsFromJson(data.getJSONObject("touchscreenGestures"));
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
            if (gestureData.has("singleTapAction")) setSingleTapAction(parseGestureBindingList(gestureData, "singleTapAction", Binding.MOUSE_LEFT_BUTTON));
            if (gestureData.has("longPressAction")) setLongPressAction(parseGestureBindingList(gestureData, "longPressAction", Binding.MOUSE_LEFT_BUTTON));
            if (gestureData.has("doubleTapAction")) setDoubleTapAction(parseGestureBindingList(gestureData, "doubleTapAction", Binding.NONE));
            if (gestureData.has("singleTap2ndFingerAction")) setSingleTap2ndFingerAction(parseGestureBindingList(gestureData, "singleTap2ndFingerAction", Binding.MOUSE_RIGHT_BUTTON));
            if (gestureData.has("longPress2ndFingerAction")) setLongPress2ndFingerAction(parseGestureBindingList(gestureData, "longPress2ndFingerAction", Binding.MOUSE_RIGHT_BUTTON));
            if (gestureData.has("doubleTap2ndFingerAction")) setDoubleTap2ndFingerAction(parseGestureBindingList(gestureData, "doubleTap2ndFingerAction", Binding.NONE));
            if (gestureData.has("singleTapDragAction")) setSingleTapDragAction(parseGestureBindingList(gestureData, "singleTapDragAction", Binding.NONE));
            if (gestureData.has("longPressDragAction")) setLongPressDragAction(parseGestureBindingList(gestureData, "longPressDragAction", Binding.NONE));
            if (gestureData.has("doubleTapDragAction")) setDoubleTapDragAction(parseGestureBindingList(gestureData, "doubleTapDragAction", Binding.NONE));
            if (gestureData.has("singleTap2ndFingerDragAction")) setSingleTap2ndFingerDragAction(parseGestureBindingList(gestureData, "singleTap2ndFingerDragAction", Binding.NONE));
            if (gestureData.has("longPress2ndFingerDragAction")) setLongPress2ndFingerDragAction(parseGestureBindingList(gestureData, "longPress2ndFingerDragAction", Binding.NONE));
            if (gestureData.has("doubleTap2ndFingerDragAction")) setDoubleTap2ndFingerDragAction(parseGestureBindingList(gestureData, "doubleTap2ndFingerDragAction", Binding.NONE));
            if (gestureData.has("doubleTapTimeout"))
                doubleTapTimeout = clamp(gestureData.getInt("doubleTapTimeout"), 50, 500);
            if (gestureData.has("longPressTimeout"))
                longPressTimeout = clamp(gestureData.getInt("longPressTimeout"), 50, 1000);
            if (gestureData.has("secondFingerMode"))
                secondFingerMode = parseEnum(SecondFingerMode.class, gestureData.getString("secondFingerMode"), SecondFingerMode.SECOND_TAP_ACTIONS);
        }
        catch (JSONException e) {
            Log.w("ControlsProfile", "Failed to parse gesture field in touchscreenGestures", e);
        }
    }

    public void markGestureSettingsLoaded() {
        this.gestureSettingsLoaded = true;
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
                if (result.isEmpty()) result.add(defaultBinding);
                return result;
            }
        } catch (JSONException ignored) {}
        Binding single = parseBinding(obj.optString(key, null), defaultBinding);
        return new ArrayList<>(Collections.singletonList(single));
    }

    private static JSONArray bindingListToJSONArray(List<Binding> bindings) {
        JSONArray arr = new JSONArray();
        for (Binding b : bindings) arr.put(b.name());
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

            JSONObject gestureData = new JSONObject();
            gestureData.put("mouseMode", mouseMode.name());
            gestureData.put("inputMode", inputMode.name());
            gestureData.put("dragMode", dragMode.name());
            gestureData.put("singleTapAction", bindingListToJSONArray(singleTapAction));
            gestureData.put("longPressAction", bindingListToJSONArray(longPressAction));
            gestureData.put("doubleTapAction", bindingListToJSONArray(doubleTapAction));
            gestureData.put("singleTap2ndFingerAction", bindingListToJSONArray(singleTap2ndFingerAction));
            gestureData.put("longPress2ndFingerAction", bindingListToJSONArray(longPress2ndFingerAction));
            gestureData.put("doubleTap2ndFingerAction", bindingListToJSONArray(doubleTap2ndFingerAction));
            gestureData.put("singleTapDragAction", bindingListToJSONArray(singleTapDragAction));
            gestureData.put("longPressDragAction", bindingListToJSONArray(longPressDragAction));
            gestureData.put("doubleTapDragAction", bindingListToJSONArray(doubleTapDragAction));
            gestureData.put("singleTap2ndFingerDragAction", bindingListToJSONArray(singleTap2ndFingerDragAction));
            gestureData.put("longPress2ndFingerDragAction", bindingListToJSONArray(longPress2ndFingerDragAction));
            gestureData.put("doubleTap2ndFingerDragAction", bindingListToJSONArray(doubleTap2ndFingerDragAction));
            gestureData.put("doubleTapTimeout", doubleTapTimeout);
            gestureData.put("longPressTimeout", longPressTimeout);
            gestureData.put("secondFingerMode", secondFingerMode.name());
            data.put("touchscreenGestures", gestureData);

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

            if (profileJSONObject.has("touchscreenGestures")) {
                loadGestureSettingsFromJson(profileJSONObject.getJSONObject("touchscreenGestures"));
            }
            gestureSettingsLoaded = true;

            JSONArray elementsJSONArray = profileJSONObject.getJSONArray("elements");
            for (int i = 0; i < elementsJSONArray.length(); i++) {
                JSONObject elementJSONObject = elementsJSONArray.getJSONObject(i);
                ControlElement element = new ControlElement(inputControlsView);
                element.setType(ControlElement.Type.valueOf(elementJSONObject.getString("type")));
                element.setShape(ControlElement.Shape.valueOf(elementJSONObject.getString("shape")));
                element.setToggleSwitch(elementJSONObject.getBoolean("toggleSwitch"));
                element.setX((int)(elementJSONObject.getDouble("x") * inputControlsView.getMaxWidth()));
                element.setY((int)(elementJSONObject.getDouble("y") * inputControlsView.getMaxHeight()));
                element.setScale((float)elementJSONObject.getDouble("scale"));
                element.setText(elementJSONObject.getString("text"));
                element.setIconId(elementJSONObject.getInt("iconId"));
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
