package com.winlator.cmod.widget;

import android.util.Log;

import com.winlator.cmod.BuildConfig;
import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.xserver.Pointer;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class TouchpadGestureHandler {
    private static final String TAG = "TouchpadGesture";

    private enum State {
        IDLE, TAP_WAITING, DOUBLE_TAP_WAITING, LONG_PRESSING, DRAGGING;

        // Debug helper: only used in debug builds
        static String nameOf(State s) {
            if (s == null) return "null";
            switch (s) {
                case IDLE: return "IDLE";
                case TAP_WAITING: return "TAP_WAITING";
                case DOUBLE_TAP_WAITING: return "DOUBLE_TAP_WAITING";
                case LONG_PRESSING: return "LONG_PRESSING";
                case DRAGGING: return "DRAGGING";
                default: return s.name();
            }
        }
    }

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
    private List<Binding> singleTapDragAction = Collections.singletonList(Binding.NONE);
    private List<Binding> longPressDragAction = Collections.singletonList(Binding.NONE);
    private List<Binding> doubleTapDragAction = Collections.singletonList(Binding.NONE);
    private List<Binding> singleTap2ndFingerDragAction = Collections.singletonList(Binding.NONE);
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
    private List<Binding> activeSingleTapDragAction;
    private boolean hasActiveDoubleTap;
    private boolean hasActiveLongPress;
    private boolean hasActiveLongPressDrag;
    private boolean hasActiveDoubleTapDrag;
    private boolean hasActiveSingleTapDrag;
    private boolean canHoldLongPress;

    // Cached firstBinding values
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
    private float mainFingerX;
    private float mainFingerY;
    private int mainPointerId = -1;
    private boolean secondFingerActive;
    private List<Binding> deferredTapAction;
    private boolean postDoubleTapDrag;
    private List<Binding> pendingDoubleTapAction;
    private List<Binding> pendingDeferredDoubleAction;
    private List<Binding> pendingSecondTapAction;
    private boolean doubleTapConsumed;
    private boolean secondFingerDoubleTapWaiting;
    private List<Binding> secondFingerDoubleTapFallback;

    // Timer callback references
    private final Runnable longPressRunnable = this::onLongPressTimer;
    private final Runnable doubleTapRunnable = this::onDoubleTapTimer;
    private final Runnable secondFingerDoubleTapRunnable = this::onSecondFingerDoubleTapTimer;

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
        if (BuildConfig.DEBUG) Log.d(TAG, "applyConfig");
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
        doubleTapTimeout = profile.getDoubleTapTimeout();
        longPressTimeout = profile.getLongPressTimeout();
        hapticFeedbackEnabled = profile.getHapticFeedbackEnabled();
        dragThreshold = profile.getDragThreshold();
        dragThresholdSq = dragThreshold * dragThreshold;
        if (actionExecutor != null) actionExecutor.setBindingDelay(profile.getBindingDelay());

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
        activeLongPressDragAction = secondFingerActive ? longPress2ndFingerDragAction : longPressDragAction;
        activeDoubleTapDragAction = secondFingerActive ? doubleTap2ndFingerDragAction : doubleTapDragAction;
        activeSingleTapDragAction = secondFingerActive ? singleTap2ndFingerDragAction : singleTapDragAction;
        Binding firstLP = secondFingerActive ? firstLongPress2nd : firstLongPress;
        Binding firstDT = secondFingerActive ? firstDoubleTap2nd : firstDoubleTap;
        Binding firstLPD = secondFingerActive ? firstLongPressDrag2nd : firstLongPressDrag;
        Binding firstDTD = secondFingerActive ? firstDoubleTapDrag2nd : firstDoubleTapDrag;
        Binding firstSTD = secondFingerActive ? firstSingleTapDrag2nd : firstSingleTapDrag;
        hasActiveDoubleTap = firstDT != Binding.NONE;
        hasActiveLongPress = firstLP != Binding.NONE;
        hasActiveLongPressDrag = firstLPD != Binding.NONE;
        hasActiveDoubleTapDrag = firstDTD != Binding.NONE;
        hasActiveSingleTapDrag = firstSTD != Binding.NONE;
        canHoldLongPress = !hasActiveLongPressDrag && !hasActiveSingleTapDrag && hasActiveLongPress &&
            !firstLP.isMouseMove() && firstLP != Binding.MOUSE_SCROLL_UP && firstLP != Binding.MOUSE_SCROLL_DOWN;
    }

    // --- Hook: finger down ---
    public boolean onFingerDown(int pointerId, float x, float y) {
        if (BuildConfig.DEBUG) Log.d(TAG, "onFingerDown ptr=" + pointerId + " (" + x + "," + y + ") state=" + State.nameOf(state) + " mainPtr=" + mainPointerId);

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
                if (BuildConfig.DEBUG) Log.d(TAG, "  → double tap confirmed on finger down");
                handleDoubleTapConfirmed();
            }
            else {
                if (hasActiveLongPress || hasActiveLongPressDrag) {
                    touchpadView.postDelayed(longPressRunnable, longPressTimeout);
                    if (BuildConfig.DEBUG) Log.d(TAG, "  → long press timer posted (" + longPressTimeout + "ms)");
                }
            }
            state = State.TAP_WAITING;
            if (BuildConfig.DEBUG) Log.d(TAG, "  → state = TAP_WAITING");
            return false;
        }
        else if (pointerId != mainPointerId) {
            if (secondFingerDoubleTapWaiting) {
                if (BuildConfig.DEBUG) Log.d(TAG, "  → second-finger double tap confirmed");
                secondFingerDoubleTapWaiting = false;
                touchpadView.removeCallbacks(secondFingerDoubleTapRunnable);
                setSecondFingerActive(true);

                if (BuildConfig.DEBUG) Log.d(TAG, "  → execute double tap: " + activeDoubleTapAction);
                actionExecutor.executeActions(activeDoubleTapAction);
                secondFingerDoubleTapFallback = null;

                if (hasActiveDoubleTapDrag) postDoubleTapDrag = true;

                touchpadView.removeCallbacks(longPressRunnable);
                if (hasActiveLongPress || hasActiveLongPressDrag) {
                    touchpadView.postDelayed(longPressRunnable, longPressTimeout);
                }
                pendingSecondTapAction = null;
                state = State.TAP_WAITING;
                return true;
            }

            if (BuildConfig.DEBUG) Log.d(TAG, "  → second finger action");
            setSecondFingerActive(true);
            if (BuildConfig.DEBUG) Log.d(TAG, "  → second finger: hasLP=" + hasActiveLongPress + " hasLPD=" + hasActiveLongPressDrag + " hasDT=" + hasActiveDoubleTap + " hasDTD=" + hasActiveDoubleTapDrag + " hasSTD=" + hasActiveSingleTapDrag);

            if (state == State.DOUBLE_TAP_WAITING) {
                handleDoubleTapConfirmed();
                setSecondFingerActive(true);
            }

            if (actionExecutor.isActionHeld()) {
                actionExecutor.releaseHeldAction();
            }

            // Anchor first finger position for drag threshold
            fingerDownX = mainFingerX;
            fingerDownY = mainFingerY;

            // Always defer second-finger tap — execute only on lift if no drag/longpress consumed it
            pendingSecondTapAction = activeSingleTapAction;
            if (BuildConfig.DEBUG) Log.d(TAG, "  → pending second-finger tap (will execute on lift if no drag): " + pendingSecondTapAction);

            // Post long press timer for second finger
            touchpadView.removeCallbacks(longPressRunnable);
            if (hasActiveLongPress || hasActiveLongPressDrag) {
                touchpadView.postDelayed(longPressRunnable, longPressTimeout);
                if (BuildConfig.DEBUG) Log.d(TAG, "  → long press timer posted for second finger (" + longPressTimeout + "ms)");
            }

            state = State.TAP_WAITING;
            if (BuildConfig.DEBUG) Log.d(TAG, "  → state = TAP_WAITING (second finger deferred)");
            return true;
        }
        return false;
    }

    // --- Hook: finger move ---
    public boolean onFingerMove(int pointerId, float x, float y) {
        if (BuildConfig.DEBUG) {
            if (pointerId == mainPointerId && state != State.IDLE)
                Log.d(TAG, "onFingerMove ptr=" + pointerId + " (" + x + "," + y + ") state=" + State.nameOf(state) + " dragDist=" + (Math.hypot(x - fingerDownX, y - fingerDownY)));
        }
        if (pointerId != mainPointerId) return false;

        mainFingerX = x;
        mainFingerY = y;

        if (state == State.DOUBLE_TAP_WAITING || state == State.IDLE) return false;

        if (state == State.DRAGGING) {
            if (BuildConfig.DEBUG) Log.d(TAG, "  → already DRAGGING");
            return true;
        }

        float dx = x - fingerDownX;
        float dy = y - fingerDownY;
        if (dx * dx + dy * dy > dragThresholdSq) {
            touchpadView.removeCallbacks(longPressRunnable);

            List<Binding> dragBinding = resolveDragAction();
            if (BuildConfig.DEBUG) Log.d(TAG, "  → drag threshold exceeded, resolveDragAction=" + dragBinding + " secondFingerActive=" + secondFingerActive + " hasSTD=" + hasActiveSingleTapDrag + " hasLPD=" + hasActiveLongPressDrag + " hasDTD=" + hasActiveDoubleTapDrag);
            if (dragBinding != null && firstBinding(dragBinding, Binding.NONE) != Binding.NONE) {
                if (!actionExecutor.isActionHeld() || !dragBinding.equals(actionExecutor.getHeldActions())) {
                    actionExecutor.releaseHeldAction();
                    actionExecutor.executeActionsAndHold(dragBinding);
                }
                pendingDoubleTapAction = null;
                pendingSecondTapAction = null;
                state = State.DRAGGING;
                if (BuildConfig.DEBUG) Log.d(TAG, "  → state = DRAGGING");
                return true;
            }
        }
        return false;
    }

    // --- Hook: finger up ---
    public void onFingerUp(int pointerId) {
        if (BuildConfig.DEBUG) Log.d(TAG, "onFingerUp ptr=" + pointerId + " state=" + State.nameOf(state) + " mainPtr=" + mainPointerId + " secondActive=" + secondFingerActive);
        if (pointerId != mainPointerId) {
            if (secondFingerActive) {
                if (BuildConfig.DEBUG) Log.d(TAG, "  → second finger lifted state=" + State.nameOf(state));
                if (pendingSecondTapAction != null && hasActiveDoubleTap) {
                    if (BuildConfig.DEBUG) Log.d(TAG, "  → waiting for second-finger double tap");
                    secondFingerDoubleTapWaiting = true;
                    secondFingerDoubleTapFallback = pendingSecondTapAction;
                    pendingSecondTapAction = null;
                    touchpadView.postDelayed(secondFingerDoubleTapRunnable, doubleTapTimeout);
                }
                else if (pendingSecondTapAction != null) {
                    if (BuildConfig.DEBUG) Log.d(TAG, "  → execute pending second-finger tap: " + pendingSecondTapAction);
                    actionExecutor.executeActions(pendingSecondTapAction);
                    pendingSecondTapAction = null;
                }
                actionExecutor.releaseHeldAction();
                if (touchpadView.getXServer() != null) {
                    if (BuildConfig.DEBUG) Log.d(TAG, "  → releasing LEFT+RIGHT via xServer");
                    touchpadView.getXServer().injectPointerButtonRelease(Pointer.Button.BUTTON_LEFT);
                    touchpadView.getXServer().injectPointerButtonRelease(Pointer.Button.BUTTON_RIGHT);
                }
                touchpadView.removeCallbacks(longPressRunnable);

                if (state == State.LONG_PRESSING && hasActiveLongPress && !canHoldLongPress) {
                    if (BuildConfig.DEBUG) Log.d(TAG, "  → execute second-finger long press on lift: " + activeLongPressAction);
                    actionExecutor.executeActions(activeLongPressAction);
                }

                postDoubleTapDrag = false;
                if (!secondFingerDoubleTapWaiting && state != State.DRAGGING) state = State.IDLE;
                if (BuildConfig.DEBUG) Log.d(TAG, "  → state = " + State.nameOf(state));
                setSecondFingerActive(false);
            }
            return;
        }

        touchpadView.removeCallbacks(longPressRunnable);

        switch (state) {
            case TAP_WAITING:
                if (BuildConfig.DEBUG) Log.d(TAG, "  → TAP_WAITING → handleTapUp");
                handleTapUp();
                mainPointerId = -1;
                setSecondFingerActive(false);
                break;
            case LONG_PRESSING:
                if (BuildConfig.DEBUG) Log.d(TAG, "  → LONG_PRESSING");
                if (actionExecutor.isActionHeld()) {
                    actionExecutor.releaseHeldAction();
                }
                else if (hasActiveLongPress) {
                    if (BuildConfig.DEBUG) Log.d(TAG, "  → execute long press: " + activeLongPressAction);
                    actionExecutor.executeActions(activeLongPressAction);
                }
                postDoubleTapDrag = false;
                mainPointerId = -1;
                setSecondFingerActive(false);
                state = State.IDLE;
                if (BuildConfig.DEBUG) Log.d(TAG, "  → state = IDLE");
                break;
            case DRAGGING:
                if (BuildConfig.DEBUG) Log.d(TAG, "  → DRAGGING → release");
                actionExecutor.releaseHeldAction();
                postDoubleTapDrag = false;
                mainPointerId = -1;
                setSecondFingerActive(false);
                state = State.IDLE;
                if (BuildConfig.DEBUG) Log.d(TAG, "  → state = IDLE");
                break;
            case DOUBLE_TAP_WAITING:
                if (BuildConfig.DEBUG) Log.d(TAG, "  → DOUBLE_TAP_WAITING → execute fallback, release");
                deferredTapAction = null;
                actionExecutor.executeActions(activeSingleTapAction);
                postDoubleTapDrag = false;
                mainPointerId = -1;
                setSecondFingerActive(false);
                state = State.IDLE;
                break;
            default:
                postDoubleTapDrag = false;
                mainPointerId = -1;
                setSecondFingerActive(false);
                state = State.IDLE;
                if (BuildConfig.DEBUG) Log.d(TAG, "  → state = IDLE (default)");
                break;
        }
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

    public void reset() {
        if (BuildConfig.DEBUG) Log.d(TAG, "reset (state was " + State.nameOf(state) + ")");
        removeAllCallbacks();
        if (actionExecutor != null) actionExecutor.releaseHeldAction();
        state = State.IDLE;
        mainPointerId = -1;
        setSecondFingerActive(false);
        deferredTapAction = null;
        postDoubleTapDrag = false;
        pendingDoubleTapAction = null;
        pendingDeferredDoubleAction = null;
        pendingSecondTapAction = null;
        doubleTapConsumed = false;
        secondFingerDoubleTapWaiting = false;
        secondFingerDoubleTapFallback = null;
    }

    // --- Internal gesture logic ---

    private void handleTapUp() {
        if (BuildConfig.DEBUG) Log.d(TAG, "handleTapUp hasActiveDoubleTap=" + hasActiveDoubleTap + " hasActiveDoubleTapDrag=" + hasActiveDoubleTapDrag + " pendingDoubleTap=" + (pendingDoubleTapAction != null ? pendingDoubleTapAction.toString() : "null"));

        if (pendingDoubleTapAction != null) {
            if (BuildConfig.DEBUG) Log.d(TAG, "  → execute deferred double tap on finger up: " + pendingDoubleTapAction);
            actionExecutor.executeActions(pendingDoubleTapAction);
            pendingDoubleTapAction = null;
            deferredTapAction = null;
            pendingDeferredDoubleAction = null;
            postDoubleTapDrag = false;
            doubleTapConsumed = true;
            state = State.IDLE;
            return;
        }

        if (postDoubleTapDrag) {
            if (BuildConfig.DEBUG) Log.d(TAG, "  → post-double-tap lift, not starting another cycle");
            deferredTapAction = null;
            pendingDeferredDoubleAction = null;
            postDoubleTapDrag = false;
            doubleTapConsumed = true;
            state = State.IDLE;
            return;
        }

        if (doubleTapConsumed) {
            if (BuildConfig.DEBUG) Log.d(TAG, "  → double tap already consumed, treating as single tap");
            doubleTapConsumed = false;
            actionExecutor.executeActions(activeSingleTapAction);
            state = State.IDLE;
            return;
        }

        pendingDeferredDoubleAction = hasActiveDoubleTap ? activeDoubleTapAction : activeSingleTapAction;

        if (hasActiveDoubleTap) {
            deferredTapAction = activeSingleTapAction;
            touchpadView.postDelayed(doubleTapRunnable, doubleTapTimeout);
            if (BuildConfig.DEBUG) Log.d(TAG, "  → waiting for double tap (" + doubleTapTimeout + "ms), deferred=" + (deferredTapAction != null ? deferredTapAction.toString() : "null"));
            state = State.DOUBLE_TAP_WAITING;
        }
        else {
            if (BuildConfig.DEBUG) Log.d(TAG, "  → execute single tap: " + activeSingleTapAction);
            actionExecutor.executeActions(activeSingleTapAction);
            if (hasActiveDoubleTapDrag) {
                touchpadView.postDelayed(doubleTapRunnable, doubleTapTimeout);
                if (BuildConfig.DEBUG) Log.d(TAG, "  → waiting for double-tap-drag (" + doubleTapTimeout + "ms)");
                state = State.DOUBLE_TAP_WAITING;
            }
            else {
                state = State.IDLE;
                if (BuildConfig.DEBUG) Log.d(TAG, "  → state = IDLE");
            }
        }
    }

    private void onLongPressTimer() {
        if (BuildConfig.DEBUG) Log.d(TAG, "onLongPressTimer state=" + State.nameOf(state) + " canHold=" + canHoldLongPress + " hasLP=" + hasActiveLongPress + " second=" + secondFingerActive + " hasLP_2nd=" + hasActiveLongPressDrag + " hasSTD=" + hasActiveSingleTapDrag);
        if (state != State.TAP_WAITING) return;

        pendingSecondTapAction = null;

        if (canHoldLongPress) {
            if (BuildConfig.DEBUG) Log.d(TAG, "  → execute and hold: " + activeLongPressAction);
            actionExecutor.executeActionsAndHold(activeLongPressAction);
        }
        state = State.LONG_PRESSING;
        if (BuildConfig.DEBUG) Log.d(TAG, "  → state = LONG_PRESSING");

        if (hapticFeedbackEnabled && (hasActiveLongPress || hasActiveLongPressDrag)) {
            com.winlator.cmod.core.AppUtils.performHapticFeedback(touchpadView.getContext(), 255);
        }
    }

    private void onDoubleTapTimer() {
        if (BuildConfig.DEBUG) Log.d(TAG, "onDoubleTapTimer state=" + State.nameOf(state) + " deferredTapAction=" + (deferredTapAction != null ? deferredTapAction.toString() : "null"));
        if (state != State.DOUBLE_TAP_WAITING) return;
        if (deferredTapAction != null) {
            if (BuildConfig.DEBUG) Log.d(TAG, "  → execute deferred tap: " + deferredTapAction);
            actionExecutor.executeActions(deferredTapAction);
            deferredTapAction = null;
        }
        pendingDeferredDoubleAction = null;
        state = State.IDLE;
        if (BuildConfig.DEBUG) Log.d(TAG, "  → state = IDLE");
    }

    private void onSecondFingerDoubleTapTimer() {
        if (BuildConfig.DEBUG) Log.d(TAG, "onSecondFingerDoubleTapTimer waiting=" + secondFingerDoubleTapWaiting);
        if (secondFingerDoubleTapWaiting) {
            secondFingerDoubleTapWaiting = false;
            if (secondFingerDoubleTapFallback != null) {
                if (BuildConfig.DEBUG) Log.d(TAG, "  → execute second-finger single tap fallback: " + secondFingerDoubleTapFallback);
                actionExecutor.executeActions(secondFingerDoubleTapFallback);
                secondFingerDoubleTapFallback = null;
            }
            if (state != State.DRAGGING) state = State.IDLE;
        }
    }

    private void handleDoubleTapConfirmed() {
        if (BuildConfig.DEBUG) Log.d(TAG, "handleDoubleTapConfirmed pendingDeferred=" + (pendingDeferredDoubleAction != null ? pendingDeferredDoubleAction.toString() : "null") + " deferredTap=" + (deferredTapAction != null ? deferredTapAction.toString() : "null"));
        touchpadView.removeCallbacks(doubleTapRunnable);

        if (deferredTapAction != null) {
            deferredTapAction = null;
            pendingDoubleTapAction = pendingDeferredDoubleAction;
            pendingDeferredDoubleAction = null;
        }

        if (pendingDoubleTapAction != null) {
            if (hasActiveDoubleTapDrag) {
                if (BuildConfig.DEBUG) Log.d(TAG, "  → pending double tap (will execute on finger up if no drag)");
            } else {
                if (BuildConfig.DEBUG) Log.d(TAG, "  → execute double tap: " + pendingDoubleTapAction);
                actionExecutor.executeActions(pendingDoubleTapAction);
                pendingDoubleTapAction = null;
            }
        }

        setSecondFingerActive(false);
        state = State.IDLE;
        postDoubleTapDrag = true;
        if (BuildConfig.DEBUG) Log.d(TAG, "  → state = IDLE, postDoubleTapDrag=true");
    }

    private List<Binding> resolveDragAction() {
        if (state == State.LONG_PRESSING) {
            if (secondFingerActive) {
                if (BuildConfig.DEBUG) Log.d(TAG, "resolveDragAction: LONG_PRESSING+2nd hasLPD=" + hasActiveLongPressDrag + " hasSTD=" + hasActiveSingleTapDrag);
                if (hasActiveLongPressDrag) return activeLongPressDragAction;
                if (hasActiveSingleTapDrag) return activeSingleTapDragAction;
                return null;
            }
            if (BuildConfig.DEBUG) Log.d(TAG, "resolveDragAction: LONG_PRESSING hasDrag=" + hasActiveLongPressDrag);
            return hasActiveLongPressDrag ? activeLongPressDragAction : null;
        }
        else if (postDoubleTapDrag) {
            if (BuildConfig.DEBUG) Log.d(TAG, "resolveDragAction: postDoubleTapDrag hasDrag=" + hasActiveDoubleTapDrag + " second=" + secondFingerActive);
            return hasActiveDoubleTapDrag ? activeDoubleTapDragAction : null;
        }
        else if (secondFingerActive) {
            if (BuildConfig.DEBUG) Log.d(TAG, "resolveDragAction: second-finger-deferred hasSTD=" + hasActiveSingleTapDrag + " hasLPD=" + hasActiveLongPressDrag + " hasDTD=" + hasActiveDoubleTapDrag);
            if (hasActiveSingleTapDrag) return activeSingleTapDragAction;
            if (hasActiveLongPressDrag) return activeLongPressDragAction;
            if (hasActiveDoubleTapDrag) return activeDoubleTapDragAction;
            return null;
        }
        else {
            if (BuildConfig.DEBUG) Log.d(TAG, "resolveDragAction: TAP_WAITING → null");
            return null;
        }
    }

    private void removeAllCallbacks() {
        touchpadView.removeCallbacks(longPressRunnable);
        touchpadView.removeCallbacks(doubleTapRunnable);
        touchpadView.removeCallbacks(secondFingerDoubleTapRunnable);
    }
}
