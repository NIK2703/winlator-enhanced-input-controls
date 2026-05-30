package com.winlator.cmod;

import android.graphics.BitmapFactory;
import android.os.Bundle;
import android.content.SharedPreferences;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.SeekBar;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.RadioGroup;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;
import androidx.preference.PreferenceManager;

import com.winlator.cmod.R;
import com.winlator.cmod.contentdialog.ContentDialog;

import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlElement;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.inputcontrols.InputControlsManager;
import com.winlator.cmod.math.Mathf;
import com.winlator.cmod.core.AppUtils;
import com.winlator.cmod.core.FileUtils;
import com.winlator.cmod.core.UnitUtils;
import com.winlator.cmod.widget.InputControlsView;
import com.winlator.cmod.widget.NumberPicker;

import java.io.IOException;
import java.io.InputStream;
import java.util.Arrays;
import java.util.List;

public class ControlsEditorActivity extends AppCompatActivity implements View.OnClickListener {
    private InputControlsView inputControlsView;
    private ControlsProfile profile;

    @Override
    public void onCreate(Bundle bundle) {
        super.onCreate(bundle);
        AppUtils.hideSystemUI(this);
        setContentView(R.layout.controls_editor_activity);

        inputControlsView = new InputControlsView(this);
        inputControlsView.setEditMode(true);
        inputControlsView.setOverlayOpacity(0.6f);

        profile = InputControlsManager.loadProfile(this, ControlsProfile.getProfileFile(this, getIntent().getIntExtra("profile_id", 0)));
        ((TextView)findViewById(R.id.TVProfileName)).setText(profile.getName());
        inputControlsView.setProfile(profile);

        FrameLayout container = findViewById(R.id.FLContainer);
        container.addView(inputControlsView, 0);

        container.findViewById(R.id.BTAddElement).setOnClickListener(this);
        container.findViewById(R.id.BTRemoveElement).setOnClickListener(this);
        container.findViewById(R.id.BTElementSettings).setOnClickListener(this);
        container.findViewById(R.id.BTCopyElement).setOnClickListener(this);
    }

    @Override
    protected void onResume() {
        super.onResume();
        new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(() -> {
            SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
            if (!prefs.getBoolean("mix_warning_shown_v4", false)) {
                ContentDialog.alert(this, R.string.warning_gamepad_mouse_mix, () -> {
                    prefs.edit().putBoolean("mix_warning_shown_v4", true).apply();
                });
            }
        }, 500);
    }

    @Override
    public void onClick(View v) {
        switch (v.getId()) {
            case R.id.BTAddElement:
                if (!inputControlsView.addElement()) {
                    AppUtils.showToast(this, R.string.no_profile_selected);
                }
                break;
            case R.id.BTRemoveElement:
                if (!inputControlsView.removeElement()) {
                    AppUtils.showToast(this, R.string.no_control_element_selected);
                }
                break;
            case R.id.BTElementSettings:
                ControlElement selectedElement = inputControlsView.getSelectedElement();
                if (selectedElement != null) {
                    showControlElementSettings(v);
                }
                else AppUtils.showToast(this, R.string.no_control_element_selected);
                break;
            case R.id.BTCopyElement:
                if (!inputControlsView.copyElement()) {
                    AppUtils.showToast(this, R.string.no_control_element_selected);
                }
                break;
        }
    }

