package com.winlator.cmod.widget;

import android.os.SystemClock;

import com.winlator.cmod.BuildConfig;
import com.winlator.cmod.inputcontrols.Binding;

import java.util.ArrayList;
import java.util.List;

public class GestureActionExecutor {
    private static final String TAG = "TouchpadGesture";

    private final InputControlsView inputControlsView;
    private final List<Binding> heldActions = new ArrayList<>();
    private final List<Binding> heldModifiers = new ArrayList<>();
    private boolean isActionHeld;
    private int bindingDelay;

    public GestureActionExecutor(InputControlsView inputControlsView) {
        this.inputControlsView = inputControlsView;
    }

    public void setBindingDelay(int delay) {
        this.bindingDelay = delay;
    }

    public boolean isActionHeld() {
        return isActionHeld;
    }

    public List<Binding> getHeldActions() {
        return heldActions;
    }

    public void executeActions(List<Binding> actions) {
        if (BuildConfig.DEBUG) android.util.Log.d(TAG, "executeActions: " + actions);
        if (actions == null) return;
        int size = actions.size();
        for (int i = 0; i < size; i++) {
            Binding b = actions.get(i);
            if (b == null || b == Binding.NONE) continue;
            Binding kb = b.toKeyboardBinding();
            if (kb != null) {
                if (BuildConfig.DEBUG) android.util.Log.d(TAG, "  press modifier: " + kb);
                inputControlsView.handleInputEvent(kb, true, 0);
            }
        }
        for (int i = 0; i < size; i++) {
            Binding b = actions.get(i);
            if (b == null || b == Binding.NONE) continue;
            if (b == Binding.MOUSE_SCROLL_UP || b == Binding.MOUSE_SCROLL_DOWN) continue;
            if (b.isMouseMove()) continue;
            if (b.isModifier()) continue;

            if (BuildConfig.DEBUG) android.util.Log.d(TAG, "  press+release: " + b);
            inputControlsView.handleInputEvent(b, true, 0);
            inputControlsView.handleInputEvent(b, false, 0);
            if (bindingDelay > 0 && i < size - 1) SystemClock.sleep(bindingDelay);
        }
        for (int i = size - 1; i >= 0; i--) {
            Binding b = actions.get(i);
            if (b == null || b == Binding.NONE) continue;
            Binding kb = b.toKeyboardBinding();
            if (kb != null) {
                if (BuildConfig.DEBUG) android.util.Log.d(TAG, "  release modifier: " + kb);
                inputControlsView.handleInputEvent(kb, false, 0);
            }
        }
    }

    public void executeActionsAndHold(List<Binding> actions) {
        if (BuildConfig.DEBUG) android.util.Log.d(TAG, "executeActionsAndHold: " + actions);
        if (actions == null) return;
        heldActions.clear();
        heldModifiers.clear();
        int size = actions.size();
        for (int i = 0; i < size; i++) {
            Binding b = actions.get(i);
            if (b == null || b == Binding.NONE) continue;
            Binding kb = b.toKeyboardBinding();
            if (kb != null) {
                if (BuildConfig.DEBUG) android.util.Log.d(TAG, "  hold modifier: " + kb);
                inputControlsView.handleInputEvent(kb, true, 0);
                heldModifiers.add(kb);
            }
        }
        for (int i = 0; i < size; i++) {
            Binding b = actions.get(i);
            if (b == null || b == Binding.NONE) continue;
            if (b.isMouseMove()) continue;
            if (b == Binding.MOUSE_SCROLL_UP || b == Binding.MOUSE_SCROLL_DOWN) continue;
            if (b.isModifier()) continue;

            if (BuildConfig.DEBUG) android.util.Log.d(TAG, "  hold: " + b);
            inputControlsView.handleInputEvent(b, true, 0);
            heldActions.add(b);
            if (bindingDelay > 0 && i < size - 1) SystemClock.sleep(bindingDelay);
        }
        isActionHeld = true;
    }

    public void releaseHeldAction() {
        if (BuildConfig.DEBUG) android.util.Log.d(TAG, "releaseHeldAction (wasHeld=" + isActionHeld + ") heldActions=" + heldActions + " heldModifiers=" + heldModifiers);
        if (isActionHeld) {
            for (Binding b : heldActions) {
                if (BuildConfig.DEBUG) android.util.Log.d(TAG, "  release: " + b);
                inputControlsView.handleInputEvent(b, false, 0);
            }
            for (Binding b : heldModifiers) {
                if (BuildConfig.DEBUG) android.util.Log.d(TAG, "  release modifier: " + b);
                inputControlsView.handleInputEvent(b, false, 0);
            }
        }
        heldActions.clear();
        heldModifiers.clear();
        isActionHeld = false;
    }
}
