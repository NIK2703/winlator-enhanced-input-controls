package com.winlator.cmod.inputcontrols;

public enum GestureType {
    SINGLE_TAP(0),
    LONG_PRESS(1),
    DOUBLE_TAP(2),
    SINGLE_TAP_DRAG(3),
    LONG_PRESS_DRAG(4),
    DOUBLE_TAP_DRAG(5),
    SINGLE_2ND(6),
    DOUBLE_2ND(7),
    SINGLE_DRAG_2ND(8),
    DOUBLE_DRAG_2ND(9);

    public final int id;
    public static final int COUNT = 10;

    GestureType(int id) { this.id = id; }

    public static GestureType fromId(int id) {
        for (GestureType g : values()) if (g.id == id) return g;
        return SINGLE_TAP;
    }
}
