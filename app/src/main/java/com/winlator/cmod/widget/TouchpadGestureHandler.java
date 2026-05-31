package com.winlator.cmod.widget;

import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.inputcontrols.SecondFingerMode;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class TouchpadGestureHandler {
    private enum State { IDLE, TAP_WAITING, DOUBLE_TAP_WAITING, LONG_PRESSING, DRAGGING }

    private State state = State.IDLE;

    private final TouchpadView touchpadView;
    private GestureActionExecutor actionExecutor;
    private InputControlsView inputControlsView;

    // Configuration (from profile) — full binding lists
    private List<Binding> singleTapAction = Collections.singletonList(Binding.MOUSE_LEFT_BUTTON);
    private List<Binding> longPressAction = Collections.singletonList(Binding.MOUSE_LEFT_BUTTON);
    private List<Binding> doubleTapAction = Collections.singletonList(Binding.NONE);
    private List<Binding> singleTap2ndFingerAction = Collections.singletonList(Binding.MOUSE_RIGHT_BUTTON);
    private List<Binding> longPress2ndFingerAction = Collections.singletonList(Binding.MOUSE_RIGHT_BUTTON);
    private List<Binding> doubleTap2ndFingerAction = Collections.singletonList(Binding.NONE);
    private List<Binding> longPressDragAction = Collections.singletonList(Binding.NONE);
    private List<Binding> doubleTapDragAction = Collections.singletonList(Binding.NONE);
    private List<Binding> longPress2ndFingerDragAction = Collections.singletonList(Binding.NONE);
    private List<Binding> doubleTap2ndFingerDragAction = Collections.singletonList(Binding.NONE);
    private int doubleTapTimeout = 200;
    private int longPressTimeout = 400;
    private boolean hapticFeedbackEnabled = true;
    private int dragThreshold = 10;
    private int dragThresholdSq = 100;

    // Pre-resolved active binding lists (switched for second finger)
    private List<Binding> activeSingleTapAction;
    private List<Binding> activeLongPressAction;
    private List<Binding> activeDoubleTapAction;
    private List<Binding> activeLongPressDragAction;
    private List<Binding> activeDoubleTapDragAction;
    private boolean hasActiveDoubleTap;
    private boolean hasActiveLongPress;
    private boolean hasActiveLongPressDrag;
    private boolean hasActiveDoubleTapDrag;
    private boolean canHoldLongPress;

    // Cached firstBinding values
    private Binding firstSingleTap;
    private Binding firstLongPress;
    private Binding firstDoubleTap;
    private Binding firstLongPressDrag;
    private Binding firstDoubleTapDrag;
    private Binding firstSingleTap2nd;
    private Binding firstLongPress2nd;
    private Binding firstDoubleTap2nd;
    private Binding firstLongPressDrag2nd;
    private Binding firstDoubleTapDrag2nd;

    // Gesture state
    private float fingerDownX;
    private float fingerDownY;
    private int mainPointerId = -1;
    private boolean secondFingerActive;
    private List<Binding> deferredTapAction;
    private boolean postDoubleTapDrag;
    private List<Binding> pendingDoubleTapAction;
    private List<Binding> pendingDeferredDoubleAction;

    // Timer callback references
    private final Runnable longPressRunnable = this::onLongPressTimer;
    private final Runnable doubleTapRunnable = this::onDoubleTapTimer;

    public TouchpadGestureHandler(TouchpadView touchpadView) {
        this.touchpadView = touchpadView;
    }

    public void setInputControlsView(InputControlsView inputControlsView) {
        this.inputControlsView = inputControlsView;
        this.actionExecutor = new GestureActionExecutor(inputControlsView);
    }

    private static Binding firstBinding(List<Binding> list, Binding defaultVal) {
        if (list != null) {
            for (Binding b : list) {
                if (b != null && b != Binding.NONE) return b;
            }
        }
        return defaultVal;
    }

    public void applyConfig(ControlsProfile profile) {
        singleTapAction = new ArrayList<>(profile.getSingleTapAction());
        longPressAction = new ArrayList<>(profile.getLongPressAction());
        doubleTapAction = new ArrayList<>(profile.getDoubleTapAction());
        singleTap2ndFingerAction = new ArrayList<>(profile.getSingleTap2ndFingerAction());
        longPress2ndFingerAction = new ArrayList<>(profile.getLongPress2ndFingerAction());
        doubleTap2ndFingerAction = new ArrayList<>(profile.getDoubleTap2ndFingerAction());
        longPressDragAction = new ArrayList<>(profile.getLongPressDragAction());
        doubleTapDragAction = new ArrayList<>(profile.getDoubleTapDragAction());
        longPress2ndFingerDragAction = new ArrayList<>(profile.getLongPress2ndFingerDragAction());
        doubleTap2ndFingerDragAction = new ArrayList<>(profile.getDoubleTap2ndFingerDragAction());
        doubleTapTimeout = profile.getDoubleTapTimeout();
        longPressTimeout = profile.getLongPressTimeout();
        hapticFeedbackEnabled = profile.getHapticFeedbackEnabled();
        dragThreshold = profile.getDragThreshold();
        dragThresholdSq = dragThreshold * dragThreshold;
        if (actionExecutor != null) actionExecutor.setBindingDelay(profile.getBindingDelay());

        firstSingleTap = firstBinding(singleTapAction, Binding.NONE);
        firstLongPress = firstBinding(longPressAction, Binding.NONE);
        firstDoubleTap = firstBinding(doubleTapAction, Binding.NONE);
        firstLongPressDrag = firstBinding(longPressDragAction, Binding.NONE);
        firstDoubleTapDrag = firstBinding(doubleTapDragAction, Binding.NONE);
        firstSingleTap2nd = firstBinding(singleTap2ndFingerAction, Binding.NONE);
        firstLongPress2nd = firstBinding(longPress2ndFingerAction, Binding.NONE);
        firstDoubleTap2nd = firstBinding(doubleTap2ndFingerAction, Binding.NONE);
        firstLongPressDrag2nd = firstBinding(longPress2ndFingerDragAction, Binding.NONE);
        firstDoubleTapDrag2nd = firstBinding(doubleTap2ndFingerDragAction, Binding.NONE);

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
        activeLongPressDragAction = secondFingerActive ? longPress2ndFingerDragAction : longPressDragAction;
        activeDoubleTapDragAction = secondFingerActive ? doubleTap2ndFingerDragAction : doubleTapDragAction;
        Binding firstLP = secondFingerActive ? firstLongPress2nd : firstLongPress;
        Binding firstDT = secondFingerActive ? firstDoubleTap2nd : firstDoubleTap;
        Binding firstLPD = secondFingerActive ? firstLongPressDrag2nd : firstLongPressDrag;
        Binding firstDTD = secondFingerActive ? firstDoubleTapDrag2nd : firstDoubleTapDrag;
        hasActiveDoubleTap = firstDT != Binding.NONE;
        hasActiveLongPress = firstLP != Binding.NONE;
        hasActiveLongPressDrag = firstLPD != Binding.NONE;
        hasActiveDoubleTapDrag = firstDTD != Binding.NONE;
        canHoldLongPress = !hasActiveLongPressDrag && hasActiveLongPress &&
            !firstLP.isMouseMove() && firstLP != Binding.MOUSE_SCROLL_UP && firstLP != Binding.MOUSE_SCROLL_DOWN;
    }

    // --- Hook: finger down ---
    // Returns true if the event was consumed as a second-finger gesture
    public boolean onFingerDown(int pointerId, float x, float y) {
        if (mainPointerId < 0) {
            mainPointerId = pointerId;
            fingerDownX = x;
            fingerDownY = y;
            setSecondFingerActive(false);

            touchpadView.removeCallbacks(doubleTapRunnable);
            touchpadView.removeCallbacks(longPressRunnable);

            if (state == State.DOUBLE_TAP_WAITING) {
                handleDoubleTapConfirmed();
            }

            if (hasActiveLongPress || hasActiveLongPressDrag) {
                touchpadView.postDelayed(longPressRunnable, longPressTimeout);
            }
            state = State.TAP_WAITING;
            return false;
        }
        else if (pointerId != mainPointerId) {
            // Second finger while first is active
            setSecondFingerActive(true);

            if (state == State.DOUBLE_TAP_WAITING) {
                handleDoubleTapConfirmed();
            }

            if (actionExecutor.isActionHeld()) {
                actionExecutor.releaseHeldAction();
            }

            // Execute second-finger single tap action
            if (firstBinding(activeSingleTapAction, Binding.NONE) != Binding.NONE) {
                actionExecutor.executeActions(activeSingleTapAction);
            }
            return true;
        }
        return false;
    }

    // --- Hook: finger move ---
    // Returns true if a drag action was started
    public boolean onFingerMove(int pointerId, float x, float y) {
        if (pointerId != mainPointerId) return false;
        if (state == State.DOUBLE_TAP_WAITING || state == State.IDLE) return false;

        if (state == State.DRAGGING) return true;

        float dx = x - fingerDownX;
        float dy = y - fingerDownY;
        if (dx * dx + dy * dy > dragThresholdSq) {
            touchpadView.removeCallbacks(longPressRunnable);

            List<Binding> dragBinding = resolveDragAction();
            if (dragBinding != null && firstBinding(dragBinding, Binding.NONE) != Binding.NONE) {
                if (!actionExecutor.isActionHeld() || !dragBinding.equals(actionExecutor.getHeldActions())) {
                    actionExecutor.releaseHeldAction();
                    actionExecutor.executeActionsAndHold(dragBinding);
                }
                pendingDoubleTapAction = null;
                state = State.DRAGGING;
                return true;
            }
        }
        return false;
    }

    // --- Hook: finger up ---
    public void onFingerUp(int pointerId) {
        if (pointerId != mainPointerId) return;

        touchpadView.removeCallbacks(longPressRunnable);

        switch (state) {
            case TAP_WAITING:
                handleTapUp();
                mainPointerId = -1;
                setSecondFingerActive(false);
                break;
            case LONG_PRESSING:
                if (actionExecutor.isActionHeld()) {
                    actionExecutor.releaseHeldAction();
                }
                else if (hasActiveLongPress) {
                    actionExecutor.executeActions(activeLongPressAction);
                }
                mainPointerId = -1;
                setSecondFingerActive(false);
                state = State.IDLE;
                break;
            case DRAGGING:
                actionExecutor.releaseHeldAction();
                mainPointerId = -1;
                setSecondFingerActive(false);
                state = State.IDLE;
                break;
            case DOUBLE_TAP_WAITING:
                deferredTapAction = null;
                actionExecutor.executeActions(activeSingleTapAction);
                mainPointerId = -1;
                setSecondFingerActive(false);
                state = State.IDLE;
                break;
            default:
                mainPointerId = -1;
                setSecondFingerActive(false);
                state = State.IDLE;
                break;
        }
    }

    public int getMainPointerId() {
        return mainPointerId;
    }

    public void reset() {
        removeAllCallbacks();
        if (actionExecutor != null) actionExecutor.releaseHeldAction();
        state = State.IDLE;
        mainPointerId = -1;
        setSecondFingerActive(false);
        deferredTapAction = null;
        postDoubleTapDrag = false;
        pendingDoubleTapAction = null;
        pendingDeferredDoubleAction = null;
    }

    // --- Internal gesture logic ---

    private void handleTapUp() {
        pendingDeferredDoubleAction = hasActiveDoubleTap ? activeDoubleTapAction : activeSingleTapAction;

        if (hasActiveDoubleTap) {
            deferredTapAction = activeSingleTapAction;
            touchpadView.postDelayed(doubleTapRunnable, doubleTapTimeout);
            state = State.DOUBLE_TAP_WAITING;
        }
        else {
            actionExecutor.executeActions(activeSingleTapAction);
            if (hasActiveDoubleTapDrag) {
                touchpadView.postDelayed(doubleTapRunnable, doubleTapTimeout);
                state = State.DOUBLE_TAP_WAITING;
            }
            else {
                state = State.IDLE;
            }
        }
    }

    private void onLongPressTimer() {
        if (state != State.TAP_WAITING) return;

        if (canHoldLongPress) {
            actionExecutor.executeActionsAndHold(activeLongPressAction);
        }
        state = State.LONG_PRESSING;

        if (hapticFeedbackEnabled && (hasActiveLongPress || hasActiveLongPressDrag)) {
            com.winlator.cmod.core.AppUtils.performHapticFeedback(touchpadView.getContext(), 255);
        }
    }

    private void onDoubleTapTimer() {
        if (state != State.DOUBLE_TAP_WAITING) return;
        if (deferredTapAction != null) {
            actionExecutor.executeActions(deferredTapAction);
            deferredTapAction = null;
        }
        pendingDeferredDoubleAction = null;
        state = State.IDLE;
    }

    private void handleDoubleTapConfirmed() {
        touchpadView.removeCallbacks(doubleTapRunnable);

        if (deferredTapAction != null) {
            deferredTapAction = null;
            pendingDoubleTapAction = pendingDeferredDoubleAction;
            pendingDeferredDoubleAction = null;
        }

        if (pendingDoubleTapAction != null) {
            actionExecutor.executeActions(pendingDoubleTapAction);
            pendingDoubleTapAction = null;
        }

        mainPointerId = -1;
        setSecondFingerActive(false);
        state = State.IDLE;
        postDoubleTapDrag = true;
    }

    private List<Binding> resolveDragAction() {
        if (state == State.LONG_PRESSING) {
            return hasActiveLongPressDrag ? activeLongPressDragAction : null;
        }
        else if (postDoubleTapDrag) {
            return hasActiveDoubleTapDrag ? activeDoubleTapDragAction : null;
        }
        else {
            return null;
        }
    }

    private void removeAllCallbacks() {
        touchpadView.removeCallbacks(longPressRunnable);
        touchpadView.removeCallbacks(doubleTapRunnable);
    }
}
