package com.winlator.cmod.widget;

import android.content.Context;
import android.os.Build;
import android.os.SystemClock;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.MotionEvent;

import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.inputcontrols.SecondFingerMode;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class TouchscreenGestureHandler {
    private static final byte TAP_TRAVEL_THRESHOLD = 10;

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
        secondFingerMode = profile.getSecondFingerMode();
        isLongTapMode = secondFingerMode == SecondFingerMode.LONG_TAP_ACTION;
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
        Binding firstDoubleTap = firstBinding(activeDoubleTapAction, Binding.NONE);
        Binding firstLongPress = firstBinding(activeLongPressAction, Binding.NONE);
        Binding firstLongPressDrag = firstBinding(activeLongPressDragAction, Binding.NONE);
        Binding firstSingleTapDrag = firstBinding(activeSingleTapDragAction, Binding.NONE);
        Binding firstDoubleTapDrag = firstBinding(activeDoubleTapDragAction, Binding.NONE);
        hasActiveDoubleTap = firstDoubleTap != Binding.NONE;
        hasActiveLongPress = firstLongPress != Binding.NONE;
        hasActiveLongPressDrag = firstLongPressDrag != Binding.NONE;
        hasActiveDoubleTapDrag = firstDoubleTapDrag != Binding.NONE;
        hasActiveSingleTapDrag = firstSingleTapDrag != Binding.NONE;
        canHoldLongPress = !hasActiveLongPressDrag && hasActiveLongPress &&
            !firstLongPress.isMouseMove() &&
            firstLongPress != Binding.MOUSE_SCROLL_UP &&
            firstLongPress != Binding.MOUSE_SCROLL_DOWN;
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
        float distance = (float) Math.hypot(cx - fingerDownX, cy - fingerDownY);

        if (distance > TAP_TRAVEL_THRESHOLD) {
            touchpadView.removeCallbacks(longPressRunnable);

            if (state == GestureState.TAP_WAITING || state == GestureState.LONG_PRESSING) {
                List<Binding> dragBinding = resolveDragAction();

                if (dragBinding != null && firstBinding(dragBinding, Binding.NONE) != Binding.NONE) {
                    if (!isActionHeld || !dragBinding.equals(heldActions)) {
                        releaseHeldAction();
                        executeActionsAndHold(dragBinding);
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
        }
        else {
            executeActions(activeSingleTapAction);
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
            executeActionsAndHold(activeLongPressAction);
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
