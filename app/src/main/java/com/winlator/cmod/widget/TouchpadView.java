package com.winlator.cmod.widget;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.StateListDrawable;
import android.os.Handler;

import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.PointerIcon;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import com.winlator.cmod.R;
import com.winlator.cmod.core.AppUtils;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.inputcontrols.InputDispatcher;
import com.winlator.cmod.inputcontrols.InputMode;
import com.winlator.cmod.inputcontrols.NativeTouchProcessor;
import com.winlator.cmod.inputcontrols.TouchTimeoutManager;
import com.winlator.cmod.math.CoordinateTransform;
import com.winlator.cmod.math.XForm;
import com.winlator.cmod.renderer.ViewTransformation;
import com.winlator.cmod.winhandler.MouseEventFlags;
import com.winlator.cmod.xserver.Pointer;
import com.winlator.cmod.xserver.XServer;

public class TouchpadView extends View {
    private final XServer xServer;
    private NativeTouchProcessor nativeTouchProcessor;
    private InputControlsView inputControlsView;
    private ControlsProfile currentProfile;
    private float sensitivity = 1.0f;
    private boolean mouseEnabled = true;
    private boolean simTouchScreen = false;
    private float resolutionScale;
    private Runnable fourFingersTapCallback;
    private final float[] xform = XForm.getInstance();
    private boolean pointerButtonLeftEnabled = true;
    private boolean pointerButtonRightEnabled = true;

    private InputDispatcher inputDispatcher;
    private TouchTimeoutManager touchTimeoutManager;

    @SuppressLint("ResourceType")
    public TouchpadView(Context context, XServer xServer, Handler timeoutHandler, Runnable hideControlsRunnable) {
        super(context);
        this.xServer = xServer;
        this.inputDispatcher = new InputDispatcher(xServer);
        this.touchTimeoutManager = new TouchTimeoutManager(timeoutHandler, hideControlsRunnable);

        setLayoutParams(new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        setBackground(createTransparentBg());
        setClickable(true);
        setFocusable(true);
        setFocusableInTouchMode(false);
        setPointerIcon(PointerIcon.load(getResources(), R.drawable.hidden_pointer_arrow));
        updateXform(AppUtils.getScreenWidth(), AppUtils.getScreenHeight(), xServer.screenInfo.width, xServer.screenInfo.height);

        setOnGenericMotionListener(new OnGenericMotionListener() {
            @Override
            public boolean onGenericMotion(View v, MotionEvent event) {
                if (event.getPointerCount() > 0 && event.getToolType(0) == MotionEvent.TOOL_TYPE_STYLUS) {
                    return handleStylusHoverEvent(event);
                }
                return false;
            }
        });
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        updateXform(w, h, xServer.screenInfo.width, xServer.screenInfo.height);
        resolutionScale = 1000.0f / Math.min(xServer.screenInfo.width, xServer.screenInfo.height);
    }

    private void updateXform(int outerWidth, int outerHeight, int innerWidth, int innerHeight) {
        ViewTransformation viewTransformation = new ViewTransformation();
        viewTransformation.update(outerWidth, outerHeight, innerWidth, innerHeight);

        CoordinateTransform ct = new CoordinateTransform();
        ct.compute(outerWidth, outerHeight, innerWidth, innerHeight,
            xServer.getRenderer() != null && xServer.getRenderer().isFullscreen());
        ct.applyToXform(xform);
        ct.applyToNativeProcessor(nativeTouchProcessor);
    }

    public void updateVisibleRelativeCursor(int x, int y) {
        if (xServer.getRenderer() != null) {
            xServer.getRenderer().updateVisualCursorPosition(x, y);
        }
    }

    public XServer getXServer() {
        return xServer;
    }

    public InputMode getCurrentInputMode() {
        return inputDispatcher.getInputMode();
    }

    public InputDispatcher getInputDispatcher() {
        return inputDispatcher;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!mouseEnabled) return true;

        resetTouchscreenTimeout();

        int toolType = event.getPointerCount() > 0 ? event.getToolType(0) : MotionEvent.TOOL_TYPE_FINGER;
        if (toolType == MotionEvent.TOOL_TYPE_STYLUS) {
            return handleStylusEvent(event);
        }

        if (nativeTouchProcessor != null) {
            int action = event.getActionMasked();
            int actionIndex = event.getActionIndex();
            int pointerId = event.getPointerId(actionIndex);

            switch (action) {
                case MotionEvent.ACTION_DOWN:
                case MotionEvent.ACTION_POINTER_DOWN: {
                    nativeTouchProcessor.onFingerDown(pointerId, event.getX(actionIndex), event.getY(actionIndex), event.getEventTime());
                    break;
                }
                case MotionEvent.ACTION_MOVE: {
                    for (int i = 0; i < event.getPointerCount(); i++) {
                        int pid = event.getPointerId(i);
                        nativeTouchProcessor.onFingerMove(pid, event.getX(i), event.getY(i), event.getEventTime());
                    }
                    break;
                }
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_POINTER_UP: {
                    nativeTouchProcessor.onFingerUp(pointerId, event.getX(actionIndex), event.getY(actionIndex), event.getEventTime());
                    break;
                }
                case MotionEvent.ACTION_CANCEL: {
                    nativeTouchProcessor.reset();
                    break;
                }
            }
            return true;
        }

        return true;
    }

