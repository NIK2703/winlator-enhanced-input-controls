package com.winlator.cmod.widget;

import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlsProfile;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import android.os.SystemClock;

public abstract class GestureHandler {
    protected enum State { IDLE, TAP_WAITING, DOUBLE_TAP_WAITING, LONG_PRESSING, DRAGGING }
    protected State state = State.IDLE;

    protected final TouchpadView touchpadView;
    protected InputControlsView inputControlsView;
    protected GestureActionExecutor actionExecutor;

    // All 12 binding lists from profile
    protected List<Binding> singleTapAction = new ArrayList<>(Collections.singletonList(Binding.MOUSE_LEFT_BUTTON));
    protected List<Binding> longPressAction = new ArrayList<>(Collections.singletonList(Binding.MOUSE_RIGHT_BUTTON));
    protected List<Binding> doubleTapAction = new ArrayList<>();
    protected List<Binding> singleTap2ndFingerAction = new ArrayList<>(Collections.singletonList(Binding.MOUSE_RIGHT_BUTTON));
    protected List<Binding> longPress2ndFingerAction = new ArrayList<>(Collections.singletonList(Binding.MOUSE_RIGHT_BUTTON));
    protected List<Binding> doubleTap2ndFingerAction = new ArrayList<>();
    protected List<Binding> singleTapDragAction = new ArrayList<>();
    protected List<Binding> longPressDragAction = new ArrayList<>(Collections.singletonList(Binding.MOUSE_LEFT_BUTTON));
    protected List<Binding> doubleTapDragAction = new ArrayList<>();
    protected List<Binding> singleTap2ndFingerDragAction = new ArrayList<>();
    protected List<Binding> longPress2ndFingerDragAction = new ArrayList<>();
    protected List<Binding> doubleTap2ndFingerDragAction = new ArrayList<>();

    protected int bindingDelay;
    protected int doubleTapTimeout = 150;
    protected int longPressTimeout = 200;
    protected boolean hapticFeedbackEnabled = true;
    protected int dragThreshold = 10;
    protected int singleTapDelay;
    protected int gestureLongPressHaptic = 1;

    // Active bindings — two precomputed sets, pointer-swapped on secondFingerActive change
    private static class BindingSet {
        final List<Binding> singleTapAction;
        final List<Binding> longPressAction;
        final List<Binding> doubleTapAction;
        final List<Binding> singleTapDragAction;
        final List<Binding> longPressDragAction;
        final List<Binding> doubleTapDragAction;
        final boolean hasActiveSingleTap;
        final boolean hasActiveDoubleTap;
        final boolean hasActiveLongPress;
        final boolean hasActiveLongPressDrag;
        final boolean hasActiveDoubleTapDrag;
        final boolean hasActiveSingleTapDrag;
        final boolean canHoldLongPress;
        final boolean hasLongPressTimer;

        BindingSet(List<Binding> st, List<Binding> lp, List<Binding> dt,
                   List<Binding> std, List<Binding> lpd, List<Binding> dtd,
                   Binding firstST, Binding firstLP, Binding firstDT, Binding firstSTD, Binding firstLPD, Binding firstDTD) {
            singleTapAction = st;
            longPressAction = lp;
            doubleTapAction = dt;
            singleTapDragAction = std;
            longPressDragAction = lpd;
            doubleTapDragAction = dtd;
            hasActiveSingleTap = firstST != Binding.NONE;
            hasActiveDoubleTap = firstDT != Binding.NONE;
            hasActiveLongPress = firstLP != Binding.NONE;
            hasActiveLongPressDrag = firstLPD != Binding.NONE;
            hasActiveDoubleTapDrag = firstDTD != Binding.NONE;
            hasActiveSingleTapDrag = firstSTD != Binding.NONE;
            canHoldLongPress = !hasActiveLongPressDrag && !hasActiveSingleTapDrag && hasActiveLongPress &&
                !firstLP.isMouseMove() && firstLP != Binding.MOUSE_SCROLL_UP && firstLP != Binding.MOUSE_SCROLL_DOWN;
            hasLongPressTimer = hasActiveLongPress || hasActiveLongPressDrag;
        }
    }

