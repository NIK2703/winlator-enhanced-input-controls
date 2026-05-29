package com.winlator.cmod.contentdialog;

import android.app.AlertDialog;
import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;

import com.winlator.cmod.R;
import com.winlator.cmod.core.AppUtils;
import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.inputcontrols.DragMode;
import com.winlator.cmod.inputcontrols.InputMode;
import com.winlator.cmod.inputcontrols.MouseMode;
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

        Spinner spMouseMode = view.findViewById(R.id.SPMouseMode);
        Spinner spInputMode = view.findViewById(R.id.SPInputMode);
        Spinner spDragMode = view.findViewById(R.id.SPDragMode);

        setupEnumSpinner(spMouseMode, MouseMode.values(), profile.getMouseMode());
        setupEnumSpinner(spInputMode, InputMode.values(), profile.getInputMode());
        setupEnumSpinner(spDragMode, DragMode.values(), profile.getDragMode());

        NumberPicker npLongPress = view.findViewById(R.id.NPLongPressTimeout);
        NumberPicker npDoubleTap = view.findViewById(R.id.NPDoubleTapTimeout);
        CheckBox cbDeferSingleTap = view.findViewById(R.id.CBDeferSingleTap);

        npLongPress.setValue(profile.getLongPressTimeout());
        npDoubleTap.setValue(profile.getDoubleTapTimeout());
        cbDeferSingleTap.setChecked(profile.isDeferSingleTap());

        LinearLayout llSingleFinger = view.findViewById(R.id.LLSingleFinger);
        addBindingPicker(llSingleFinger, "single", "Single Tap", profile.getSingleTapAction());
        addBindingPicker(llSingleFinger, "long", "Long Press", profile.getLongPressAction());
        addBindingPicker(llSingleFinger, "double", "Double Tap", profile.getDoubleTapAction());

        LinearLayout llTwoFinger = view.findViewById(R.id.LLTwoFinger);
        addBindingPicker(llTwoFinger, "single_2nd", "Single Tap", profile.getSingleTap2ndFingerAction());
        addBindingPicker(llTwoFinger, "long_2nd", "Long Press", profile.getLongPress2ndFingerAction());
        addBindingPicker(llTwoFinger, "double_2nd", "Double Tap", profile.getDoubleTap2ndFingerAction());

        builder.setPositiveButton("Save", (dialog, which) -> save(
                spMouseMode, spInputMode, spDragMode,
                npLongPress, npDoubleTap, cbDeferSingleTap));
        builder.setNegativeButton("Cancel", null);

        builder.create().show();
    }

    private void save(Spinner spMouseMode, Spinner spInputMode, Spinner spDragMode,
                      NumberPicker npLongPress, NumberPicker npDoubleTap, CheckBox cbDeferSingleTap) {
        profile.setMouseMode((MouseMode) spMouseMode.getSelectedItem());
        profile.setInputMode((InputMode) spInputMode.getSelectedItem());
        profile.setDragMode((DragMode) spDragMode.getSelectedItem());
        profile.setLongPressTimeout(npLongPress.getValue());
        profile.setDoubleTapTimeout(npDoubleTap.getValue());
        profile.setDeferSingleTap(cbDeferSingleTap.isChecked());

        profile.setSingleTapAction(bindingValues.getOrDefault("single", Binding.NONE));
        profile.setLongPressAction(bindingValues.getOrDefault("long", Binding.NONE));
        profile.setDoubleTapAction(bindingValues.getOrDefault("double", Binding.NONE));
        profile.setSingleTap2ndFingerAction(bindingValues.getOrDefault("single_2nd", Binding.NONE));
        profile.setLongPress2ndFingerAction(bindingValues.getOrDefault("long_2nd", Binding.NONE));
        profile.setDoubleTap2ndFingerAction(bindingValues.getOrDefault("double_2nd", Binding.NONE));

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
        String[] labels = new String[values.length];
        int selectedIndex = 0;
        for (int i = 0; i < values.length; i++) {
            labels[i] = values[i].toString();
            if (values[i] == current) selectedIndex = i;
        }
        spinner.setAdapter(new ArrayAdapter<>(context, android.R.layout.simple_spinner_dropdown_item, labels));
        spinner.setSelection(selectedIndex, false);
    }
}
