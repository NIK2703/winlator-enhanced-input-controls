package com.winlator.cmod.widget;

import android.os.SystemClock;

import com.winlator.cmod.inputcontrols.Binding;

import java.util.ArrayList;
import java.util.List;

public class GestureActionExecutor {
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
        if (actions == null) return;
        int size = actions.size();
        for (int i = 0; i < size; i++) {
            Binding b = actions.get(i);
            if (b == null || b == Binding.NONE) continue;
            Binding kb = b.toKeyboardBinding();
            if (kb != null) {
                inputControlsView.handleInputEvent(kb, true, 0);
            }
        }
        for (int i = 0; i < size; i++) {
            Binding b = actions.get(i);
            if (b == null || b == Binding.NONE) continue;
            if (b == Binding.MOUSE_SCROLL_UP || b == Binding.MOUSE_SCROLL_DOWN) continue;
            if (b.isMouseMove()) continue;
            if (b.isModifier()) continue;

            inputControlsView.handleInputEvent(b, true, 0);
            inputControlsView.handleInputEvent(b, false, 0);
            if (bindingDelay > 0 && i < size - 1) SystemClock.sleep(bindingDelay);
        }
        for (int i = size - 1; i >= 0; i--) {
            Binding b = actions.get(i);
            if (b == null || b == Binding.NONE) continue;
            Binding kb = b.toKeyboardBinding();
            if (kb != null) {
                inputControlsView.handleInputEvent(kb, false, 0);
            }
        }
    }

    public void executeActionsAndHold(List<Binding> actions) {
        if (actions == null) return;
        heldActions.clear();
        heldModifiers.clear();
        int size = actions.size();
        for (int i = 0; i < size; i++) {
            Binding b = actions.get(i);
            if (b == null || b == Binding.NONE) continue;
            Binding kb = b.toKeyboardBinding();
            if (kb != null) {
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

            inputControlsView.handleInputEvent(b, true, 0);
            heldActions.add(b);
            if (bindingDelay > 0 && i < size - 1) SystemClock.sleep(bindingDelay);
        }
        isActionHeld = true;
    }

    public void releaseHeldAction() {
        if (isActionHeld) {
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
}
