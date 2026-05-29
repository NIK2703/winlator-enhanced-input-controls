package com.winlator.cmod.inputcontrols;

public enum SecondFingerMode {
    LONG_TAP_ACTION,
    SECOND_TAP_ACTIONS;

    @Override
    public String toString() {
        switch (this) {
            case LONG_TAP_ACTION: return "Long tap action";
            case SECOND_TAP_ACTIONS: return "Second tap actions";
            default: return name();
        }
    }
}
