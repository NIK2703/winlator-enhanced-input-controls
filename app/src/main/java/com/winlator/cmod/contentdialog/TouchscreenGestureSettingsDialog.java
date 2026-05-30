package com.winlator.cmod.contentdialog;

import android.app.AlertDialog;
import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;

import com.winlator.cmod.R;
import com.winlator.cmod.core.AppUtils;
import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.inputcontrols.SecondFingerMode;
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
        addBindingSection(llSingleFinger, "single", "Single Tap", profile.getSingleTapAction());
        addBindingSection(llSingleFinger, "single_drag", "  Single Tap Drag", profile.getSingleTapDragAction());
        addBindingSection(llSingleFinger, "double", "Double Tap", profile.getDoubleTapAction());
        addBindingSection(llSingleFinger, "double_drag", "  Double Tap Drag", profile.getDoubleTapDragAction());

        LinearLayout llHoldGestures = view.findViewById(R.id.LLHoldGestures);
        addBindingSection(llHoldGestures, "long", "Long Press", profile.getLongPressAction());
        addBindingSection(llHoldGestures, "long_drag", "  Long Press Drag", profile.getLongPressDragAction());

        LinearLayout llTwoFinger = view.findViewById(R.id.LLTwoFinger);
        addBindingSection(llTwoFinger, "single_2nd", "Single Tap", profile.getSingleTap2ndFingerAction());
        addBindingSection(llTwoFinger, "single_2nd_drag", "  Single Tap Drag", profile.getSingleTap2ndFingerDragAction());
        addBindingSection(llTwoFinger, "long_2nd", "Long Press", profile.getLongPress2ndFingerAction());
        addBindingSection(llTwoFinger, "long_2nd_drag", "  Long Press Drag", profile.getLongPress2ndFingerDragAction());
        addBindingSection(llTwoFinger, "double_2nd", "Double Tap", profile.getDoubleTap2ndFingerAction());
        addBindingSection(llTwoFinger, "double_2nd_drag", "  Double Tap Drag", profile.getDoubleTap2ndFingerDragAction());

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

        profile.setSingleTapAction(bindingValues.getOrDefault("single", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setSingleTapDragAction(bindingValues.getOrDefault("single_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setLongPressAction(bindingValues.getOrDefault("long", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setLongPressDragAction(bindingValues.getOrDefault("long_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setDoubleTapAction(bindingValues.getOrDefault("double", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setDoubleTapDragAction(bindingValues.getOrDefault("double_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setSingleTap2ndFingerAction(bindingValues.getOrDefault("single_2nd", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setSingleTap2ndFingerDragAction(bindingValues.getOrDefault("single_2nd_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setLongPress2ndFingerAction(bindingValues.getOrDefault("long_2nd", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setLongPress2ndFingerDragAction(bindingValues.getOrDefault("long_2nd_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setDoubleTap2ndFingerAction(bindingValues.getOrDefault("double_2nd", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));
        profile.setDoubleTap2ndFingerDragAction(bindingValues.getOrDefault("double_2nd_drag", new ArrayList<>(java.util.Collections.singletonList(Binding.NONE))));

        profile.save();
        if (onSaveListener != null) onSaveListener.onSave(profile);
    }

    private void addBindingSection(LinearLayout container, String key, String label, List<Binding> current) {
        final LinearLayout section = new LinearLayout(context);
        section.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams sectionLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        sectionLp.bottomMargin = (int)com.winlator.cmod.core.UnitUtils.dpToPx(8);
        section.setLayoutParams(sectionLp);

        TextView tvTitle = new TextView(context);
        tvTitle.setText(label);
        section.addView(tvTitle);

        int buttonHeight = (int)com.winlator.cmod.core.UnitUtils.dpToPx(36);
        int modMargin = (int)com.winlator.cmod.core.UnitUtils.dpToPx(2);
        LinearLayout llMods = new LinearLayout(context);
        llMods.setOrientation(LinearLayout.HORIZONTAL);
        final Button btCtrl = new Button(context, null, 0, R.style.ButtonNeutral);
        btCtrl.setText("Ctrl");
        LinearLayout.LayoutParams lpCtrl = new LinearLayout.LayoutParams(0, buttonHeight, 1);
        lpCtrl.setMargins(0, 0, modMargin, 0);
        btCtrl.setLayoutParams(lpCtrl);
        final Button btShift = new Button(context, null, 0, R.style.ButtonNeutral);
        btShift.setText("Shift");
        LinearLayout.LayoutParams lpShift = new LinearLayout.LayoutParams(0, buttonHeight, 1);
        lpShift.setMargins(modMargin, 0, modMargin, 0);
        btShift.setLayoutParams(lpShift);
        final Button btAlt = new Button(context, null, 0, R.style.ButtonNeutral);
        btAlt.setText("Alt");
        LinearLayout.LayoutParams lpAlt = new LinearLayout.LayoutParams(0, buttonHeight, 1);
        lpAlt.setMargins(modMargin, 0, 0, 0);
        btAlt.setLayoutParams(lpAlt);
        llMods.addView(btCtrl);
        llMods.addView(btShift);
        llMods.addView(btAlt);
        section.addView(llMods);

        final LinearLayout llItems = new LinearLayout(context);
        llItems.setOrientation(LinearLayout.VERTICAL);
        section.addView(llItems);

        bindingValues.put(key, new ArrayList<>(current));

        final Runnable[] populateRef = new Runnable[1];
        populateRef[0] = () -> {
            llItems.removeAllViews();
            List<Binding> seq = bindingValues.get(key);
            if (seq == null) {
                seq = new ArrayList<>();
                seq.add(Binding.NONE);
                bindingValues.put(key, seq);
            }

            final String fKey = key;
            boolean ctrlOn = seq.contains(Binding.MOD_CTRL);
            boolean shiftOn = seq.contains(Binding.MOD_SHIFT);
            boolean altOn = seq.contains(Binding.MOD_ALT);
            btCtrl.setBackgroundResource(ctrlOn ? R.drawable.button_positive : R.drawable.button_neutral);
            btCtrl.setTextColor(ctrlOn ? 0xffffffff : 0xaaffffff);
            btShift.setBackgroundResource(shiftOn ? R.drawable.button_positive : R.drawable.button_neutral);
            btShift.setTextColor(shiftOn ? 0xffffffff : 0xaaffffff);
            btAlt.setBackgroundResource(altOn ? R.drawable.button_positive : R.drawable.button_neutral);
            btAlt.setTextColor(altOn ? 0xffffffff : 0xaaffffff);
            btCtrl.setOnClickListener(null);
            btShift.setOnClickListener(null);
            btAlt.setOnClickListener(null);
            btCtrl.setOnClickListener((v) -> {
                List<Binding> s = bindingValues.get(fKey);
                boolean wasOn = s.contains(Binding.MOD_CTRL);
                if (wasOn) s.remove(Binding.MOD_CTRL); else s.add(Binding.MOD_CTRL);
                if (populateRef[0] != null) populateRef[0].run();
            });
            btShift.setOnClickListener((v) -> {
                List<Binding> s = bindingValues.get(fKey);
                boolean wasOn = s.contains(Binding.MOD_SHIFT);
                if (wasOn) s.remove(Binding.MOD_SHIFT); else s.add(Binding.MOD_SHIFT);
                if (populateRef[0] != null) populateRef[0].run();
            });
            btAlt.setOnClickListener((v) -> {
                List<Binding> s = bindingValues.get(fKey);
                boolean wasOn = s.contains(Binding.MOD_ALT);
                if (wasOn) s.remove(Binding.MOD_ALT); else s.add(Binding.MOD_ALT);
                if (populateRef[0] != null) populateRef[0].run();
            });

            for (int i = 0; i < seq.size(); i++) {
                final int seqIndex = i;
                final Binding currentBinding = seq.get(seqIndex);
                if (currentBinding.isModifier()) continue;

                View row = LayoutInflater.from(context).inflate(R.layout.binding_sequence_item, llItems, false);
                final Spinner sBindingType = row.findViewById(R.id.SBindingType);
                final Spinner sBinding = row.findViewById(R.id.SBinding);

                Runnable update = () -> {
                    switch (sBindingType.getSelectedItemPosition()) {
                        case 0:
                            sBinding.setAdapter(new ArrayAdapter<>(context, android.R.layout.simple_spinner_dropdown_item, Binding.keyboardBindingLabels()));
                            break;
                        case 1:
                            sBinding.setAdapter(new ArrayAdapter<>(context, android.R.layout.simple_spinner_dropdown_item, Binding.mouseBindingLabels()));
                            break;
                        case 2:
                            sBinding.setAdapter(new ArrayAdapter<>(context, android.R.layout.simple_spinner_dropdown_item, Binding.gamepadBindingLabels()));
                            break;
                    }
                    AppUtils.setSpinnerSelectionFromValue(sBinding, currentBinding.toString());
                };

                if (currentBinding.isKeyboard()) sBindingType.setSelection(0, false);
                else if (currentBinding.isMouse()) sBindingType.setSelection(1, false);
                else if (currentBinding.isGamepad()) sBindingType.setSelection(2, false);

                update.run();

                final int slotIndex = i;
                sBindingType.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
                    @Override public void onItemSelected(AdapterView<?> parent, View v, int pos, long id) {
                        update.run();
                    }
                    @Override public void onNothingSelected(AdapterView<?> parent) {}
                });

                sBinding.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
                    @Override public void onItemSelected(AdapterView<?> parent, View v, int pos, long id) {
                        Binding binding = Binding.NONE;
                        switch (sBindingType.getSelectedItemPosition()) {
                            case 0: binding = Binding.keyboardBindingValues()[pos]; break;
                            case 1: binding = Binding.mouseBindingValues()[pos]; break;
                            case 2: binding = Binding.gamepadBindingValues()[pos]; break;
                        }
                        List<Binding> seq2 = bindingValues.get(key);
                        if (seq2 != null && seqIndex < seq2.size() && binding != seq2.get(seqIndex)) {
                            seq2.set(seqIndex, binding);
                        }
                    }
                    @Override public void onNothingSelected(AdapterView<?> parent) {}
                });

                row.findViewById(R.id.BTRemove).setOnClickListener((v) -> {
                    List<Binding> seq2 = bindingValues.get(key);
                    if (seq2 != null && seqIndex < seq2.size()) {
                        seq2.remove(seqIndex);
                        if (seq2.isEmpty()) seq2.add(Binding.NONE);
                    }
                    if (populateRef[0] != null) populateRef[0].run();
                });

                llItems.addView(row);
            }
        };

        populateRef[0].run();

        Button btAdd = new Button(context, null, 0, R.style.ButtonNeutral);
        btAdd.setText("+ Add Binding");
        btAdd.setLayoutParams(new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, buttonHeight));
        btAdd.setOnClickListener((v) -> {
            List<Binding> seq = bindingValues.get(key);
            if (seq == null) {
                seq = new ArrayList<>();
                seq.add(Binding.NONE);
                bindingValues.put(key, seq);
            }
            seq.add(Binding.NONE);
            if (populateRef[0] != null) populateRef[0].run();
        });
        section.addView(btAdd);

        container.addView(section);
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