    protected BindingSet firstFingerSet;
    protected BindingSet secondFingerSet;
    protected BindingSet cur;  // points to firstFingerSet or secondFingerSet

    // Convenience accessors via cur
    protected final List<Binding> activeSingleTapAction() { return cur != null ? cur.singleTapAction : singleTapAction; }
    protected final List<Binding> activeLongPressAction() { return cur != null ? cur.longPressAction : longPressAction; }
    protected final List<Binding> activeDoubleTapAction() { return cur != null ? cur.doubleTapAction : doubleTapAction; }
    protected final List<Binding> activeSingleTapDragAction() { return cur != null ? cur.singleTapDragAction : singleTapDragAction; }
    protected final List<Binding> activeLongPressDragAction() { return cur != null ? cur.longPressDragAction : longPressDragAction; }
    protected final List<Binding> activeDoubleTapDragAction() { return cur != null ? cur.doubleTapDragAction : doubleTapDragAction; }
    protected final boolean hasActiveSingleTap() { return cur != null && cur.hasActiveSingleTap; }
    protected final boolean hasActiveDoubleTap() { return cur != null && cur.hasActiveDoubleTap; }
    protected final boolean hasActiveLongPress() { return cur != null && cur.hasActiveLongPress; }
    protected final boolean hasActiveLongPressDrag() { return cur != null && cur.hasActiveLongPressDrag; }
    protected final boolean hasActiveDoubleTapDrag() { return cur != null && cur.hasActiveDoubleTapDrag; }
    protected final boolean hasActiveSingleTapDrag() { return cur != null && cur.hasActiveSingleTapDrag; }
    protected final boolean canHoldLongPress() { return cur != null && cur.canHoldLongPress; }
    protected final boolean hasLongPressTimer() { return cur != null && cur.hasLongPressTimer; }

    // Gesture state shared by both subclasses
    protected float fingerDownX;
    protected float fingerDownY;
    protected int mainPointerId = -1;
    protected boolean secondFingerActive;
    protected List<Binding> deferredTapAction;
    protected boolean postDoubleTapDrag;
    protected List<Binding> pendingDoubleTapAction;
    protected List<Binding> pendingDeferredDoubleAction;
    protected boolean doubleTapConsumed;

    // Timer callbacks
    protected final Runnable longPressRunnable = this::onLongPressTimer;
    protected final Runnable doubleTapRunnable = this::onDoubleTapTimer;
    protected final Runnable singleTapHoldRunnable = () -> {
        if (state == State.TAP_WAITING && hasActiveSingleTap() && !actionExecutor.isActionHeld()) {
            actionExecutor.executeActionsAndHold(activeSingleTapAction());
        }
    };

    private static Binding firstBinding(List<Binding> list, Binding defaultVal) {
        if (list != null) {
            for (Binding b : list) {
                if (b != null && b != Binding.NONE) return b;
            }
        }
        return defaultVal;
    }

    protected static BindingSet buildBindingSet(List<Binding> st, List<Binding> lp, List<Binding> dt,
                                               List<Binding> std, List<Binding> lpd, List<Binding> dtd) {
        return new BindingSet(
            st, lp, dt, std, lpd, dtd,
            firstBinding(st, Binding.NONE),
            firstBinding(lp, Binding.NONE),
            firstBinding(dt, Binding.NONE),
            firstBinding(std, Binding.NONE),
            firstBinding(lpd, Binding.NONE),
            firstBinding(dtd, Binding.NONE)
        );
    }

    protected GestureHandler(TouchpadView touchpadView) {
        this.touchpadView = touchpadView;
        applyDefaultBindings();
    }

    void applyDefaultBindings() {
        firstFingerSet = buildBindingSet(singleTapAction, longPressAction, doubleTapAction,
            singleTapDragAction, longPressDragAction, doubleTapDragAction);
        secondFingerSet = buildBindingSet(singleTap2ndFingerAction, longPress2ndFingerAction, doubleTap2ndFingerAction,
            singleTap2ndFingerDragAction, longPress2ndFingerDragAction, doubleTap2ndFingerDragAction);
        cur = firstFingerSet;
    }

    public void setInputControlsView(InputControlsView inputControlsView) {
        this.inputControlsView = inputControlsView;
        this.actionExecutor = new GestureActionExecutor(inputControlsView);
    }