    private void showControlElementSettings(View anchorView) {
        final ControlElement element = inputControlsView.getSelectedElement();
        View view = LayoutInflater.from(this).inflate(R.layout.control_element_settings, null);

        final Runnable updateLayout = () -> {
            ControlElement.Type type = element.getType();
            view.findViewById(R.id.LLShape).setVisibility(View.GONE);
            view.findViewById(R.id.CBToggleSwitch).setVisibility(View.GONE);
            view.findViewById(R.id.LLCustomTextIcon).setVisibility(View.GONE);
            view.findViewById(R.id.LLRangeOptions).setVisibility(View.GONE);

            if (type == ControlElement.Type.BUTTON) {
                view.findViewById(R.id.LLShape).setVisibility(View.VISIBLE);
                view.findViewById(R.id.CBToggleSwitch).setVisibility(View.VISIBLE);
                view.findViewById(R.id.LLCustomTextIcon).setVisibility(View.VISIBLE);
            }
            else if (type == ControlElement.Type.RANGE_BUTTON) {
                view.findViewById(R.id.LLRangeOptions).setVisibility(View.VISIBLE);
            }

            loadBindingSpinners(element, view);
        };

        loadTypeSpinner(element, view.findViewById(R.id.SType), updateLayout);
        loadShapeSpinner(element, view.findViewById(R.id.SShape));
        loadRangeSpinner(element, view.findViewById(R.id.SRange));

        RadioGroup rgOrientation = view.findViewById(R.id.RGOrientation);
        rgOrientation.check(element.getOrientation() == 1 ? R.id.RBVertical : R.id.RBHorizontal);
        rgOrientation.setOnCheckedChangeListener((group, checkedId) -> {
            element.setOrientation((byte)(checkedId == R.id.RBVertical ? 1 : 0));
            profile.save();
            inputControlsView.invalidate();
        });

        NumberPicker npColumns = view.findViewById(R.id.NPColumns);
        npColumns.setValue(element.getBindingCount());
        npColumns.setOnValueChangeListener((numberPicker, value) -> {
            element.setBindingCount(value);
            profile.save();
            inputControlsView.invalidate();
        });

        final TextView tvScale = view.findViewById(R.id.TVScale);
        SeekBar sbScale = view.findViewById(R.id.SBScale);
        sbScale.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                tvScale.setText(progress+"%");
                if (fromUser) {
                    progress = (int)Mathf.roundTo(progress, 5);
                    seekBar.setProgress(progress);
                    element.setScale(progress / 100.0f);
                    profile.save();
                    inputControlsView.invalidate();
                }
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {}

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        sbScale.setProgress((int)(element.getScale() * 100));

        CheckBox cbToggleSwitch = view.findViewById(R.id.CBToggleSwitch);
        cbToggleSwitch.setChecked(element.isToggleSwitch());
        cbToggleSwitch.setOnCheckedChangeListener((buttonView, isChecked) -> {
            element.setToggleSwitch(isChecked);
            profile.save();
        });

        final EditText etCustomText = view.findViewById(R.id.ETCustomText);
        etCustomText.setText(element.getText());
        final LinearLayout llIconList = view.findViewById(R.id.LLIconList);
        loadIcons(llIconList, element.getIconId());

        updateLayout.run();

        PopupWindow popupWindow = AppUtils.showPopupWindow(anchorView, view, 340, 0);
        popupWindow.setOnDismissListener(() -> {
            String text = etCustomText.getText().toString().trim();
            byte iconId = 0;
            for (int i = 0; i < llIconList.getChildCount(); i++) {
                View child = llIconList.getChildAt(i);
                if (child.isSelected()) {
                    iconId = (byte)child.getTag();
                    break;
                }
            }

            element.setText(text);
            element.setIconId(iconId);
            profile.save();
            inputControlsView.invalidate();
        });
    }

