package com.winlator.cmod.widget;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Point;
import android.graphics.PointF;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.Rect;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.util.SparseArray;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.PointerIcon;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import androidx.preference.PreferenceManager;

import com.winlator.cmod.R;
import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlElement;
import com.winlator.cmod.inputcontrols.InputMode;
import com.winlator.cmod.inputcontrols.MouseMode;
import com.winlator.cmod.inputcontrols.TouchActivationMode;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.inputcontrols.ExternalController;
import com.winlator.cmod.inputcontrols.ExternalControllerBinding;
import com.winlator.cmod.inputcontrols.GamepadState;
import com.winlator.cmod.math.Mathf;
import com.winlator.cmod.winhandler.MouseEventFlags;
import com.winlator.cmod.winhandler.WinHandler;
import com.winlator.cmod.xserver.Pointer;
import com.winlator.cmod.xserver.XServer;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Timer;
import java.util.TimerTask;

public class InputControlsView extends View {
    public static final float DEFAULT_OVERLAY_OPACITY = 0.4f;
    private static final byte MOUSE_WHEEL_DELTA = 120;
    private boolean editMode = false;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final ColorFilter colorFilter = new PorterDuffColorFilter(0xffffffff, PorterDuff.Mode.SRC_IN);
    private final Point cursor = new Point();
    private boolean readyToDraw = false;
    private boolean moveCursor = false;
    private int snappingSize;
    private float offsetX;
    private float offsetY;
    private ControlElement selectedElement;
    private ControlsProfile profile;
    private float overlayOpacity = DEFAULT_OVERLAY_OPACITY;
    private float cacheAlphaOverride = -1;
    private TouchpadView touchpadView;
    private XServer xServer;
    private final Bitmap[] icons = new Bitmap[17];
    private Timer mouseMoveTimer;
    private final PointF mouseMoveOffset = new PointF();
    private boolean showTouchscreenControls = true;
    private boolean cachesPreBuilt = false;
    private Handler timeoutHandler; // Reference to the activity's timeout handler
    private Runnable hideControlsRunnable; // Runnable to hide the controls

    private SharedPreferences preferences;
    private boolean cachedRenderingEnabled;
    private final SharedPreferences.OnSharedPreferenceChangeListener prefListener = (prefs, key) -> {
        if ("cached_rendering".equals(key)) cachedRenderingEnabled = prefs.getBoolean(key, true);
    };

    public boolean isCachingEnabled() {
        return cachedRenderingEnabled;
    }

    private void initPreferences() {
        preferences = PreferenceManager.getDefaultSharedPreferences(getContext());
        cachedRenderingEnabled = preferences.getBoolean("cached_rendering", true);
        preferences.registerOnSharedPreferenceChangeListener(prefListener);
    }

    private ControlElement stickElement;

    private InputMode inputMode = InputMode.ABSOLUTE;
    private boolean focusOnStick = false;

    private final SparseArray<ControlElement> hoveredButtons = new SparseArray<>();
    private final SparseArray<ArrayList<ControlElement>> trackedButtons = new SparseArray<>();

    private ControlElement findButtonAt(float x, float y) {
        for (ControlElement element : profile.getElements()) {
            if (element.getType() == ControlElement.Type.BUTTON && element.containsPoint(x, y)) {
                return element;
            }
        }
        return null;
    }

    public void setInputMode(InputMode mode) {
        this.inputMode = mode;
    }

    public boolean isFocusedOnStick() {
        return focusOnStick;
    }

    public void setFocusOnStick(boolean focus) {
        this.focusOnStick = focus;
        invalidate(); // Redraw the view with the new focus setting
    }



