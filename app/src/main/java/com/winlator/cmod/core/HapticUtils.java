package com.winlator.cmod.core;

import android.content.Context;
import android.os.Build;
import android.os.VibrationEffect;
import android.os.Vibrator;

public class HapticUtils {
    public static final int HAPTIC_NONE = 0;
    public static final int HAPTIC_TICK = 1;
    public static final int HAPTIC_CLICK = 2;
    public static final int HAPTIC_HEAVY_CLICK = 3;
    public static final int HAPTIC_DOUBLE_CLICK = 4;
    public static final int HAPTIC_PULSE = 5;

    public static String getName(int type) {
        switch (type) {
            case HAPTIC_NONE: return "No Feedback";
            case HAPTIC_TICK: return "Tick";
            case HAPTIC_CLICK: return "Click";
            case HAPTIC_HEAVY_CLICK: return "Heavy Click";
            case HAPTIC_DOUBLE_CLICK: return "Double Click";
            case HAPTIC_PULSE: return "Pulse";
            default: return "No Feedback";
        }
    }

    public static int getTypeCount() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) return 6;
        return 3;
    }

    public static int getType(int index) {
        switch (index) {
            case 0: return HAPTIC_NONE;
            case 1: return HAPTIC_TICK;
            case 2: return Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q ? HAPTIC_CLICK : HAPTIC_PULSE;
            case 3: return Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q ? HAPTIC_HEAVY_CLICK : HAPTIC_NONE;
            case 4: return Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q ? HAPTIC_DOUBLE_CLICK : HAPTIC_NONE;
            case 5: return Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q ? HAPTIC_PULSE : HAPTIC_NONE;
            default: return HAPTIC_NONE;
        }
    }

    public static int indexOf(int type) {
        for (int i = 0; i < getTypeCount(); i++) {
            if (getType(i) == type) return i;
        }
        return 0;
    }

    public static void perform(Context context, int hapticType) {
        if (context == null || hapticType <= 0) return;
        Vibrator vibrator = (Vibrator) context.getSystemService(Context.VIBRATOR_SERVICE);
        if (vibrator == null || !vibrator.hasVibrator()) return;

        switch (hapticType) {
            case HAPTIC_TICK:
                playTick(vibrator);
                break;
            case HAPTIC_CLICK:
                playPredefined(vibrator, VibrationEffect.EFFECT_CLICK, 30);
                break;
            case HAPTIC_HEAVY_CLICK:
                playPredefined(vibrator, VibrationEffect.EFFECT_HEAVY_CLICK, 50);
                break;
            case HAPTIC_DOUBLE_CLICK:
                playPredefined(vibrator, VibrationEffect.EFFECT_DOUBLE_CLICK, 30);
                break;
            case HAPTIC_PULSE:
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    vibrator.vibrate(VibrationEffect.createOneShot(10, VibrationEffect.DEFAULT_AMPLITUDE));
                } else {
                    vibrator.vibrate(10);
                }
                break;
        }
    }

    private static void playTick(Vibrator vibrator) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            vibrator.areAllPrimitivesSupported(VibrationEffect.Composition.PRIMITIVE_TICK)) {
            vibrator.vibrate(VibrationEffect.startComposition()
                    .addPrimitive(VibrationEffect.Composition.PRIMITIVE_TICK, 1.0f, 0)
                    .compose());
        }
        else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            try {
                vibrator.vibrate(VibrationEffect.createPredefined(VibrationEffect.EFFECT_TICK));
            } catch (IllegalArgumentException e) {
                vibrator.vibrate(VibrationEffect.createOneShot(30, 255));
            }
        }
        else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            vibrator.vibrate(VibrationEffect.createOneShot(30, 255));
        }
    }

    private static void playPredefined(Vibrator vibrator, int effectId, int fallbackMs) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            try {
                vibrator.vibrate(VibrationEffect.createPredefined(effectId));
            } catch (IllegalArgumentException e) {
                vibrator.vibrate(VibrationEffect.createOneShot(fallbackMs, 255));
            }
        }
    }

    public static boolean hasLinearVibrator(Context context) {
        if (context == null) return false;
        Vibrator vibrator = (Vibrator) context.getSystemService(Context.VIBRATOR_SERVICE);
        if (vibrator == null || !vibrator.hasVibrator()) return false;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (vibrator.areAllPrimitivesSupported(VibrationEffect.Composition.PRIMITIVE_TICK)) {
                return true;
            }
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            return vibrator.hasAmplitudeControl();
        }
        return false;
    }

    public static boolean hasVibrator(Context context) {
        if (context == null) return false;
        Vibrator vibrator = (Vibrator) context.getSystemService(Context.VIBRATOR_SERVICE);
        return vibrator != null && vibrator.hasVibrator();
    }

    public static int resolveHaptic(Context context, int lraDefault) {
        if (!hasVibrator(context)) return HAPTIC_NONE;
        if (!hasLinearVibrator(context)) return HAPTIC_PULSE;
        return lraDefault;
    }
}