    private void resetTouchscreenTimeout() {
        touchTimeoutManager.reset();
    }

    private boolean handleStylusHoverEvent(MotionEvent event) {
        int action = event.getActionMasked();

        switch (action) {
            case MotionEvent.ACTION_HOVER_ENTER:

                break;
            case MotionEvent.ACTION_HOVER_MOVE:

                float[] transformedPoint = XForm.transformPoint(xform, event.getX(), event.getY());
                xServer.injectPointerMove((int) transformedPoint[0], (int) transformedPoint[1]);
                break;
            case MotionEvent.ACTION_HOVER_EXIT:

                break;
            default:
                return false;
        }
        return true;
    }

    private boolean handleStylusEvent(MotionEvent event) {
        int action = event.getActionMasked();
        int buttonState = event.getButtonState();

        switch (action) {
            case MotionEvent.ACTION_DOWN:
                if ((buttonState & MotionEvent.BUTTON_SECONDARY) != 0) {
                    handleStylusRightClick(event);
                } else {
                    handleStylusLeftClick(event);
                }
                break;
            case MotionEvent.ACTION_MOVE:
                handleStylusMove(event);
                break;
            case MotionEvent.ACTION_UP:
                handleStylusUp(event);
                break;
        }

        return true;
    }

    private void handleStylusLeftClick(MotionEvent event) {
        float[] transformedPoint = XForm.transformPoint(xform, event.getX(), event.getY());
        xServer.injectPointerMove((int) transformedPoint[0], (int) transformedPoint[1]);
        xServer.injectPointerButtonPress(Pointer.Button.BUTTON_LEFT);
    }

    private void handleStylusRightClick(MotionEvent event) {
        float[] transformedPoint = XForm.transformPoint(xform, event.getX(), event.getY());
        xServer.injectPointerMove((int) transformedPoint[0], (int) transformedPoint[1]);
        xServer.injectPointerButtonPress(Pointer.Button.BUTTON_RIGHT);
    }

    private void handleStylusMove(MotionEvent event) {
        float[] transformedPoint = XForm.transformPoint(xform, event.getX(), event.getY());
        xServer.injectPointerMove((int) transformedPoint[0], (int) transformedPoint[1]);
    }

