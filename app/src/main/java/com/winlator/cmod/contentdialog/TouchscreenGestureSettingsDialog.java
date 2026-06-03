package com.winlator.cmod.contentdialog;

import android.app.AlertDialog;
import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.LinearLayout;

import com.winlator.cmod.R;
import com.winlator.cmod.core.AppUtils;
import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.widget.NumberPicker;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

public class TouchscreenGestureSettingsDialog {
    private final Context context;
    private final ControlsProfile profile;
    private OnSaveListener onSaveListener;
    private final HashMap<String, List<Binding>> bindingValues = new HashMap<>();

    public interface OnSaveListener {
        void onSave(ControlsProfile profile);
    }

    public TouchscreenGestureSettingsDialog(Context context, ControlsProfile profile) {
        this.context = context;
        this.profile = profile;
    }

    public void setOnSaveListener(OnSaveListener listener) {
        this.onSaveListener = listener;
    }

    public void show() {
        AlertDialog.Builder builder = new AlertDialog.Builder(context);
        builder.setTitle("Touchscreen Gesture Settings");

        View view = LayoutInflater.from(context).inflate(R.layout.touchscreen_gesture_settings_dialog, null);
        builder.setView(view);

        
        view.findViewById(R.id.BTHelpTwoFinger).setOnClickListener((v) ->
                AppUtils.showHelpBox(context, v, R.string.two_finger_help));

        NumberPicker npLongPress = view.findViewById(R.id.NPLongPressTimeout);
        NumberPicker npDoubleTap = view.findViewById(R.id.NPDoubleTapTimeout);
        NumberPicker npDragThreshold = view.findViewById(R.id.NPDragThreshold);
        NumberPicker npSingleTapDelay = view.findViewById(R.id.NPSingleTapDelay);

        npLongPress.setValue(profile.getLongPressTimeout());
        npDoubleTap.setValue(profile.getDoubleTapTimeout());
        npDragThreshold.setValue(profile.getDragThreshold());
        npSingleTapDelay.setValue(profile.getSingleTapDelay());

        LinearLayout llSingleFinger = view.findViewById(R.id.LLSingleFinger);
        addBindingSection(llSingleFinger, "single", "Single Tap", profile.getSingleTapAction(), R.string.single_tap_delay_help);
        addBindingSection(llSingleFinger, "single_drag", "  Single Tap Drag", profile.getSingleTapDragAction());
        addBindingSection(llSingleFinger, "double", "Double Tap", profile.getDoubleTapAction(), R.string.single_tap_delay_help);
        addBindingSection(llSingleFinger, "double_drag", "  Double Tap Drag", profile.getDoubleTapDragAction(), R.string.single_tap_delay_help);

        LinearLayout llHoldGestures = view.findViewById(R.id.LLHoldGesturesContent);
        addBindingSection(llHoldGestures, "long", "Long Press", profile.getLongPressAction(), R.string.long_press_drag_help);
        addBindingSection(llHoldGestures, "long_drag", "  Long Press Drag", profile.getLongPressDragAction(), R.string.long_press_drag_help);

        LinearLayout llTwoFinger = view.findViewById(R.id.LLTwoFinger);
        addBindingSection(llTwoFinger, "single_2nd", "Single Tap", profile.getSingleTap2ndFingerAction());
        addBindingSection(llTwoFinger, "single_2nd_drag", "  Single Tap Drag", profile.getSingleTap2ndFingerDragAction());
        addBindingSection(llTwoFinger, "double_2nd", "Double Tap", profile.getDoubleTap2ndFingerAction(), R.string.single_tap_delay_help);
        addBindingSection(llTwoFinger, "double_2nd_drag", "  Double Tap Drag", profile.getDoubleTap2ndFingerDragAction(), R.string.single_tap_delay_help);

        builder.setPositiveButton("Save", (dialog, which) -> save(
                npLongPress, npDoubleTap, npDragThreshold, npSingleTapDelay));
        builder.setNegativeButton("Cancel", null);

        builder.create().show();
    }

    private void save(NumberPicker npLongPress, NumberPicker npDoubleTap, NumberPicker npDragThreshold, NumberPicker npSingleTapDelay) {
        profile.setLongPressTimeout(npLongPress.getValue());
        profile.setDoubleTapTimeout(npDoubleTap.getValue());
        profile.setDragThreshold(npDragThreshold.getValue());
        profile.setSingleTapDelay(npSingleTapDelay.getValue());

        profile.setSingleTapAction(bindingValues.getOrDefault("single", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setSingleTapDragAction(bindingValues.getOrDefault("single_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setLongPressAction(bindingValues.getOrDefault("long", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setLongPressDragAction(bindingValues.getOrDefault("long_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setDoubleTapAction(bindingValues.getOrDefault("double", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setDoubleTapDragAction(bindingValues.getOrDefault("double_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setSingleTap2ndFingerAction(bindingValues.getOrDefault("single_2nd", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setSingleTap2ndFingerDragAction(bindingValues.getOrDefault("single_2nd_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setDoubleTap2ndFingerAction(bindingValues.getOrDefault("double_2nd", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setDoubleTap2ndFingerDragAction(bindingValues.getOrDefault("double_2nd_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));

        profile.save();
        if (onSaveListener != null) onSaveListener.onSave(profile);
    }

    private void addBindingSection(LinearLayout container, String key, String label, List<Binding> current) {
        addBindingSection(container, key, label, current, 0);
    }

    private void addBindingSection(LinearLayout container, String key, String label, List<Binding> current, int helpTextResId) {
        bindingValues.put(key, new ArrayList<>(current));
        List<Binding> seq = bindingValues.get(key);
        View section = com.winlator.cmod.widget.BindingSequenceEditor.createView(context, label, helpTextResId, seq, () -> {});
        container.addView(section);
    }

}
