package com.winlator.cmod.contentdialog;

import android.app.AlertDialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.preference.PreferenceManager;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;
import android.view.ViewGroup;

import com.google.android.material.tabs.TabLayout;

import com.winlator.cmod.R;
import com.winlator.cmod.core.AppUtils;
import com.winlator.cmod.inputcontrols.BindPackage;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.inputcontrols.MouseMode;
import com.winlator.cmod.widget.NumberPicker;

import java.util.HashMap;

public class GestureSettingsDialog {
    private final Context context;
    private final ControlsProfile profile;
    private OnSaveListener onSaveListener;
    private final HashMap<String, BindPackage> bindingValues = new HashMap<>();

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
        TabLayout tabLayout = view.findViewById(R.id.TabLayoutInputMode);
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(context);
        boolean isDarkMode = prefs.getBoolean("dark_mode", true);
        if (isDarkMode) {
            tabLayout.setBackgroundResource(R.drawable.tab_layout_background_dark);
        } else {
            tabLayout.setBackgroundResource(R.drawable.tab_layout_background);
        }
        MouseMode currentMode = profile.getMouseMode();
        MouseMode[] modes = MouseMode.values();
        tabLayout.getTabAt(currentMode.ordinal()).select();

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
                tabLayout, modes, npLongPress, npDoubleTap, npDragThreshold, npSingleTapDelay, sbCursorSpeed));
        builder.setNegativeButton("Cancel", null);

        AlertDialog dialog = builder.create();
        dialog.show();

        // Listen for mode changes to update visibility
        tabLayout.addOnTabSelectedListener(new TabLayout.OnTabSelectedListener() {
            @Override
            public void onTabSelected(TabLayout.Tab tab) {
                boolean tpMode = tab.getPosition() == 0;
                updateSingleTapDragVisibility(llSingleFinger, tpMode);
                updateCursorSpeedVisibility(llCursorSpeed, tpMode);
            }
            @Override
            public void onTabUnselected(TabLayout.Tab tab) {}
            @Override
            public void onTabReselected(TabLayout.Tab tab) {
                boolean tpMode = tab.getPosition() == 0;
                updateSingleTapDragVisibility(llSingleFinger, tpMode);
                updateCursorSpeedVisibility(llCursorSpeed, tpMode);
            }
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

    private void save(TabLayout tabLayout, MouseMode[] modes, NumberPicker npLongPress, NumberPicker npDoubleTap,
                      NumberPicker npDragThreshold, NumberPicker npSingleTapDelay, SeekBar sbCursorSpeed) {
        // Save mode
        profile.setMouseMode(modes[tabLayout.getSelectedTabPosition()]);

        // Save timing
        profile.setLongPressTimeout(npLongPress.getValue());
        profile.setDoubleTapTimeout(npDoubleTap.getValue());
        profile.setDragThreshold(npDragThreshold.getValue());
        profile.setSingleTapDelay(npSingleTapDelay.getValue());

        // Save cursor speed (touchpad-only)
        float speed = 0.25f + (sbCursorSpeed.getProgress() / 55.0f) * 2.75f;
        profile.setCursorSpeed(speed);

        // Save gesture bindings (BindPackage includes sticky flags)
        profile.setGestureSingleTapAction(bindingValues.get("single"));
        profile.setGestureSingleTapDragAction(bindingValues.get("single_drag"));
        profile.setGestureLongPressAction(bindingValues.get("long"));
        profile.setGestureLongPressDragAction(bindingValues.get("long_drag"));
        profile.setGestureDoubleTapAction(bindingValues.get("double"));
        profile.setGestureDoubleTapDragAction(bindingValues.get("double_drag"));
        profile.setGestureSingleTap2ndFingerAction(bindingValues.get("single_2nd"));
        profile.setGestureSingleTap2ndFingerDragAction(bindingValues.get("single_2nd_drag"));
        profile.setGestureDoubleTap2ndFingerAction(bindingValues.get("double_2nd"));
        profile.setGestureDoubleTap2ndFingerDragAction(bindingValues.get("double_2nd_drag"));

        profile.save();
        if (onSaveListener != null) onSaveListener.onSave(profile);
    }

    private void addBindingSection(LinearLayout container, String key, String label, BindPackage bp) {
        addBindingSection(container, key, label, bp, 0);
    }

    private void addBindingSection(LinearLayout container, String key, String label, BindPackage bp, int helpTextResId) {
        BindPackage stored = new BindPackage(bp);
        if (android.util.Log.isLoggable("Winlator_Gesture", android.util.Log.WARN))
            android.util.Log.w("Winlator_Gesture", "open: key="+key+" toggleSwitch="+stored.isToggleSwitch()+" label="+label);
        bindingValues.put(key, stored);
        View section = com.winlator.cmod.widget.BindingSequenceEditor.createView(context, label, helpTextResId, stored, () -> {});
        container.addView(section);
    }
}