    private void handleStylusUp(MotionEvent event) {
        xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_LEFT);
        xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_RIGHT);
    }

    public boolean onExternalMouseEvent(MotionEvent event) {
        boolean handled = false;
        if (event.isFromSource(InputDevice.SOURCE_MOUSE)) {
            int actionButton = event.getActionButton();
            switch (event.getAction()) {
                case MotionEvent.ACTION_BUTTON_PRESS: {
                    Pointer.Button btn = toPointerButton(actionButton);
                    if (btn != null) inputDispatcher.dispatchPointerButton(btn, true);
                    handled = true;
                    break;
                }
                case MotionEvent.ACTION_BUTTON_RELEASE: {
                    Pointer.Button btn = toPointerButton(actionButton);
                    if (btn != null) inputDispatcher.dispatchPointerButton(btn, false);
                    handled = true;
                    break;
                }
                case MotionEvent.ACTION_MOVE:
                case MotionEvent.ACTION_HOVER_MOVE: {
                    float[] transformedPoint = XForm.transformPoint(xform, event.getX(), event.getY());
                    if (inputDispatcher.getInputMode() == InputMode.RELATIVE) {
                        inputDispatcher.dispatchMouseEvent(MouseEventFlags.MOVE, (int)transformedPoint[0], (int)transformedPoint[1]);
                        updateVisibleRelativeCursor((int) transformedPoint[0], (int) transformedPoint[1]);
                    } else {
                        inputDispatcher.dispatchPointerMove((int)transformedPoint[0], (int)transformedPoint[1]);
                    }
                    handled = true;
                    break;
                }
                case MotionEvent.ACTION_SCROLL: {
                    float scrollY = event.getAxisValue(MotionEvent.AXIS_VSCROLL);
                    inputDispatcher.dispatchScroll(scrollY);
                    handled = true;
                    break;
                }
            }
        }
        return handled;
    }

    private static Pointer.Button toPointerButton(int actionButton) {
        if (actionButton == MotionEvent.BUTTON_PRIMARY) return Pointer.Button.BUTTON_LEFT;
        if (actionButton == MotionEvent.BUTTON_SECONDARY) return Pointer.Button.BUTTON_RIGHT;
        if (actionButton == MotionEvent.BUTTON_TERTIARY) return Pointer.Button.BUTTON_MIDDLE;
        return null;
    }

    private StateListDrawable createTransparentBg() {
        StateListDrawable stateListDrawable = new StateListDrawable();
        ColorDrawable focusedDrawable = new ColorDrawable(Color.TRANSPARENT);
        ColorDrawable defaultDrawable = new ColorDrawable(Color.TRANSPARENT);
        stateListDrawable.addState(new int[]{android.R.attr.state_focused}, focusedDrawable);
        stateListDrawable.addState(new int[]{}, defaultDrawable);
        return stateListDrawable;
    }

    public void setInputControlsView(InputControlsView iv) {
        this.inputControlsView = iv;
    }

    public void setNativeTouchProcessor(NativeTouchProcessor p) {
        this.nativeTouchProcessor = p;
    }

    public float getResolutionScale() {
        return resolutionScale;
    }

    public void setProfile(ControlsProfile profile) {
        this.currentProfile = profile;
        if (profile != null) {
            inputDispatcher.setInputMode(profile.getInputMode());
        }
    }

    public void clearProfile() {
        this.currentProfile = null;
    }

    public void setSimTouchScreen(boolean simTouchScreen) {
        this.simTouchScreen = simTouchScreen;
        xServer.setSimulateTouchScreen(this.simTouchScreen);
    }

    public boolean isSimTouchScreen() {
        return simTouchScreen;
    }

    public void setSensitivity(float sensitivity) {
        this.sensitivity = sensitivity;
    }

    public boolean isPointerButtonLeftEnabled() {
        return pointerButtonLeftEnabled;
    }

    public void setPointerButtonLeftEnabled(boolean pointerButtonLeftEnabled) {
        this.pointerButtonLeftEnabled = pointerButtonLeftEnabled;
    }

    public boolean isPointerButtonRightEnabled() {
        return pointerButtonRightEnabled;
    }

    public void setPointerButtonRightEnabled(boolean pointerButtonRightEnabled) {
        this.pointerButtonRightEnabled = pointerButtonRightEnabled;
    }

    public void setFourFingersTapCallback(Runnable fourFingersTapCallback) {
        this.fourFingersTapCallback = fourFingersTapCallback;
    }

    public void toggleFullscreen() {
        new Handler().postDelayed(() -> updateXform(getWidth(), getHeight(), xServer.screenInfo.width, xServer.screenInfo.height), 50);
    }

    public void setMouseEnabled(boolean enabled) {
        this.mouseEnabled = enabled;
    }
}