    public void applyConfig(ControlsProfile profile) {
        singleTapAction = new ArrayList<>(profile.getSingleTapAction());
        longPressAction = new ArrayList<>(profile.getLongPressAction());
        doubleTapAction = new ArrayList<>(profile.getDoubleTapAction());
        singleTap2ndFingerAction = new ArrayList<>(profile.getSingleTap2ndFingerAction());
        longPress2ndFingerAction = new ArrayList<>(profile.getLongPress2ndFingerAction());
        doubleTap2ndFingerAction = new ArrayList<>(profile.getDoubleTap2ndFingerAction());
        singleTapDragAction = new ArrayList<>(profile.getSingleTapDragAction());
        longPressDragAction = new ArrayList<>(profile.getLongPressDragAction());
        doubleTapDragAction = new ArrayList<>(profile.getDoubleTapDragAction());
        singleTap2ndFingerDragAction = new ArrayList<>(profile.getSingleTap2ndFingerDragAction());
        longPress2ndFingerDragAction = new ArrayList<>(profile.getLongPress2ndFingerDragAction());
        doubleTap2ndFingerDragAction = new ArrayList<>(profile.getDoubleTap2ndFingerDragAction());
        bindingDelay = profile.getBindingDelay();
        doubleTapTimeout = profile.getDoubleTapTimeout();
        longPressTimeout = profile.getLongPressTimeout();
        gestureLongPressHaptic = profile.getGestureLongPressHaptic();
        dragThreshold = profile.getDragThreshold();
        singleTapDelay = profile.getSingleTapDelay();
        if (actionExecutor != null) actionExecutor.setBindingDelay(bindingDelay);

        firstFingerSet = buildBindingSet(singleTapAction, longPressAction, doubleTapAction,
            singleTapDragAction, longPressDragAction, doubleTapDragAction);
        secondFingerSet = buildBindingSet(singleTap2ndFingerAction, longPress2ndFingerAction, doubleTap2ndFingerAction,
            singleTap2ndFingerDragAction, longPress2ndFingerDragAction, doubleTap2ndFingerDragAction);
        cur = firstFingerSet;
    }

    protected void setSecondFingerActive(boolean active) {
        this.secondFingerActive = active;
        this.cur = active ? secondFingerSet : firstFingerSet;
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
        doubleTapConsumed = false;
    }

    // --- Timer callbacks ---

    protected void onLongPressTimer() {
        if (state != State.TAP_WAITING) return;

        if (canHoldLongPress()) {
            actionExecutor.executeActionsAndHold(activeLongPressAction());
        }
        state = State.LONG_PRESSING;

        if (hasLongPressTimer()) {
            com.winlator.cmod.core.HapticUtils.perform(touchpadView.getContext(), gestureLongPressHaptic);
        }
    }

    protected void onDoubleTapTimer() {
        if (state != State.DOUBLE_TAP_WAITING) return;
        if (deferredTapAction != null) {
            actionExecutor.executeActions(deferredTapAction);
            deferredTapAction = null;
        }
        pendingDeferredDoubleAction = null;
        state = State.IDLE;
    }

    protected void handleDoubleTapConfirmed() {
        touchpadView.removeCallbacks(doubleTapRunnable);

        if (deferredTapAction != null) {
            deferredTapAction = null;
            pendingDoubleTapAction = pendingDeferredDoubleAction;
            pendingDeferredDoubleAction = null;
        }

        if (pendingDoubleTapAction != null) {
            if (!hasActiveDoubleTapDrag()) {
                actionExecutor.executeActionsAndHold(pendingDoubleTapAction);
                pendingDoubleTapAction = null;
            }
        }

        setSecondFingerActive(false);
        state = State.IDLE;
        postDoubleTapDrag = true;
    }

