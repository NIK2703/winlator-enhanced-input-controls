package com.winlator.cmod.widget;

import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.xserver.Pointer;

import java.util.ArrayList;
import java.util.List;

public class TouchpadGestureHandler extends GestureHandler {
    private float mainFingerX;
    private float mainFingerY;
    private float secondFingerDownX;
    private float secondFingerDownY;

    // Deferred second-finger actions (execute on lift if no drag)
    private List<Binding> pendingSecondTapAction;
    private List<Binding> pendingSecondDoubleTapAction;

    // Second-finger double-tap detection
    private boolean secondFingerDoubleTapWaiting;
    private List<Binding> secondFingerDoubleTapFallback;
    private final Runnable secondFingerDoubleTapRunnable = this::onSecondFingerDoubleTapTimer;

    public TouchpadGestureHandler(TouchpadView touchpadView) {
        super(touchpadView);
    }

    @Override
    public void reset() {
        super.reset();
        pendingSecondTapAction = null;
        pendingSecondDoubleTapAction = null;
        secondFingerDoubleTapWaiting = false;
        secondFingerDoubleTapFallback = null;
    }

    @Override
    public void applyConfig(ControlsProfile profile) {
        singleTapAction = new ArrayList<>(profile.getTouchpadSingleTapAction());
        longPressAction = new ArrayList<>(profile.getTouchpadLongPressAction());
        doubleTapAction = new ArrayList<>(profile.getTouchpadDoubleTapAction());
        singleTap2ndFingerAction = new ArrayList<>(profile.getTouchpadSingleTap2ndFingerAction());
        doubleTap2ndFingerAction = new ArrayList<>(profile.getTouchpadDoubleTap2ndFingerAction());
        singleTapDragAction = new ArrayList<>(profile.getTouchpadSingleTapDragAction());
        longPressDragAction = new ArrayList<>(profile.getTouchpadLongPressDragAction());
        doubleTapDragAction = new ArrayList<>(profile.getTouchpadDoubleTapDragAction());
        singleTap2ndFingerDragAction = new ArrayList<>(profile.getTouchpadSingleTap2ndFingerDragAction());
        doubleTap2ndFingerDragAction = new ArrayList<>(profile.getTouchpadDoubleTap2ndFingerDragAction());
        bindingDelay = profile.getBindingDelay();
        doubleTapTimeout = profile.getTouchpadDoubleTapTimeout();
        longPressTimeout = profile.getTouchpadLongPressTimeout();
        gestureLongPressHaptic = profile.getGestureLongPressHaptic();
        dragThreshold = profile.getTouchpadDragThreshold();
        doubleTapDistance = profile.getDoubleTapDistance();
        if (actionExecutor != null) actionExecutor.setBindingDelay(bindingDelay);

        firstFingerSet = buildBindingSet(singleTapAction, longPressAction, doubleTapAction,
            singleTapDragAction, longPressDragAction, doubleTapDragAction);
        secondFingerSet = buildBindingSet(singleTap2ndFingerAction, new ArrayList<>(), doubleTap2ndFingerAction,
            singleTap2ndFingerDragAction, new ArrayList<>(), doubleTap2ndFingerDragAction);
        cur = firstFingerSet;
    }

    public int getMainPointerId() {
        return mainPointerId;
    }

    public boolean isInTapWaiting() {
        return state == State.TAP_WAITING;
    }

    public boolean isSecondFingerActive() {
        return secondFingerActive;
    }

    // --- Helpers ---

    private void resetLongPressTimer() {
        touchpadView.removeCallbacks(longPressRunnable);
        if (hasLongPressTimer()) {
            touchpadView.postDelayed(longPressRunnable, longPressTimeout);
        }
    }

    // --- Hook: finger down ---
    public boolean onFingerDown(int pointerId, float x, float y) {
        if (mainPointerId < 0) {
            postDoubleTapDrag = false;
            mainPointerId = pointerId;
            fingerDownX = x;
            fingerDownY = y;
            mainFingerX = x;
            mainFingerY = y;
            setSecondFingerActive(false);

            touchpadView.removeCallbacks(doubleTapRunnable);
            touchpadView.removeCallbacks(longPressRunnable);

            if (state == State.DOUBLE_TAP_WAITING) {
                if (isWithinTapDistance(x, y)) {
                    handleDoubleTapConfirmed();
                } else {
                    cancelDoubleTapWait();
                }
            }
            else if (hasLongPressTimer()) {
                touchpadView.postDelayed(longPressRunnable, longPressTimeout);
            }
            state = State.TAP_WAITING;
            return false;
        }
        else if (pointerId != mainPointerId) {
            if (secondFingerDoubleTapWaiting) {
                secondFingerDoubleTapWaiting = false;
                touchpadView.removeCallbacks(secondFingerDoubleTapRunnable);
                setSecondFingerActive(true);

                if (hasActiveDoubleTapDrag()) {
                    pendingSecondDoubleTapAction = activeDoubleTapAction();
                } else {
                    actionExecutor.executeActionsAndHold(activeDoubleTapAction());
                }
                secondFingerDoubleTapFallback = null;

                if (hasActiveDoubleTapDrag()) postDoubleTapDrag = true;

                resetLongPressTimer();
                pendingSecondTapAction = null;
                state = State.TAP_WAITING;
                return true;
            }

            setSecondFingerActive(true);

            if (state == State.DOUBLE_TAP_WAITING) {
                if (isWithinTapDistance(x, y)) {
                    handleDoubleTapConfirmed();
                    setSecondFingerActive(true);
                } else {
                    cancelDoubleTapWait();
                }
            }

            if (actionExecutor.isActionHeld()) {
                actionExecutor.releaseHeldAction();
            }

            // Store second finger down position for drag detection
            secondFingerDownX = x;
            secondFingerDownY = y;

            // Anchor first finger position for drag threshold
            fingerDownX = mainFingerX;
            fingerDownY = mainFingerY;

            // Post long press timer for second finger
            resetLongPressTimer();

            state = State.TAP_WAITING;
            pendingSecondTapAction = activeSingleTapAction();
            return true;
        }
        return false;
    }

