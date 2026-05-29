package com.winlator.cmod.widget;

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
    private int doubleTapTimeout = 200;
    private int longPressTimeout = 400;
    private int tapClickDelay = 0;
    private SecondFingerMode secondFingerMode = SecondFingerMode.SECOND_TAP_ACTIONS;

    // Gesture state
    private float fingerDownX;
    private float fingerDownY;
    private int mainPointerId = -1;
    private boolean secondFingerActive;
    private int originalPointerId = -1;
    private Binding deferredTapAction;

    // Held action tracking
    private Binding activeAction;
    private boolean isActionHeld;

    // Timer callback references
    private final Runnable longPressRunnable = this::onLongPressTimer;
    private final Runnable doubleTapRunnable = this::onDoubleTapTimer;
    private Runnable pendingClickRunnable;

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
        this.doubleTapTimeout = profile.getDoubleTapTimeout();
        this.longPressTimeout = profile.getLongPressTimeout();
        this.tapClickDelay = profile.getTapClickDelay();
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
        cancelPendingClick();
        removeAllCallbacks();
        releaseHeldAction();
        state = GestureState.IDLE;
        mainPointerId = -1;
        secondFingerActive = false;
        originalPointerId = -1;
        deferredTapAction = null;
    }

    // --- Down handling ---

    private void handlePointerDown(MotionEvent event, int pointerId, int actionIndex) {
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
                    return;
                }

                mainPointerId = pointerId;
                fingerDownX = sx;
                fingerDownY = sy;
                secondFingerActive = true;

                touchpadView.removeCallbacks(longPressRunnable);
                touchpadView.removeCallbacks(doubleTapRunnable);
                cancelPendingClick();

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
            cancelPendingClick();

            if (state == GestureState.DOUBLE_TAP_WAITING) {
                int savedOriginalPointerId = originalPointerId;
                handleDoubleTapConfirmed();
                originalPointerId = savedOriginalPointerId;
                return;
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
            cancelPendingClick();

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
                Binding dragBinding = resolveTapAction();
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
                    cancelPendingClick();
                    executeActionAndHold(dragBinding);
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
        if (pointerId == mainPointerId) {
            touchpadView.removeCallbacks(longPressRunnable);

            switch (state) {
                case TAP_WAITING:
                    if (secondFingerActive) {
                        secondFingerActive = false;
                        if (doubleTap2ndFingerAction != Binding.NONE) {
                            deferredTapAction = singleTap2ndFingerAction;
                            touchpadView.postDelayed(doubleTapRunnable, doubleTapTimeout);
                            state = GestureState.DOUBLE_TAP_WAITING;
                        }
                        else {
                            fireSingleTap(singleTap2ndFingerAction);
                            touchpadView.postDelayed(doubleTapRunnable, doubleTapTimeout);
                            state = GestureState.DOUBLE_TAP_WAITING;
                        }
                        mainPointerId = -1;
                    }
                    else {
                        handleFirstFingerTapUp();
                        mainPointerId = -1;
                    }
                    break;
                case LONG_PRESSING:
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
                cancelPendingClick();
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

    private void handleFirstFingerTapUp() {
        if (doubleTapAction != Binding.NONE) {
            touchpadView.postDelayed(doubleTapRunnable, doubleTapTimeout);
            state = GestureState.DOUBLE_TAP_WAITING;
        }
        else {
            fireSingleTap(singleTapAction);
            touchpadView.postDelayed(doubleTapRunnable, doubleTapTimeout);
            state = GestureState.DOUBLE_TAP_WAITING;
        }
    }

    private long lastTapUpTime;

    private void fireSingleTap(Binding binding) {
        if (tapClickDelay > 0) {
            pendingClickRunnable = () -> {
                executeActionNow(binding);
                pendingClickRunnable = null;
            };
            touchpadView.postDelayed(pendingClickRunnable, tapClickDelay);
        }
        else {
            executeAction(binding);
        }
    }

    private void handleCancel() {
        reset();
    }

    // --- Timer callbacks ---

    private void onLongPressTimer() {
        if (state != GestureState.TAP_WAITING) return;

        Binding action = secondFingerActive ? longPress2ndFingerAction : longPressAction;
        if (action != null && action != Binding.NONE &&
            !action.isMouseMove() &&
            action != Binding.MOUSE_SCROLL_UP &&
            action != Binding.MOUSE_SCROLL_DOWN) {
            executeActionAndHold(action);
        }
        state = GestureState.LONG_PRESSING;
    }

    private void onDoubleTapTimer() {
        if (state != GestureState.DOUBLE_TAP_WAITING) return;
        if (deferredTapAction != null) {
            executeAction(deferredTapAction);
            deferredTapAction = null;
        }
        else if (doubleTapAction != Binding.NONE) {
            executeAction(singleTapAction);
        }
        state = GestureState.IDLE;
    }

    private void handleDoubleTapConfirmed() {
        cancelPendingClick();
        touchpadView.removeCallbacks(doubleTapRunnable);

        if (deferredTapAction != null) {
            deferredTapAction = null;
            if (doubleTap2ndFingerAction != Binding.NONE) {
                executeAction(doubleTap2ndFingerAction);
            }
            else {
                executeAction(singleTap2ndFingerAction);
                executeAction(singleTap2ndFingerAction);
            }
        }
        else {
            if (doubleTapAction != Binding.NONE) {
                executeAction(doubleTapAction);
            }
            else {
                executeAction(singleTapAction);
                executeAction(singleTapAction);
            }
        }

        mainPointerId = -1;
        secondFingerActive = false;
        originalPointerId = -1;
        state = GestureState.IDLE;
    }

    // --- Action resolution ---

    private Binding resolveTapAction() {
        return secondFingerActive ? singleTap2ndFingerAction : singleTapAction;
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

    private void executeActionNow(Binding binding) {
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

    private void cancelPendingClick() {
        if (pendingClickRunnable != null) {
            touchpadView.removeCallbacks(pendingClickRunnable);
            pendingClickRunnable = null;
        }
    }

    private void removeAllCallbacks() {
        touchpadView.removeCallbacks(longPressRunnable);
        touchpadView.removeCallbacks(doubleTapRunnable);
    }
}
