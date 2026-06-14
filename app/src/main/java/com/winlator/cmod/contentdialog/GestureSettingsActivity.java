package com.winlator.cmod.contentdialog;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.preference.PreferenceManager;
import android.view.MenuItem;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.tabs.TabLayout;

import com.winlator.cmod.R;
import com.winlator.cmod.core.AppUtils;
import com.winlator.cmod.core.UnitUtils;
import com.winlator.cmod.inputcontrols.Bind;
import com.winlator.cmod.inputcontrols.BindPackage;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.inputcontrols.InputControlsManager;
import com.winlator.cmod.inputcontrols.MouseMode;
import com.winlator.cmod.widget.BindingSequenceEditor;
import com.winlator.cmod.widget.NumberPicker;

import java.util.HashMap;

public class GestureSettingsActivity extends AppCompatActivity {
    private ControlsProfile profile;
    private final HashMap<String, BindPackage> bindingValues = new HashMap<>();
    private final HashMap<String, BindPackage> scrollBindingValues = new HashMap<>();

    public static void start(Context context, ControlsProfile profile) {
        Intent intent = new Intent(context, GestureSettingsActivity.class);
        intent.putExtra("profile_id", profile.id);
        context.startActivity(intent);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
        boolean isDarkMode = prefs.getBoolean("dark_mode", true);
        if (isDarkMode) {
            setTheme(R.style.AppTheme_Dark);
        } else {
            setTheme(R.style.AppTheme);
        }

        super.onCreate(savedInstanceState);
        setContentView(R.layout.gesture_settings_activity);

        if (getSupportActionBar() != null) {
            getSupportActionBar().setDisplayHomeAsUpEnabled(true);
            getSupportActionBar().setTitle("Gesture Settings");
        }

        int profileId = getIntent().getIntExtra("profile_id", 0);
        profile = InputControlsManager.loadProfile(this, ControlsProfile.getProfileFile(this, profileId));

        View view = findViewById(android.R.id.content);

        TabLayout tabLayout = view.findViewById(R.id.TabLayoutInputMode);
        MouseMode currentMode = profile.getMouseMode();
        MouseMode[] modes = MouseMode.values();
        tabLayout.getTabAt(currentMode.ordinal()).select();

        NumberPicker npLongPress = view.findViewById(R.id.NPLongPressTimeout);
        NumberPicker npDoubleTap = view.findViewById(R.id.NPDoubleTapTimeout);
        NumberPicker npDragThreshold = view.findViewById(R.id.NPDragThreshold);
        NumberPicker npSingleTapDelay = view.findViewById(R.id.NPSingleTapDelay);

        npLongPress.setValue(profile.getLongPressTimeout());
        npDoubleTap.setValue(profile.getDoubleTapTimeout());
        npDragThreshold.setValue(profile.getDragThreshold());
        npSingleTapDelay.setValue(profile.getSingleTapDelay());

        View llScrollThreshold = view.findViewById(R.id.LLScrollThreshold);
        NumberPicker npScrollThreshold = view.findViewById(R.id.NPScrollThreshold);
        npScrollThreshold.setValue(profile.getScrollThreshold());

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
                AppUtils.showHelpBox(this, v, R.string.two_finger_help));

        boolean isTouchpad = currentMode == MouseMode.TOUCHPAD;

        LinearLayout llSingleFinger = view.findViewById(R.id.LLSingleFinger);
        LinearLayout llHoldGestures = view.findViewById(R.id.LLHoldGesturesContent);
        LinearLayout llTwoFinger = view.findViewById(R.id.LLTwoFinger);

        final int[] DRAG_GESTURE_INDICES = {
            ControlsProfile.GESTURE_SINGLE_TAP_DRAG,
            ControlsProfile.GESTURE_LONG_PRESS_DRAG,
            ControlsProfile.GESTURE_DOUBLE_TAP_DRAG,
            ControlsProfile.GESTURE_SINGLE_DRAG_2ND,
            ControlsProfile.GESTURE_DOUBLE_DRAG_2ND
        };

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
            int gestureIdx = sectionGestureIndices[i];
            BindPackage bp = profile.getGestureAction(gestureIdx);
            BindPackage stored = new BindPackage(bp);
            bindingValues.put(sectionKeys[i], stored);

            boolean isDragGesture = false;
            int scrollSlotIndex = -1;
            for (int d = 0; d < DRAG_GESTURE_INDICES.length; d++) {
                if (DRAG_GESTURE_INDICES[d] == gestureIdx) {
                    isDragGesture = true;
                    scrollSlotIndex = d;
                    break;
                }
            }