    private void loadTypeSpinner(final ControlElement element, Spinner spinner, Runnable callback) {
        final boolean[] init = {true};
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                if (init[0]) return;
                element.setType(ControlElement.Type.values()[position]);
                profile.save();
                callback.run();
                inputControlsView.invalidate();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {}
        });
        spinner.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, ControlElement.Type.names()));
        spinner.setSelection(element.getType().ordinal(), false);
        init[0] = false;
    }

    private void loadShapeSpinner(final ControlElement element, Spinner spinner) {
        spinner.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, ControlElement.Shape.names()));
        spinner.setSelection(element.getShape().ordinal(), false);
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                element.setShape(ControlElement.Shape.values()[position]);
                profile.save();
                inputControlsView.invalidate();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {}
        });
    }

    private void loadBindingSpinners(ControlElement element, View view) {
        LinearLayout container = view.findViewById(R.id.LLBindings);
        container.removeAllViews();

        ControlElement.Type type = element.getType();
        if (type == ControlElement.Type.BUTTON) {
            loadBindingSection(element, container, 0, R.string.binding);
        }
        else if (type == ControlElement.Type.D_PAD || type == ControlElement.Type.STICK || type == ControlElement.Type.TRACKPAD) {
            loadBindingSection(element, container, 0, R.string.binding_up);
            loadBindingSection(element, container, 1, R.string.binding_right);
            loadBindingSection(element, container, 2, R.string.binding_down);
            loadBindingSection(element, container, 3, R.string.binding_left);
        }
    }

    private void loadBindingSection(final ControlElement element, LinearLayout container, final int index, int titleResId) {
        final LinearLayout section = new LinearLayout(this);
        section.setOrientation(LinearLayout.VERTICAL);
        section.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        TextView tvTitle = new TextView(this);
        tvTitle.setText(titleResId);
        tvTitle.setTextSize(14);
        section.addView(tvTitle);

        LinearLayout llMods = new LinearLayout(this);
        llMods.setOrientation(LinearLayout.HORIZONTAL);
        int buttonHeight = (int)UnitUtils.dpToPx(36);
        int modMargin = (int)UnitUtils.dpToPx(2);
        final Button btCtrl = new Button(this, null, 0, R.style.ButtonNeutral);
        btCtrl.setText("Ctrl");
        LinearLayout.LayoutParams lpCtrl = new LinearLayout.LayoutParams(0, buttonHeight, 1);
        lpCtrl.setMargins(0, 0, modMargin, 0);
        btCtrl.setLayoutParams(lpCtrl);
        final Button btShift = new Button(this, null, 0, R.style.ButtonNeutral);
        btShift.setText("Shift");
        LinearLayout.LayoutParams lpShift = new LinearLayout.LayoutParams(0, buttonHeight, 1);
        lpShift.setMargins(modMargin, 0, modMargin, 0);
        btShift.setLayoutParams(lpShift);
        final Button btAlt = new Button(this, null, 0, R.style.ButtonNeutral);
        btAlt.setText("Alt");
        LinearLayout.LayoutParams lpAlt = new LinearLayout.LayoutParams(0, buttonHeight, 1);
        lpAlt.setMargins(modMargin, 0, 0, 0);
        btAlt.setLayoutParams(lpAlt);
        llMods.addView(btCtrl);
        llMods.addView(btShift);
        llMods.addView(btAlt);
        section.addView(llMods);

        final LinearLayout llItems = new LinearLayout(this);
        llItems.setOrientation(LinearLayout.VERTICAL);
        section.addView(llItems);

        final Runnable[] populateItemsRef = new Runnable[1];
        populateItemsRef[0] = () -> {
            llItems.removeAllViews();
            List<Binding> seq = element.getBindingSequence(index);

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
            btCtrl.setOnClickListener((v) -> toggleModifier(element, index, Binding.MOD_CTRL, !ctrlOn, populateItemsRef[0]));
            btShift.setOnClickListener((v) -> toggleModifier(element, index, Binding.MOD_SHIFT, !shiftOn, populateItemsRef[0]));
            btAlt.setOnClickListener((v) -> toggleModifier(element, index, Binding.MOD_ALT, !altOn, populateItemsRef[0]));

            int displayIndex = 0;
            for (int i = 0; i < seq.size(); i++) {
                Binding currentBinding = seq.get(i);
                if (currentBinding.isModifier()) continue;
                final int seqIndex = i;
                final int dIndex = displayIndex;
                View row = LayoutInflater.from(this).inflate(R.layout.binding_sequence_item, llItems, false);
                final Spinner sBindingType = row.findViewById(R.id.SBindingType);
                final Spinner sBinding = row.findViewById(R.id.SBinding);

                Runnable update = () -> {
                    switch (sBindingType.getSelectedItemPosition()) {
                        case 0:
                            sBinding.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, Binding.keyboardBindingLabels()));
                            break;
                        case 1:
                            sBinding.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, Binding.mouseBindingLabels()));
                            break;
                        case 2:
                            sBinding.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, Binding.gamepadBindingLabels()));
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
                        if (binding != seq.get(seqIndex)) {
                            element.setBindingAtSequenceIndex(index, seqIndex, binding);
                            profile.save();
                            inputControlsView.invalidate();
                        }
                    }
                    @Override public void onNothingSelected(AdapterView<?> parent) {}
                });

                row.findViewById(R.id.BTRemove).setOnClickListener((v) -> {
                    element.removeBindingFromSequence(index, seqIndex);
                    profile.save();
                    inputControlsView.invalidate();
                    populateItemsRef[0].run();
                });

                llItems.addView(row);
                displayIndex++;
            }
        };

        populateItemsRef[0].run();

        Button btAdd = new Button(this, null, 0, R.style.ButtonNeutral);
        btAdd.setText("+ Add Binding");
        btAdd.setLayoutParams(new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, buttonHeight));
        btAdd.setOnClickListener((v) -> {
            element.addBindingToSequence(index, Binding.NONE);
            profile.save();
            inputControlsView.invalidate();
            populateItemsRef[0].run();
        });
        section.addView(btAdd);

        container.addView(section);
    }

    private void toggleModifier(ControlElement element, int seqIndex, Binding modBinding, boolean checked, Runnable populate) {
        if (checked) {
            element.addBindingToSequence(seqIndex, modBinding);
        } else {
            int idx = element.getBindingSequence(seqIndex).indexOf(modBinding);
            if (idx >= 0) element.removeBindingFromSequence(seqIndex, idx);
        }
        profile.save();
        inputControlsView.invalidate();
        populate.run();
    }

    private void loadRangeSpinner(final ControlElement element, Spinner spinner) {
        spinner.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, ControlElement.Range.names()));
        spinner.setSelection(element.getRange().ordinal(), false);
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                element.setRange(ControlElement.Range.values()[position]);
                profile.save();
                inputControlsView.invalidate();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {}
        });
    }

    private void loadIcons(final LinearLayout parent, byte selectedId) {
        byte[] iconIds = new byte[0];
        try {
            String[] filenames = getAssets().list("inputcontrols/icons/");
            iconIds = new byte[filenames.length];
            for (int i = 0; i < filenames.length; i++) {
                iconIds[i] = Byte.parseByte(FileUtils.getBasename(filenames[i]));
            }
        }
        catch (IOException e) {}

        Arrays.sort(iconIds);

        int size = (int)UnitUtils.dpToPx(40);
        int margin = (int)UnitUtils.dpToPx(2);
        int padding = (int)UnitUtils.dpToPx(4);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(size, size);
        params.setMargins(margin, 0, margin, 0);

        for (final byte id : iconIds) {
            ImageView imageView = new ImageView(this);
            imageView.setLayoutParams(params);
            imageView.setPadding(padding, padding, padding, padding);
            imageView.setBackgroundResource(R.drawable.icon_background);
            imageView.setTag(id);
            imageView.setSelected(id == selectedId);
            imageView.setOnClickListener((v) -> {
                for (int i = 0; i < parent.getChildCount(); i++) parent.getChildAt(i).setSelected(false);
                imageView.setSelected(true);
            });

            try (InputStream is = getAssets().open("inputcontrols/icons/"+id+".png")) {
                imageView.setImageBitmap(BitmapFactory.decodeStream(is));
            }
            catch (IOException e) {}

            parent.addView(imageView);
        }
    }

    @Override
    public void onBackPressed() {
        super.onBackPressed();
        overridePendingTransition(R.anim.slide_in_down, R.anim.slide_out_up);  // Custom slide animations for exiting
    }

}
