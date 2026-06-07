package com.winlator.cmod;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Rect;
import android.os.Bundle;
import android.content.Intent;
import android.content.SharedPreferences;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.SeekBar;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.view.Gravity;
import android.widget.PopupWindow;
import android.widget.RadioGroup;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;
import androidx.preference.PreferenceManager;

import com.winlator.cmod.R;
import com.winlator.cmod.contentdialog.ContentDialog;

import com.winlator.cmod.inputcontrols.BindPackage;
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
        SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(this);
        inputControlsView.setOverlayOpacity(preferences.getFloat("overlay_opacity", InputControlsView.DEFAULT_OVERLAY_OPACITY));

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
        container.findViewById(R.id.BTProfileSettings).setOnClickListener(this);

        inputControlsView.setOnEditActionListener(() -> adjustToolbarPosition());

        inputControlsView.post(this::adjustToolbarPosition);
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
            case R.id.BTProfileSettings:
                showProfileSettings(v);
                break;
        }
    }

    private void adjustToolbarPosition() {
        View toolbar = findViewById(R.id.LLControlsToolbar);
        if (profile == null || toolbar == null || inputControlsView.getSnappingSize() == 0) return;

        int snappingSize = inputControlsView.getSnappingSize();
        int hPad = snappingSize * 3;
        int vPad = snappingSize * 2;
        int screenHeight = getResources().getDisplayMetrics().heightPixels;

        // Use getTop() — stable reference, never includes translationY (fixes oscillation)
        int defaultTop = toolbar.getTop();
        int th = toolbar.getHeight();
        int tw = toolbar.getWidth();
        int tl = toolbar.getLeft();
        int tr = tl + tw;

        // Greedy: start from default top, find first Y with no element overlap
        int testY = defaultTop;
        boolean conflict;
        int maxIter = 100;

        do {
            conflict = false;
            for (ControlElement element : profile.getElements()) {
                Rect box = element.getBoundingBox();
                if (box.right > tl - hPad && box.left < tr + hPad) {
                    if (box.top < testY + th + vPad && box.bottom > testY - vPad) {
                        testY = Math.max(testY, box.bottom + vPad);
                        conflict = true;
                        break;
                    }
                }
            }
        } while (conflict && --maxIter > 0);

        float newTranslationY = Math.max(0, testY - defaultTop);
        if (toolbar.getTranslationY() != newTranslationY) {
            toolbar.setTranslationY(newTranslationY);
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
            view.findViewById(R.id.CBPassthroughTouch).setVisibility(View.GONE);
            view.findViewById(R.id.CBAutoRepeat).setVisibility(View.GONE);
            view.findViewById(R.id.LLRepeatRate).setVisibility(View.GONE);
            view.findViewById(R.id.SBRepeatRate).setVisibility(View.GONE);
            view.findViewById(R.id.LLCustomTextIcon).setVisibility(View.GONE);
            view.findViewById(R.id.LLRangeOptions).setVisibility(View.GONE);
            view.findViewById(R.id.LLRectDimensions).setVisibility(View.GONE);
            view.findViewById(R.id.LLElementCornerRadius).setVisibility(View.GONE);
            view.findViewById(R.id.LLDPadOptions).setVisibility(View.GONE);

            if (type == ControlElement.Type.BUTTON) {
                view.findViewById(R.id.LLShape).setVisibility(View.VISIBLE);
                view.findViewById(R.id.CBToggleSwitch).setVisibility(View.VISIBLE);
                view.findViewById(R.id.CBPassthroughTouch).setVisibility(View.VISIBLE);
                view.findViewById(R.id.CBAutoRepeat).setVisibility(View.VISIBLE);
                if (element.isAutoRepeat()) {
                    view.findViewById(R.id.LLRepeatRate).setVisibility(View.VISIBLE);
                    view.findViewById(R.id.SBRepeatRate).setVisibility(View.VISIBLE);
                }
                view.findViewById(R.id.LLCustomTextIcon).setVisibility(View.VISIBLE);
                if (shape == ControlElement.Shape.RECT) {
                    view.findViewById(R.id.LLRectDimensions).setVisibility(View.VISIBLE);
                }
                view.findViewById(R.id.LLElementCornerRadius).setVisibility(View.VISIBLE);
            }
            else if (type == ControlElement.Type.RANGE_BUTTON) {
                view.findViewById(R.id.LLRangeOptions).setVisibility(View.VISIBLE);
                view.findViewById(R.id.LLElementCornerRadius).setVisibility(View.VISIBLE);
            }
            else if (type == ControlElement.Type.TRACKPAD) {
                view.findViewById(R.id.LLElementCornerRadius).setVisibility(View.VISIBLE);
            }
            else if (type == ControlElement.Type.D_PAD) {
                view.findViewById(R.id.LLElementCornerRadius).setVisibility(View.VISIBLE);
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
            inputControlsView.post(this::adjustToolbarPosition);
        });

        NumberPicker npColumns = view.findViewById(R.id.NPColumns);
        npColumns.setValue(element.getBindingCount());
        npColumns.setOnValueChangeListener((numberPicker, value) -> {
            element.setBindingCount(value);
            profile.save();
            inputControlsView.invalidate();
            inputControlsView.post(this::adjustToolbarPosition);
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
                    inputControlsView.post(ControlsEditorActivity.this::adjustToolbarPosition);
                }
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {}

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        sbScale.setProgress((int)(element.getScale() * 100));

        final TextView tvOpacity = view.findViewById(R.id.TVOpacity);
        SeekBar sbOpacity = view.findViewById(R.id.SBOpacity);
        final int profileOpacityProgress = Math.round(inputControlsView.getOverlayOpacity() * 100);
        float initOpacity = element.getOpacity();
        if (initOpacity < 0) {
            tvOpacity.setText(getString(R.string.default_) + " (" + profileOpacityProgress + "%)");
            sbOpacity.setProgress(profileOpacityProgress);
        }
        else {
            tvOpacity.setText(Math.round(initOpacity * 100) + "%");
            sbOpacity.setProgress(Math.round(initOpacity * 100));
        }
        sbOpacity.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if (fromUser) {
                    progress = (int)Mathf.roundTo(progress, 5);
                    seekBar.setProgress(progress);
                    if (progress == profileOpacityProgress) {
                        element.setOpacity(-1f);
                        tvOpacity.setText(getString(R.string.default_) + " (" + profileOpacityProgress + "%)");
                    } else {
                        element.setOpacity(progress / 100.0f);
                        tvOpacity.setText(progress + "%");
                    }
                    profile.save();
                    inputControlsView.invalidate();
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });

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
                    inputControlsView.post(ControlsEditorActivity.this::adjustToolbarPosition);
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
                    inputControlsView.post(ControlsEditorActivity.this::adjustToolbarPosition);
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        sbHeight.setProgress((int)(element.getElementHeight() * 10));

        final TextView tvRadius = view.findViewById(R.id.TVRadius);
        SeekBar sbRadius = view.findViewById(R.id.SBRadius);
        final int profileRadiusProgress = Math.round(profile.getCornerRadius() * 10);
        float initRadius = element.getCornerRadius();
        if (initRadius < 0) {
            tvRadius.setText(getString(R.string.default_) + " (" + String.format("%.1f", profileRadiusProgress / 10.0f) + ")");
            sbRadius.setProgress(profileRadiusProgress);
        } else {
            tvRadius.setText(String.format("%.1f", initRadius));
            sbRadius.setProgress(Math.round(initRadius * 10));
        }
        sbRadius.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if (fromUser) {
                    if (progress == profileRadiusProgress) {
                        element.setCornerRadius(-1f);
                        tvRadius.setText(getString(R.string.default_) + " (" + String.format("%.1f", profileRadiusProgress / 10.0f) + ")");
                    } else {
                        element.setCornerRadius(progress / 10.0f);
                        tvRadius.setText(String.format("%.1f", progress / 10.0f));
                    }
                    profile.save();
                    inputControlsView.invalidate();
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });

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

        CheckBox cbPassthrough = view.findViewById(R.id.CBPassthroughTouch);
        cbPassthrough.setChecked(element.isPassthroughTouch());
        cbPassthrough.setOnCheckedChangeListener((buttonView, isChecked) -> {
            element.setPassthroughTouch(isChecked);
            profile.save();
        });

        CheckBox cbAutoRepeat = view.findViewById(R.id.CBAutoRepeat);
        cbAutoRepeat.setChecked(element.isAutoRepeat());
        cbAutoRepeat.setOnCheckedChangeListener((buttonView, isChecked) -> {
            element.setAutoRepeat(isChecked);
            profile.save();
            updateLayout.run();
        });

        TextView tvRepeatRate = view.findViewById(R.id.ETRepeatRate);
        SeekBar sbRepeatRate = view.findViewById(R.id.SBRepeatRate);
        int progress = Math.round(element.getAutoRepeatIntervalMs() / 10.0f) - 1;
        sbRepeatRate.setProgress(progress);
        tvRepeatRate.setText(String.valueOf((progress + 1) * 10));
        sbRepeatRate.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                int value = (progress + 1) * 10;
                tvRepeatRate.setText(String.valueOf(value));
                if (fromUser) {
                    element.setAutoRepeatIntervalMs(value);
                    profile.save();
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });

        final EditText etCustomText = view.findViewById(R.id.ETCustomText);
        etCustomText.setText(element.getText());
        final LinearLayout llIconList = view.findViewById(R.id.LLIconList);
        final String[] editingCustomIconData = {element.getCustomIconData()};
        loadIcons(llIconList, element.getIconId(), editingCustomIconData);

        updateLayout.run();

        // --- Popup: gravitate toward right, stay connected to element ---
        PopupWindow popupWindow = new PopupWindow(this);
        popupWindow.setElevation(5.0f);
        int popupWidthPx = (int)UnitUtils.dpToPx(340);
        popupWindow.setWidth(popupWidthPx);
        popupWindow.setHeight(ViewGroup.LayoutParams.WRAP_CONTENT);
        popupWindow.setContentView(view);
        popupWindow.setFocusable(false);
        popupWindow.setOutsideTouchable(true);
        popupWindow.update();

        int widthMeasureSpec = View.MeasureSpec.makeMeasureSpec(popupWidthPx, View.MeasureSpec.AT_MOST);
        int heightMeasureSpec = View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED);
        view.measure(widthMeasureSpec, heightMeasureSpec);
        int popupHeightPx = view.getMeasuredHeight();

        int screenWidth = getResources().getDisplayMetrics().widthPixels;
        int screenHeight = getResources().getDisplayMetrics().heightPixels;
        int marginPx = (int)UnitUtils.dpToPx(8);

        // Element screen coordinates
        Rect box = element.getBoundingBox();
        int[] viewLocation = new int[2];
        inputControlsView.getLocationOnScreen(viewLocation);
        int elemLeft = viewLocation[0] + box.left;
        int elemTop = viewLocation[1] + box.top;
        int elemRight = viewLocation[0] + box.right;
        int elemBottom = viewLocation[1] + box.bottom;

        // --- Y: prefer top-aligned → below → above → clamped ---
        int popupY;
        if (elemTop + popupHeightPx <= screenHeight - marginPx) {
            popupY = elemTop;
        } else if (elemBottom + popupHeightPx <= screenHeight - marginPx) {
            popupY = elemBottom;
        } else if (elemTop - popupHeightPx >= marginPx) {
            popupY = elemTop - popupHeightPx;
        } else {
            popupY = Math.max(marginPx, screenHeight - popupHeightPx - marginPx);
        }

        // --- X: right-of-element (connected) → left-of-element (fallback) → right edge (last resort) ---
        int popupX;
        if (elemRight + marginPx + popupWidthPx <= screenWidth - marginPx) {
            popupX = elemRight + marginPx;
        } else if (elemLeft - marginPx - popupWidthPx >= marginPx) {
            popupX = elemLeft - marginPx - popupWidthPx;
        } else {
            popupX = Math.max(marginPx, screenWidth - marginPx - popupWidthPx);
        }

        // If the fallback right-edge position still overlaps, try vertical separation
        if (popupX < elemRight && popupX + popupWidthPx > elemLeft &&
            popupY < elemBottom && popupY + popupHeightPx > elemTop) {
            if (elemBottom + popupHeightPx <= screenHeight - marginPx) {
                popupY = elemBottom;
            } else if (elemTop - popupHeightPx >= marginPx) {
                popupY = elemTop - popupHeightPx;
            }
        }

        popupWindow.showAtLocation(inputControlsView, Gravity.LEFT | Gravity.TOP, popupX, popupY);
        popupWindow.setFocusable(true);
        popupWindow.update();
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
            inputControlsView.post(this::adjustToolbarPosition);
        });
    }

    private void showProfileSettings(View anchorView) {
        View view = LayoutInflater.from(this).inflate(R.layout.profile_settings_popup, null);

        // --- Overlay Opacity ---
        final TextView tvOpacity = view.findViewById(R.id.TVPopupOpacity);
        SeekBar sbOpacity = view.findViewById(R.id.SBPopupOpacity);
        sbOpacity.setProgress(Math.round(inputControlsView.getOverlayOpacity() * 100));
        tvOpacity.setText(sbOpacity.getProgress() + "%");
        sbOpacity.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                tvOpacity.setText(progress + "%");
                if (fromUser) {
                    progress = (int)Mathf.roundTo(progress, 5);
                    seekBar.setProgress(progress);
                    float value = progress / 100.0f;
                    inputControlsView.setOverlayOpacity(value);
                    PreferenceManager.getDefaultSharedPreferences(ControlsEditorActivity.this).edit().putFloat("overlay_opacity", value).apply();
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });

        // --- Stroke Width ---
        final TextView tvStrokeWidth = view.findViewById(R.id.TVPopupStrokeWidth);
        SeekBar sbStrokeWidth = view.findViewById(R.id.SBPopupStrokeWidth);
        float initSW = profile.getStrokeWidth();
        sbStrokeWidth.setProgress(Math.round((initSW - 0.05f) * 100));
        tvStrokeWidth.setText(String.format("%.2fx", initSW));
        sbStrokeWidth.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                float value = 0.05f + progress * 0.01f;
                tvStrokeWidth.setText(String.format("%.2fx", value));
                if (fromUser) {
                    inputControlsView.setProfileStrokeWidth(value);
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) { profile.save(); }
        });

        // --- Inactive Fill Alpha ---
        final TextView tvFillAlpha = view.findViewById(R.id.TVPopupFillAlpha);
        SeekBar sbFillAlpha = view.findViewById(R.id.SBPopupFillAlpha);
        sbFillAlpha.setProgress(profile.getFillAlphaInactive());
        tvFillAlpha.setText(String.valueOf(sbFillAlpha.getProgress()));
        sbFillAlpha.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                tvFillAlpha.setText(String.valueOf(progress));
                if (fromUser) {
                    inputControlsView.setProfileFillAlphaInactive(progress);
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) { profile.save(); }
        });

        // --- Corner Radius ---
        final TextView tvCornerRadius = view.findViewById(R.id.TVPopupCornerRadius);
        SeekBar sbCornerRadius = view.findViewById(R.id.SBPopupCornerRadius);
        float initCR = profile.getCornerRadius();
        sbCornerRadius.setProgress(Math.round(initCR * 10));
        tvCornerRadius.setText(String.format("%.1f", initCR));
        sbCornerRadius.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                float value = progress / 10.0f;
                tvCornerRadius.setText(String.format("%.1f", value));
                if (fromUser) {
                    inputControlsView.setProfileCornerRadius(value);
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) { profile.save(); }
        });

        // --- Popup ---
        PopupWindow popupWindow = new PopupWindow(this);
        popupWindow.setElevation(5.0f);
        int popupWidthPx = (int)UnitUtils.dpToPx(340);
        popupWindow.setWidth(popupWidthPx);
        popupWindow.setHeight(ViewGroup.LayoutParams.WRAP_CONTENT);
        popupWindow.setContentView(view);
        popupWindow.setFocusable(false);
        popupWindow.setOutsideTouchable(true);
        popupWindow.update();

        int widthMeasureSpec = View.MeasureSpec.makeMeasureSpec(popupWidthPx, View.MeasureSpec.AT_MOST);
        int heightMeasureSpec = View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED);
        view.measure(widthMeasureSpec, heightMeasureSpec);
        int popupHeightPx = view.getMeasuredHeight();

        int screenWidth = getResources().getDisplayMetrics().widthPixels;
        int screenHeight = getResources().getDisplayMetrics().heightPixels;
        int marginPx = (int)UnitUtils.dpToPx(8);

        // Position: centered below toolbar or above if not enough space
        int toolbarBottom = anchorView.getRootView().findViewById(R.id.LLControlsToolbar).getBottom();
        int popupY;
        if (toolbarBottom + popupHeightPx <= screenHeight - marginPx) {
            popupY = toolbarBottom + marginPx;
        } else if (toolbarBottom - marginPx - popupHeightPx >= marginPx) {
            popupY = toolbarBottom - marginPx - popupHeightPx;
        } else {
            popupY = marginPx;
        }
        int popupX = Math.max(marginPx, (screenWidth - popupWidthPx) / 2);

        popupWindow.showAtLocation(inputControlsView, Gravity.LEFT | Gravity.TOP, popupX, popupY);
        popupWindow.setFocusable(true);
        popupWindow.update();
        popupWindow.setOnDismissListener(() -> {
            profile.save();
            inputControlsView.invalidate();
            inputControlsView.post(this::adjustToolbarPosition);
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
                inputControlsView.post(ControlsEditorActivity.this::adjustToolbarPosition);
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
                inputControlsView.post(ControlsEditorActivity.this::adjustToolbarPosition);
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
            if (!element.isAutoRepeat())
                loadLongPressBindingSection(element, container);
            loadGestureBindingSection(element, container);
        }
        else if (type == ControlElement.Type.D_PAD || type == ControlElement.Type.STICK || type == ControlElement.Type.TRACKPAD) {
            loadBindingSection(element, container, 0, R.string.binding_up);
            loadBindingSection(element, container, 1, R.string.binding_right);
            loadBindingSection(element, container, 2, R.string.binding_down);
            loadBindingSection(element, container, 3, R.string.binding_left);
        }
    }

    private void loadBindingSection(final ControlElement element, LinearLayout container, final int index, int titleResId) {
        BindPackage bp = new BindPackage(element.getBindingSequence(index), element.getBindingSticky(index));
        View section = com.winlator.cmod.widget.BindingSequenceEditor.createView(this, getString(titleResId), 0, bp, () -> {
            element.setBindingSequence(index, new ArrayList<>(bp.getBindings()));
            List<Boolean> flags = bp.getStickyFlags();
            for (int i = 0; i < flags.size(); i++)
                element.setBindingSticky(index, i, flags.get(i));
            profile.save();
            inputControlsView.invalidate();
        });
        container.addView(section);
    }

    private void loadLongPressBindingSection(final ControlElement element, LinearLayout container) {
        BindPackage bp = new BindPackage(element.getLongPressBindings());
        View section = com.winlator.cmod.widget.BindingSequenceEditor.createView(this, "Long Press", 0, bp, () -> {
            element.setLongPressBindings(new ArrayList<>(bp.getBindings()));
            profile.save();
            inputControlsView.invalidate();
        });
        container.addView(section);
    }

    private void loadGestureBindingSection(final ControlElement element, LinearLayout container) {
        BindPackage bp = new BindPackage(element.getGestureBindings());
        View section = com.winlator.cmod.widget.BindingSequenceEditor.createView(this, "Gesture", 0, bp, () -> {
            element.setGestureBindings(new ArrayList<>(bp.getBindings()));
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
