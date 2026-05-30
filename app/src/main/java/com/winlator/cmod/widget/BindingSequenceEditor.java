package com.winlator.cmod.widget;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;

import com.winlator.cmod.R;
import com.winlator.cmod.core.AppUtils;
import com.winlator.cmod.core.UnitUtils;
import com.winlator.cmod.inputcontrols.Binding;

import java.util.List;

public class BindingSequenceEditor {
    public static View createView(Context context, String label, int helpTextResId,
                                   final List<Binding> bindings, final Runnable onChanged) {
        final LinearLayout section = new LinearLayout(context);
        section.setOrientation(LinearLayout.VERTICAL);
        section.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        if (helpTextResId != 0) {
            LinearLayout titleRow = new LinearLayout(context);
            titleRow.setOrientation(LinearLayout.HORIZONTAL);
            TextView tvTitle = new TextView(context);
            tvTitle.setText(label);
            titleRow.addView(tvTitle);
            ImageView btHelp = new ImageView(context);
            int helpSize = (int) UnitUtils.dpToPx(22);
            LinearLayout.LayoutParams lpHelp = new LinearLayout.LayoutParams(helpSize, helpSize);
            lpHelp.leftMargin = (int) UnitUtils.dpToPx(4);
            btHelp.setLayoutParams(lpHelp);
            btHelp.setImageResource(R.drawable.icon_help);
            btHelp.setColorFilter(0xffe0e0e0);
            btHelp.setOnClickListener((v) -> AppUtils.showHelpBox(context, v, helpTextResId));
            titleRow.addView(btHelp);
            section.addView(titleRow);
        }
        else {
            TextView tvTitle = new TextView(context);
            tvTitle.setText(label);
            section.addView(tvTitle);
        }

        final LinearLayout llMods = new LinearLayout(context);
        llMods.setOrientation(LinearLayout.HORIZONTAL);
        int buttonHeight = (int) UnitUtils.dpToPx(36);
        int modMargin = (int) UnitUtils.dpToPx(2);
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

        final Runnable[] populateRef = new Runnable[1];
        populateRef[0] = () -> {
            llItems.removeAllViews();

            boolean hasNonModifier = false;
            for (Binding b : bindings) {
                if (!b.isModifier()) { hasNonModifier = true; break; }
            }

            llMods.setVisibility(hasNonModifier ? View.VISIBLE : View.GONE);
            if (!hasNonModifier) return;

            boolean ctrlOn = bindings.contains(Binding.MOD_CTRL);
            boolean shiftOn = bindings.contains(Binding.MOD_SHIFT);
            boolean altOn = bindings.contains(Binding.MOD_ALT);
            btCtrl.setBackgroundResource(ctrlOn ? R.drawable.button_positive : R.drawable.button_neutral);
            btCtrl.setTextColor(ctrlOn ? 0xffffffff : 0xaaffffff);
            btCtrl.setAlpha(ctrlOn ? 1.0f : 0.55f);
            btShift.setBackgroundResource(shiftOn ? R.drawable.button_positive : R.drawable.button_neutral);
            btShift.setTextColor(shiftOn ? 0xffffffff : 0xaaffffff);
            btShift.setAlpha(shiftOn ? 1.0f : 0.55f);
            btAlt.setBackgroundResource(altOn ? R.drawable.button_positive : R.drawable.button_neutral);
            btAlt.setTextColor(altOn ? 0xffffffff : 0xaaffffff);
            btAlt.setAlpha(altOn ? 1.0f : 0.55f);
            btCtrl.setOnClickListener(null);
            btShift.setOnClickListener(null);
            btAlt.setOnClickListener(null);
            btCtrl.setOnClickListener((v) -> {
                boolean wasOn = bindings.contains(Binding.MOD_CTRL);
                if (wasOn) bindings.remove(Binding.MOD_CTRL); else bindings.add(Binding.MOD_CTRL);
                if (onChanged != null) onChanged.run();
                if (populateRef[0] != null) populateRef[0].run();
            });
            btShift.setOnClickListener((v) -> {
                boolean wasOn = bindings.contains(Binding.MOD_SHIFT);
                if (wasOn) bindings.remove(Binding.MOD_SHIFT); else bindings.add(Binding.MOD_SHIFT);
                if (onChanged != null) onChanged.run();
                if (populateRef[0] != null) populateRef[0].run();
            });
            btAlt.setOnClickListener((v) -> {
                boolean wasOn = bindings.contains(Binding.MOD_ALT);
                if (wasOn) bindings.remove(Binding.MOD_ALT); else bindings.add(Binding.MOD_ALT);
                if (onChanged != null) onChanged.run();
                if (populateRef[0] != null) populateRef[0].run();
            });

            for (int i = 0; i < bindings.size(); i++) {
                final Binding currentBinding = bindings.get(i);
                if (currentBinding.isModifier()) continue;
                final int seqIndex = i;
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
                        if (seqIndex < bindings.size() && binding != bindings.get(seqIndex)) {
                            bindings.set(seqIndex, binding);
                            if (onChanged != null) onChanged.run();
                        }
                    }
                    @Override public void onNothingSelected(AdapterView<?> parent) {}
                });

                row.findViewById(R.id.BTRemove).setOnClickListener((v) -> {
                    if (seqIndex < bindings.size()) {
                        bindings.remove(seqIndex);
                    }
                    if (onChanged != null) onChanged.run();
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
            bindings.add(Binding.NONE);
            if (onChanged != null) onChanged.run();
            if (populateRef[0] != null) populateRef[0].run();
        });
        section.addView(btAdd);

        return section;
    }
}
