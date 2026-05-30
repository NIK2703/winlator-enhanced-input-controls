package com.winlator.cmod.widget;

import android.content.Context;
import android.os.Build;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.MotionEvent;

import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.inputcontrols.DragMode;
import com.winlator.cmod.inputcontrols.SecondFingerMode;

public class TouchscreenGestureHandler {
    private static final byte TAP_TRAVEL_THRESHOLD = 10;

    private enum GestureState { IDLE, TAP_WAITING, DOUBLE_TAP_WAITING, LONG_PRESSING, DRAGGING }

    private GestureState state = GestureState.IDLE;

    // Dependencies
    private final TouchpadView touchpadView;
    private InputControlsView inputControlsView;

    // Configuration (from profile)
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
    private boolean isLongTapMode;

    // Pre-resolved active bindings (set via updateActiveBindings when secondFingerActive changes)
    private Binding activeSingleTapAction;
    private Binding activeLongPressAction;
    private Binding activeDoubleTapAction;
    private Binding activeSingleTapDragAction;
    private Binding activeLongPressDragAction;
    private Binding activeDoubleTapDragAction;
    private boolean hasActiveDoubleTap;
    private boolean hasActiveLongPress;
    private boolean hasActiveLongPressDrag;
    private boolean hasActiveDoubleTapDrag;
    private boolean hasActiveSingleTapDrag;
    private boolean canHoldLongPress;

    // Gesture state
    private float fingerDownX;
    private float fingerDownY;
    private int mainPointerId = -1;
    private boolean secondFingerActive;
    private int originalPointerId = -1;
    private Binding deferredTapAction;
    private boolean deferredSecondFingerTap;

    // Held action tracking
    private Binding activeAction;
    private boolean isActionHeld;

    private boolean postDoubleTapDrag;
    private Binding pendingDoubleTapAction;