            View section;
            if (isDragGesture) {
                HashMap<String, BindPackage> scrollBps = new HashMap<>();
                String[] dirKeys = {"up", "down", "left", "right"};
                boolean hasScrollBindings = false;
                for (int dir = 0; dir < 4; dir++) {
                    String scrollKey = "scroll_" + gestureIdx + "_" + dirKeys[dir];
                    BindPackage scrollBp = profile.getScrollBinding(scrollSlotIndex, dir);
                    BindPackage storedScroll = new BindPackage(scrollBp);
                    scrollBindingValues.put(scrollKey, storedScroll);
                    scrollBps.put(dirKeys[dir], storedScroll);
                    if (storedScroll.size() > 0) hasScrollBindings = true;
                }
                section = BindingSequenceEditor.createDragView(this, sectionLabels[i], sectionHelpResIds[i],
                        stored, () -> {}, scrollBps, hasScrollBindings);
            } else {
                section = BindingSequenceEditor.createView(this, sectionLabels[i], sectionHelpResIds[i],
                        stored, () -> {});
            }
            sectionContainers[i].addView(section);
        }

        updateSingleTapDragVisibility(llSingleFinger, isTouchpad);
        updateCursorSpeedVisibility(llCursorSpeed, isTouchpad);
        updateScrollThresholdVisibility(llScrollThreshold, isTouchpad);

        tabLayout.addOnTabSelectedListener(new TabLayout.OnTabSelectedListener() {
            @Override
            public void onTabSelected(TabLayout.Tab tab) {
                boolean tpMode = tab.getPosition() == 0;
                updateSingleTapDragVisibility(llSingleFinger, tpMode);
                updateCursorSpeedVisibility(llCursorSpeed, tpMode);
                updateScrollThresholdVisibility(llScrollThreshold, tpMode);
            }
            @Override
            public void onTabUnselected(TabLayout.Tab tab) {}
            @Override
            public void onTabReselected(TabLayout.Tab tab) {
                boolean tpMode = tab.getPosition() == 0;
                updateSingleTapDragVisibility(llSingleFinger, tpMode);
                updateCursorSpeedVisibility(llCursorSpeed, tpMode);
                updateScrollThresholdVisibility(llScrollThreshold, tpMode);
            }
        });
    }

    @Override
    public void onBackPressed() {
        save();
        super.onBackPressed();
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        if (item.getItemId() == android.R.id.home) {
            save();
            finish();
            return true;
        }
        return super.onOptionsItemSelected(item);
    }

    private void save() {
        TabLayout tabLayout = findViewById(R.id.TabLayoutInputMode);
        NumberPicker npLongPress = findViewById(R.id.NPLongPressTimeout);
        NumberPicker npDoubleTap = findViewById(R.id.NPDoubleTapTimeout);
        NumberPicker npDragThreshold = findViewById(R.id.NPDragThreshold);
        NumberPicker npSingleTapDelay = findViewById(R.id.NPSingleTapDelay);
        SeekBar sbCursorSpeed = findViewById(R.id.SBCursorSpeed);
        NumberPicker npScrollThreshold = findViewById(R.id.NPScrollThreshold);

        MouseMode[] modes = MouseMode.values();
        profile.setMouseMode(modes[tabLayout.getSelectedTabPosition()]);
        profile.setLongPressTimeout(npLongPress.getValue());
        profile.setDoubleTapTimeout(npDoubleTap.getValue());
        profile.setDragThreshold(npDragThreshold.getValue());
        profile.setSingleTapDelay(npSingleTapDelay.getValue());

        float speed = 0.25f + (sbCursorSpeed.getProgress() / 55.0f) * 2.75f;
        profile.setCursorSpeed(speed);
        profile.setScrollThreshold(npScrollThreshold.getValue());

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

        String[] dirKeys = {"up", "down", "left", "right"};
        int[] dragGestureIndices = {
            ControlsProfile.GESTURE_SINGLE_TAP_DRAG,
            ControlsProfile.GESTURE_LONG_PRESS_DRAG,
            ControlsProfile.GESTURE_DOUBLE_TAP_DRAG,
            ControlsProfile.GESTURE_SINGLE_DRAG_2ND,
            ControlsProfile.GESTURE_DOUBLE_DRAG_2ND
        };
        for (int slot = 0; slot < 5; slot++) {
            for (int dir = 0; dir < 4; dir++) {
                String scrollKey = "scroll_" + dragGestureIndices[slot] + "_" + dirKeys[dir];
                BindPackage scrollBp = scrollBindingValues.get(scrollKey);
                if (scrollBp != null) {
                    profile.setScrollBinding(slot, dir, scrollBp);
                }
            }
        }

        profile.save();
    }

    private void updateSingleTapDragVisibility(LinearLayout container, boolean isTouchpad) {
        if (container.getChildCount() > 1) {
            View section = container.getChildAt(1);
            section.setAlpha(isTouchpad ? 0.4f : 1.0f);
            setViewEnabled(section, !isTouchpad);
        }
    }

    private void updateCursorSpeedVisibility(View container, boolean isTouchpad) {
        container.setAlpha(isTouchpad ? 1.0f : 0.4f);
    }

    private void updateScrollThresholdVisibility(View container, boolean isTouchpad) {
        container.setAlpha(isTouchpad ? 0.4f : 1.0f);
    }

    private void setViewEnabled(View view, boolean enabled) {
        view.setEnabled(enabled);
        if (view instanceof android.view.ViewGroup) {
            android.view.ViewGroup group = (android.view.ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++) {
                setViewEnabled(group.getChildAt(i), enabled);
            }
        }
    }
}
