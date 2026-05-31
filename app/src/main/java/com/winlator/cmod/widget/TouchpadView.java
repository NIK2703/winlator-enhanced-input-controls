package com.winlator.cmod.widget;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.StateListDrawable;
import android.util.Log;
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
import com.winlator.cmod.inputcontrols.InputMode;
import com.winlator.cmod.inputcontrols.MouseMode;
import com.winlator.cmod.math.Mathf;
import com.winlator.cmod.math.XForm;
import com.winlator.cmod.renderer.ViewTransformation;
import com.winlator.cmod.winhandler.MouseEventFlags;
import com.winlator.cmod.winhandler.WinHandler;
import com.winlator.cmod.xserver.Pointer;
import com.winlator.cmod.xserver.XServer;

public class TouchpadView extends View {
    private static final byte MAX_FINGERS = 4;
    private static final short MAX_TWO_FINGERS_SCROLL_DISTANCE = 350;
    public static final byte MAX_TAP_TRAVEL_DISTANCE = 10;
    public static final short MAX_TAP_MILLISECONDS = 200;
    public static final float CURSOR_ACCELERATION = 1.25f;
    public static final byte CURSOR_ACCELERATION_THRESHOLD = 6;
    private final Finger[] fingers = new Finger[MAX_FINGERS];
    private byte numFingers = 0;
    private float sensitivity = 1.0f;
    private boolean pointerButtonLeftEnabled = true;
    private boolean pointerButtonRightEnabled = true;
    private Finger fingerPointerButtonLeft;
    private Finger fingerPointerButtonRight;
    private float scrollAccumY = 0;
    private boolean scrolling = false;
    private final XServer xServer;
    private Runnable fourFingersTapCallback;
    private final float[] xform = XForm.getInstance();
    private boolean simTouchScreen = false;
    private boolean continueClick = true;
    private int lastTouchedPosX;
    private int lastTouchedPosY;
    private static final Byte CLICK_DELAYED_TIME = 50;
    private static final Byte EFFECTIVE_TOUCH_DISTANCE = 20;
    private float resolutionScale;
    private static final int UPDATE_FORM_DELAYED_TIME = 50;
    private boolean mouseEnabled = true;

    private TouchscreenGestureHandler gestureHandler;
    private TouchpadGestureHandler touchpadGestureHandler;
    private InputControlsView inputControlsView;
    private ControlsProfile currentProfile;
    private boolean isTouchscreenMode = false;
    private int lastTransformedX;
    private int lastTransformedY;

    private Handler timeoutHandler; // Reference to the activity's timeout handler
    private Runnable hideControlsRunnable; // Runnable to hide the controls



