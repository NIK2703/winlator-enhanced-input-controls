package com.winlator.cmod.widget;

import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.MotionEvent;

import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.inputcontrols.SecondFingerMode;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class TouchscreenGestureHandler {
    private enum GestureState { IDLE, TAP_WAITING, DOUBLE_TAP_WAITING, LONG_PRESSING, DRAGGING }

    private GestureState state = GestureState.IDLE;

    // Dependencies
    private final TouchpadView touchpadView;
    private InputControlsView inputControlsView;

    // Configuration (from profile) — full binding lists
    private List<Binding> singleTapAction = Collections.singletonList(Binding.MOUSE_LEFT_BUTTON);
    private List<Binding> longPressAction = Collections.singletonList(Binding.MOUSE_LEFT_BUTTON);
    private List<Binding> doubleTapAction = Collections.singletonList(Binding.NONE);
    private List<Binding> singleTap2ndFingerAction = Collections.singletonList(Binding.MOUSE_RIGHT_BUTTON);
    private List<Binding> longPress2ndFingerAction = Collections.singletonList(Binding.MOUSE_RIGHT_BUTTON);
    private List<Binding> doubleTap2ndFingerAction = Collections.singletonList(Binding.NONE);
    private List<Binding> singleTapDragAction = Collections.singletonList(Binding.NONE);
    private List<Binding> longPressDragAction = Collections.singletonList(Binding.NONE);
    private List<Binding> doubleTapDragAction = Collections.singletonList(Binding.NONE);
    private List<Binding> singleTap2ndFingerDragAction = Collections.singletonList(Binding.NONE);
    private List<Binding> longPress2ndFingerDragAction = Collections.singletonList(Binding.NONE);
    private List<Binding> doubleTap2ndFingerDragAction = Collections.singletonList(Binding.NONE);
    private int bindingDelay;
    private int doubleTapTimeout = 200;
    private int longPressTimeout = 400;
    private int longPressHapticIntensity = 50;
    private boolean longPressHapticEnabled = true;
    private int dragThreshold = 10;
    private int dragThresholdSq = 100;
    private SecondFingerMode secondFingerMode = SecondFingerMode.SECOND_TAP_ACTIONS;
    private boolean isLongTapMode;

    // Pre-resolved active binding lists
    private List<Binding> activeSingleTapAction;
    private List<Binding> activeLongPressAction;
    private List<Binding> activeDoubleTapAction;
    private List<Binding> activeSingleTapDragAction;
    private List<Binding> activeLongPressDragAction;
    private List<Binding> activeDoubleTapDragAction;
    private boolean hasActiveDoubleTap;
    private boolean hasActiveLongPress;
    private boolean hasActiveLongPressDrag;
    private boolean hasActiveDoubleTapDrag;
    private boolean hasActiveSingleTapDrag;
    private boolean canHoldLongPress;

    // Cached firstBinding values (computed once in applyConfig, used by updateActiveBindings)
    private Binding firstSingleTap;
    private Binding firstLongPress;
    private Binding firstDoubleTap;
    private Binding firstSingleTapDrag;
    private Binding firstLongPressDrag;
    private Binding firstDoubleTapDrag;
    private Binding firstSingleTap2nd;
    private Binding firstLongPress2nd;
    private Binding firstDoubleTap2nd;
    private Binding firstSingleTapDrag2nd;
    private Binding firstLongPressDrag2nd;
    private Binding firstDoubleTapDrag2nd;

    // Gesture state
    private float fingerDownX;
    private float fingerDownY;
    private int mainPointerId = -1;
    private boolean secondFingerActive;
    private int originalPointerId = -1;
    private List<Binding> deferredTapAction;
    private boolean deferredSecondFingerTap;

    // Held action tracking
    private final List<Binding> heldActions = new ArrayList<>();
    private final List<Binding> heldModifiers = new ArrayList<>();
    private boolean isActionHeld;

    private boolean postDoubleTapDrag;
    private List<Binding> pendingDoubleTapAction;

    // Stored at tap-up time for handleDoubleTapConfirmed
    private List<Binding> pendingDeferredDoubleAction;

    // Timer callback references
    private final Runnable longPressRunnable = this::onLongPressTimer;
    private final Runnable doubleTapRunnable = this::onDoubleTapTimer;

    public TouchscreenGestureHandler(TouchpadView touchpadView) {
        this.touchpadView = touchpadView;
    }

    public void setInputControlsView(InputControlsView inputControlsView) {
        this.inputControlsView = inputControlsView;
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
        singleTapDragAction = new ArrayList<>(profile.getSingleTapDragAction());
        longPressDragAction = new ArrayList<>(profile.getLongPressDragAction());
        doubleTapDragAction = new ArrayList<>(profile.getDoubleTapDragAction());
        singleTap2ndFingerDragAction = new ArrayList<>(profile.getSingleTap2ndFingerDragAction());
        longPress2ndFingerDragAction = new ArrayList<>(profile.getLongPress2ndFingerDragAction());
        doubleTap2ndFingerDragAction = new ArrayList<>(profile.getDoubleTap2ndFingerDragAction());
        bindingDelay = profile.getBindingDelay();
        doubleTapTimeout = profile.getDoubleTapTimeout();
        longPressTimeout = profile.getLongPressTimeout();
        longPressHapticIntensity = profile.getLongPressHapticIntensity();
        longPressHapticEnabled = profile.getLongPressHapticEnabled();
        dragThreshold = profile.getDragThreshold();
        dragThresholdSq = dragThreshold * dragThreshold;
        secondFingerMode = profile.getSecondFingerMode();
        isLongTapMode = secondFingerMode == SecondFingerMode.LONG_TAP_ACTION;

        firstSingleTap = firstBinding(singleTapAction, Binding.NONE);
        firstLongPress = firstBinding(longPressAction, Binding.NONE);
        firstDoubleTap = firstBinding(doubleTapAction, Binding.NONE);
        firstSingleTapDrag = firstBinding(singleTapDragAction, Binding.NONE);
        firstLongPressDrag = firstBinding(longPressDragAction, Binding.NONE);
        firstDoubleTapDrag = firstBinding(doubleTapDragAction, Binding.NONE);
        firstSingleTap2nd = firstBinding(singleTap2ndFingerAction, Binding.NONE);
        firstLongPress2nd = firstBinding(longPress2ndFingerAction, Binding.NONE);
        firstDoubleTap2nd = firstBinding(doubleTap2ndFingerAction, Binding.NONE);
        firstSingleTapDrag2nd = firstBinding(singleTap2ndFingerDragAction, Binding.NONE);
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
        activeSingleTapDragAction = secondFingerActive ? singleTap2ndFingerDragAction : singleTapDragAction;
        activeLongPressDragAction = secondFingerActive ? longPress2ndFingerDragAction : longPressDragAction;
        activeDoubleTapDragAction = secondFingerActive ? doubleTap2ndFingerDragAction : doubleTapDragAction;
        Binding firstST = secondFingerActive ? firstSingleTap2nd : firstSingleTap;
        Binding firstLP = secondFingerActive ? firstLongPress2nd : firstLongPress;
        Binding firstDT = secondFingerActive ? firstDoubleTap2nd : firstDoubleTap;
        Binding firstSTD = secondFingerActive ? firstSingleTapDrag2nd : firstSingleTapDrag;
        Binding firstLPD = secondFingerActive ? firstLongPressDrag2nd : firstLongPressDrag;
        Binding firstDTD = secondFingerActive ? firstDoubleTapDrag2nd : firstDoubleTapDrag;
        hasActiveDoubleTap = firstDT != Binding.NONE;
        hasActiveLongPress = firstLP != Binding.NONE;
        hasActiveLongPressDrag = firstLPD != Binding.NONE;
        hasActiveDoubleTapDrag = firstDTD != Binding.NONE;
        hasActiveSingleTapDrag = firstSTD != Binding.NONE;
        canHoldLongPress = !hasActiveLongPressDrag && hasActiveLongPress &&
            !firstLP.isMouseMove() && firstLP != Binding.MOUSE_SCROLL_UP && firstLP != Binding.MOUSE_SCROLL_DOWN;
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
                    executeActions(pendingDoubleTapAction);
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

        if (state == GestureState.DRAGGING) {
            touchpadView.movePointer(cx, cy);
            return;
        }

        float dx = cx - fingerDownX;
        float dy = cy - fingerDownY;
        if (dx * dx + dy * dy > dragThresholdSq) {
            touchpadView.removeCallbacks(longPressRunnable);

            List<Binding> dragBinding = resolveDragAction();
            if (dragBinding != null && firstBinding(dragBinding, Binding.NONE) != Binding.NONE) {
                if (!isActionHeld || !dragBinding.equals(heldActions)) {
                    releaseHeldAction();
                    executeActionsAndHold(dragBinding);
                }
                pendingDoubleTapAction = null;
            }

            state = GestureState.DRAGGING;
            touchpadView.movePointer(cx, cy);
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
                            executeActions(pendingDoubleTapAction);
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
                        executeActions(activeLongPressAction);
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
                    executeActions(activeSingleTapAction);
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
            touchpadView.postDelayed(doubleTapRunnable, doubleTapTimeout);
            state = GestureState.DOUBLE_TAP_WAITING;
        }
        else {
            executeActions(activeSingleTapAction);
            if (hasActiveDoubleTapDrag) {
                touchpadView.postDelayed(doubleTapRunnable, doubleTapTimeout);
                state = GestureState.DOUBLE_TAP_WAITING;
            }
            else {
                state = GestureState.IDLE;
            }
        }
    }

    private void handleCancel() {
        reset();
    }

    // --- Timer callbacks ---

    private void onLongPressTimer() {
        if (state != GestureState.TAP_WAITING) return;

        if (canHoldLongPress) {
            executeActionsAndHold(activeLongPressAction);
        }
        state = GestureState.LONG_PRESSING;

        if (longPressHapticEnabled && (hasActiveLongPress || hasActiveLongPressDrag)) {
            com.winlator.cmod.core.AppUtils.performHapticFeedback(touchpadView.getContext(), longPressHapticIntensity);
        }
    }

    private void onDoubleTapTimer() {
        if (state != GestureState.DOUBLE_TAP_WAITING) return;
        if (deferredTapAction != null) {
            executeActions(deferredTapAction);
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

    private List<Binding> resolveDragAction() {
        if (state == GestureState.LONG_PRESSING) {
            return hasActiveLongPressDrag ? activeLongPressDragAction : activeLongPressAction;
        }
        else if (postDoubleTapDrag) {
            List<Binding> fallback = hasActiveDoubleTap ? activeDoubleTapAction : activeSingleTapAction;
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

    private void executeActions(List<Binding> actions) {
        if (actions == null) return;
        int size = actions.size();
        if (inputControlsView != null) {
            for (int i = 0; i < size; i++) {
                Binding b = actions.get(i);
                if (b == null || b == Binding.NONE) continue;
                Binding kb = b.toKeyboardBinding();
                if (kb != null) inputControlsView.handleInputEvent(kb, true, 0);
            }
        }
        for (int i = 0; i < size; i++) {
            Binding b = actions.get(i);
            if (b == null || b == Binding.NONE) continue;
            if (b == Binding.MOUSE_SCROLL_UP || b == Binding.MOUSE_SCROLL_DOWN) continue;
            if (b.isMouseMove()) continue;
            if (b.isModifier()) continue;

            if (inputControlsView != null) {
                inputControlsView.handleInputEvent(b, true, 0);
                inputControlsView.handleInputEvent(b, false, 0);
            }
            if (bindingDelay > 0 && i < size - 1) SystemClock.sleep(bindingDelay);
        }
        if (inputControlsView != null) {
            for (int i = size - 1; i >= 0; i--) {
                Binding b = actions.get(i);
                if (b == null || b == Binding.NONE) continue;
                Binding kb = b.toKeyboardBinding();
                if (kb != null) inputControlsView.handleInputEvent(kb, false, 0);
            }
        }
    }

    private void executeActionsAndHold(List<Binding> actions) {
        if (actions == null) return;
        heldActions.clear();
        heldModifiers.clear();
        int size = actions.size();
        if (inputControlsView != null) {
            for (int i = 0; i < size; i++) {
                Binding b = actions.get(i);
                if (b == null || b == Binding.NONE) continue;
                Binding kb = b.toKeyboardBinding();
                if (kb != null) {
                    inputControlsView.handleInputEvent(kb, true, 0);
                    heldModifiers.add(kb);
                }
            }
        }
        for (int i = 0; i < size; i++) {
            Binding b = actions.get(i);
            if (b == null || b == Binding.NONE) continue;
            if (b.isMouseMove()) continue;
            if (b == Binding.MOUSE_SCROLL_UP || b == Binding.MOUSE_SCROLL_DOWN) continue;
            if (b.isModifier()) continue;

            if (inputControlsView != null) {
                inputControlsView.handleInputEvent(b, true, 0);
                heldActions.add(b);
            }
            if (bindingDelay > 0 && i < size - 1) SystemClock.sleep(bindingDelay);
        }
        isActionHeld = true;
    }

    private void releaseHeldAction() {
        if (isActionHeld && inputControlsView != null) {
            for (Binding b : heldActions) {
                inputControlsView.handleInputEvent(b, false, 0);
            }
            for (Binding b : heldModifiers) {
                inputControlsView.handleInputEvent(b, false, 0);
            }
        }
        heldActions.clear();
        heldModifiers.clear();
        isActionHeld = false;
    }

    private void removeAllCallbacks() {
        touchpadView.removeCallbacks(longPressRunnable);
        touchpadView.removeCallbacks(doubleTapRunnable);
    }
}
