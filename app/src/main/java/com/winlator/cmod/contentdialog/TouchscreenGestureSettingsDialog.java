package com.winlator.cmod.contentdialog;

import android.app.AlertDialog;
import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;

import android.view.ViewGroup;

import com.winlator.cmod.R;
import com.winlator.cmod.core.AppUtils;
import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.inputcontrols.SecondFingerMode;
import com.winlator.cmod.widget.NumberPicker;

import java.util.HashMap;

public class TouchscreenGestureSettingsDialog {
    private final Context context;
    private final ControlsProfile profile;
    private OnSaveListener onSaveListener;
    private final HashMap<String, Binding> bindingValues = new HashMap<>();

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

        Spinner spHoldMode = view.findViewById(R.id.SPHoldMode);

        setupEnumSpinner(spHoldMode, SecondFingerMode.values(), profile.getSecondFingerMode());

        LinearLayout llHoldModeOptions = view.findViewById(R.id.LLHoldModeOptions);
        LinearLayout llTwoFingerSection = view.findViewById(R.id.LLTwoFingerSection);
        Runnable updateHoldModeVisibility = () -> {
            SecondFingerMode mode = (SecondFingerMode) spHoldMode.getSelectedItem();
            boolean isLongTap = mode == SecondFingerMode.LONG_TAP_ACTION;
            llHoldModeOptions.setVisibility(isLongTap ? View.VISIBLE : View.GONE);
            llTwoFingerSection.setVisibility(isLongTap ? View.GONE : View.VISIBLE);
        };
        spHoldMode.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View v, int pos, long id) { updateHoldModeVisibility.run(); }
            @Override public void onNothingSelected(AdapterView<?> parent) {}
        });
        updateHoldModeVisibility.run();

        view.findViewById(R.id.BTHelpHoldMode).setOnClickListener((v) ->
                AppUtils.showHelpBox(context, v, R.string.hold_mode_help));
        view.findViewById(R.id.BTHelpTwoFinger).setOnClickListener((v) ->
                AppUtils.showHelpBox(context, v, R.string.two_finger_help));

        NumberPicker npLongPress = view.findViewById(R.id.NPLongPressTimeout);
        NumberPicker npDoubleTap = view.findViewById(R.id.NPDoubleTapTimeout);

        npLongPress.setValue(profile.getLongPressTimeout());
        npDoubleTap.setValue(profile.getDoubleTapTimeout());

        LinearLayout llSingleFinger = view.findViewById(R.id.LLSingleFinger);
        addBindingPicker(llSingleFinger, "single", "Single Tap", profile.getSingleTapAction());
        addBindingPicker(llSingleFinger, "single_drag", "  Single Tap Drag", profile.getSingleTapDragAction());
        addBindingPicker(llSingleFinger, "double", "Double Tap", profile.getDoubleTapAction());
        addBindingPicker(llSingleFinger, "double_drag", "  Double Tap Drag", profile.getDoubleTapDragAction());

        LinearLayout llHoldGestures = view.findViewById(R.id.LLHoldGestures);
        addBindingPicker(llHoldGestures, "long", "Long Press", profile.getLongPressAction());
        addBindingPicker(llHoldGestures, "long_drag", "  Long Press Drag", profile.getLongPressDragAction());

        LinearLayout llTwoFinger = view.findViewById(R.id.LLTwoFinger);
        addBindingPicker(llTwoFinger, "single_2nd", "Single Tap", profile.getSingleTap2ndFingerAction());
        addBindingPicker(llTwoFinger, "single_2nd_drag", "  Single Tap Drag", profile.getSingleTap2ndFingerDragAction());
        addBindingPicker(llTwoFinger, "long_2nd", "Long Press", profile.getLongPress2ndFingerAction());
        addBindingPicker(llTwoFinger, "long_2nd_drag", "  Long Press Drag", profile.getLongPress2ndFingerDragAction());
        addBindingPicker(llTwoFinger, "double_2nd", "Double Tap", profile.getDoubleTap2ndFingerAction());
        addBindingPicker(llTwoFinger, "double_2nd_drag", "  Double Tap Drag", profile.getDoubleTap2ndFingerDragAction());

        builder.setPositiveButton("Save", (dialog, which) -> save(
                spHoldMode,
                npLongPress, npDoubleTap));
        builder.setNegativeButton("Cancel", null);

        builder.create().show();
    }

    private void save(Spinner spHoldMode,
                      NumberPicker npLongPress, NumberPicker npDoubleTap) {
        profile.setSecondFingerMode((SecondFingerMode) spHoldMode.getSelectedItem());
        profile.setLongPressTimeout(npLongPress.getValue());
        profile.setDoubleTapTimeout(npDoubleTap.getValue());

        profile.setSingleTapAction(bindingValues.getOrDefault("single", Binding.NONE));
        profile.setSingleTapDragAction(bindingValues.getOrDefault("single_drag", Binding.NONE));
        profile.setLongPressAction(bindingValues.getOrDefault("long", Binding.NONE));
        profile.setLongPressDragAction(bindingValues.getOrDefault("long_drag", Binding.NONE));
        profile.setDoubleTapAction(bindingValues.getOrDefault("double", Binding.NONE));
        profile.setDoubleTapDragAction(bindingValues.getOrDefault("double_drag", Binding.NONE));
        profile.setSingleTap2ndFingerAction(bindingValues.getOrDefault("single_2nd", Binding.NONE));
        profile.setSingleTap2ndFingerDragAction(bindingValues.getOrDefault("single_2nd_drag", Binding.NONE));
        profile.setLongPress2ndFingerAction(bindingValues.getOrDefault("long_2nd", Binding.NONE));
        profile.setLongPress2ndFingerDragAction(bindingValues.getOrDefault("long_2nd_drag", Binding.NONE));
        profile.setDoubleTap2ndFingerAction(bindingValues.getOrDefault("double_2nd", Binding.NONE));
        profile.setDoubleTap2ndFingerDragAction(bindingValues.getOrDefault("double_2nd_drag", Binding.NONE));

        profile.save();
        if (onSaveListener != null) onSaveListener.onSave(profile);
    }

    private void addBindingPicker(LinearLayout container, String key, String label, Binding current) {
        View row = LayoutInflater.from(context).inflate(R.layout.binding_field, container, false);
        ((TextView) row.findViewById(R.id.TVTitle)).setText(label);

        Spinner spCategory = row.findViewById(R.id.SBindingType);
        Spinner spValue = row.findViewById(R.id.SBinding);

        String[] categories = {"Keyboard", "Mouse", "Gamepad", "None"};
        spCategory.setAdapter(new ArrayAdapter<>(context, android.R.layout.simple_spinner_dropdown_item, categories));

        bindingValues.put(key, current);

        Runnable updateValueSpinner = () -> {
            Binding[] values;
            switch (spCategory.getSelectedItemPosition()) {
                case 0: values = Binding.keyboardBindingValues(); break;
                case 1: values = Binding.mouseBindingValues(); break;
                case 2: values = Binding.gamepadBindingValues(); break;
                default: values = new Binding[]{Binding.NONE}; break;
            }
            spValue.setAdapter(new ArrayAdapter<>(context, android.R.layout.simple_spinner_dropdown_item, values));
            AppUtils.setSpinnerSelectionFromValue(spValue, current.toString());
        };

        if (current.isKeyboard()) spCategory.setSelection(0, false);
        else if (current.isMouse()) spCategory.setSelection(1, false);
        else if (current.isGamepad()) spCategory.setSelection(2, false);
        else spCategory.setSelection(3, false);

        spCategory.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View v, int pos, long id) { updateValueSpinner.run(); }
            @Override public void onNothingSelected(AdapterView<?> parent) {}
        });

        spValue.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View v, int pos, long id) {
                Binding[] values;
                switch (spCategory.getSelectedItemPosition()) {
                    case 0: values = Binding.keyboardBindingValues(); break;
                    case 1: values = Binding.mouseBindingValues(); break;
                    case 2: values = Binding.gamepadBindingValues(); break;
                    default: values = new Binding[]{Binding.NONE}; break;
                }
                if (pos >= 0 && pos < values.length) {
                    bindingValues.put(key, values[pos]);
                }
            }
            @Override public void onNothingSelected(AdapterView<?> parent) {}
        });

        updateValueSpinner.run();
        container.addView(row);
    }

    private <T extends Enum<T>> void setupEnumSpinner(Spinner spinner, T[] values, T current) {
        int selectedIndex = 0;
        for (int i = 0; i < values.length; i++) {
            if (values[i] == current) selectedIndex = i;
        }
        spinner.setAdapter(new ArrayAdapter<>(context, android.R.layout.simple_spinner_dropdown_item, values));
        spinner.setSelection(selectedIndex, false);
    }
}