    @SuppressLint("ResourceType")
    public TouchpadView(Context context, XServer xServer, Handler timeoutHandler, Runnable hideControlsRunnable) {
        super(context);
        this.xServer = xServer;

        this.timeoutHandler = timeoutHandler; // Store the reference to timeout handler
        this.hideControlsRunnable = hideControlsRunnable; // Store the reference to the hide controls runnable

        setLayoutParams(new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        setBackground(createTransparentBg());
        setClickable(true);
        setFocusable(true);
        setFocusableInTouchMode(false);
        setPointerIcon(PointerIcon.load(getResources(), R.drawable.hidden_pointer_arrow));
        updateXform(AppUtils.getScreenWidth(), AppUtils.getScreenHeight(), xServer.screenInfo.width, xServer.screenInfo.height);

        this.timeoutHandler = timeoutHandler;
        this.hideControlsRunnable = hideControlsRunnable;

        setOnGenericMotionListener(new OnGenericMotionListener() {
            @Override
            public boolean onGenericMotion(View v, MotionEvent event) {
                if (event.getToolType(0) == MotionEvent.TOOL_TYPE_STYLUS) {
                    return handleStylusHoverEvent(event);
                }
                return false;
            }
        });

        gestureHandler = new TouchscreenGestureHandler(this);
        touchpadGestureHandler = new TouchpadGestureHandler(this);
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

        float invAspect = 1.0f / viewTransformation.aspect;
        if (!xServer.getRenderer().isFullscreen()) {
            XForm.makeTranslation(xform, -viewTransformation.viewOffsetX, -viewTransformation.viewOffsetY);
            XForm.scale(xform, invAspect, invAspect);
        } else
            XForm.makeScale(xform, (float) innerWidth / outerWidth, (float) innerHeight / outerHeight);
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
        return currentProfile != null ? currentProfile.getInputMode() : InputMode.ABSOLUTE;
    }

    public void movePointer(float screenX, float screenY) {
        float[] transformed = XForm.transformPoint(xform, screenX, screenY);
        int tx = (int)transformed[0];
        int ty = (int)transformed[1];

        if (getCurrentInputMode() == InputMode.RELATIVE) {
            int dx = tx - lastTransformedX;
            int dy = ty - lastTransformedY;
            lastTransformedX = tx;
            lastTransformedY = ty;
            xServer.getWinHandler().mouseEvent(MouseEventFlags.MOVE, dx, dy, 0);
            updateVisibleRelativeCursor(tx, ty);
        } else {
            xServer.injectPointerMove(tx, ty);
        }
    }

    private class Finger {
        private int x;
        private int y;
        private final int startX;
        private final int startY;
        private int lastX;
        private int lastY;
        private final long touchTime;

        public Finger(float x, float y) {
            float[] transformedPoint = XForm.transformPoint(xform, x, y);
            this.x = this.startX = this.lastX = (int)transformedPoint[0];
            this.y = this.startY = this.lastY = (int)transformedPoint[1];
            touchTime = System.currentTimeMillis();
        }

        public void update(float x, float y) {
            lastX = this.x;
            lastY = this.y;
            float[] transformedPoint = XForm.transformPoint(xform, x, y);
            this.x = (int)transformedPoint[0];
            this.y = (int)transformedPoint[1];
        }

        private int deltaX() {
            float dx = (x - lastX) * sensitivity;
            if (Math.abs(dx) > CURSOR_ACCELERATION_THRESHOLD) dx *= CURSOR_ACCELERATION;
            return Mathf.roundPoint(dx);
        }

        private int deltaY() {
            float dy = (y - lastY) * sensitivity;
            if (Math.abs(dy) > CURSOR_ACCELERATION_THRESHOLD) dy *= CURSOR_ACCELERATION;
            return Mathf.roundPoint(dy);
        }

        private boolean isTap() {
            return (System.currentTimeMillis() - touchTime) < MAX_TAP_MILLISECONDS && travelDistance() < MAX_TAP_TRAVEL_DISTANCE;
        }

        private float travelDistance() {
            return (float)Math.hypot(x - startX, y - startY);
        }
    }


    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!mouseEnabled) return true;

        resetTouchscreenTimeout();

        int toolType = event.getToolType(0);

