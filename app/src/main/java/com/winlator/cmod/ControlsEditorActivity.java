package com.winlator.cmod;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Bundle;
import android.content.Intent;
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
import com.winlator.cmod.inputcontrols.IconPackManager;
import com.winlator.cmod.inputcontrols.InputControlsManager;
import com.winlator.cmod.math.Mathf;
import com.winlator.cmod.core.AppUtils;
import com.winlator.cmod.core.FileUtils;
import com.winlator.cmod.core.UnitUtils;
import com.winlator.cmod.widget.InputControlsView;
import com.winlator.cmod.widget.NumberPicker;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public class ControlsEditorActivity extends AppCompatActivity implements View.OnClickListener {
    private InputControlsView inputControlsView;
    private ControlsProfile profile;
    private IconPackManager iconPackManager;

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

        iconPackManager = new IconPackManager(this);

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
            ControlElement.Shape shape = element.getShape();
            view.findViewById(R.id.LLShape).setVisibility(View.GONE);
            view.findViewById(R.id.CBToggleSwitch).setVisibility(View.GONE);
            view.findViewById(R.id.LLCustomTextIcon).setVisibility(View.GONE);
            view.findViewById(R.id.LLRangeOptions).setVisibility(View.GONE);
            view.findViewById(R.id.LLRectDimensions).setVisibility(View.GONE);
            view.findViewById(R.id.LLDPadOptions).setVisibility(View.GONE);

            if (type == ControlElement.Type.BUTTON) {
                view.findViewById(R.id.LLShape).setVisibility(View.VISIBLE);
                view.findViewById(R.id.CBToggleSwitch).setVisibility(View.VISIBLE);
                view.findViewById(R.id.LLCustomTextIcon).setVisibility(View.VISIBLE);
                if (shape == ControlElement.Shape.RECT) {
                    view.findViewById(R.id.LLRectDimensions).setVisibility(View.VISIBLE);
                }
            }
            else if (type == ControlElement.Type.RANGE_BUTTON) {
                view.findViewById(R.id.LLRangeOptions).setVisibility(View.VISIBLE);
            }
            else if (type == ControlElement.Type.D_PAD) {
                view.findViewById(R.id.LLDPadOptions).setVisibility(View.VISIBLE);
            }

            loadBindingSpinners(element, view);
        };

        loadTypeSpinner(element, view.findViewById(R.id.SType), updateLayout);
        loadShapeSpinner(element, view.findViewById(R.id.SShape), updateLayout);
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

        final TextView tvWidth = view.findViewById(R.id.TVWidth);
        SeekBar sbWidth = view.findViewById(R.id.SBWidth);
        sbWidth.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                tvWidth.setText(String.format("%.1f", progress / 10.0f));
                if (fromUser) {
                    element.setElementWidth(progress / 10.0f);
                    profile.save();
                    inputControlsView.invalidate();
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        sbWidth.setProgress((int)(element.getElementWidth() * 10));

        final TextView tvHeight = view.findViewById(R.id.TVHeight);
        SeekBar sbHeight = view.findViewById(R.id.SBHeight);
        sbHeight.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                tvHeight.setText(String.format("%.1f", progress / 10.0f));
                if (fromUser) {
                    element.setElementHeight(progress / 10.0f);
                    profile.save();
                    inputControlsView.invalidate();
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        sbHeight.setProgress((int)(element.getElementHeight() * 10));

        final TextView tvRadius = view.findViewById(R.id.TVRadius);
        SeekBar sbRadius = view.findViewById(R.id.SBRadius);
        sbRadius.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                tvRadius.setText(String.format("%.1f", progress / 10.0f));
                if (fromUser) {
                    element.setCornerRadius(progress / 10.0f);
                    profile.save();
                    inputControlsView.invalidate();
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        sbRadius.setProgress((int)(element.getCornerRadius() * 10));

        final TextView tvDPadRadius = view.findViewById(R.id.TVDPadRadius);
        SeekBar sbDPadRadius = view.findViewById(R.id.SBDPadRadius);
        sbDPadRadius.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                tvDPadRadius.setText(String.format("%.1f", progress / 10.0f));
                if (fromUser) {
                    element.setDpadCornerRadius(progress / 10.0f);
                    profile.save();
                    inputControlsView.invalidate();
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        sbDPadRadius.setProgress((int)(element.getDpadCornerRadius() * 10));

        CheckBox cbToggleSwitch = view.findViewById(R.id.CBToggleSwitch);
        cbToggleSwitch.setChecked(element.isToggleSwitch());
        cbToggleSwitch.setOnCheckedChangeListener((buttonView, isChecked) -> {
            element.setToggleSwitch(isChecked);
            profile.save();
        });

        final EditText etCustomText = view.findViewById(R.id.ETCustomText);
        etCustomText.setText(element.getText());
        final LinearLayout llIconList = view.findViewById(R.id.LLIconList);
        final String[] editingCustomIconData = {element.getCustomIconData()};
        loadIcons(llIconList, element.getIconId(), editingCustomIconData);

        updateLayout.run();

        PopupWindow popupWindow = AppUtils.showPopupWindow(anchorView, view, 340, 0);
        popupWindow.setOnDismissListener(() -> {
            String text = etCustomText.getText().toString().trim();
            byte iconId = 0;
            String customIconData = editingCustomIconData[0];
            for (int i = 0; i < llIconList.getChildCount(); i++) {
                View child = llIconList.getChildAt(i);
                if (child.isSelected()) {
                    Object tag = child.getTag();
                    if (tag instanceof Integer) {
                        iconId = (byte)((Integer)tag & 0xFF);
                    }
                    break;
                }
            }

            element.setText(text);
            element.setIconId(iconId);
            element.setCustomIconData(customIconData);
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

    private void loadShapeSpinner(final ControlElement element, Spinner spinner, Runnable callback) {
        final boolean[] init = {true};
        spinner.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, ControlElement.Shape.names()));
        spinner.setSelection(element.getShape().ordinal(), false);
        init[0] = false;
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                if (init[0]) return;
                element.setShape(ControlElement.Shape.values()[position]);
                profile.save();
                callback.run();
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
            loadLongPressBindingSection(element, container);
            loadGestureBindingSection(element, container);
            loadDoubleTapBindingSection(element, container);
        }
        else if (type == ControlElement.Type.D_PAD || type == ControlElement.Type.STICK || type == ControlElement.Type.TRACKPAD) {
            loadBindingSection(element, container, 0, R.string.binding_up);
            loadBindingSection(element, container, 1, R.string.binding_right);
            loadBindingSection(element, container, 2, R.string.binding_down);
            loadBindingSection(element, container, 3, R.string.binding_left);
        }
    }

    private void loadBindingSection(final ControlElement element, LinearLayout container, final int index, int titleResId) {
        List<Binding> seq = new ArrayList<>(element.getBindingSequence(index));
        View section = com.winlator.cmod.widget.BindingSequenceEditor.createView(this, getString(titleResId), 0, seq, () -> {
            element.setBindingSequence(index, seq);
            profile.save();
            inputControlsView.invalidate();
        });
        container.addView(section);
    }

    private void loadLongPressBindingSection(final ControlElement element, LinearLayout container) {
        List<Binding> seq = new ArrayList<>(element.getLongPressBindings());
        View section = com.winlator.cmod.widget.BindingSequenceEditor.createView(this, "Long Press", 0, seq, () -> {
            element.setLongPressBindings(seq);
            profile.save();
            inputControlsView.invalidate();
        });
        container.addView(section);
    }

    private void loadGestureBindingSection(final ControlElement element, LinearLayout container) {
        List<Binding> seq = new ArrayList<>(element.getGestureBindings());
        View section = com.winlator.cmod.widget.BindingSequenceEditor.createView(this, "Gesture", 0, seq, () -> {
            element.setGestureBindings(seq);
            profile.save();
            inputControlsView.invalidate();
        });
        container.addView(section);
    }

    private void loadDoubleTapBindingSection(final ControlElement element, LinearLayout container) {
        List<Binding> seq = new ArrayList<>(element.getDoubleTapBindings());
        View section = com.winlator.cmod.widget.BindingSequenceEditor.createView(this, "Double Tap", R.string.button_double_tap_help, seq, () -> {
            element.setDoubleTapBindings(seq);
            profile.save();
            inputControlsView.invalidate();
        });
        container.addView(section);
    }

    private void toggleLongPressModifier(ControlElement element, Binding modBinding, boolean add, Runnable populate) {
        List<Binding> seq = new ArrayList<>(element.getLongPressBindings());
        if (add) {
            seq.add(modBinding);
        } else {
            seq.remove(modBinding);
        }
        element.setLongPressBindings(seq);
        profile.save();
        inputControlsView.invalidate();
        populate.run();
    }

    private void toggleGestureModifier(ControlElement element, Binding modBinding, boolean add, Runnable populate) {
        List<Binding> seq = new ArrayList<>(element.getGestureBindings());
        if (add) {
            seq.add(modBinding);
        } else {
            seq.remove(modBinding);
        }
        element.setGestureBindings(seq);
        profile.save();
        inputControlsView.invalidate();
        populate.run();
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

    private void loadIcons(final LinearLayout parent, byte selectedId, final String[] customIconDataRef) {
        parent.removeAllViews();

        int size = (int)UnitUtils.dpToPx(40);
        int margin = (int)UnitUtils.dpToPx(2);
        int padding = (int)UnitUtils.dpToPx(4);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(size, size);
        params.setMargins(margin, 0, margin, 0);

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

        final boolean userHasCustomIcon = customIconDataRef[0] != null && !customIconDataRef[0].isEmpty();
        boolean selectedIsCustom = userHasCustomIcon;
        boolean builtinSelected = false;

        for (final byte id : iconIds) {
            ImageView imageView = new ImageView(this);
            imageView.setLayoutParams(params);
            imageView.setPadding(padding, padding, padding, padding);
            imageView.setBackgroundResource(R.drawable.icon_background);
            imageView.setTag((int)id);
            boolean isSelected = id == selectedId && !userHasCustomIcon;
            imageView.setSelected(isSelected);
            if (isSelected) builtinSelected = true;
            imageView.setOnClickListener((v) -> {
                for (int i = 0; i < parent.getChildCount(); i++) parent.getChildAt(i).setSelected(false);
                imageView.setSelected(true);
                customIconDataRef[0] = "";
            });

            try (InputStream is = getAssets().open("inputcontrols/icons/"+id+".png")) {
                imageView.setImageBitmap(BitmapFactory.decodeStream(is));
            }
            catch (IOException e) {}

            parent.addView(imageView);
        }

        ArrayList<IconPackManager.StoredIconPack> activePacks = iconPackManager.getActivePacks();
        for (final IconPackManager.StoredIconPack pack : activePacks) {
            for (final IconPackManager.PackIcon packIcon : pack.icons) {
                ImageView imageView = new ImageView(this);
                imageView.setLayoutParams(params);
                imageView.setPadding(padding, padding, padding, padding);
                imageView.setBackgroundResource(R.drawable.icon_background);
                imageView.setTag(null);
                imageView.setOnClickListener((v) -> {
                    for (int i = 0; i < parent.getChildCount(); i++) parent.getChildAt(i).setSelected(false);
                    imageView.setSelected(true);
                    byte[] bytes = packIcon.readBytes();
                    if (bytes != null) {
                        customIconDataRef[0] = android.util.Base64.encodeToString(bytes, android.util.Base64.NO_WRAP);
                    }
                });

                if (!builtinSelected && !selectedIsCustom) {
                    selectedIsCustom = false;
                }
                if (selectedIsCustom && userHasCustomIcon) {
                    byte[] bytes = packIcon.readBytes();
                    if (bytes != null) {
                        String packIconB64 = android.util.Base64.encodeToString(bytes, android.util.Base64.NO_WRAP);
                        if (packIconB64.equals(customIconDataRef[0])) {
                            imageView.setSelected(true);
                            selectedIsCustom = false;
                        }
                    }
                }

                byte[] bytes = packIcon.readBytes();
                if (bytes != null) {
                    imageView.setImageBitmap(BitmapFactory.decodeByteArray(bytes, 0, bytes.length));
                }
                else {
                    imageView.setImageResource(R.drawable.icon_image_picker);
                }

                parent.addView(imageView);
            }
        }
    }

    @Override
    public void onBackPressed() {
        super.onBackPressed();
        overridePendingTransition(R.anim.slide_in_down, R.anim.slide_out_up);  // Custom slide animations for exiting
    }

}
