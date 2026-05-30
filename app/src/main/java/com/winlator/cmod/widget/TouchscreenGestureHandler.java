package com.winlator.cmod.widget;

import android.content.Context;
import android.os.Build;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.MotionEvent;

import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.inputcontrols.DragMode;
import com.winlator.cmod.inputcontrols.InputMode;
import com.winlator.cmod.inputcontrols.SecondFingerMode;

public class TouchscreenGestureHandler {
    private static final byte TAP_TRAVEL_THRESHOLD = 10;

    private enum GestureState { IDLE, TAP_WAITING, DOUBLE_TAP_WAITING, LONG_PRESSING, DRAGGING }

    private GestureState state = GestureState.IDLE;

    // Dependencies
    private final TouchpadView touchpadView;
    private InputControlsView inputControlsView;

    // Configuration (from profile)
    private InputMode inputMode = InputMode.ABSOLUTE;
    private DragMode dragMode = DragMode.AUTO;
    private Binding singleTapAction = Binding.MOUSE_LEFT_BUTTON;
    private Binding longPressAction = Binding.MOUSE_LEFT_BUTTON;
    private Binding doubleTapAction = Binding.NONE;
    private Binding singleTap2ndFingerAction = Binding.MOUSE_RIGHT_BUTTON;
    private Binding longPress2ndFingerAction = Binding.MOUSE_RIGHT_BUTTON;
    private Binding doubleTap2ndFingerAction = Binding.NONE;
    private Binding singleTapDragAction = Binding.NONE;
    private Binding longPressDragAction = Binding.NONE;
    private Binding doubleTapDragAction = Binding.NONE;
    private Binding singleTap2ndFingerDragAction = Binding.NONE;
    private Binding longPress2ndFingerDragAction = Binding.NONE;
    private Binding doubleTap2ndFingerDragAction = Binding.NONE;
    private int doubleTapTimeout = 200;
    private int longPressTimeout = 400;
    private SecondFingerMode secondFingerMode = SecondFingerMode.SECOND_TAP_ACTIONS;

    // Gesture state
    private float fingerDownX;
    private float fingerDownY;
    private int mainPointerId = -1;
    private boolean secondFingerActive;
    private int originalPointerId = -1;
    private Binding deferredTapAction;
    // True when deferredTapAction was set by the second finger
    private boolean deferredSecondFingerTap;

    // Held action tracking
    private Binding activeAction;
    private boolean isActionHeld;

    // Set to true after a double tap is confirmed (subsequent drag should use doubleTapDragAction)
    private boolean postDoubleTapDrag;
    // Deferred double-tap action — fires on UP (or cancelled on drag)
    private Binding pendingDoubleTapAction;

    // Timer callback references
    private final Runnable longPressRunnable = this::onLongPressTimer;
    private final Runnable doubleTapRunnable = this::onDoubleTapTimer;

    public TouchscreenGestureHandler(TouchpadView touchpadView) {
        this.touchpadView = touchpadView;
    }

    public void setInputControlsView(InputControlsView inputControlsView) {
        this.inputControlsView = inputControlsView;
    }

    public void applyConfig(ControlsProfile profile) {
        this.inputMode = profile.getInputMode();
        this.dragMode = profile.getDragMode();
        this.singleTapAction = profile.getSingleTapAction();
        this.longPressAction = profile.getLongPressAction();
        this.doubleTapAction = profile.getDoubleTapAction();
        this.singleTap2ndFingerAction = profile.getSingleTap2ndFingerAction();
        this.longPress2ndFingerAction = profile.getLongPress2ndFingerAction();
        this.doubleTap2ndFingerAction = profile.getDoubleTap2ndFingerAction();
        this.singleTapDragAction = profile.getSingleTapDragAction();
        this.longPressDragAction = profile.getLongPressDragAction();
        this.doubleTapDragAction = profile.getDoubleTapDragAction();
        this.singleTap2ndFingerDragAction = profile.getSingleTap2ndFingerDragAction();
        this.longPress2ndFingerDragAction = profile.getLongPress2ndFingerDragAction();
        this.doubleTap2ndFingerDragAction = profile.getDoubleTap2ndFingerDragAction();
        this.doubleTapTimeout = profile.getDoubleTapTimeout();
        this.longPressTimeout = profile.getLongPressTimeout();
        this.secondFingerMode = profile.getSecondFingerMode();
    }

