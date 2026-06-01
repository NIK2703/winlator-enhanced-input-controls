package com.winlator.cmod.widget;

import android.view.MotionEvent;

import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.inputcontrols.SecondFingerMode;

import java.util.ArrayList;
import java.util.List;

public class TouchscreenGestureHandler extends GestureHandler {
    private int originalPointerId = -1;
    private int secondPointerId = -1;
    private boolean deferredSecondFingerTap;
    private boolean isLongTapMode;
    private List<Binding> pendingResumeAction;

    public TouchscreenGestureHandler(TouchpadView touchpadView) {
        super(touchpadView);
    }

    @Override
    public void applyConfig(ControlsProfile profile) {
        super.applyConfig(profile);
        isLongTapMode = profile.getSecondFingerMode() == SecondFingerMode.LONG_TAP_ACTION;
    }

    @Override
    public void reset() {
        super.reset();
        originalPointerId = -1;
        secondPointerId = -1;
        deferredSecondFingerTap = false;
        pendingResumeAction = null;
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

    // --- Down handling ---

    private void handlePointerDown(MotionEvent event, int pointerId, int actionIndex) {
        postDoubleTapDrag = false;
        pendingDoubleTapAction = null;
        if (mainPointerId < 0) {
            if (originalPointerId >= 0 && pointerId != originalPointerId) {
                if (state == State.DOUBLE_TAP_WAITING) {
                    float newX = event.getX(actionIndex);
                    float newY = event.getY(actionIndex);
                    if (isWithinTapDistance(newX, newY)) {
                        int savedOriginalPointerId = originalPointerId;
                        handleDoubleTapConfirmed();
                        originalPointerId = savedOriginalPointerId;
                        mainPointerId = pointerId;
                        fingerDownX = newX;
                        fingerDownY = newY;
                        setSecondFingerActive(true);
                        state = State.TAP_WAITING;
                        return;
                    } else {
                        cancelDoubleTapWait();
                    }
                }

                secondPointerId = pointerId;
                setSecondFingerActive(true);

                touchpadView.removeCallbacks(longPressRunnable);
                touchpadView.removeCallbacks(doubleTapRunnable);

                if (hasLongPressTimer()) touchpadView.postDelayed(longPressRunnable, longPressTimeout);
                state = State.TAP_WAITING;
                if (hasActiveSingleTap() && !hasActiveSingleTapDrag()) {
                    boolean hasDoubleTapAction = hasActiveDoubleTap() || hasActiveDoubleTapDrag();
                    if (hasDoubleTapAction) {
                        touchpadView.postDelayed(singleTapHoldRunnable, doubleTapTimeout);
                    } else {
                        if (singleTapDelay > 0) {
                            touchpadView.postDelayed(singleTapHoldRunnable, singleTapDelay);
                        } else {
                            actionExecutor.executeActionsAndHold(activeSingleTapAction());
                        }
                    }
                }
                return;
            }

            mainPointerId = pointerId;
            fingerDownX = event.getX(actionIndex);
            fingerDownY = event.getY(actionIndex);
            setSecondFingerActive(false);

            touchpadView.removeCallbacks(doubleTapRunnable);
            touchpadView.removeCallbacks(longPressRunnable);

            if (state == State.DOUBLE_TAP_WAITING) {
                float newX = event.getX(actionIndex);
                float newY = event.getY(actionIndex);
                if (isWithinTapDistance(newX, newY)) {
                    int savedOriginalPointerId = originalPointerId;
                    boolean wasSecondFingerDeferred = deferredSecondFingerTap;
                    handleDoubleTapConfirmed();
                    originalPointerId = savedOriginalPointerId;
                    if (savedOriginalPointerId >= 0) {
                        mainPointerId = savedOriginalPointerId;
                        setSecondFingerActive(false);
                        movePointerToTapPoint();
                        state = State.TAP_WAITING;
                        return;
                    }
                    mainPointerId = pointerId;
                    fingerDownX = newX;
                    fingerDownY = newY;
                    if (wasSecondFingerDeferred) {
                        setSecondFingerActive(true);
                    }
                } else {
                    cancelDoubleTapWait();
                }
            }

            movePointerToTapPoint();

            if (isLongTapMode) {
                touchpadView.postDelayed(longPressRunnable, longPressTimeout);
            }
            state = State.TAP_WAITING;
            if (hasActiveSingleTap() && !hasActiveSingleTapDrag()) {
                boolean hasDoubleTapAction = hasActiveDoubleTap() || hasActiveDoubleTapDrag();
                if (hasDoubleTapAction) {
                    touchpadView.postDelayed(singleTapHoldRunnable, doubleTapTimeout);
                } else {
                    if (singleTapDelay > 0) {
                        touchpadView.postDelayed(singleTapHoldRunnable, singleTapDelay);
                    } else {
                        actionExecutor.executeActionsAndHold(activeSingleTapAction());
                    }
                }
            }
        }
        else if (pointerId != mainPointerId) {
            if (isLongTapMode) {
                return;
            }

            if (state == State.DOUBLE_TAP_WAITING) {
                float newX = event.getX(actionIndex);
                float newY = event.getY(actionIndex);
                if (isWithinTapDistance(newX, newY)) {
                    int savedOriginalPointerId = originalPointerId;
                    handleDoubleTapConfirmed();
                    originalPointerId = savedOriginalPointerId;
                    if (pendingDoubleTapAction != null) {
                        actionExecutor.executeActions(pendingDoubleTapAction);
                        pendingDoubleTapAction = null;
                    }
                    return;
                } else {
                    cancelDoubleTapWait();
                }
            }

            secondPointerId = pointerId;
            setSecondFingerActive(true);

            touchpadView.removeCallbacks(longPressRunnable);
            touchpadView.removeCallbacks(doubleTapRunnable);
            if (actionExecutor.isActionHeld()) {
                pendingResumeAction = new ArrayList<>(actionExecutor.getHeldActions());
                actionExecutor.releaseHeldAction();
            }

            if (hasLongPressTimer()) touchpadView.postDelayed(longPressRunnable, longPressTimeout);
            state = State.TAP_WAITING;
            if (hasActiveSingleTap() && !hasActiveSingleTapDrag()) {
                boolean hasDoubleTapAction = hasActiveDoubleTap() || hasActiveDoubleTapDrag();
                if (hasDoubleTapAction) {
                    touchpadView.postDelayed(singleTapHoldRunnable, doubleTapTimeout);
                } else {
                    if (singleTapDelay > 0) {
                        touchpadView.postDelayed(singleTapHoldRunnable, singleTapDelay);
                    } else {
                        actionExecutor.executeActionsAndHold(activeSingleTapAction());
                    }
                }
            }
        }
    }

    // --- Move handling ---

    private void handlePointerMove(MotionEvent event) {
        int mainIndex = event.findPointerIndex(mainPointerId);
        if (mainIndex < 0) return;

        float cx = event.getX(mainIndex);
        float cy = event.getY(mainIndex);

        if (!hasActiveSingleTapDrag()) {
            touchpadView.removeCallbacks(longPressRunnable);
            if (hasActiveSingleTap()) {
                touchpadView.movePointer(cx, cy);
                return;
            }
        }

        handleMoveDelta(cx - fingerDownX, cy - fingerDownY);
        touchpadView.movePointer(cx, cy);
    }

    // --- Up handling ---

    private void handlePointerUp(MotionEvent event, int pointerId, int actionIndex) {
        boolean wasPostDoubleTapDrag = postDoubleTapDrag;
        postDoubleTapDrag = false;
        if (pointerId == mainPointerId) {
            touchpadView.removeCallbacks(longPressRunnable);
            pendingResumeAction = null;
            secondPointerId = -1;

            switch (state) {
                case TAP_WAITING:
                    if (wasPostDoubleTapDrag) {
                        if (pendingDoubleTapAction != null) {
                            if (!hasActiveDoubleTapDrag()) {
                                actionExecutor.executeActionsAndHold(pendingDoubleTapAction);
                                actionExecutor.releaseHeldAction();
                            } else {
                                actionExecutor.executeActions(pendingDoubleTapAction);
                            }
                            pendingDoubleTapAction = null;
                        }
                        actionExecutor.releaseHeldAction();
                        state = State.IDLE;
                        setSecondFingerActive(false);
                        mainPointerId = -1;
                    }
                    else {
                        deferredSecondFingerTap = secondFingerActive;
                        int mainIndex = event.findPointerIndex(mainPointerId);
                        if (mainIndex >= 0) {
                            tapUpX = event.getX(mainIndex);
                            tapUpY = event.getY(mainIndex);
                        }
                        handleTapUp();
                        actionExecutor.releaseHeldAction();
                        setSecondFingerActive(false);
                        mainPointerId = -1;
                    }
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
        else if (originalPointerId >= 0 && pointerId == originalPointerId) {
            if (secondFingerActive) {
                originalPointerId = -1;
            }
            else {
                removeAllCallbacks();
                actionExecutor.releaseHeldAction();
                pendingResumeAction = null;
                deferredTapAction = null;
                state = State.IDLE;
                mainPointerId = -1;
                setSecondFingerActive(false);
                originalPointerId = -1;
            }
        }
        else if (pointerId == secondPointerId) {
            secondPointerId = -1;
            actionExecutor.releaseHeldAction();
            if (mainPointerId >= 0 && pendingResumeAction != null) {
                actionExecutor.executeActionsAndHold(pendingResumeAction);
                pendingResumeAction = null;
                state = State.DRAGGING;
            }
        }
    }

    private void handleCancel() {
        reset();
    }

    private void movePointerToTapPoint() {
        if (mainPointerId >= 0) {
            touchpadView.movePointer(fingerDownX, fingerDownY);
        }
    }

    @Override
    protected void cleanupMainPointer() {
        super.cleanupMainPointer();
        originalPointerId = -1;
        secondPointerId = -1;
        pendingResumeAction = null;
    }

    @Override
    protected List<Binding> resolveDragAction() {
        if (state == State.LONG_PRESSING) {
            return hasActiveLongPressDrag() ? activeLongPressDragAction() : activeLongPressAction();
        }
        else if (postDoubleTapDrag) {
            List<Binding> fallback = hasActiveDoubleTap() ? activeDoubleTapAction() : activeSingleTapAction();
            return hasActiveDoubleTapDrag() ? activeDoubleTapDragAction() : fallback;
        }
        else {
            return hasActiveSingleTapDrag() ? activeSingleTapDragAction() : null;
        }
    }
}
