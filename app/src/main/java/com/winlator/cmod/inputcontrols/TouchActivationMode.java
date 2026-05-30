package com.winlator.cmod.inputcontrols;

public enum TouchActivationMode {
    LOCK,
    TRACK,
    HOVER;

    @Override
    public String toString() {
        switch (this) {
            case LOCK: return "Lock";
            case TRACK: return "Track";
            case HOVER: return "Hover";
            default: return name();
        }
    }
}