    protected void handleTapUp() {
        if (singleTapDelay > 0) {
            SystemClock.sleep(singleTapDelay);
        }

        if (pendingDoubleTapAction != null) {
            if (!hasActiveDoubleTapDrag()) {
                actionExecutor.executeActionsAndHold(pendingDoubleTapAction);
            } else {
                actionExecutor.executeActions(pendingDoubleTapAction);
            }
            pendingDoubleTapAction = null;
            deferredTapAction = null;
            pendingDeferredDoubleAction = null;
            postDoubleTapDrag = false;
            doubleTapConsumed = true;
            state = State.IDLE;
            return;
        }

        if (postDoubleTapDrag) {
            deferredTapAction = null;
            pendingDeferredDoubleAction = null;
            postDoubleTapDrag = false;
            doubleTapConsumed = true;
            state = State.IDLE;
            return;
        }

        if (doubleTapConsumed) {
            doubleTapConsumed = false;
            if (!actionExecutor.isActionHeld()) {
                if (!hasActiveSingleTapDrag()) {
                    actionExecutor.executeActionsAndHold(activeSingleTapAction());
                } else {
                    actionExecutor.executeActions(activeSingleTapAction());
                }
            }
            state = State.IDLE;
            return;
        }

        boolean hasDT = hasActiveDoubleTap();
        pendingDeferredDoubleAction = hasDT ? activeDoubleTapAction() : activeSingleTapAction();

        if (hasDT) {
            deferredTapAction = activeSingleTapAction();
            touchpadView.postDelayed(doubleTapRunnable, doubleTapTimeout);
            state = State.DOUBLE_TAP_WAITING;
        }
        else {
            if (!actionExecutor.isActionHeld()) {
                if (!hasActiveSingleTapDrag()) {
                    actionExecutor.executeActionsAndHold(activeSingleTapAction());
                } else {
                    actionExecutor.executeActions(activeSingleTapAction());
                }
            }
            if (hasActiveDoubleTapDrag()) {
                touchpadView.postDelayed(doubleTapRunnable, doubleTapTimeout);
                state = State.DOUBLE_TAP_WAITING;
            }
            else {
                state = State.IDLE;
            }
        }
    }

    // --- Action resolution ---

    protected List<Binding> resolveDragAction() {
        if (state == State.LONG_PRESSING) {
            if (secondFingerActive) {
                if (hasActiveLongPressDrag()) return activeLongPressDragAction();
                if (hasActiveSingleTapDrag()) return activeSingleTapDragAction();
                return null;
            }
            return hasActiveLongPressDrag() ? activeLongPressDragAction() : null;
        }
        else if (postDoubleTapDrag) {
            return hasActiveDoubleTapDrag() ? activeDoubleTapDragAction() : null;
        }
        else if (secondFingerActive) {
            if (hasActiveSingleTapDrag()) return activeSingleTapDragAction();
            if (hasActiveLongPressDrag()) return activeLongPressDragAction();
            if (hasActiveDoubleTapDrag()) return activeDoubleTapDragAction();
            return null;
        }
        else {
            return null;
        }
    }

    // --- Shared drag detection ---

    protected void onDragStart() {}

    protected boolean checkStartDrag(float dx, float dy) {
        if (Math.abs(dx) > dragThreshold || Math.abs(dy) > dragThreshold) {
            touchpadView.removeCallbacks(longPressRunnable);
            List<Binding> dragBinding = resolveDragAction();
            if (dragBinding != null) {
                if (!actionExecutor.isActionHeld() || !dragBinding.equals(actionExecutor.getHeldActions())) {
                    actionExecutor.releaseHeldAction();
                    actionExecutor.executeActionsAndHold(dragBinding);
                }
                pendingDoubleTapAction = null;
                onDragStart();
                state = State.DRAGGING;
                return true;
            }
        }
        return false;
    }

    // Returns true if the event is consumed (drag active or just started)
    protected boolean handleMoveDelta(float dx, float dy) {
        if (state == State.DOUBLE_TAP_WAITING || state == State.IDLE) return false;
        if (state == State.DRAGGING) return true;
        return checkStartDrag(dx, dy);
    }

    // --- Cleanup ---

    protected void cleanupMainPointer() {
        mainPointerId = -1;
        setSecondFingerActive(false);
        state = State.IDLE;
        postDoubleTapDrag = false;
        deferredTapAction = null;
    }

    protected void removeAllCallbacks() {
        touchpadView.removeCallbacks(longPressRunnable);
        touchpadView.removeCallbacks(doubleTapRunnable);
        touchpadView.removeCallbacks(singleTapHoldRunnable);
    }
}
