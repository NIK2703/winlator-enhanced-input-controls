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
        LinearLayout llHoldGestures = view.findViewById(R.id.LLHoldGesturesContent);
        LinearLayout llTwoFinger = view.findViewById(R.id.LLTwoFinger);

        // Gesture binding sections (mirrors saveKeys/saveIndices pattern in save())
        LinearLayout[] sectionContainers = {llSingleFinger, llSingleFinger, llSingleFinger, llSingleFinger,
                                            llHoldGestures, llHoldGestures,
                                            llTwoFinger, llTwoFinger, llTwoFinger, llTwoFinger};
        String[] sectionKeys = {"single", "single_drag", "double", "double_drag",
                                "long", "long_drag",
                                "single_2nd", "single_2nd_drag", "double_2nd", "double_2nd_drag"};
        String[] sectionLabels = {"Single Tap", "Single Tap Drag", "Double Tap", "Double Tap Drag",
                                  "Long Press", "Long Press Drag",
                                  "Single Tap", "Single Tap Drag", "Double Tap", "Double Tap Drag"};
        int[] sectionGestureIndices = {ControlsProfile.GESTURE_SINGLE_TAP, ControlsProfile.GESTURE_SINGLE_TAP_DRAG,
                                       ControlsProfile.GESTURE_DOUBLE_TAP, ControlsProfile.GESTURE_DOUBLE_TAP_DRAG,
                                       ControlsProfile.GESTURE_LONG_PRESS, ControlsProfile.GESTURE_LONG_PRESS_DRAG,
                                       ControlsProfile.GESTURE_SINGLE_2ND, ControlsProfile.GESTURE_SINGLE_DRAG_2ND,
                                       ControlsProfile.GESTURE_DOUBLE_2ND, ControlsProfile.GESTURE_DOUBLE_DRAG_2ND};
        int[] sectionHelpResIds = {R.string.single_tap_help, R.string.single_tap_drag_help,
                                    R.string.double_tap_help, R.string.single_tap_delay_help,
                                   R.string.long_press_drag_help, R.string.long_press_drag_help,
                                   0, 0, R.string.single_tap_delay_help, R.string.single_tap_delay_help};
        for (int i = 0; i < sectionKeys.length; i++) {
            addBindingSection(sectionContainers[i], sectionKeys[i], sectionLabels[i],
                              profile.getGestureAction(sectionGestureIndices[i]), sectionHelpResIds[i]);
        }

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
        String[] saveKeys = {"single", "single_drag", "long", "long_drag",
            "double", "double_drag",
            "single_2nd", "single_2nd_drag", "double_2nd", "double_2nd_drag"};
        int[] saveIndices = {ControlsProfile.GESTURE_SINGLE_TAP, ControlsProfile.GESTURE_SINGLE_TAP_DRAG,
            ControlsProfile.GESTURE_LONG_PRESS, ControlsProfile.GESTURE_LONG_PRESS_DRAG,
            ControlsProfile.GESTURE_DOUBLE_TAP, ControlsProfile.GESTURE_DOUBLE_TAP_DRAG,
            ControlsProfile.GESTURE_SINGLE_2ND, ControlsProfile.GESTURE_SINGLE_DRAG_2ND,
            ControlsProfile.GESTURE_DOUBLE_2ND, ControlsProfile.GESTURE_DOUBLE_DRAG_2ND};

        for (int i = 0; i < saveKeys.length; i++) {
            profile.setGestureAction(saveIndices[i], bindingValues.get(saveKeys[i]));
        }

        profile.save();
        if (onSaveListener != null) onSaveListener.onSave(profile);
    }

    private void addBindingSection(LinearLayout container, String key, String label, BindPackage bp) {
        addBindingSection(container, key, label, bp, 0);
    }

    private void addBindingSection(LinearLayout container, String key, String label, BindPackage bp, int helpTextResId) {
        BindPackage stored = new BindPackage(bp);
        bindingValues.put(key, stored);
        View section = com.winlator.cmod.widget.BindingSequenceEditor.createView(context, label, helpTextResId, stored, () -> {});
        container.addView(section);
    }
}