        if (toolType == MotionEvent.TOOL_TYPE_STYLUS) {
            return handleStylusEvent(event);
        } else if (isTouchscreenMode && currentProfile != null) {
            return handleTouchscreenEvent(event);
        } else {
            return handleTouchpadEvent(event);
        }
    }

    private void resetTouchscreenTimeout() {
        if (timeoutHandler != null && hideControlsRunnable != null) {
            timeoutHandler.removeCallbacks(hideControlsRunnable);
            timeoutHandler.postDelayed(hideControlsRunnable, 5000); // Adjust timeout as necessary
        }
    }
    private boolean handleStylusHoverEvent(MotionEvent event) {
        int action = event.getActionMasked();

        switch (action) {
            case MotionEvent.ACTION_HOVER_ENTER:
                Log.d("StylusEvent", "Hover Enter");
                break;
            case MotionEvent.ACTION_HOVER_MOVE:
                Log.d("StylusEvent", "Hover Move: (" + event.getX() + ", " + event.getY() + ")");
                float[] transformedPoint = XForm.transformPoint(xform, event.getX(), event.getY());
                xServer.injectPointerMove((int) transformedPoint[0], (int) transformedPoint[1]);
                break;
            case MotionEvent.ACTION_HOVER_EXIT:
                Log.d("StylusEvent", "Hover Exit");
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



    private boolean handleTouchpadEvent(MotionEvent event) {
        int actionIndex = event.getActionIndex();
        int pointerId = event.getPointerId(actionIndex);
        int actionMasked = event.getActionMasked();
        if (pointerId >= MAX_FINGERS) return true;

        switch (actionMasked) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                if (event.isFromSource(InputDevice.SOURCE_MOUSE)) return true;
                scrollAccumY = 0;
                scrolling = false;
                fingers[pointerId] = new Finger(event.getX(actionIndex), event.getY(actionIndex));
                numFingers++;
                if (simTouchScreen) {
                    final Runnable clickDelay = () -> {
                        if (continueClick) {
                            xServer.injectPointerMove(lastTouchedPosX, lastTouchedPosY);
                            xServer.injectPointerButtonPress(Pointer.Button.BUTTON_LEFT);
                        }
                    };
                    if (pointerId == 0) {
                        continueClick = true;
                        if (Math.hypot(fingers[0].x - lastTouchedPosX, fingers[0].y - lastTouchedPosY) * resolutionScale > EFFECTIVE_TOUCH_DISTANCE) {
                            lastTouchedPosX = fingers[0].x;
                            lastTouchedPosY = fingers[0].y;
                        }
                        postDelayed(clickDelay, CLICK_DELAYED_TIME);
                    } else if (pointerId == 1) {
                        if (numFingers < 2) {
                            continueClick = true;
                            if (Math.hypot(fingers[1].x - lastTouchedPosX, fingers[1].y - lastTouchedPosY) * resolutionScale > EFFECTIVE_TOUCH_DISTANCE) {
                                lastTouchedPosX = fingers[1].x;
                                lastTouchedPosY = fingers[1].y;
                            }
                            postDelayed(clickDelay, CLICK_DELAYED_TIME);
                        } else
                            continueClick = System.currentTimeMillis() - fingers[0].touchTime > CLICK_DELAYED_TIME;
                    }
                }
                else if (touchpadGestureHandler != null && !isTouchscreenMode) {
                    touchpadGestureHandler.onFingerDown(pointerId, fingers[pointerId].x, fingers[pointerId].y);
                }
                break;
            case MotionEvent.ACTION_MOVE:
                if (event.isFromSource(InputDevice.SOURCE_MOUSE)) {
                    float[] transformedPoint = XForm.transformPoint(xform, event.getX(), event.getY());
                    if (getCurrentInputMode() == InputMode.RELATIVE) {
                        xServer.getWinHandler().mouseEvent(MouseEventFlags.MOVE, (int)transformedPoint[0], (int)transformedPoint[1], 0);
                        updateVisibleRelativeCursor((int) transformedPoint[0], (int) transformedPoint[1]);
                    } else
                        xServer.injectPointerMove((int)transformedPoint[0], (int)transformedPoint[1]);
                } else {
                    for (byte i = 0; i < MAX_FINGERS; i++) {
                        if (fingers[i] != null) {
                            int pointerIndex = event.findPointerIndex(i);
                            if (pointerIndex >= 0) {
                                fingers[i].update(event.getX(pointerIndex), event.getY(pointerIndex));
                                handleFingerMove(fingers[i]);
                            } else {
                                handleFingerUp(fingers[i]);
                                fingers[i] = null;
                                numFingers--;
                            }
                        }
                    }
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                if (fingers[pointerId] != null) {
                    fingers[pointerId].update(event.getX(actionIndex), event.getY(actionIndex));
                    handleFingerUp(fingers[pointerId]);
                    fingers[pointerId] = null;
                    numFingers--;
                }
                break;
            case MotionEvent.ACTION_CANCEL:
                if (touchpadGestureHandler != null) touchpadGestureHandler.reset();
                for (byte i = 0; i < MAX_FINGERS; i++) fingers[i] = null;
                numFingers = 0;
                break;
        }

        return true;
    }

    private boolean handleTouchscreenEvent(MotionEvent event) {
        if (gestureHandler != null) {
            gestureHandler.onTouchEvent(event);
            return true;
        }
        // Fallback: old logic if gestureHandler not initialized
        int action = event.getActionMasked();

        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                handleTouchDown(event);
                break;
            case MotionEvent.ACTION_MOVE:
                if (event.getPointerCount() == 2) {
                    handleTwoFingerScroll(event);
                } else {
                    handleTouchMove(event);
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                if (event.getPointerCount() == 2) {
                    handleTwoFingerTap(event);
                } else {
                    handleTouchUp(event);
                }
                break;
            case MotionEvent.ACTION_CANCEL:
                if (getCurrentInputMode() == InputMode.RELATIVE) {
                    xServer.getWinHandler().mouseEvent(MouseEventFlags.LEFTUP, 0, 0, 0);
                    xServer.getWinHandler().mouseEvent(MouseEventFlags.RIGHTUP, 0, 0, 0);
                }
                else {
                    xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_LEFT);
                    xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_RIGHT);
                }
                break;
        }
        return true;
    }

    private void handleTouchDown(MotionEvent event) {
        float[] transformedPoint = XForm.transformPoint(xform, event.getX(), event.getY());
        if (getCurrentInputMode() == InputMode.RELATIVE) {
            xServer.getWinHandler().mouseEvent(MouseEventFlags.MOVE, (int)transformedPoint[0], (int)transformedPoint[1], 0);
            updateVisibleRelativeCursor((int) transformedPoint[0], (int) transformedPoint[1]);
        } else
            xServer.injectPointerMove((int) transformedPoint[0], (int) transformedPoint[1]);

        if (event.getPointerCount() == 1) {
            if (getCurrentInputMode() == InputMode.RELATIVE)
                xServer.getWinHandler().mouseEvent(MouseEventFlags.LEFTDOWN, 0, 0, 0);
            else
                xServer.injectPointerButtonPress(Pointer.Button.BUTTON_LEFT);
        }
    }

    private void handleTouchMove(MotionEvent event) {
        float[] transformedPoint = XForm.transformPoint(xform, event.getX(), event.getY());
        if (getCurrentInputMode() == InputMode.RELATIVE) {
            xServer.getWinHandler().mouseEvent(MouseEventFlags.MOVE, (int)transformedPoint[0], (int)transformedPoint[1], 0);
            updateVisibleRelativeCursor((int) transformedPoint[0], (int) transformedPoint[1]);
        } else
            xServer.injectPointerMove((int) transformedPoint[0], (int) transformedPoint[1]);
    }

    private void handleTouchUp(MotionEvent event) {
        if (getCurrentInputMode() == InputMode.RELATIVE)
            xServer.getWinHandler().mouseEvent(MouseEventFlags.LEFTUP, 0, 0, 0);
        else
            xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_LEFT);
    }

    private void handleTwoFingerScroll(MotionEvent event) {
        float scrollDistance = event.getY(0) - event.getY(1);
        if (Math.abs(scrollDistance) > 10) {
            if (scrollDistance > 0) {
                xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_UP);
                xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_UP);
            } else {
                xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_DOWN);
                xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_DOWN);
            }
        }
    }

    private void handleTwoFingerTap(MotionEvent event) {
        if (event.getPointerCount() == 2) {
            if (getCurrentInputMode() == InputMode.RELATIVE) {
                xServer.getWinHandler().mouseEvent(MouseEventFlags.RIGHTDOWN, 0, 0, 0);
                xServer.getWinHandler().mouseEvent(MouseEventFlags.RIGHTUP, 0, 0, 0);
            }
            else {
                xServer.injectPointerButtonPress(Pointer.Button.BUTTON_RIGHT);
                xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_RIGHT);
            }
        }
    }



    private void handleFingerUp(Finger finger1) {
        int pointerId = findPointerId(finger1);
        boolean gestureHandlerActive = !simTouchScreen && touchpadGestureHandler != null;
        boolean handledByGesture = false;


        if (gestureHandlerActive) {
            int mainId = touchpadGestureHandler.getMainPointerId();
            if (pointerId >= 0 && pointerId == mainId) {
                if (finger1.isTap() || !touchpadGestureHandler.isInTapWaiting()) {
                    touchpadGestureHandler.onFingerUp(pointerId);
                    handledByGesture = true;
                }
                else {
                    touchpadGestureHandler.reset();
                }
            }
            else if (pointerId >= 0) {
                touchpadGestureHandler.onFingerUp(pointerId);
                handledByGesture = true;
            }
        }

        if (!handledByGesture) {
            switch (numFingers) {
                case 1:
                    if (simTouchScreen) {
                        final Runnable clickDelay = () -> {
                            if (continueClick)
                                xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_LEFT);
                        };
                        postDelayed(clickDelay, CLICK_DELAYED_TIME);
                    }
                    else if (!gestureHandlerActive && finger1.isTap()) {
                        pressPointerButtonLeft(finger1);
                    }
                    break;
                case 2:
                    if (!gestureHandlerActive) {
                        Finger finger2 = findSecondFinger(finger1);
                        if (finger2 != null && finger1.isTap()) pressPointerButtonRight(finger1);
                    }
                    break;
                case 4:
                    if (fourFingersTapCallback != null) {
                        for (byte i = 0; i < 4; i++) {
                            if (fingers[i] != null && !fingers[i].isTap()) return;
                        }
                        fourFingersTapCallback.run();
                    }
                    break;
            }
        }

        releasePointerButtonLeft(finger1);
        releasePointerButtonRight(finger1);
    }

    private void handleFingerMove(Finger finger1) {
        boolean skipPointerMove = false;
        boolean isSecondFinger = numFingers == 2 && fingers[0] != null && finger1 != fingers[0];

        boolean gestureHandlesSecond = touchpadGestureHandler != null && touchpadGestureHandler.isSecondFingerActive();
        Finger finger2 = numFingers == 2 ? findSecondFinger(finger1) : null;
        if (finger2 != null && !isSecondFinger && !gestureHandlesSecond) {
            final float resolutionScale = 1000.0f / Math.min(xServer.screenInfo.width, xServer.screenInfo.height);
            float currDistance = (float)Math.hypot(finger1.x - finger2.x, finger1.y - finger2.y) * resolutionScale;

            if (currDistance < MAX_TWO_FINGERS_SCROLL_DISTANCE) {
                scrollAccumY += ((finger1.y + finger2.y) * 0.5f) - (finger1.lastY + finger2.lastY) * 0.5f;

                if (scrollAccumY < -100) {
                    xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_DOWN);
                    xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_DOWN);
                    scrollAccumY = 0;
                }
                else if (scrollAccumY > 100) {
                    xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_UP);
                    xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_UP);
                    scrollAccumY = 0;
                }
                scrolling = true;
            }
            else if (currDistance >= MAX_TWO_FINGERS_SCROLL_DISTANCE && !xServer.pointer.isButtonPressed(Pointer.Button.BUTTON_LEFT) &&
                     finger2.travelDistance() < MAX_TAP_TRAVEL_DISTANCE) {
                pressPointerButtonLeft(finger1);
                skipPointerMove = true;
            }
        }

        // Only first finger controls pointer movement
        if (isSecondFinger) {
            skipPointerMove = true;
        }

        if (!scrolling && numFingers <= 2 && !skipPointerMove) {
            int dx = finger1.deltaX();
            int dy = finger1.deltaY();

            if (simTouchScreen) {
                if (System.currentTimeMillis() - finger1.touchTime > CLICK_DELAYED_TIME)
                    xServer.injectPointerMove(finger1.x, finger1.y);
            }
            else if (getCurrentInputMode() == InputMode.RELATIVE) {
                WinHandler winHandler = xServer.getWinHandler();
                winHandler.mouseEvent(MouseEventFlags.MOVE, dx, dy, 0);
                updateVisibleRelativeCursor(xServer.pointer.getX() + dx, xServer.pointer.getY() + dy);
            }
            else xServer.injectPointerMoveDelta(dx, dy);
        }

        // Notify touchpad gesture handler of finger movement (drag detection)
        if (touchpadGestureHandler != null) {
            int pointerId = findPointerId(finger1);
            if (pointerId >= 0) {
                touchpadGestureHandler.onFingerMove(pointerId, finger1.x, finger1.y);
            }
        }
    }

    private Finger findSecondFinger(Finger finger) {
        for (byte i = 0; i < MAX_FINGERS; i++) {
            if (fingers[i] != null && fingers[i] != finger) return fingers[i];
        }
        return null;
    }

    private int findPointerId(Finger finger) {
        for (int i = 0; i < MAX_FINGERS; i++) {
            if (fingers[i] == finger) return i;
        }
        return -1;
    }

    private void pressPointerButtonLeft(Finger finger) {
        if (pointerButtonLeftEnabled && !xServer.pointer.isButtonPressed(Pointer.Button.BUTTON_LEFT)) {
            xServer.injectPointerButtonPress(Pointer.Button.BUTTON_LEFT);
            fingerPointerButtonLeft = finger;
        }
    }

    private void pressPointerButtonRight(Finger finger) {
        if (pointerButtonRightEnabled && !xServer.pointer.isButtonPressed(Pointer.Button.BUTTON_RIGHT)) {
            xServer.injectPointerButtonPress(Pointer.Button.BUTTON_RIGHT);
            fingerPointerButtonRight = finger;
        }
    }

    private void releasePointerButtonLeft(final Finger finger) {
        if (pointerButtonLeftEnabled && finger == fingerPointerButtonLeft && xServer.pointer.isButtonPressed(Pointer.Button.BUTTON_LEFT)) {
            postDelayed(() -> {
                xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_LEFT);
                fingerPointerButtonLeft = null;
            }, 30);
        }
    }

    private void releasePointerButtonRight(final Finger finger) {
        if (pointerButtonRightEnabled && finger == fingerPointerButtonRight && xServer.pointer.isButtonPressed(Pointer.Button.BUTTON_RIGHT)) {
            postDelayed(() -> {
                xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_RIGHT);
                fingerPointerButtonRight = null;
            }, 30);
        }
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

    public boolean onExternalMouseEvent(MotionEvent event) {
        boolean handled = false;
        if (event.isFromSource(InputDevice.SOURCE_MOUSE)) {
            int actionButton = event.getActionButton();
            switch (event.getAction()) {
                case MotionEvent.ACTION_BUTTON_PRESS:
                    if (actionButton == MotionEvent.BUTTON_PRIMARY) {
                        if (getCurrentInputMode() == InputMode.RELATIVE)
                            xServer.getWinHandler().mouseEvent(MouseEventFlags.LEFTDOWN, 0, 0, 0);
                        else
                            xServer.injectPointerButtonPress(Pointer.Button.BUTTON_LEFT);
                    } else if (actionButton == MotionEvent.BUTTON_SECONDARY) {
                        if (getCurrentInputMode() == InputMode.RELATIVE)
                            xServer.getWinHandler().mouseEvent(MouseEventFlags.RIGHTDOWN, 0, 0, 0);
                        else
                            xServer.injectPointerButtonPress(Pointer.Button.BUTTON_RIGHT);
                    } else if (actionButton == MotionEvent.BUTTON_TERTIARY) {
                        if (getCurrentInputMode() == InputMode.RELATIVE)
                            xServer.getWinHandler().mouseEvent(MouseEventFlags.MIDDLEDOWN, 0, 0, 0);
                        else
                            xServer.injectPointerButtonPress(Pointer.Button.BUTTON_MIDDLE); // Add this line for middle mouse button press
                    }
                    handled = true;
                    break;
                case MotionEvent.ACTION_BUTTON_RELEASE:
                    if (actionButton == MotionEvent.BUTTON_PRIMARY) {
                        if (getCurrentInputMode() == InputMode.RELATIVE)
                            xServer.getWinHandler().mouseEvent(MouseEventFlags.LEFTUP, 0, 0, 0);
                        else
                            xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_LEFT);
                    } else if (actionButton == MotionEvent.BUTTON_SECONDARY) {
                        if (getCurrentInputMode() == InputMode.RELATIVE)
                            xServer.getWinHandler().mouseEvent(MouseEventFlags.RIGHTUP, 0, 0, 0);
                        else
                            xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_RIGHT);
                    } else if (actionButton == MotionEvent.BUTTON_TERTIARY) {
                        if (getCurrentInputMode() == InputMode.RELATIVE)
                            xServer.getWinHandler().mouseEvent(MouseEventFlags.MIDDLEUP, 0, 0, 0);
                        else
                            xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_MIDDLE); // Add this line for middle mouse button release
                    }
                    handled = true;
                    break;
                case MotionEvent.ACTION_MOVE:
                case MotionEvent.ACTION_HOVER_MOVE:
                    float[] transformedPoint = XForm.transformPoint(xform, event.getX(), event.getY());
                    if (getCurrentInputMode() == InputMode.RELATIVE) {
                        xServer.getWinHandler().mouseEvent(MouseEventFlags.MOVE, (int)transformedPoint[0], (int)transformedPoint[1], 0);
                        updateVisibleRelativeCursor((int) transformedPoint[0], (int) transformedPoint[1]);
                    } else
                        xServer.injectPointerMove((int)transformedPoint[0], (int)transformedPoint[1]);
                    handled = true;
                    break;
                case MotionEvent.ACTION_SCROLL:
                    float scrollY = event.getAxisValue(MotionEvent.AXIS_VSCROLL);
                    if (scrollY <= -1.0f) {
                        if (getCurrentInputMode() == InputMode.RELATIVE)
                            xServer.getWinHandler().mouseEvent(MouseEventFlags.WHEEL, 0, 0, (int)scrollY);
                        else {
                            xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_DOWN);
                            xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_DOWN);
                        }
                    } else if (scrollY >= 1.0f) {
                        if (getCurrentInputMode() == InputMode.RELATIVE)
                            xServer.getWinHandler().mouseEvent(MouseEventFlags.WHEEL, 0, 0,(int)scrollY);
                        else {
                            xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_UP);
                            xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_UP);
                        }
                    }
                    handled = true;
                    break;
            }
        }
        return handled;
    }


    private final float[] deltaResult = new float[2];

    public float[] computeDeltaPoint(float lastX, float lastY, float x, float y) {
        XForm.transformPoint(xform, lastX, lastY, deltaResult);
        lastX = deltaResult[0];
        lastY = deltaResult[1];

        XForm.transformPoint(xform, x, y, deltaResult);
        x = deltaResult[0];
        y = deltaResult[1];

        deltaResult[0] = x - lastX;
        deltaResult[1] = y - lastY;
        return deltaResult;
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
        if (gestureHandler != null) {
            gestureHandler.setInputControlsView(iv);
        }
        if (touchpadGestureHandler != null) {
            touchpadGestureHandler.setInputControlsView(iv);
        }
    }

    public void setProfile(ControlsProfile profile) {
        this.currentProfile = profile;
        this.isTouchscreenMode = (profile.getMouseMode() == MouseMode.TOUCHSCREEN);
        if (gestureHandler != null) {
            gestureHandler.applyConfig(profile);
            gestureHandler.reset();
        }
        if (touchpadGestureHandler != null) {
            touchpadGestureHandler.applyConfig(profile);
            touchpadGestureHandler.reset();
        }
    }

    public void clearProfile() {
        this.currentProfile = null;
        this.isTouchscreenMode = false;
        if (gestureHandler != null) {
            gestureHandler.reset();
        }
        if (touchpadGestureHandler != null) {
            touchpadGestureHandler.reset();
        }
    }

    public TouchscreenGestureHandler getGestureHandler() {
        return gestureHandler;
    }

    public void setSimTouchScreen(boolean simTouchScreen) {
        this.simTouchScreen = simTouchScreen;
        xServer.setSimulateTouchScreen(this.simTouchScreen);
    }

    public boolean isSimTouchScreen() {
        return simTouchScreen;
    }

    public void toggleFullscreen() {
        new Handler().postDelayed(() -> updateXform(getWidth(), getHeight(), xServer.screenInfo.width, xServer.screenInfo.height),
                UPDATE_FORM_DELAYED_TIME);
    }
    
    public void setMouseEnabled(boolean enabled) {
        this.mouseEnabled = enabled;
    }
}
