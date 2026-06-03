package com.winlator.cmod.contentdialog;

import android.app.AlertDialog;
import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

import com.winlator.cmod.R;
import com.winlator.cmod.core.AppUtils;
import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.widget.NumberPicker;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

public class TouchpadGestureSettingsDialog {
    private final Context context;
    private final ControlsProfile profile;
    private OnSaveListener onSaveListener;
    private final HashMap<String, List<Binding>> bindingValues = new HashMap<>();

    public interface OnSaveListener {
        void onSave(ControlsProfile profile);
    }

    public TouchpadGestureSettingsDialog(Context context, ControlsProfile profile) {
        this.context = context;
        this.profile = profile;
    }

    public void setOnSaveListener(OnSaveListener listener) {
        this.onSaveListener = listener;
    }

    public void show() {
        AlertDialog.Builder builder = new AlertDialog.Builder(context);
        builder.setTitle("Touchpad Gesture Settings");

        View view = LayoutInflater.from(context).inflate(R.layout.touchpad_gesture_settings_dialog, null);
        builder.setView(view);

        view.findViewById(R.id.BTHelpTwoFinger).setOnClickListener((v) ->
                AppUtils.showHelpBox(context, v, R.string.two_finger_help));

        NumberPicker npLongPress = view.findViewById(R.id.NPLongPressTimeout);
        NumberPicker npDoubleTap = view.findViewById(R.id.NPDoubleTapTimeout);
        NumberPicker npDragThreshold = view.findViewById(R.id.NPDragThreshold);

        npLongPress.setValue(profile.getTouchpadLongPressTimeout());
        npDoubleTap.setValue(profile.getTouchpadDoubleTapTimeout());
        npDragThreshold.setValue(profile.getTouchpadDragThreshold());

        SeekBar sbCursorSpeed = view.findViewById(R.id.SBCursorSpeed);
        TextView tvCursorSpeed = view.findViewById(R.id.TVCursorSpeed);
        float initialSpeed = profile.getTouchpadCursorSpeed();
        sbCursorSpeed.setProgress(Math.round((initialSpeed - 0.25f) / 2.75f * 55f));
        tvCursorSpeed.setText(Math.round(initialSpeed * 100) + "%");
        sbCursorSpeed.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                float speed = 0.25f + (progress / 55.0f) * 2.75f;
                tvCursorSpeed.setText(Math.round(speed * 100) + "%");
            }
            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {}
        });

        LinearLayout llSingleFinger = view.findViewById(R.id.LLSingleFinger);
        addBindingSection(llSingleFinger, "single", "Single Tap", profile.getTouchpadSingleTapAction(), R.string.single_tap_delay_help);
        addBindingSection(llSingleFinger, "double", "Double Tap", profile.getTouchpadDoubleTapAction(), R.string.single_tap_delay_help);
        addBindingSection(llSingleFinger, "double_drag", "  Double Tap Drag", profile.getTouchpadDoubleTapDragAction(), R.string.single_tap_delay_help);
        addBindingSection(llSingleFinger, "long", "Long Press", profile.getTouchpadLongPressAction(), R.string.long_press_drag_help);
        addBindingSection(llSingleFinger, "long_drag", "  Long Press Drag", profile.getTouchpadLongPressDragAction(), R.string.long_press_drag_help);

        LinearLayout llTwoFinger = view.findViewById(R.id.LLTwoFinger);
        addBindingSection(llTwoFinger, "single_2nd", "Single Tap", profile.getTouchpadSingleTap2ndFingerAction());
        addBindingSection(llTwoFinger, "single_2nd_drag", "  Single Tap Drag", profile.getTouchpadSingleTap2ndFingerDragAction());
        addBindingSection(llTwoFinger, "double_2nd", "Double Tap", profile.getTouchpadDoubleTap2ndFingerAction(), R.string.single_tap_delay_help);
        addBindingSection(llTwoFinger, "double_2nd_drag", "  Double Tap Drag", profile.getTouchpadDoubleTap2ndFingerDragAction(), R.string.single_tap_delay_help);

        builder.setPositiveButton("Save", (dialog, which) -> save(
                npLongPress, npDoubleTap, npDragThreshold, sbCursorSpeed));
        builder.setNegativeButton("Cancel", null);

        builder.create().show();
    }

    private void save(NumberPicker npLongPress, NumberPicker npDoubleTap, NumberPicker npDragThreshold, SeekBar sbCursorSpeed) {
        profile.setTouchpadLongPressTimeout(npLongPress.getValue());
        profile.setTouchpadDoubleTapTimeout(npDoubleTap.getValue());
        profile.setTouchpadDragThreshold(npDragThreshold.getValue());
        profile.setTouchpadCursorSpeed(0.25f + (sbCursorSpeed.getProgress() / 55.0f) * 2.75f);

        profile.setTouchpadSingleTapAction(bindingValues.getOrDefault("single", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setTouchpadLongPressAction(bindingValues.getOrDefault("long", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setTouchpadLongPressDragAction(bindingValues.getOrDefault("long_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setTouchpadDoubleTapAction(bindingValues.getOrDefault("double", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setTouchpadDoubleTapDragAction(bindingValues.getOrDefault("double_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setTouchpadSingleTap2ndFingerAction(bindingValues.getOrDefault("single_2nd", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setTouchpadSingleTap2ndFingerDragAction(bindingValues.getOrDefault("single_2nd_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setTouchpadDoubleTap2ndFingerAction(bindingValues.getOrDefault("double_2nd", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setTouchpadDoubleTap2ndFingerDragAction(bindingValues.getOrDefault("double_2nd_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));

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
