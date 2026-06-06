package com.winlator.cmod.inputcontrols;

import org.json.JSONArray;
import org.json.JSONException;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class BindPackage {
    private static final int BINDING_KEYBOARD_FIRST = 0x100;
    private static final int BINDING_GAMEPAD_BASE = 0x500;

    private final List<Binding> bindings = new ArrayList<>();
    private final List<Boolean> stickyFlags = new ArrayList<>();

    public BindPackage() {}

    public BindPackage(List<Binding> bindings) {
        for (Binding b : bindings) {
            this.bindings.add(b);
            this.stickyFlags.add(false);
        }
    }

    public BindPackage(List<Binding> bindings, List<Boolean> stickyFlags) {
        for (int i = 0; i < bindings.size(); i++) {
            this.bindings.add(bindings.get(i));
            this.stickyFlags.add(i < stickyFlags.size() && stickyFlags.get(i) != null && stickyFlags.get(i));
        }
    }

    public BindPackage(BindPackage other) {
        this.bindings.addAll(other.bindings);
        this.stickyFlags.addAll(other.stickyFlags);
    }

    public List<Binding> getBindings() {
        return Collections.unmodifiableList(bindings);
    }

    public List<Boolean> getStickyFlags() {
        return Collections.unmodifiableList(stickyFlags);
    }

    public Binding get(int index) {
        return index >= 0 && index < bindings.size() ? bindings.get(index) : Binding.NONE;
    }

    public int size() {
        return bindings.size();
    }

    public boolean isEmpty() {
        return bindings.isEmpty() || (bindings.size() == 1 && bindings.get(0) == Binding.NONE);
    }

    public void clear() {
        bindings.clear();
        stickyFlags.clear();
    }

    public void add(Binding binding) {
        bindings.add(binding);
        stickyFlags.add(false);
    }

    public void set(int index, Binding binding) {
        if (index >= 0 && index < bindings.size()) {
            bindings.set(index, binding);
        }
    }

    public void remove(int index) {
        if (index >= 0 && index < bindings.size()) {
            bindings.remove(index);
            stickyFlags.remove(index);
        }
    }

    public boolean isSticky(int index) {
        return index >= 0 && index < stickyFlags.size() && stickyFlags.get(index) != null && stickyFlags.get(index);
    }

    public void setSticky(int index, boolean sticky) {
        while (stickyFlags.size() <= index) stickyFlags.add(false);
        stickyFlags.set(index, sticky);
    }

    public void sync() {
        while (stickyFlags.size() < bindings.size()) stickyFlags.add(false);
        while (stickyFlags.size() > bindings.size()) stickyFlags.remove(stickyFlags.size() - 1);
    }

    public int[] encode() {
        int count = 0;
        for (Binding b : bindings) {
            if (b != null && b != Binding.NONE) count++;
        }
        int[] result = new int[count * 2];
        int idx = 0;
        for (Binding b : bindings) {
            if (b == null || b == Binding.NONE) continue;
            int typeVal;
            int keycodeVal = 0;
            if (b == Binding.MOUSE_LEFT_BUTTON) {
                typeVal = 1;
            } else if (b == Binding.MOUSE_RIGHT_BUTTON) {
                typeVal = 2;
            } else if (b == Binding.MOUSE_MIDDLE_BUTTON) {
                typeVal = 3;
            } else if (b == Binding.MOUSE_SCROLL_UP) {
                typeVal = 6;
            } else if (b == Binding.MOUSE_SCROLL_DOWN) {
                typeVal = 7;
            } else if (b.isMouseMove()) {
                if (b == Binding.MOUSE_MOVE_LEFT) typeVal = 8;
                else if (b == Binding.MOUSE_MOVE_RIGHT) typeVal = 9;
                else if (b == Binding.MOUSE_MOVE_UP) typeVal = 10;
                else typeVal = 11;
            } else if (b.isKeyboard()) {
                typeVal = BINDING_KEYBOARD_FIRST + b.keycode.id;
                keycodeVal = b.keycode.id;
            } else if (b.isGamepad()) {
                int ordinal = b.ordinal() - Binding.GAMEPAD_BUTTON_A.ordinal();
                typeVal = BINDING_GAMEPAD_BASE + ordinal;
                keycodeVal = ordinal;
            } else if (b.isModifier()) {
                Binding kb = b.toKeyboardBinding();
                if (kb != null) {
                    typeVal = BINDING_KEYBOARD_FIRST + kb.keycode.id;
                    keycodeVal = kb.keycode.id;
                } else {
                    typeVal = 0;
                }
            } else {
                typeVal = 0;
            }
            result[idx++] = typeVal;
            result[idx++] = keycodeVal;
        }
        return result;
    }

    public int encodeStickyBitmask() {
        int mask = 0;
        int nonNoneIdx = 0;
        for (int i = 0; i < bindings.size(); i++) {
            Binding b = bindings.get(i);
            if (b == null || b == Binding.NONE) continue;
            if (i < stickyFlags.size() && stickyFlags.get(i) != null && stickyFlags.get(i)) {
                mask |= (1 << nonNoneIdx);
            }
            nonNoneIdx++;
        }
        return mask;
    }

    public JSONArray toJSONArray() {
        JSONArray arr = new JSONArray();
        for (Binding b : bindings) {
            if (b != null && b != Binding.NONE) arr.put(b.name());
        }
        return arr;
    }

    public JSONArray stickyToJSONArray() {
        JSONArray arr = new JSONArray();
        for (int i = 0; i < bindings.size(); i++) {
            boolean sticky = i < stickyFlags.size() && stickyFlags.get(i) != null && stickyFlags.get(i);
            arr.put(sticky ? 1 : 0);
        }
        return arr;
    }

    public static BindPackage fromJSONArray(JSONArray arr, Binding defaultBinding) {
        BindPackage bp = new BindPackage();
        if (arr != null) {
            for (int i = 0; i < arr.length(); i++) {
                try {
                    bp.add(ControlsProfile.parseBinding(arr.getString(i), defaultBinding));
                } catch (JSONException e) {
                    bp.add(defaultBinding);
                }
            }
        }
        if (bp.isEmpty()) bp.add(Binding.NONE);
        return bp;
    }

    public static BindPackage fromJSONArray(JSONArray arr, JSONArray stickyArr, Binding defaultBinding) {
        BindPackage bp = fromJSONArray(arr, defaultBinding);
        if (stickyArr != null) {
            for (int i = 0; i < stickyArr.length() && i < bp.bindings.size(); i++) {
                try {
                    bp.stickyFlags.set(i, stickyArr.getInt(i) != 0);
                } catch (JSONException e) {
                }
            }
        }
        return bp;
    }

    public static BindPackage fromSingle(Binding binding) {
        BindPackage bp = new BindPackage();
        bp.bindings.add(binding);
        bp.stickyFlags.add(false);
        return bp;
    }
}