    @SuppressLint("ResourceType")
    public InputControlsView(Context context) {
        super(context);
        setClickable(true);
        setFocusable(true);
        setFocusableInTouchMode(true);
        requestFocus();
        setBackgroundColor(0x00000000);
        setPointerIcon(PointerIcon.load(getResources(), R.drawable.hidden_pointer_arrow));
        setLayoutParams(new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        initPreferences();
    }

    @SuppressLint("ResourceType")
    public InputControlsView(Context context, Handler timeoutHandler, Runnable hideControlsRunnable) {
        super(context);
        this.timeoutHandler = timeoutHandler;
        this.hideControlsRunnable = hideControlsRunnable;
        setClickable(true);
        setFocusable(true);
        setFocusableInTouchMode(true);
        requestFocus();
        setBackgroundColor(0x00000000);
        setPointerIcon(PointerIcon.load(getResources(), R.drawable.hidden_pointer_arrow));
        setLayoutParams(new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        initPreferences();
    }

    public InputControlsView(Context context, boolean focusOnStick) {
        super(context);
        setClickable(true);
        setFocusable(true);
        setFocusableInTouchMode(true);
        requestFocus();
        setBackgroundColor(0x00000000);
        setPointerIcon(PointerIcon.load(getResources(), R.drawable.hidden_pointer_arrow));

        if (focusOnStick) {
            setLayoutParams(new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        } else {
            setLayoutParams(new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        }

        initPreferences();
    }


    public void setEditMode(boolean editMode) {
        this.editMode = editMode;
    }

    public void setOverlayOpacity(float overlayOpacity) {
        this.overlayOpacity = overlayOpacity;
        if (profile != null) {
            for (ControlElement element : profile.getElements()) {
                element.invalidateElementCachesKeepDisk();
            }
        }
        ControlElement.clearSharedPool();
        cachesPreBuilt = false;
        invalidate();
    }

    public void invalidateCache() {
        if (profile != null) {
            for (ControlElement element : profile.getElements()) {
                element.invalidateElementCachesKeepDisk();
            }
        }
        ControlElement.clearSharedPool();
        cachesPreBuilt = false;
        invalidate();
    }

    public int getSnappingSize() {
        return snappingSize;
    }

    @Override
    protected synchronized void onDraw(Canvas canvas) {
        int width, height;

        if (stickElement != null && isFocusedOnStick()) {
            Rect boundingBox = stickElement.getBoundingBox();
            width = boundingBox.width();
            height = boundingBox.height();
        } else {
            width = getWidth();
            height = getHeight();
        }

        if (width == 0 || height == 0) {
            readyToDraw = false;
            return;
        }

        snappingSize = width / 100;
        readyToDraw = true;

        if (editMode) {
            drawGrid(canvas);
            drawCursor(canvas);
        }

        if (stickElement != null) {
            stickElement.draw(canvas);
        }

        if (profile != null && showTouchscreenControls && !isFocusedOnStick()) {
            if (!profile.isElementsLoaded()) profile.loadElements(this);
            if (cachedRenderingEnabled) {
                for (ControlElement element : profile.getElements()) {
                    if (!cachesPreBuilt) element.buildCache();
                    element.drawCached(canvas);
                }
                cachesPreBuilt = true;
            }
            else {
                for (ControlElement element : profile.getElements()) {
                    element.draw(canvas);
                }
            }
        }

        super.onDraw(canvas);
    }


    public void resetStickPosition() {
        if (stickElement != null) {
            Rect boundingBox = stickElement.getBoundingBox();
            float centerX = boundingBox.centerX();
            float centerY = boundingBox.centerY();

            stickElement.setCurrentPosition(centerX, centerY); // Reset to the center of the bounding box
            invalidate(); // Redraw the stick in the centered position
        }
    }



    public void initializeStickElement(float x, float y, float scale) {
        stickElement = new ControlElement(this);
        stickElement.setType(ControlElement.Type.STICK); // Set type to STICK
        stickElement.setX((int) x);
        stickElement.setY((int) y);
        stickElement.setScale(scale);
        invalidate(); // Force the view to redraw with the stick
    }


    public void updateStickPosition(float x, float y) {
        if (stickElement != null) {
            stickElement.getCurrentPosition().x = x;  // Update the thumbstick's position
            stickElement.getCurrentPosition().y = y;  // Update the thumbstick's position
            invalidate(); // Redraw the view
        }
    }


    public ControlElement getStickElement() {
        return stickElement;
    }

    private void drawGrid(Canvas canvas) {
        paint.setStyle(Paint.Style.FILL);
        paint.setStrokeWidth(snappingSize * 0.0625f);
        paint.setColor(0xff000000);
        canvas.drawColor(Color.BLACK);

        paint.setAntiAlias(false);
        paint.setColor(0xff303030);

        int width = getMaxWidth();
        int height = getMaxHeight();

        for (int i = 0; i < width; i += snappingSize) {
            canvas.drawLine(i, 0, i, height, paint);
            canvas.drawLine(0, i, width, i, paint);
        }

        float cx = Mathf.roundTo(width * 0.5f, snappingSize);
        float cy = Mathf.roundTo(height * 0.5f, snappingSize);
        paint.setColor(0xff424242);

        for (int i = 0; i < width; i += snappingSize * 2) {
            canvas.drawLine(cx, i, cx, i + snappingSize, paint);
            canvas.drawLine(i, cy, i + snappingSize, cy, paint);
        }

        paint.setAntiAlias(true);
    }

    private void drawCursor(Canvas canvas) {
        paint.setStyle(Paint.Style.FILL);
        paint.setStrokeWidth(snappingSize * 0.0625f);
        paint.setColor(0xffc62828);

        paint.setAntiAlias(false);
        canvas.drawLine(0, cursor.y, getMaxWidth(), cursor.y, paint);
        canvas.drawLine(cursor.x, 0, cursor.x, getMaxHeight(), paint);

        paint.setAntiAlias(true);
    }

    public synchronized boolean addElement() {
        if (editMode && profile != null) {
            ControlElement element = new ControlElement(this);
            element.setX(cursor.x);
            element.setY(cursor.y);
            profile.addElement(element);
            profile.save();
            invalidateCache();
            selectElement(element);
            return true;
        }
        else return false;
    }

    public synchronized boolean copyElement() {
        if (editMode && selectedElement != null && profile != null) {
            ControlElement element = new ControlElement(this);
            element.setType(selectedElement.getType());
            element.setBindingCount(selectedElement.getBindingCount());
            for (int i = 0; i < element.getBindingCount(); i++) {
                element.setBindingSequence(i, new ArrayList<>(selectedElement.getBindingSequence(i)));
            }
            element.setShape(selectedElement.getShape());
            element.setToggleSwitch(selectedElement.isToggleSwitch());
            element.setScale(selectedElement.getScale());
            element.setText(selectedElement.getText());
            element.setIconId(selectedElement.getIconId());
            if (selectedElement.getRange() != null) element.setRange(selectedElement.getRange());
            element.setOrientation(selectedElement.getOrientation());
            element.setElementWidth(selectedElement.getElementWidth());
            element.setElementHeight(selectedElement.getElementHeight());
            element.setCornerRadius(selectedElement.getCornerRadius());
            element.setDpadCornerRadius(selectedElement.getDpadCornerRadius());
            if (selectedElement.hasLongPressBinding()) {
                element.setLongPressBindings(new ArrayList<>(selectedElement.getLongPressBindings()));
            }
            if (selectedElement.hasGestureBinding()) {
                element.setGestureBindings(new ArrayList<>(selectedElement.getGestureBindings()));
            }
            element.setX(cursor.x);
            element.setY(cursor.y);
            profile.addElement(element);
            profile.save();
            invalidateCache();
            selectElement(element);
            return true;
        }
        else return false;
    }

    public synchronized boolean removeElement() {
        if (editMode && selectedElement != null && profile != null) {
            profile.removeElement(selectedElement);
            selectedElement = null;
            profile.save();
            invalidateCache();
            return true;
        }
        else return false;
    }

    public ControlElement getSelectedElement() {
        return selectedElement;
    }

    private synchronized void deselectAllElements() {
        selectedElement = null;
        if (profile != null) {
            for (ControlElement element : profile.getElements()) element.setSelected(false);
        }
    }

    private void selectElement(ControlElement element) {
        deselectAllElements();
        if (element != null) {
            selectedElement = element;
            selectedElement.setSelected(true);
        }
        invalidate();
    }

    public synchronized ControlsProfile getProfile() {
        return profile;
    }

    public synchronized void setProfile(ControlsProfile profile) {
        if (profile != null) {
            this.profile = profile;
            deselectAllElements();
        }
        else this.profile = null;
        cachesPreBuilt = false;
        invalidateCache();
    }

    public boolean isShowTouchscreenControls() {
        return showTouchscreenControls;
    }

    public void setShowTouchscreenControls(boolean showTouchscreenControls) {
        this.showTouchscreenControls = showTouchscreenControls;
    }

    public float getOverlayOpacity() {
        return cacheAlphaOverride >= 0 ? cacheAlphaOverride : overlayOpacity;
    }

    public int getPrimaryColor() {
        float alpha = cacheAlphaOverride >= 0 ? cacheAlphaOverride : overlayOpacity;
        return Color.argb((int)(alpha * 255), 255, 255, 255);
    }

    public int getSecondaryColor() {
        float alpha = cacheAlphaOverride >= 0 ? cacheAlphaOverride : overlayOpacity;
        return Color.argb((int)(alpha * 255), 2, 119, 189);
    }

    public void setCacheAlphaOverride(float alpha) {
        this.cacheAlphaOverride = alpha;
    }

    private synchronized ControlElement intersectElement(float x, float y) {
        if (profile != null) {
            for (ControlElement element : profile.getElements()) {
                if (element.containsPoint(x, y)) return element;
            }
        }
        return null;
    }

    public Paint getPaint() {
        return paint;
    }

    public Path getPath() {
        return path;
    }

    public ColorFilter getColorFilter() {
        return colorFilter;
    }

    public TouchpadView getTouchpadView() {
        return touchpadView;
    }

    public void setTouchpadView(TouchpadView touchpadView) {
        this.touchpadView = touchpadView;
    }

    public XServer getXServer() {
        return xServer;
    }

    public void setXServer(XServer xServer) {
        this.xServer = xServer;
        createMouseMoveTimer();
    }

    public int getMaxWidth() {
        return (int)Mathf.roundTo(getWidth(), snappingSize);
    }

    @Override
    protected void onDetachedFromWindow() {
        if (mouseMoveTimer != null)
            mouseMoveTimer.cancel();
        super.onDetachedFromWindow();
    }

    public int getMaxHeight() {
        return (int)Mathf.roundTo(getHeight(), snappingSize);
    }

    private void createMouseMoveTimer() {
        WinHandler winHandler = xServer.getWinHandler();
        if (mouseMoveTimer == null && profile != null) {
            final float cursorSpeed = profile.getCursorSpeed();
            mouseMoveTimer = new Timer();
            mouseMoveTimer.schedule(new TimerTask() {
                @Override
                public void run() {
                    if (mouseMoveOffset.x != 0 || mouseMoveOffset.y != 0) {// Only move if there's an offsete if there's an offset
                        if (inputMode == InputMode.RELATIVE)
                            winHandler.mouseEvent(MouseEventFlags.MOVE, (int) (mouseMoveOffset.x * cursorSpeed * 10), (int) (mouseMoveOffset.y * cursorSpeed * 10), 0);
                        else
                            xServer.injectPointerMoveDelta(
                                (int) (mouseMoveOffset.x * cursorSpeed * 10),
                                (int) (mouseMoveOffset.y * cursorSpeed * 10)
                        );
                    }
                }
            }, 0, 1000 / 60); // 60 FPS
        }
    }



    private void processJoystickInput(ExternalController controller) {
        final int[] axes = {
                MotionEvent.AXIS_X, MotionEvent.AXIS_Y,
                MotionEvent.AXIS_Z, MotionEvent.AXIS_RZ,
                MotionEvent.AXIS_HAT_X, MotionEvent.AXIS_HAT_Y
        };
        final float[] values = {
                controller.state.thumbLX, controller.state.thumbLY,
                controller.state.thumbRX, controller.state.thumbRY,
                controller.state.getDPadX(), controller.state.getDPadY()
        };

        for (int i = 0; i < axes.length; i++) {
            float value = values[i];
            if (Math.abs(value) > ControlElement.STICK_DEAD_ZONE) {
                byte sign = Mathf.sign(value);
                int keyCode = ExternalControllerBinding.getKeyCodeForAxis(axes[i], sign);
                ExternalControllerBinding controllerBinding = controller.getControllerBinding(keyCode);
                if (controllerBinding != null) {
                    handleInputEvent(controller, controllerBinding.getBinding(), true, value, false);
                }
            } else {
                for (byte sign = -1; sign <= 1; sign += 2) {
                    int keyCode = ExternalControllerBinding.getKeyCodeForAxis(axes[i], sign);
                    ExternalControllerBinding controllerBinding = controller.getControllerBinding(keyCode);
                    if (controllerBinding != null) {
                        handleInputEvent(controller, controllerBinding.getBinding(), false, value, false);
                    }
                }
            }
        }

        processTriggerInput(controller, controller.state.triggerL, KeyEvent.KEYCODE_BUTTON_L2, false);
        processTriggerInput(controller, controller.state.triggerR, KeyEvent.KEYCODE_BUTTON_R2, false);

        WinHandler winHandler = xServer != null ? xServer.getWinHandler() : null;
        if (winHandler != null) {
            winHandler.sendGamepadState(controller);
        }
    }

    private void processTriggerInput(ExternalController controller, float value, int keyCode, boolean sendUpdate) {
        ExternalControllerBinding binding = controller.getControllerBinding(keyCode);
        if (binding != null) {
            boolean isPressed = value > ControlElement.STICK_DEAD_ZONE; // Use deadzone or simple > 0
            if (isPressed) {
                handleInputEvent(controller, binding.getBinding(), true, value, sendUpdate);
            } else {
                handleInputEvent(controller, binding.getBinding(), false, 0, sendUpdate);
            }
        }
    }




    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        Log.d("InputControlsView", "dispatchGenericMotionEvent called. Source: " + event.getSource());
        return super.dispatchGenericMotionEvent(event);
    }


    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {

        Log.d("InputControlsView", "Motion event received. Source: " + event.getSource());
        Log.d("InputControlsView", "Device ID: " + event.getDeviceId());
        Log.d("InputControlsView", "Profile is " + (profile != null ? "set" : "null"));


        if (!editMode && profile != null) {
            ExternalController controller = profile.getController(event.getDeviceId());

            if (controller != null && controller.updateStateFromMotionEvent(event)) {
                ExternalControllerBinding controllerBinding;

                controllerBinding = controller.getControllerBinding(KeyEvent.KEYCODE_BUTTON_L2);
                if (controllerBinding != null) {
                    handleInputEvent(controller, controllerBinding.getBinding(), controller.state.isPressed(ExternalController.IDX_BUTTON_L2));
                }

                controllerBinding = controller.getControllerBinding(KeyEvent.KEYCODE_BUTTON_R2);
                if (controllerBinding != null) {
                    handleInputEvent(controller, controllerBinding.getBinding(), controller.state.isPressed(ExternalController.IDX_BUTTON_R2));
                }

                Log.d("InputEvent", "Event source: " + event.getSource());
                Log.d("InputEvent", "Device ID: " + event.getDeviceId());
                Log.d("InputEvent", "Action: " + event.getAction());

                processJoystickInput(controller);

                return true;
            }
        }

        return super.onGenericMotionEvent(event);
    }


    @Override
    public boolean onTouchEvent(MotionEvent event) {
        resetTouchscreenTimeout();

        if (editMode && readyToDraw) {
            switch (event.getAction()) {
                case MotionEvent.ACTION_DOWN: {
                    float x = event.getX();
                    float y = event.getY();
                    ControlElement element = intersectElement(x, y);
                    moveCursor = true;
                    if (element != null) {
                        offsetX = x - element.getX();
                        offsetY = y - element.getY();
                        moveCursor = false;
                    }
                    selectElement(element);
                    break;
                }
                case MotionEvent.ACTION_MOVE: {
                    if (selectedElement != null) {
                        selectedElement.setX((int) Mathf.roundTo(event.getX() - offsetX, snappingSize));
                        selectedElement.setY((int) Mathf.roundTo(event.getY() - offsetY, snappingSize));
                        invalidate();
                    }
                    break;
                }
                case MotionEvent.ACTION_UP: {
                    if (selectedElement != null && profile != null) profile.save();
                    if (moveCursor) cursor.set((int) Mathf.roundTo(event.getX(), snappingSize), (int) Mathf.roundTo(event.getY(), snappingSize));
                    invalidate();
                    break;
                }
            }
        }

        if (!editMode && profile != null) {
            int actionIndex = event.getActionIndex();
            int pointerId = event.getPointerId(actionIndex);
            int actionMasked = event.getActionMasked();

            if (profile.getElements().isEmpty()) {
                touchpadView.onTouchEvent(event);
                return true;
            }

            TouchActivationMode actMode = profile.getTouchActivationMode();

            switch (actionMasked) {
                case MotionEvent.ACTION_DOWN:
                case MotionEvent.ACTION_POINTER_DOWN: {
                    float x = event.getX(actionIndex);
                    float y = event.getY(actionIndex);
                    touchpadView.setPointerButtonLeftEnabled(true);

                    boolean handled = handleDownByMode(pointerId, x, y, actMode);
                    if (!handled) touchpadView.onTouchEvent(event);
                    break;
                }
                case MotionEvent.ACTION_MOVE: {
                    handleMoveByMode(event, actMode);
                    break;
                }
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_POINTER_UP:
                case MotionEvent.ACTION_CANCEL: {
                    boolean handled = handleUpByMode(pointerId, actMode);
                    for (ControlElement element : profile.getElements()) {
                        if (element.handleTouchUp(pointerId)) handled = true;
                    }
                    if (!handled) touchpadView.onTouchEvent(event);
                    break;
                }
            }
        }
        return true;
    }

    private boolean handleDownByMode(int pointerId, float x, float y, TouchActivationMode actMode) {
        boolean handled = false;
        for (ControlElement element : profile.getElements()) {
            if (element.getBindingAt(0) == Binding.MOUSE_LEFT_BUTTON) {
                touchpadView.setPointerButtonLeftEnabled(false);
            }
        }
        int longPressDelay = profile.getLongPressDelay();
        switch (actMode) {
            case LOCK: {
                for (ControlElement element : profile.getElements()) {
                    if (element.handleTouchDown(pointerId, x, y)) {
                        handled = true;
                    }
                }
                break;
            }
            case TRACK:
            case HOVER: {
                for (ControlElement element : profile.getElements()) {
                    if (element.getType() != ControlElement.Type.BUTTON && element.handleTouchDown(pointerId, x, y)) {
                        handled = true;
                    }
                }
                ControlElement btn = findButtonAt(x, y);
                if (btn != null) {
                    btn.setGestureDownPosition(x, y);
                    ArrayList<ControlElement> tracked = trackedButtons.get(pointerId);
                    if (tracked == null) {
                        tracked = new ArrayList<>();
                        trackedButtons.put(pointerId, tracked);
                    }
                    if (!tracked.contains(btn)) {
                        btn.handleTouchDown(pointerId, x, y);
                        tracked.add(btn);
                    }
                    if (actMode == TouchActivationMode.HOVER) {
                        hoveredButtons.put(pointerId, btn);
                    }
                    handled = true;
                }
                break;
            }
        }
        return handled;
    }

    private void handleMoveByMode(MotionEvent event, TouchActivationMode actMode) {
        switch (actMode) {
            case LOCK:
                for (byte i = 0, count = (byte) event.getPointerCount(); i < count; i++) {
                    if (!processElementsTouchMove(event.getPointerId(i), event.getX(i), event.getY(i)))
                        touchpadView.onTouchEvent(event);
                }
                break;
            case TRACK:
                for (byte i = 0, count = (byte) event.getPointerCount(); i < count; i++) {
                    float x = event.getX(i);
                    float y = event.getY(i);
                    int pid = event.getPointerId(i);
                    boolean h = processElementsTouchMove(pid, x, y);
                    ArrayList<ControlElement> tracked = trackedButtons.get(pid);
                    if (tracked != null) {
                        ControlElement btn = findButtonAt(x, y);
                        if (btn != null && !tracked.contains(btn)) {
                            btn.activate();
                            tracked.add(btn);
                            if (tracked.size() == 2) tracked.get(0).cancelPendingLongPress();
                        }
                        h = true;
                    }
                    if (!h) touchpadView.onTouchEvent(event);
                }
                break;
            case HOVER:
                for (byte i = 0, count = (byte) event.getPointerCount(); i < count; i++) {
                    float x = event.getX(i);
                    float y = event.getY(i);
                    int pid = event.getPointerId(i);
                    boolean h = processElementsTouchMove(pid, x, y);
                    ArrayList<ControlElement> tracked = trackedButtons.get(pid);
                    if (tracked != null) {
                        ControlElement btn = findButtonAt(x, y);
                        ControlElement prev = hoveredButtons.get(pid);
                        if (btn != prev) {
                            if (prev != null) {
                                prev.deactivate();
                            }
                            if (btn != null) {
                                btn.activate();
                                if (!tracked.contains(btn)) {
                                    tracked.add(btn);
                                    if (tracked.size() == 2) tracked.get(0).cancelPendingLongPress();
                                }
                                hoveredButtons.put(pid, btn);
                            }
                            else {
                                hoveredButtons.remove(pid);
                            }
                        }
                        h = true;
                    }
                    if (!h) touchpadView.onTouchEvent(event);
                }
                break;
        }
    }

    private boolean handleUpByMode(int pointerId, TouchActivationMode actMode) {
        boolean handled = false;
        switch (actMode) {
            case TRACK:
            case HOVER: {
                ArrayList<ControlElement> tracked = trackedButtons.get(pointerId);
                if (tracked != null) {
                    if (tracked.size() > 1) {
                        for (ControlElement btn : tracked) btn.deactivate();
                    }
                    tracked.clear();
                    trackedButtons.remove(pointerId);
                    if (actMode == TouchActivationMode.HOVER) {
                        hoveredButtons.remove(pointerId);
                    }
                    handled = true;
                }
                break;
            }
        }
        return handled;
    }

    private boolean processElementsTouchMove(int pointerId, float x, float y) {
        for (ControlElement element : profile.getElements()) {
            if (element.handleTouchMove(pointerId, x, y)) return true;
        }
        return false;
    }

    private void resetTouchscreenTimeout() {
        Log.d("InputControlsView", "Touch detected, resetting timeout.");
        if (timeoutHandler != null && hideControlsRunnable != null) {
            timeoutHandler.removeCallbacks(hideControlsRunnable);
            timeoutHandler.postDelayed(hideControlsRunnable, 5000); // Adjust timeout as necessary
        }
    }

    public boolean onKeyEvent(KeyEvent event) {
        if (profile != null && event.getRepeatCount() == 0) {
            ExternalController controller = profile.getController(event.getDeviceId());
            
            if (controller != null) {
                ExternalControllerBinding controllerBinding = controller.getControllerBinding(event.getKeyCode());
                
                if (controllerBinding != null) {
                    int action = event.getAction();

                    if (action == KeyEvent.ACTION_DOWN) {
                        handleInputEvent(controller, controllerBinding.getBinding(), true);
                    }
                    else if (action == KeyEvent.ACTION_UP) {
                        handleInputEvent(controller, controllerBinding.getBinding(), false);
                    }
                    return true;
                }
            }
        }
        return false;
    }

    public void handleInputEvent(Binding binding, boolean isActionDown) {
        handleInputEvent(null, binding, isActionDown, 0);
    }

    public void handleInputEvent(ExternalController controller, Binding binding, boolean isActionDown) {
        handleInputEvent(controller, binding, isActionDown, 0);
    }

    /**
     * Handle stick input with proper 2D axis management.
     * Use this for analog sticks to avoid per-direction axis conflicts.
     */
    public void handleStickInput(Binding firstBinding, float deltaX, float deltaY) {
        if (!firstBinding.isGamepad()) return;
        
        GamepadState state = profile.getGamepadState();
        WinHandler winHandler = xServer != null ? xServer.getWinHandler() : null;
        
        boolean isLeftStick = firstBinding == Binding.GAMEPAD_LEFT_THUMB_UP || 
                             firstBinding == Binding.GAMEPAD_LEFT_THUMB_DOWN ||
                             firstBinding == Binding.GAMEPAD_LEFT_THUMB_LEFT ||
                             firstBinding == Binding.GAMEPAD_LEFT_THUMB_RIGHT;
        
        if (isLeftStick) {
            state.thumbLX = deltaX;
            state.thumbLY = deltaY;
        } else {
            state.thumbRX = deltaX;
            state.thumbRY = deltaY;
        }
        
        if (winHandler != null) {
            winHandler.sendGamepadState();
        }
    }

    public void handleInputEvent(Binding binding, boolean isActionDown, float offset) {
        handleInputEvent(null, binding, isActionDown, offset);
    }

    public void handleInputEvent(ExternalController controller, Binding binding, boolean isActionDown, float offset) {
        handleInputEvent(controller, binding, isActionDown, offset, true);
    }

    public void handleInputEvent(ExternalController controller, Binding binding, boolean isActionDown, float offset, boolean sendUpdate) {
        WinHandler winHandler = xServer != null ? xServer.getWinHandler() : null;
        if (binding.isGamepad()) {
            GamepadState state = (controller != null) ? controller.remappedState : profile.getGamepadState();

            int buttonIdx = binding.ordinal() - Binding.GAMEPAD_BUTTON_A.ordinal();
            if (buttonIdx <= ExternalController.IDX_BUTTON_R2) {
                if (buttonIdx == ExternalController.IDX_BUTTON_L2)
                    state.triggerL = isActionDown ? (offset != 0 ? offset : 1.0f) : 0f;
                else if (buttonIdx == ExternalController.IDX_BUTTON_R2)
                    state.triggerR = isActionDown ? (offset != 0 ? offset : 1.0f) : 0f;
                else
                    state.setPressed(buttonIdx, isActionDown);
            }
            else if (binding == Binding.GAMEPAD_LEFT_THUMB_UP || binding == Binding.GAMEPAD_LEFT_THUMB_DOWN) {
                float val = (isActionDown && offset == 0) ? 1.0f : Math.abs(offset);
                state.thumbLY = isActionDown ? (binding == Binding.GAMEPAD_LEFT_THUMB_UP ? -val : val) : 0;
            }
            else if (binding == Binding.GAMEPAD_LEFT_THUMB_LEFT || binding == Binding.GAMEPAD_LEFT_THUMB_RIGHT) {
                float val = (isActionDown && offset == 0) ? 1.0f : Math.abs(offset);
                state.thumbLX = isActionDown ? (binding == Binding.GAMEPAD_LEFT_THUMB_LEFT ? -val : val) : 0;
            }
            else if (binding == Binding.GAMEPAD_RIGHT_THUMB_UP || binding == Binding.GAMEPAD_RIGHT_THUMB_DOWN) {
                float val = (isActionDown && offset == 0) ? 1.0f : Math.abs(offset);
                state.thumbRY = isActionDown ? (binding == Binding.GAMEPAD_RIGHT_THUMB_UP ? -val : val) : 0;
            }
            else if (binding == Binding.GAMEPAD_RIGHT_THUMB_LEFT || binding == Binding.GAMEPAD_RIGHT_THUMB_RIGHT) {
                float val = (isActionDown && offset == 0) ? 1.0f : Math.abs(offset);
                state.thumbRX = isActionDown ? (binding == Binding.GAMEPAD_RIGHT_THUMB_LEFT ? -val : val) : 0;
            }
            else if (binding == Binding.GAMEPAD_DPAD_UP || binding == Binding.GAMEPAD_DPAD_RIGHT ||
                     binding == Binding.GAMEPAD_DPAD_DOWN || binding == Binding.GAMEPAD_DPAD_LEFT) {
                state.dpad[binding.ordinal() - Binding.GAMEPAD_DPAD_UP.ordinal()] = isActionDown;
            }

            if (winHandler != null && sendUpdate) {
                if (controller != null)
                    winHandler.sendGamepadState(controller);
                else
                    winHandler.sendGamepadState();
            }
        }
        else {
            if (binding == Binding.MOUSE_MOVE_LEFT || binding == Binding.MOUSE_MOVE_RIGHT) {
                mouseMoveOffset.x = isActionDown ? (offset != 0 ? offset : (binding == Binding.MOUSE_MOVE_LEFT ? -1 : 1)) : 0;
                if (isActionDown) createMouseMoveTimer();
            }
            else if (binding == Binding.MOUSE_MOVE_DOWN || binding == Binding.MOUSE_MOVE_UP) {
                mouseMoveOffset.y = isActionDown ? (offset != 0 ? offset : (binding == Binding.MOUSE_MOVE_UP ? -1 : 1)) : 0;
                if (isActionDown) createMouseMoveTimer();
            }
            else {
                Pointer.Button pointerButton = binding.getPointerButton();
                if (isActionDown) {
                    if (pointerButton != null) {
                        if (inputMode == InputMode.RELATIVE) {
                            int wheelDelta = pointerButton == Pointer.Button.BUTTON_SCROLL_UP ? MOUSE_WHEEL_DELTA : (pointerButton == Pointer.Button.BUTTON_SCROLL_DOWN ? -MOUSE_WHEEL_DELTA : 0);
                            winHandler.mouseEvent(MouseEventFlags.getFlagFor(pointerButton, true), 0, 0, wheelDelta);
                        } else {
                            xServer.injectPointerButtonPress(pointerButton);
                        }
                    }
                    else xServer.injectKeyPress(binding.keycode);
                }
                else {
                    if (pointerButton != null) {
                        if (inputMode == InputMode.RELATIVE) {
                            winHandler.mouseEvent(MouseEventFlags.getFlagFor(pointerButton, false), 0, 0, 0);
                        } else {
                            xServer.injectPointerButtonRelease(pointerButton);
                        }
                    }
                    else xServer.injectKeyRelease(binding.keycode);
                }
            }
        }
    }

    public Bitmap getIcon(byte id) {
        if (icons[id] == null) {
            Context context = getContext();
            try (InputStream is = context.getAssets().open("inputcontrols/icons/"+id+".png")) {
                icons[id] = BitmapFactory.decodeStream(is);
            }
            catch (IOException e) {}
        }
        return icons[id];
    }
}
