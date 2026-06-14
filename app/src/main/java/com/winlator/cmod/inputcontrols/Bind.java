package com.winlator.cmod.inputcontrols;

import androidx.annotation.NonNull;

import com.winlator.cmod.xserver.Pointer;
import com.winlator.cmod.xserver.XKeycode;

import java.util.ArrayList;
import java.util.HashMap;

public enum Bind {
    NONE, MOD_CTRL, MOD_SHIFT, MOD_ALT, MOUSE_LEFT_BUTTON, MOUSE_MIDDLE_BUTTON, MOUSE_RIGHT_BUTTON, MOUSE_MOVE_LEFT, MOUSE_MOVE_RIGHT, MOUSE_MOVE_UP, MOUSE_MOVE_DOWN, MOUSE_SCROLL_UP, MOUSE_SCROLL_DOWN, KEY_UP, KEY_RIGHT, KEY_DOWN, KEY_LEFT, KEY_ENTER, KEY_ESC, KEY_BKSP, KEY_DEL, KEY_TAB, KEY_SPACE, KEY_CTRL_L, KEY_CTRL_R, KEY_INSERT, KEY_SHIFT_L, KEY_SHIFT_R, KEY_ALT_L, KEY_ALT_R, KEY_HOME, KEY_END, KEY_PRTSCN, KEY_PG_UP, KEY_PG_DOWN, KEY_CAPS_LOCK, KEY_NUM_LOCK, KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z, KEY_BRACKET_LEFT, KEY_BRACKET_RIGHT, KEY_BACKSLASH, KEY_SLASH, KEY_SEMICOLON, KEY_COMMA, KEY_PERIOD, KEY_APOSTROPHE, KEY_KP_ADD, KEY_MINUS, KEY_GRAVE, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_KP_0, KEY_KP_1, KEY_KP_2, KEY_KP_3, KEY_KP_4, KEY_KP_5, KEY_KP_6, KEY_KP_7, KEY_KP_8, KEY_KP_9, GAMEPAD_BUTTON_A, GAMEPAD_BUTTON_B, GAMEPAD_BUTTON_X, GAMEPAD_BUTTON_Y, GAMEPAD_BUTTON_L1, GAMEPAD_BUTTON_R1, GAMEPAD_BUTTON_SELECT, GAMEPAD_BUTTON_START, GAMEPAD_BUTTON_L3, GAMEPAD_BUTTON_R3, GAMEPAD_BUTTON_L2, GAMEPAD_BUTTON_R2, GAMEPAD_LEFT_THUMB_UP, GAMEPAD_LEFT_THUMB_RIGHT, GAMEPAD_LEFT_THUMB_DOWN, GAMEPAD_LEFT_THUMB_LEFT, GAMEPAD_RIGHT_THUMB_UP, GAMEPAD_RIGHT_THUMB_RIGHT, GAMEPAD_RIGHT_THUMB_DOWN, GAMEPAD_RIGHT_THUMB_LEFT, GAMEPAD_DPAD_UP, GAMEPAD_DPAD_RIGHT, GAMEPAD_DPAD_DOWN, GAMEPAD_DPAD_LEFT;
    private final XKeycode keycode;
    private final String displayName;

    private static final int BINDING_KEYBOARD_FIRST = 0x100;
    private static final int BINDING_GAMEPAD_BASE = 0x500;
    private static final HashMap<String, Bind> lookup = new HashMap<>();
    public static final int ORD_MOUSE_MOVE_LEFT = MOUSE_MOVE_LEFT.ordinal();
    public static final int ORD_MOUSE_MOVE_RIGHT = MOUSE_MOVE_RIGHT.ordinal();
    public static final int ORD_MOUSE_MOVE_UP = MOUSE_MOVE_UP.ordinal();
    public static final int ORD_MOUSE_MOVE_DOWN = MOUSE_MOVE_DOWN.ordinal();
    public static final int ORD_GAMEPAD_BUTTON_A = GAMEPAD_BUTTON_A.ordinal();
    public static final int ORD_GAMEPAD_LEFT_THUMB_UP = GAMEPAD_LEFT_THUMB_UP.ordinal();
    public static final int ORD_GAMEPAD_LEFT_THUMB_DOWN = GAMEPAD_LEFT_THUMB_DOWN.ordinal();
    public static final int ORD_GAMEPAD_LEFT_THUMB_LEFT = GAMEPAD_LEFT_THUMB_LEFT.ordinal();
    public static final int ORD_GAMEPAD_LEFT_THUMB_RIGHT = GAMEPAD_LEFT_THUMB_RIGHT.ordinal();
    public static final int ORD_GAMEPAD_RIGHT_THUMB_UP = GAMEPAD_RIGHT_THUMB_UP.ordinal();
    public static final int ORD_GAMEPAD_RIGHT_THUMB_DOWN = GAMEPAD_RIGHT_THUMB_DOWN.ordinal();
    public static final int ORD_GAMEPAD_RIGHT_THUMB_LEFT = GAMEPAD_RIGHT_THUMB_LEFT.ordinal();
    public static final int ORD_GAMEPAD_RIGHT_THUMB_RIGHT = GAMEPAD_RIGHT_THUMB_RIGHT.ordinal();
    public static final int ORD_GAMEPAD_DPAD_UP = GAMEPAD_DPAD_UP.ordinal();
    public static final int ORD_GAMEPAD_DPAD_RIGHT = GAMEPAD_DPAD_RIGHT.ordinal();
    public static final int ORD_GAMEPAD_DPAD_DOWN = GAMEPAD_DPAD_DOWN.ordinal();
    public static final int ORD_GAMEPAD_DPAD_LEFT = GAMEPAD_DPAD_LEFT.ordinal();