    // Stored at tap-up time for handleDoubleTapConfirmed
    private Binding pendingDeferredDoubleAction;

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
        this.isLongTapMode = secondFingerMode == SecondFingerMode.LONG_TAP_ACTION;
        updateActiveBindings();
    }

    private void setSecondFingerActive(boolean active) {
        this.secondFingerActive = active;
        updateActiveBindings();
    }

    private void updateActiveBindings() {
        activeSingleTapAction = secondFingerActive ? singleTap2ndFingerAction : singleTapAction;
        activeLongPressAction = secondFingerActive ? longPress2ndFingerAction : longPressAction;
        activeDoubleTapAction = secondFingerActive ? doubleTap2ndFingerAction : doubleTapAction;
        activeSingleTapDragAction = secondFingerActive ? singleTap2ndFingerDragAction : singleTapDragAction;
        activeLongPressDragAction = secondFingerActive ? longPress2ndFingerDragAction : longPressDragAction;
        activeDoubleTapDragAction = secondFingerActive ? doubleTap2ndFingerDragAction : doubleTapDragAction;
        hasActiveDoubleTap = activeDoubleTapAction != Binding.NONE;
        hasActiveLongPress = activeLongPressAction != null && activeLongPressAction != Binding.NONE;
        hasActiveLongPressDrag = activeLongPressDragAction != Binding.NONE;
        hasActiveDoubleTapDrag = activeDoubleTapDragAction != Binding.NONE;
        hasActiveSingleTapDrag = activeSingleTapDragAction != Binding.NONE;
        canHoldLongPress = !hasActiveLongPressDrag && hasActiveLongPress &&
            !activeLongPressAction.isMouseMove() &&
            activeLongPressAction != Binding.MOUSE_SCROLL_UP &&
            activeLongPressAction != Binding.MOUSE_SCROLL_DOWN;
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
        setSecondFingerActive(false);
        originalPointerId = -1;
        deferredTapAction = null;
        deferredSecondFingerTap = false;
        postDoubleTapDrag = false;
        pendingDoubleTapAction = null;
        pendingDeferredDoubleAction = null;
    }

    // --- Down handling ---

    private void handlePointerDown(MotionEvent event, int pointerId, int actionIndex) {
        postDoubleTapDrag = false;
        pendingDoubleTapAction = null;
        if (mainPointerId < 0) {
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
                    setSecondFingerActive(true);
                    state = GestureState.TAP_WAITING;
                    return;
                }

                mainPointerId = pointerId;
                fingerDownX = sx;
                fingerDownY = sy;
                setSecondFingerActive(true);

                touchpadView.removeCallbacks(longPressRunnable);
                touchpadView.removeCallbacks(doubleTapRunnable);

                touchpadView.postDelayed(longPressRunnable, longPressTimeout);
                state = GestureState.TAP_WAITING;
                return;
            }

            mainPointerId = pointerId;
            fingerDownX = event.getX(actionIndex);
            fingerDownY = event.getY(actionIndex);
            setSecondFingerActive(false);

            touchpadView.removeCallbacks(doubleTapRunnable);
            touchpadView.removeCallbacks(longPressRunnable);

            if (state == GestureState.DOUBLE_TAP_WAITING) {
                int savedOriginalPointerId = originalPointerId;
                boolean wasSecondFingerDeferred = deferredSecondFingerTap;
                handleDoubleTapConfirmed();
                originalPointerId = savedOriginalPointerId;
                if (savedOriginalPointerId >= 0) {
                    mainPointerId = savedOriginalPointerId;
                    setSecondFingerActive(false);
                    movePointerToTapPoint();
                    state = GestureState.TAP_WAITING;
                    return;
                }
                mainPointerId = pointerId;
                fingerDownX = event.getX(actionIndex);
                fingerDownY = event.getY(actionIndex);
                if (wasSecondFingerDeferred) {
                    setSecondFingerActive(true);
                }
            }

            movePointerToTapPoint();

            if (isLongTapMode) {
                touchpadView.postDelayed(longPressRunnable, longPressTimeout);
            }
            state = GestureState.TAP_WAITING;
        }
        else if (pointerId != mainPointerId) {
            if (isLongTapMode) {
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
            setSecondFingerActive(true);

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
                    if (!isActionHeld || dragBinding != activeAction) {
                        releaseHeldAction();
                        executeActionAndHold(dragBinding);
                    }
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
                        setSecondFingerActive(false);
                        mainPointerId = -1;
                    }
                    else {
                        handleTapUp();
                        setSecondFingerActive(false);
                        mainPointerId = -1;
                    }
                    break;
                case LONG_PRESSING:
                    if (isActionHeld) {
                        releaseHeldAction();
                    }
                    else if (hasActiveLongPress) {
                        executeAction(activeLongPressAction);
                    }
                    setSecondFingerActive(false);
                    state = GestureState.IDLE;
                    mainPointerId = -1;
                    break;
                case DRAGGING:
                    releaseHeldAction();
                    setSecondFingerActive(false);
                    state = GestureState.IDLE;
                    mainPointerId = -1;
                    break;
                case DOUBLE_TAP_WAITING:
                    deferredTapAction = null;
                    executeAction(activeSingleTapAction);
                    state = GestureState.IDLE;
                    mainPointerId = -1;
                    setSecondFingerActive(false);
                    originalPointerId = -1;
                    break;
                default:
                    deferredTapAction = null;
                    state = GestureState.IDLE;
                    mainPointerId = -1;
                    setSecondFingerActive(false);
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
                setSecondFingerActive(false);
                originalPointerId = -1;
            }
        }
    }

    private void handleTapUp() {
        deferredSecondFingerTap = secondFingerActive;
        pendingDeferredDoubleAction = hasActiveDoubleTap ? activeDoubleTapAction : activeSingleTapAction;

        if (hasActiveDoubleTap) {
            deferredTapAction = activeSingleTapAction;
        }
        else {
            executeAction(activeSingleTapAction);
        }

        touchpadView.postDelayed(doubleTapRunnable, doubleTapTimeout);
        state = GestureState.DOUBLE_TAP_WAITING;
    }

    private void handleCancel() {
        reset();
    }

    // --- Timer callbacks ---

    private void onLongPressTimer() {
        if (state != GestureState.TAP_WAITING) return;

        if (canHoldLongPress) {
            executeActionAndHold(activeLongPressAction);
        }
        state = GestureState.LONG_PRESSING;

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
        pendingDeferredDoubleAction = null;
        state = GestureState.IDLE;
    }

    private void handleDoubleTapConfirmed() {
        touchpadView.removeCallbacks(doubleTapRunnable);

        deferredSecondFingerTap = false;

        if (deferredTapAction != null) {
            deferredTapAction = null;
            pendingDoubleTapAction = pendingDeferredDoubleAction;
            pendingDeferredDoubleAction = null;
        }

        mainPointerId = -1;
        setSecondFingerActive(false);
        originalPointerId = -1;
        state = GestureState.IDLE;
        postDoubleTapDrag = true;
    }

    // --- Action resolution ---

    private Binding resolveDragAction() {
        if (state == GestureState.LONG_PRESSING) {
            return hasActiveLongPressDrag ? activeLongPressDragAction : activeLongPressAction;
        }
        else if (postDoubleTapDrag) {
            Binding fallback = hasActiveDoubleTap ? activeDoubleTapAction : activeSingleTapAction;
            return hasActiveDoubleTapDrag ? activeDoubleTapDragAction : fallback;
        }
        else {
            return hasActiveSingleTapDrag ? activeSingleTapDragAction : activeSingleTapAction;
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