    // --- Hook: finger move ---
    @Override
    protected void onDragStart() {
        pendingSecondTapAction = null;
        pendingSecondDoubleTapAction = null;
    }

    public boolean onFingerMove(int pointerId, float x, float y) {
        if (pointerId != mainPointerId) {
            if (secondFingerActive) {
                return handleMoveDelta(x - secondFingerDownX, y - secondFingerDownY);
            }
            return false;
        }
        mainFingerX = x;
        mainFingerY = y;
        return handleMoveDelta(x - fingerDownX, y - fingerDownY);
    }

    // --- Hook: finger up ---
    public void onFingerUp(int pointerId) {
        if (pointerId != mainPointerId) {
            if (secondFingerActive) {
                if (actionExecutor.isActionHeld()) {
                    // Action held from finger-down, skip deferred execution
                    pendingSecondTapAction = null;
                    pendingSecondDoubleTapAction = null;
                }
                else if (pendingSecondTapAction != null && hasActiveDoubleTap()) {
                    secondFingerDoubleTapWaiting = true;
                    secondFingerDoubleTapFallback = pendingSecondTapAction;
                    pendingSecondTapAction = null;
                    touchpadView.postDelayed(secondFingerDoubleTapRunnable, doubleTapTimeout);
                }
                else if (pendingSecondTapAction != null) {
                    actionExecutor.executeActions(pendingSecondTapAction);
                    pendingSecondTapAction = null;
                }
                if (pendingSecondDoubleTapAction != null) {
                    actionExecutor.executeActions(pendingSecondDoubleTapAction);
                    pendingSecondDoubleTapAction = null;
                }
                actionExecutor.releaseHeldAction();
                if (touchpadView.getXServer() != null) {
                    touchpadView.getXServer().injectPointerButtonRelease(Pointer.Button.BUTTON_LEFT);
                    touchpadView.getXServer().injectPointerButtonRelease(Pointer.Button.BUTTON_RIGHT);
                }
                touchpadView.removeCallbacks(longPressRunnable);

                if (state == State.LONG_PRESSING && hasActiveLongPress() && !canHoldLongPress()) {
                    actionExecutor.executeActions(activeLongPressAction());
                }

                postDoubleTapDrag = false;
                if (!secondFingerDoubleTapWaiting && state != State.DRAGGING) state = State.IDLE;
                setSecondFingerActive(false);
            }
            return;
        }

        touchpadView.removeCallbacks(longPressRunnable);

        tapUpX = mainFingerX;
        tapUpY = mainFingerY;
        switch (state) {
            case TAP_WAITING:
                handleTapUp();
                actionExecutor.releaseHeldAction();
                mainPointerId = -1;
                setSecondFingerActive(false);
                break;
            case LONG_PRESSING:
                if (actionExecutor.isActionHeld()) {
                    actionExecutor.releaseHeldAction();
                }
                else if (hasActiveLongPress()) {
                    actionExecutor.executeActions(activeLongPressAction());
                }
                cleanupMainPointer();
                break;
            case DRAGGING:
                actionExecutor.releaseHeldAction();
                cleanupMainPointer();
                break;
            case DOUBLE_TAP_WAITING:
                if (!hasActiveSingleTapDrag()) {
                    actionExecutor.executeActionsAndHold(activeSingleTapAction());
                    actionExecutor.releaseHeldAction();
                } else {
                    actionExecutor.executeActions(activeSingleTapAction());
                }
                cleanupMainPointer();
                break;
            default:
                cleanupMainPointer();
                break;
        }
    }

    @Override
    protected void onLongPressTimer() {
        if (state != State.TAP_WAITING) return;

        pendingSecondTapAction = null;
        pendingSecondDoubleTapAction = null;

        if (canHoldLongPress()) {
            actionExecutor.executeActionsAndHold(activeLongPressAction());
        }
        state = State.LONG_PRESSING;

        if (hasLongPressTimer()) {
            com.winlator.cmod.core.HapticUtils.perform(touchpadView.getContext(), gestureLongPressHaptic);
        }
    }

    private void onSecondFingerDoubleTapTimer() {
        if (secondFingerDoubleTapWaiting) {
            secondFingerDoubleTapWaiting = false;
            if (secondFingerDoubleTapFallback != null) {
                actionExecutor.executeActions(secondFingerDoubleTapFallback);
                secondFingerDoubleTapFallback = null;
            }
            if (state != State.DRAGGING) state = State.IDLE;
        }
    }

    @Override
    protected void removeAllCallbacks() {
        super.removeAllCallbacks();
        touchpadView.removeCallbacks(secondFingerDoubleTapRunnable);
    }
}
