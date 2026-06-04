package com.winlator.cmod.contentdialog;

import android.app.AlertDialog;
import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.view.ViewGroup;

import com.winlator.cmod.R;
import com.winlator.cmod.core.AppUtils;
import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.inputcontrols.MouseMode;
import com.winlator.cmod.widget.NumberPicker;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

public class GestureSettingsDialog {
    private final Context context;
    private final ControlsProfile profile;
    private OnSaveListener onSaveListener;
    private final HashMap<String, List<Binding>> bindingValues = new HashMap<>();

    public interface OnSaveListener {
        void onSave(ControlsProfile profile);
    }

    public GestureSettingsDialog(Context context, ControlsProfile profile) {
        this.context = context;
        this.profile = profile;
    }

    public void setOnSaveListener(OnSaveListener listener) {
        this.onSaveListener = listener;
    }

    public void show() {
        AlertDialog.Builder builder = new AlertDialog.Builder(context);
        builder.setTitle("Gesture Settings");

        View view = LayoutInflater.from(context).inflate(R.layout.gesture_settings_dialog, null);
        builder.setView(view);

        // Mode toggle
        Spinner spInputMode = view.findViewById(R.id.SPInputMode);
        MouseMode currentMode = profile.getMouseMode();
        MouseMode[] modes = MouseMode.values();
        int selectedIndex = 0;
        for (int i = 0; i < modes.length; i++) {
            if (modes[i] == currentMode) { selectedIndex = i; break; }
        }
        spInputMode.setAdapter(new ArrayAdapter<>(context, android.R.layout.simple_spinner_dropdown_item, modes));
        spInputMode.setSelection(selectedIndex, false);

        // Timing controls
        NumberPicker npLongPress = view.findViewById(R.id.NPLongPressTimeout);
        NumberPicker npDoubleTap = view.findViewById(R.id.NPDoubleTapTimeout);
        NumberPicker npDragThreshold = view.findViewById(R.id.NPDragThreshold);
        NumberPicker npSingleTapDelay = view.findViewById(R.id.NPSingleTapDelay);

        npLongPress.setValue(profile.getLongPressTimeout());
        npDoubleTap.setValue(profile.getDoubleTapTimeout());
        npDragThreshold.setValue(profile.getDragThreshold());
        npSingleTapDelay.setValue(profile.getSingleTapDelay());

        // Cursor speed
        View llCursorSpeed = view.findViewById(R.id.LLCursorSpeed);
        SeekBar sbCursorSpeed = view.findViewById(R.id.SBCursorSpeed);
        TextView tvCursorSpeed = view.findViewById(R.id.TVCursorSpeed);
        float initialSpeed = profile.getCursorSpeed();
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

        view.findViewById(R.id.BTHelpTwoFinger).setOnClickListener((v) ->
                AppUtils.showHelpBox(context, v, R.string.two_finger_help));

        // Gesture bindings — using unified gesture fields from profile
        // In touchpad mode, single_tap_drag is used for cursor movement and unavailable as gesture
        boolean isTouchpad = currentMode == MouseMode.TOUCHPAD;

        LinearLayout llSingleFinger = view.findViewById(R.id.LLSingleFinger);
        addBindingSection(llSingleFinger, "single", "Single Tap", profile.getGestureSingleTapAction(), R.string.single_tap_delay_help);
        addBindingSection(llSingleFinger, "single_drag", "Single Tap Drag", profile.getGestureSingleTapDragAction());
        addBindingSection(llSingleFinger, "double", "Double Tap", profile.getGestureDoubleTapAction(), R.string.single_tap_delay_help);
        addBindingSection(llSingleFinger, "double_drag", "Double Tap Drag", profile.getGestureDoubleTapDragAction(), R.string.single_tap_delay_help);

        LinearLayout llHoldGestures = view.findViewById(R.id.LLHoldGesturesContent);
        addBindingSection(llHoldGestures, "long", "Long Press", profile.getGestureLongPressAction(), R.string.long_press_drag_help);
        addBindingSection(llHoldGestures, "long_drag", "Long Press Drag", profile.getGestureLongPressDragAction(), R.string.long_press_drag_help);

        LinearLayout llTwoFinger = view.findViewById(R.id.LLTwoFinger);
        addBindingSection(llTwoFinger, "single_2nd", "Single Tap", profile.getGestureSingleTap2ndFingerAction());
        addBindingSection(llTwoFinger, "single_2nd_drag", "Single Tap Drag", profile.getGestureSingleTap2ndFingerDragAction());
        addBindingSection(llTwoFinger, "double_2nd", "Double Tap", profile.getGestureDoubleTap2ndFingerAction(), R.string.single_tap_delay_help);
        addBindingSection(llTwoFinger, "double_2nd_drag", "Double Tap Drag", profile.getGestureDoubleTap2ndFingerDragAction(), R.string.single_tap_delay_help);

        // Update single-tap-drag visibility based on mode
        updateSingleTapDragVisibility(llSingleFinger, isTouchpad);
        // Cursor speed only applies to touchpad mode
        updateCursorSpeedVisibility(llCursorSpeed, isTouchpad);

        builder.setPositiveButton("Save", (dialog, which) -> save(
                spInputMode, npLongPress, npDoubleTap, npDragThreshold, npSingleTapDelay, sbCursorSpeed));
        builder.setNegativeButton("Cancel", null);

        AlertDialog dialog = builder.create();
        dialog.show();

        // Listen for mode changes to update visibility
        spInputMode.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View v, int pos, long id) {
                boolean tpMode = modes[pos] == MouseMode.TOUCHPAD;
                updateSingleTapDragVisibility(llSingleFinger, tpMode);
                updateCursorSpeedVisibility(llCursorSpeed, tpMode);
            }
            @Override
            public void onNothingSelected(AdapterView<?> parent) {}
        });
    }

    private void updateSingleTapDragVisibility(LinearLayout container, boolean isTouchpad) {
        // Single tap drag section is added 2nd (index 1) in LLSingleFinger
        if (container.getChildCount() > 1) {
            View section = container.getChildAt(1);
            section.setAlpha(isTouchpad ? 0.4f : 1.0f);
            setViewEnabled(section, !isTouchpad);
        }
    }

    private void updateCursorSpeedVisibility(View container, boolean isTouchpad) {
        // Cursor speed only applies to touchpad; dim in touchscreen mode
        container.setAlpha(isTouchpad ? 1.0f : 0.4f);
    }

    private void setViewEnabled(View view, boolean enabled) {
        view.setEnabled(enabled);
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++) {
                setViewEnabled(group.getChildAt(i), enabled);
            }
        }
    }

    private void save(Spinner spInputMode, NumberPicker npLongPress, NumberPicker npDoubleTap,
                      NumberPicker npDragThreshold, NumberPicker npSingleTapDelay, SeekBar sbCursorSpeed) {
        // Save mode
        MouseMode selectedMode = (MouseMode) spInputMode.getSelectedItem();
        profile.setMouseMode(selectedMode);

        // Save timing
        profile.setLongPressTimeout(npLongPress.getValue());
        profile.setDoubleTapTimeout(npDoubleTap.getValue());
        profile.setDragThreshold(npDragThreshold.getValue());
        profile.setSingleTapDelay(npSingleTapDelay.getValue());

        // Save cursor speed (touchpad-only)
        float speed = 0.25f + (sbCursorSpeed.getProgress() / 55.0f) * 2.75f;
        profile.setCursorSpeed(speed);

        // Save gesture bindings
        profile.setGestureSingleTapAction(bindingValues.getOrDefault("single", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setGestureSingleTapDragAction(bindingValues.getOrDefault("single_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setGestureLongPressAction(bindingValues.getOrDefault("long", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setGestureLongPressDragAction(bindingValues.getOrDefault("long_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setGestureDoubleTapAction(bindingValues.getOrDefault("double", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setGestureDoubleTapDragAction(bindingValues.getOrDefault("double_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setGestureSingleTap2ndFingerAction(bindingValues.getOrDefault("single_2nd", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setGestureSingleTap2ndFingerDragAction(bindingValues.getOrDefault("single_2nd_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setGestureDoubleTap2ndFingerAction(bindingValues.getOrDefault("double_2nd", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setGestureDoubleTap2ndFingerDragAction(bindingValues.getOrDefault("double_2nd_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));

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