    Bind() {
        XKeycode keycode;
        try {
            keycode = XKeycode.valueOf(name());
        }
        catch (IllegalArgumentException e) {
            keycode = XKeycode.KEY_NONE;
            String name = name();
            if (name.equals("KEY_PG_UP")) {
                keycode = XKeycode.KEY_PRIOR;
            }
            else if (name.equals("KEY_PG_DOWN")) {
                keycode = XKeycode.KEY_NEXT;
            }
        }
        this.keycode = keycode;
        this.displayName = computeDisplayName();
    }

    private String computeDisplayName() {
        return displayNameOf(name());
    }

    public static Bind fromString(String name) {
        Bind cached = lookup.get(name);
        if (cached != null) return cached;
        Bind result;
        switch (name) {
            case "KEY_INSERT": result = Bind.KEY_INSERT; break;
            case "KEY_CTRL": result = Bind.KEY_CTRL_L; break;
            case "KEY_SHIFT": result = Bind.KEY_SHIFT_L; break;
            case "KEY_ALT": result = Bind.KEY_ALT_L; break;
            default: result = valueOf(name);
        }
        lookup.put(name, result);
        return result;
    }

    static {
        for (Bind b : values()) lookup.put(b.name(), b);
    }

    @NonNull
    @Override
    public String toString() {
        return displayName;
    }

    public Pointer.Button getPointerButton() {
        switch (this) {
            case MOUSE_LEFT_BUTTON:
                return Pointer.Button.BUTTON_LEFT;
            case MOUSE_MIDDLE_BUTTON:
                return Pointer.Button.BUTTON_MIDDLE;
            case MOUSE_RIGHT_BUTTON:
                return Pointer.Button.BUTTON_RIGHT;
            case MOUSE_SCROLL_UP:
                return Pointer.Button.BUTTON_SCROLL_UP;
            case MOUSE_SCROLL_DOWN:
                return Pointer.Button.BUTTON_SCROLL_DOWN;
            default:
                return null;
        }
    }

    public boolean isModifier() {
        return this == MOD_CTRL || this == MOD_SHIFT || this == MOD_ALT;
    }

    public Bind toKeyboardBinding() {
        switch (this) {
            case MOD_CTRL: return KEY_CTRL_L;
            case MOD_SHIFT: return KEY_SHIFT_L;
            case MOD_ALT: return KEY_ALT_L;
            default: return null;
        }
    }

    public boolean isMouse() {
        return name().startsWith("MOUSE_");
    }

    public boolean isKeyboard() {
        return name().startsWith("KEY_") && !isModifier();
    }

    public boolean isGamepad() {
        return name().startsWith("GAMEPAD_");
    }

    public boolean isMouseMove() {
        return this == MOUSE_MOVE_UP || this == MOUSE_MOVE_RIGHT || this == MOUSE_MOVE_DOWN || this == MOUSE_MOVE_LEFT;
    }

    public int getKeycodeId() {
        return keycode.id;
    }

    public XKeycode getXKeycode() {
        return keycode;
    }

    public int encodeTypeValue() {
        if (this == NONE) return 0;
        if (this == MOUSE_LEFT_BUTTON) return 1;
        if (this == MOUSE_RIGHT_BUTTON) return 2;
        if (this == MOUSE_MIDDLE_BUTTON) return 3;
        if (this == MOUSE_SCROLL_UP) return 6;
        if (this == MOUSE_SCROLL_DOWN) return 7;
        if (this == MOUSE_MOVE_LEFT) return 8;
        if (this == MOUSE_MOVE_RIGHT) return 9;
        if (this == MOUSE_MOVE_UP) return 10;
        if (this == MOUSE_MOVE_DOWN) return 11;
        if (isModifier()) {
            Bind kb = toKeyboardBinding();
            return kb != null ? BINDING_KEYBOARD_FIRST + kb.keycode.id : 0;
        }
        if (isKeyboard()) return BINDING_KEYBOARD_FIRST + keycode.id;
        if (isGamepad()) return BINDING_GAMEPAD_BASE + (ordinal() - GAMEPAD_BUTTON_A.ordinal());
        return 0;
    }