    public void onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        int actionIndex = event.getActionIndex();
        int pointerId = event.getPointerId(actionIndex);

        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                handlePointerDown(event, pointerId, actionIndex);
                break;
            case MotionEvent.ACTION_MOVE:
                handlePointerMove(event);
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                handlePointerUp(event, pointerId, actionIndex);
                break;
            case MotionEvent.ACTION_CANCEL:
                handleCancel();
                break;
        }
    }

    public void reset() {
        removeAllCallbacks();
        releaseHeldAction();
        state = GestureState.IDLE;
        mainPointerId = -1;
        secondFingerActive = false;
        originalPointerId = -1;
        deferredTapAction = null;
        deferredSecondFingerTap = false;
        postDoubleTapDrag = false;
        pendingDoubleTapAction = null;
    }

    // --- Down handling ---

    private void handlePointerDown(MotionEvent event, int pointerId, int actionIndex) {
        postDoubleTapDrag = false;
        pendingDoubleTapAction = null;
        if (mainPointerId < 0) {
            // If first finger is on screen as modifier, new touch is the second finger
            if (originalPointerId >= 0 && pointerId != originalPointerId) {
                float sx = event.getX(actionIndex);
                float sy = event.getY(actionIndex);
                touchpadView.movePointer(sx, sy);

                if (state == GestureState.DOUBLE_TAP_WAITING) {
                    int savedOriginalPointerId = originalPointerId;
                    handleDoubleTapConfirmed();
                    originalPointerId = savedOriginalPointerId;
                    mainPointerId = pointerId;
                    fingerDownX = sx;
                    fingerDownY = sy;
                    secondFingerActive = true;
                    state = GestureState.TAP_WAITING;
                    return;
                }

                mainPointerId = pointerId;
                fingerDownX = sx;
                fingerDownY = sy;
                secondFingerActive = true;

                touchpadView.removeCallbacks(longPressRunnable);
                touchpadView.removeCallbacks(doubleTapRunnable);

                touchpadView.postDelayed(longPressRunnable, longPressTimeout);
                state = GestureState.TAP_WAITING;
                return;
            }

            mainPointerId = pointerId;
            fingerDownX = event.getX(actionIndex);
            fingerDownY = event.getY(actionIndex);
            secondFingerActive = false;

            touchpadView.removeCallbacks(doubleTapRunnable);
            touchpadView.removeCallbacks(longPressRunnable);

            if (state == GestureState.DOUBLE_TAP_WAITING) {
                int savedOriginalPointerId = originalPointerId;
                boolean wasSecondFingerDeferred = deferredSecondFingerTap;
                handleDoubleTapConfirmed();
                originalPointerId = savedOriginalPointerId;
                if (savedOriginalPointerId >= 0) {
                    mainPointerId = savedOriginalPointerId;
                    secondFingerActive = false;
                    movePointerToTapPoint();
                    state = GestureState.TAP_WAITING;
                    return;
                }
                mainPointerId = pointerId;
                fingerDownX = event.getX(actionIndex);
                fingerDownY = event.getY(actionIndex);
                if (wasSecondFingerDeferred) {
                    secondFingerActive = true;
                }
            }

            movePointerToTapPoint();

            if (secondFingerMode == SecondFingerMode.LONG_TAP_ACTION) {
                touchpadView.postDelayed(longPressRunnable, longPressTimeout);
            }
            state = GestureState.TAP_WAITING;
        }
        else if (pointerId != mainPointerId) {
            if (secondFingerMode == SecondFingerMode.LONG_TAP_ACTION) {
                return;
            }

            float sx = event.getX(actionIndex);
            float sy = event.getY(actionIndex);
            touchpadView.movePointer(sx, sy);

            if (state == GestureState.DOUBLE_TAP_WAITING) {
                int savedOriginalPointerId = originalPointerId;
                handleDoubleTapConfirmed();
                originalPointerId = savedOriginalPointerId;
                if (pendingDoubleTapAction != null) {
                    executeAction(pendingDoubleTapAction);
                    pendingDoubleTapAction = null;
                }
                return;
            }

            originalPointerId = mainPointerId;
            mainPointerId = pointerId;
            fingerDownX = sx;
            fingerDownY = sy;
            secondFingerActive = true;

            touchpadView.removeCallbacks(longPressRunnable);
            touchpadView.removeCallbacks(doubleTapRunnable);
            if (isActionHeld) {
                releaseHeldAction();
            }

            touchpadView.postDelayed(longPressRunnable, longPressTimeout);
            state = GestureState.TAP_WAITING;
        }
    }

    // --- Move handling ---

    private void handlePointerMove(MotionEvent event) {
        if (state == GestureState.DOUBLE_TAP_WAITING || state == GestureState.IDLE) return;

        int mainIndex = event.findPointerIndex(mainPointerId);
        if (mainIndex < 0) return;

        float cx = event.getX(mainIndex);
        float cy = event.getY(mainIndex);
        float distance = (float) Math.hypot(cx - fingerDownX, cy - fingerDownY);

        if (distance > TAP_TRAVEL_THRESHOLD) {
            touchpadView.removeCallbacks(longPressRunnable);

            if (state == GestureState.TAP_WAITING || state == GestureState.LONG_PRESSING) {
                Binding dragBinding = resolveDragAction();
                boolean shouldDrag = false;

                if (dragMode == DragMode.ALWAYS) {
                    shouldDrag = true;
                }
                else if (dragMode == DragMode.AUTO && dragBinding != null &&
                         (dragBinding == Binding.MOUSE_LEFT_BUTTON ||
                          dragBinding == Binding.MOUSE_RIGHT_BUTTON ||
                          dragBinding == Binding.MOUSE_MIDDLE_BUTTON)) {
                    shouldDrag = true;
                }

                if (shouldDrag && dragBinding != null && dragBinding != Binding.NONE) {
                    releaseHeldAction();
                    executeActionAndHold(dragBinding);
                    pendingDoubleTapAction = null;
                }

                state = GestureState.DRAGGING;
            }

            if (state == GestureState.DRAGGING) {
                touchpadView.movePointer(cx, cy);
            }
        }
    }

    // --- Up handling ---

    private void handlePointerUp(MotionEvent event, int pointerId, int actionIndex) {
        boolean wasPostDoubleTapDrag = postDoubleTapDrag;
        postDoubleTapDrag = false;
        if (pointerId == mainPointerId) {
            touchpadView.removeCallbacks(longPressRunnable);

            switch (state) {
                case TAP_WAITING:
                    if (wasPostDoubleTapDrag) {
                        if (pendingDoubleTapAction != null) {
                            executeAction(pendingDoubleTapAction);
                            pendingDoubleTapAction = null;
                        }
                        state = GestureState.IDLE;
                        secondFingerActive = false;
                        mainPointerId = -1;
                    }
                    else {
                        handleTapUp();
                        secondFingerActive = false;
                        mainPointerId = -1;
                    }
                    break;
                case LONG_PRESSING:
                    if (isActionHeld) {
                        releaseHeldAction();
                    }
                    else {
                        Binding action = secondFingerActive ? longPress2ndFingerAction : longPressAction;
                        if (action != null && action != Binding.NONE) {
                            executeAction(action);
                        }
                    }
                    if (secondFingerActive) {
                        secondFingerActive = false;
                    }
                    state = GestureState.IDLE;
                    mainPointerId = -1;
                    break;
                case DRAGGING:
                    releaseHeldAction();
                    if (secondFingerActive) {
                        secondFingerActive = false;
                        mainPointerId = -1;
                        state = GestureState.IDLE;
                    }
                    else {
                        state = GestureState.IDLE;
                        mainPointerId = -1;
                    }
                    break;
                case DOUBLE_TAP_WAITING:
                    deferredTapAction = null;
                    executeAction(resolveTapAction());
                    state = GestureState.IDLE;
                    mainPointerId = -1;
                    secondFingerActive = false;
                    originalPointerId = -1;
                    break;
                default:
                    deferredTapAction = null;
                    state = GestureState.IDLE;
                    mainPointerId = -1;
                    secondFingerActive = false;
                    originalPointerId = -1;
                    break;
            }
        }
        else if (originalPointerId >= 0 && pointerId == originalPointerId) {
            if (secondFingerActive) {
                originalPointerId = -1;
            }
            else {
                removeAllCallbacks();
                releaseHeldAction();
                deferredTapAction = null;
                state = GestureState.IDLE;
                mainPointerId = -1;
                secondFingerActive = false;
                originalPointerId = -1;
            }
        }
    }

    private void handleTapUp() {
        Binding singleBinding = secondFingerActive ? singleTap2ndFingerAction : singleTapAction;
        Binding doubleBinding = secondFingerActive ? doubleTap2ndFingerAction : doubleTapAction;
        deferredSecondFingerTap = secondFingerActive;

        if (doubleBinding != Binding.NONE) {
            deferredTapAction = singleBinding;
            touchpadView.postDelayed(doubleTapRunnable, doubleTapTimeout);
            state = GestureState.DOUBLE_TAP_WAITING;
        }
        else {
            fireSingleTap(singleBinding);
            touchpadView.postDelayed(doubleTapRunnable, doubleTapTimeout);
            state = GestureState.DOUBLE_TAP_WAITING;
        }
    }

    private void fireSingleTap(Binding binding) {
        executeAction(binding);
    }

    private void handleCancel() {
        reset();
    }

    // --- Timer callbacks ---

    private void onLongPressTimer() {
        if (state != GestureState.TAP_WAITING) return;

        Binding action = secondFingerActive ? longPress2ndFingerAction : longPressAction;
        Binding dragAction = secondFingerActive ? longPress2ndFingerDragAction : longPressDragAction;

        // If a separate drag action is configured, defer the long press action
        // to avoid firing both on drag. Execute it only on UP if no drag follows.
        if (dragAction == Binding.NONE && action != null && action != Binding.NONE &&
            !action.isMouseMove() &&
            action != Binding.MOUSE_SCROLL_UP &&
            action != Binding.MOUSE_SCROLL_DOWN) {
            executeActionAndHold(action);
        }
        state = GestureState.LONG_PRESSING;

        // Modern linear-motor haptic feedback for long press
        Context context = touchpadView.getContext();
        if (context != null) {
            Vibrator vibrator = (Vibrator) context.getSystemService(Context.VIBRATOR_SERVICE);
            if (vibrator != null && vibrator.hasVibrator()) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    vibrator.vibrate(VibrationEffect.createPredefined(VibrationEffect.EFFECT_TICK));
                }
                else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    vibrator.vibrate(VibrationEffect.createOneShot(30, 255));
                }
            }
        }
    }

    private void onDoubleTapTimer() {
        if (state != GestureState.DOUBLE_TAP_WAITING) return;
        if (deferredTapAction != null) {
            executeAction(deferredTapAction);
            deferredTapAction = null;
        }
        deferredSecondFingerTap = false;
        state = GestureState.IDLE;
    }

    private void handleDoubleTapConfirmed() {
        touchpadView.removeCallbacks(doubleTapRunnable);

        boolean wasSecondFinger = deferredSecondFingerTap;
        deferredSecondFingerTap = false;

        if (deferredTapAction != null) {
            deferredTapAction = null;
            Binding doubleBinding = wasSecondFinger ? doubleTap2ndFingerAction : doubleTapAction;
            Binding singleBinding = wasSecondFinger ? singleTap2ndFingerAction : singleTapAction;
            pendingDoubleTapAction = doubleBinding != Binding.NONE ? doubleBinding : singleBinding;
        }

        mainPointerId = -1;
        secondFingerActive = false;
        originalPointerId = -1;
        state = GestureState.IDLE;
        postDoubleTapDrag = true;
    }

    // --- Action resolution ---

    private Binding resolveTapAction() {
        return secondFingerActive ? singleTap2ndFingerAction : singleTapAction;
    }

    private Binding resolveDragAction() {
        if (state == GestureState.LONG_PRESSING) {
            if (secondFingerActive) {
                return longPress2ndFingerDragAction != Binding.NONE ? longPress2ndFingerDragAction : longPress2ndFingerAction;
            }
            else {
                return longPressDragAction != Binding.NONE ? longPressDragAction : longPressAction;
            }
        }
        else if (postDoubleTapDrag) {
            Binding dragAction = secondFingerActive ? doubleTap2ndFingerDragAction : doubleTapDragAction;
            Binding fallback = secondFingerActive ? doubleTap2ndFingerAction : doubleTapAction;
            return dragAction != Binding.NONE ? dragAction : (fallback != Binding.NONE ? fallback : resolveTapAction());
        }
        else {
            Binding fallback = resolveTapAction();
            if (secondFingerActive) {
                return singleTap2ndFingerDragAction != Binding.NONE ? singleTap2ndFingerDragAction : fallback;
            }
            else {
                return singleTapDragAction != Binding.NONE ? singleTapDragAction : fallback;
            }
        }
    }

    // --- Action execution ---

    private void movePointerToTapPoint() {
        if (mainPointerId >= 0) {
            touchpadView.movePointer(fingerDownX, fingerDownY);
        }
    }

    private void executeAction(Binding binding) {
        if (binding == null || binding == Binding.NONE) return;
        if (binding == Binding.MOUSE_SCROLL_UP || binding == Binding.MOUSE_SCROLL_DOWN) return;
        if (binding.isMouseMove()) return;

        if (inputControlsView != null) {
            inputControlsView.handleInputEvent(binding, true, 0);
            inputControlsView.handleInputEvent(binding, false, 0);
        }
    }

    private void executeActionAndHold(Binding binding) {
        if (binding == null || binding == Binding.NONE) return;
        if (binding.isMouseMove()) return;
        if (binding == Binding.MOUSE_SCROLL_UP || binding == Binding.MOUSE_SCROLL_DOWN) return;

        if (inputControlsView != null) {
            inputControlsView.handleInputEvent(binding, true, 0);
        }
        activeAction = binding;
        isActionHeld = true;
    }

    private void releaseHeldAction() {
        if (isActionHeld && activeAction != null && inputControlsView != null) {
            inputControlsView.handleInputEvent(activeAction, false, 0);
        }
        activeAction = null;
        isActionHeld = false;
    }

    private void removeAllCallbacks() {
        touchpadView.removeCallbacks(longPressRunnable);
        touchpadView.removeCallbacks(doubleTapRunnable);
    }
}
