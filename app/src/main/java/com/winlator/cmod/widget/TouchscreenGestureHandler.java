package com.winlator.cmod.widget;

import android.view.MotionEvent;

import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.inputcontrols.DragMode;
import com.winlator.cmod.inputcontrols.InputMode;

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
    private boolean deferSingleTap = false;

    // Gesture state
    private float fingerDownX;
    private float fingerDownY;
    private int mainPointerId = -1;
    private boolean hasSecondFingerHeld;

    // Held action tracking
    private Binding activeAction;
    private boolean isActionHeld;

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
        this.doubleTapTimeout = profile.getDoubleTapTimeout();
        this.longPressTimeout = profile.getLongPressTimeout();
        this.deferSingleTap = profile.isDeferSingleTap();
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
        hasSecondFingerHeld = false;
    }

    // --- Down handling ---

    private void handlePointerDown(MotionEvent event, int pointerId, int actionIndex) {
        if (mainPointerId < 0) {
            // First finger down — begin gesture tracking
            mainPointerId = pointerId;
            fingerDownX = event.getX(actionIndex);
            fingerDownY = event.getY(actionIndex);
            hasSecondFingerHeld = event.getPointerCount() > 1;

            // Cancel any pending double tap
            touchpadView.removeCallbacks(doubleTapRunnable);

            // Cancel any pending long press from previous gesture
            touchpadView.removeCallbacks(longPressRunnable);

            // If we were waiting for a double tap, handle it now
            if (state == GestureState.DOUBLE_TAP_WAITING) {
                handleDoubleTapConfirmed();
                return;
            }

            // Start long press timer
            touchpadView.postDelayed(longPressRunnable, longPressTimeout);
            state = GestureState.TAP_WAITING;
        }
        else if (pointerId != mainPointerId) {
            // Second finger
            hasSecondFingerHeld = true;

            // Cancel first finger's long press (two fingers = no long press)
            touchpadView.removeCallbacks(longPressRunnable);
            // If first finger was long pressing, release it
            if (state == GestureState.LONG_PRESSING) {
                releaseHeldAction();
                state = GestureState.TAP_WAITING;
            }
        }
    }

    // --- Move handling ---

    private void handlePointerMove(MotionEvent event) {
        if (state == GestureState.DOUBLE_TAP_WAITING || state == GestureState.IDLE) return;

        int mainIndex = event.findPointerIndex(mainPointerId);
        if (mainIndex < 0) return;  // main finger lifted

        float cx = event.getX(mainIndex);
        float cy = event.getY(mainIndex);
        float distance = (float) Math.hypot(cx - fingerDownX, cy - fingerDownY);

        if (distance > TAP_TRAVEL_THRESHOLD) {
            // Finger moved beyond tap threshold
            touchpadView.removeCallbacks(longPressRunnable);

            if (state == GestureState.TAP_WAITING || state == GestureState.LONG_PRESSING) {
                // Determine drag binding
                Binding dragBinding = resolveCurrentAction();
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
            // Main finger lifted
            touchpadView.removeCallbacks(longPressRunnable);

            switch (state) {
                case TAP_WAITING:
                    if (!deferSingleTap) {
                        executeAction(resolveAction(singleTapAction, singleTap2ndFingerAction));
                    }
                    lastTapUpTime = System.currentTimeMillis();
                    touchpadView.postDelayed(doubleTapRunnable, doubleTapTimeout);
                    state = GestureState.DOUBLE_TAP_WAITING;
                    break;
                case LONG_PRESSING:
                    releaseHeldAction();
                    state = GestureState.IDLE;
                    break;
                case DRAGGING:
                    releaseHeldAction();
                    state = GestureState.IDLE;
                    break;
                case DOUBLE_TAP_WAITING:
                    // Second tap came from a different finger? Fallback to single tap
                    if (!deferSingleTap) {
                        executeAction(resolveAction(singleTapAction, singleTap2ndFingerAction));
                    }
                    state = GestureState.IDLE;
                    break;
                default:
                    state = GestureState.IDLE;
                    break;
            }

            mainPointerId = -1;
            hasSecondFingerHeld = false;
        }
        else {
            // Second finger lifted
            hasSecondFingerHeld = false;
        }
    }

    private void handleCancel() {
        reset();
    }

    // --- Timer callbacks ---

    private void onLongPressTimer() {
        if (state != GestureState.TAP_WAITING) return;

        Binding action = resolveAction(longPressAction, longPress2ndFingerAction);
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

        if (deferSingleTap) {
            executeAction(resolveAction(singleTapAction, singleTap2ndFingerAction));
        }
        state = GestureState.IDLE;
    }

    private void handleDoubleTapConfirmed() {
        touchpadView.removeCallbacks(doubleTapRunnable);

        Binding action = resolveAction(doubleTapAction, doubleTap2ndFingerAction);
        if (action != Binding.NONE) {
            executeAction(action);
        }
        else {
            // Auto-detect: fire two single taps
            Binding single = resolveAction(singleTapAction, singleTap2ndFingerAction);
            executeAction(single);
            executeAction(single);
        }

        // Stay in TAP_WAITING for the new touch sequence
        fingerDownX = -1;
        fingerDownY = -1;
        state = GestureState.TAP_WAITING;
    }

    // --- Action execution ---

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

    // --- Helpers ---

    private Binding resolveCurrentAction() {
        return state == GestureState.LONG_PRESSING
                ? resolveAction(longPressAction, longPress2ndFingerAction)
                : resolveAction(singleTapAction, singleTap2ndFingerAction);
    }

    private Binding resolveAction(Binding primary, Binding secondary) {
        return hasSecondFingerHeld ? secondary : primary;
    }

    private void removeAllCallbacks() {
        touchpadView.removeCallbacks(longPressRunnable);
        touchpadView.removeCallbacks(doubleTapRunnable);
    }
}