    public int encodeKeyValue() {
        if (isModifier()) {
            Bind kb = toKeyboardBinding();
            return kb != null ? kb.keycode.id : 0;
        }
        if (isKeyboard()) return keycode.id;
        if (isGamepad()) return ordinal() - GAMEPAD_BUTTON_A.ordinal();
        return 0;
    }

    private static String displayNameOf(String name) {
        switch (name) {
            case "MOD_CTRL": return "CTRL";
            case "MOD_SHIFT": return "SHIFT";
            case "MOD_ALT": return "ALT";
            case "KEY_INSERT": return "INSERT";
            case "KEY_SHIFT_L": return "L SHIFT";
            case "KEY_SHIFT_R": return "R SHIFT";
            case "KEY_CTRL_L": return "L CTRL";
            case "KEY_CTRL_R": return "R CTRL";
            case "KEY_ALT_L": return "L ALT";
            case "KEY_ALT_R": return "R ALT";
            case "KEY_BRACKET_LEFT": return "[";
            case "KEY_BRACKET_RIGHT": return "]";
            case "KEY_BACKSLASH": return "\\";
            case "KEY_SLASH": return "/";
            case "KEY_SEMICOLON": return ";";
            case "KEY_COMMA": return ",";
            case "KEY_PERIOD": return ".";
            case "KEY_APOSTROPHE": return "'";
            case "KEY_MINUS": return "-";
            case "KEY_KP_ADD": return "+";
            case "KEY_GRAVE": return "`";
            default: return name.replaceAll("^(MOUSE_|KEY_|GAMEPAD_)", "").replace("KP_", "NUMPAD_").replace("_", " ");
        }
    }

    private static String[] cachedMouseLabels;
    private static String[] cachedKeyboardLabels;
    private static String[] cachedGamepadLabels;
    private static Bind[] cachedMouseValues;
    private static Bind[] cachedKeyboardValues;
    private static Bind[] cachedGamepadValues;

    public static String[] mouseBindingLabels() {
        if (cachedMouseLabels != null) return cachedMouseLabels;
        ArrayList<String> names = new ArrayList<>();
        for (Bind binding : values()) if (binding.isMouse()) names.add(binding.toString());
        cachedMouseLabels = names.toArray(new String[0]);
        return cachedMouseLabels;
    }

    public static String[] keyboardBindingLabels() {
        if (cachedKeyboardLabels != null) return cachedKeyboardLabels;
        ArrayList<String> labels = new ArrayList<>();
        for (Bind binding : values()) if (binding.isKeyboard()) labels.add(binding.toString());
        cachedKeyboardLabels = labels.toArray(new String[0]);
        return cachedKeyboardLabels;
    }

    public static String[] gamepadBindingLabels() {
        if (cachedGamepadLabels != null) return cachedGamepadLabels;
        ArrayList<String> names = new ArrayList<>();
        for (Bind binding : values()) if (binding.isGamepad()) names.add(binding.toString());
        cachedGamepadLabels = names.toArray(new String[0]);
        return cachedGamepadLabels;
    }

    public static Bind[] mouseBindingValues() {
        if (cachedMouseValues != null) return cachedMouseValues;
        ArrayList<Bind> labels = new ArrayList<>();
        for (Bind binding : values()) if (binding.isMouse()) labels.add(binding);
        cachedMouseValues = labels.toArray(new Bind[0]);
        return cachedMouseValues;
    }

    public static Bind[] keyboardBindingValues() {
        if (cachedKeyboardValues != null) return cachedKeyboardValues;
        ArrayList<Bind> values = new ArrayList<>();
        for (Bind binding : values()) if (binding.isKeyboard()) values.add(binding);
        cachedKeyboardValues = values.toArray(new Bind[0]);
        return cachedKeyboardValues;
    }

    public static Bind[] gamepadBindingValues() {
        if (cachedGamepadValues != null) return cachedGamepadValues;
        ArrayList<Bind> labels = new ArrayList<>();
        for (Bind binding : values()) if (binding.isGamepad()) labels.add(binding);
        cachedGamepadValues = labels.toArray(new Bind[0]);
        return cachedGamepadValues;
    }
}
