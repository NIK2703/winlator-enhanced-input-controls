package com.winlator.cmod;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.content.res.TypedArray;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.RadioButton;

import android.widget.CheckBox;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;
import androidx.core.widget.ImageViewCompat;
import androidx.fragment.app.Fragment;
import androidx.preference.PreferenceManager;

import com.winlator.cmod.R;
import com.winlator.cmod.contentdialog.TouchpadGestureSettingsDialog;
import com.winlator.cmod.contentdialog.TouchscreenGestureSettingsDialog;
import com.winlator.cmod.core.AppUtils;
import com.winlator.cmod.core.Callback;
import com.winlator.cmod.core.FileUtils;
import com.winlator.cmod.core.HttpUtils;
import com.winlator.cmod.inputcontrols.ControlElement;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.inputcontrols.ExternalController;
import com.winlator.cmod.inputcontrols.InputControlsManager;
import com.winlator.cmod.inputcontrols.MouseMode;
import com.winlator.cmod.inputcontrols.TouchActivationMode;
import com.winlator.cmod.math.Mathf;
import com.winlator.cmod.contentdialog.ContentDialog;
import com.winlator.cmod.widget.InputControlsView;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.BiConsumer;
import java.util.function.Function;

public class InputControlsFragment extends Fragment {
    private static final String INPUT_CONTROLS_URL = "https://raw.githubusercontent.com/brunodev85/winlator/main/input_controls/%s";
    private InputControlsManager manager;
    private ControlsProfile currentProfile;
    private Runnable updateLayout;
    private Callback<ControlsProfile> importProfileCallback;
    private final int selectedProfileId;
    private SharedPreferences preferences;
    private Spinner spMouseMode;
    private SeekBar sbBindingDelay;
    private TextView tvBindingDelay;
    private SeekBar sbLongPressDelay;
    private TextView tvLongPressDelay;
    private Spinner spButtonLongPressHaptic;
    private Spinner spButtonGestureHaptic;
    private Spinner spGestureLongPressHaptic;
    private SeekBar sbGestureThreshold;
    private TextView tvGestureThreshold;
    private SeekBar sbDoubleTapDistance;
    private TextView tvDoubleTapDistance;
    private SeekBar sbStrokeWidth;
    private TextView tvStrokeWidth;
    private SeekBar sbFillAlphaInactive;
    private TextView tvFillAlphaInactive;

    private int[] keycodes;



    private boolean isDarkMode;
    private Runnable updateStarIcon;

    public InputControlsFragment(int selectedProfileId) {
        this.selectedProfileId = selectedProfileId;
    }



    @Override
    public void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setHasOptionsMenu(false);
        manager = new InputControlsManager(getContext());

        preferences = PreferenceManager.getDefaultSharedPreferences(getContext());
        isDarkMode = preferences.getBoolean("dark_mode", true);
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);
        ((AppCompatActivity)getActivity()).getSupportActionBar().setTitle(R.string.input_controls);
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        if (requestCode == MainActivity.OPEN_FILE_REQUEST_CODE && resultCode == Activity.RESULT_OK) {
            try {
                ControlsProfile importedProfile = manager.importProfile(new JSONObject(FileUtils.readString(getContext(), data.getData())));
                if (importProfileCallback != null) importProfileCallback.call(importedProfile);
            }
            catch (Exception e) {
                AppUtils.showToast(getContext(), R.string.unable_to_import_profile);
            }
            importProfileCallback = null;
        }
    }

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container, @Nullable Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.input_controls_fragment, container, false);
        final Context context = getContext();

        currentProfile = selectedProfileId > 0 ? manager.getProfile(selectedProfileId) : null;

        final Spinner sProfile = view.findViewById(R.id.SProfile);

        sProfile.setPopupBackgroundResource(isDarkMode ? R.drawable.content_dialog_background_dark : R.drawable.content_dialog_background);

        ImageButton btStarDefault = view.findViewById(R.id.BTStarDefault);

        updateStarIcon = () -> {
            if (currentProfile != null) {
                int defaultId = preferences.getInt("default_profile_id", -1);
                boolean isDefault = currentProfile.id == defaultId;
                btStarDefault.setImageResource(isDefault ? R.drawable.icon_star_filled : R.drawable.icon_star_outline);
                btStarDefault.clearColorFilter();
                btStarDefault.setAlpha(1.0f);
            }
            else {
                btStarDefault.setImageResource(R.drawable.icon_star_outline);
                btStarDefault.setAlpha(0.3f);
            }
        };

        btStarDefault.setOnClickListener((v) -> {
            if (currentProfile == null) return;

            int defaultId = preferences.getInt("default_profile_id", -1);
            if (currentProfile.id == defaultId) {
                preferences.edit().putInt("default_profile_id", -1).apply();
            }
            else {
                preferences.edit().putInt("default_profile_id", currentProfile.id).apply();
            }
            updateStarIcon.run();
        });

        loadProfileSpinner(sProfile);

        updateLayout = () -> {
            loadExternalControllers(view);
        };

        updateLayout.run();

        final TextView tvUiOpacity = view.findViewById(R.id.TVUiOpacity);
        SeekBar sbUiOpacity = view.findViewById(R.id.SBOverlayOpacity);
        sbUiOpacity.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                tvUiOpacity.setText(progress+"%");
                if (fromUser) {
                    progress = (int)Mathf.roundTo(progress, 5);
                    seekBar.setProgress(progress);
                    preferences.edit().putFloat("overlay_opacity", progress / 100.0f).apply();
                }
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {}

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        sbUiOpacity.setProgress((int)(preferences.getFloat("overlay_opacity", InputControlsView.DEFAULT_OVERLAY_OPACITY) * 100));

        tvBindingDelay = view.findViewById(R.id.TVBindingDelay);
        sbBindingDelay = view.findViewById(R.id.SBBindingDelay);
        view.findViewById(R.id.BTHelpBindingDelay).setOnClickListener((v) ->
                AppUtils.showHelpBox(context, v, R.string.binding_delay_help));
        sbBindingDelay.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                tvBindingDelay.setText(progress + " ms");
                if (fromUser && currentProfile != null) {
                    currentProfile.setBindingDelay(progress);
                    currentProfile.save();
                }
            }

            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        if (currentProfile != null) {
            sbBindingDelay.setProgress(currentProfile.getBindingDelay());
        }

        tvLongPressDelay = view.findViewById(R.id.TVLongPressDelay);
        sbLongPressDelay = view.findViewById(R.id.SBLongPressDelay);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            sbLongPressDelay.setMin(50);
        }
        sbLongPressDelay.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                int stepVal = Math.round(progress / 10.0f) * 10;
                if (fromUser && stepVal != progress) seekBar.setProgress(stepVal);
                tvLongPressDelay.setText(stepVal + " ms");
                if (fromUser && currentProfile != null) {
                    currentProfile.setLongPressDelay(stepVal);
                    currentProfile.save();
                }
            }

            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        sbLongPressDelay.post(() -> {
            int delay = currentProfile != null ? currentProfile.getLongPressDelay() : 50;
            sbLongPressDelay.setProgress(delay);
            tvLongPressDelay.setText(delay + " ms");
        });

        spButtonLongPressHaptic = setupHapticSpinner(view, R.id.SPButtonLongPressHaptic,
            profile -> profile.getButtonLongPressHaptic(),
            (profile, value) -> { profile.setButtonLongPressHaptic(value); profile.save(); });

        spButtonGestureHaptic = setupHapticSpinner(view, R.id.SPButtonGestureHaptic,
            profile -> profile.getButtonGestureHaptic(),
            (profile, value) -> { profile.setButtonGestureHaptic(value); profile.save(); });

        spGestureLongPressHaptic = setupHapticSpinner(view, R.id.SPGestureLongPressHaptic,
            profile -> profile.getGestureLongPressHaptic(),
            (profile, value) -> { profile.setGestureLongPressHaptic(value); profile.save(); });

        tvGestureThreshold = view.findViewById(R.id.TVGestureThreshold);
        sbGestureThreshold = view.findViewById(R.id.SBGestureThreshold);
        sbGestureThreshold.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                int value = progress + 10;
                tvGestureThreshold.setText(value + " px");
                if (fromUser && currentProfile != null) {
                    currentProfile.setGestureThreshold(value);
                    currentProfile.save();
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        sbGestureThreshold.post(() -> {
            int threshold = currentProfile != null ? currentProfile.getGestureThreshold() : 20;
            sbGestureThreshold.setProgress(threshold - 10);
            tvGestureThreshold.setText(threshold + " px");
        });

        tvDoubleTapDistance = view.findViewById(R.id.TVDoubleTapDistance);
        sbDoubleTapDistance = view.findViewById(R.id.SBDoubleTapDistance);
        sbDoubleTapDistance.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                int value = progress + 10;
                tvDoubleTapDistance.setText(value + " px");
                if (fromUser && currentProfile != null) {
                    currentProfile.setDoubleTapDistance(value);
                    currentProfile.save();
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        sbDoubleTapDistance.post(() -> {
            int distance = currentProfile != null ? currentProfile.getDoubleTapDistance() : 50;
            sbDoubleTapDistance.setProgress(distance - 10);
            tvDoubleTapDistance.setText(distance + " px");
        });

        tvStrokeWidth = view.findViewById(R.id.TVStrokeWidth);
        sbStrokeWidth = view.findViewById(R.id.SBStrokeWidth);
        sbStrokeWidth.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                float value = 0.05f + progress * 0.01f;
                tvStrokeWidth.setText(String.format("%.2fx", value));
                if (fromUser && currentProfile != null) {
                    currentProfile.setStrokeWidth(value);
                    currentProfile.save();
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        sbStrokeWidth.post(() -> {
            float sw = currentProfile != null ? currentProfile.getStrokeWidth() : 0.2f;
            sbStrokeWidth.setProgress(Math.round((sw - 0.05f) * 100));
            tvStrokeWidth.setText(String.format("%.2fx", sw));
        });

        tvFillAlphaInactive = view.findViewById(R.id.TVFillAlphaInactive);
        sbFillAlphaInactive = view.findViewById(R.id.SBFillAlphaInactive);
        sbFillAlphaInactive.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                tvFillAlphaInactive.setText(String.valueOf(progress));
                if (fromUser && currentProfile != null) {
                    currentProfile.setFillAlphaInactive(progress);
                    currentProfile.save();
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        sbFillAlphaInactive.post(() -> {
            int fa = currentProfile != null ? currentProfile.getFillAlphaInactive() : 50;
            sbFillAlphaInactive.setProgress(fa);
            tvFillAlphaInactive.setText(String.valueOf(fa));
        });

        spMouseMode = view.findViewById(R.id.SPMouseMode);
        setupEnumSpinner(spMouseMode, MouseMode.values(), currentProfile != null ? currentProfile.getMouseMode() : MouseMode.TOUCHPAD);
        spMouseMode.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View v, int pos, long id) {
                if (currentProfile != null) {
                    MouseMode newMode = (MouseMode) spMouseMode.getSelectedItem();
                    if (newMode != currentProfile.getMouseMode()) {
                        currentProfile.setMouseMode(newMode);
                        currentProfile.save();
                    }
                }
            }
            @Override public void onNothingSelected(AdapterView<?> parent) {}
        });

        Spinner spTouchActivation = view.findViewById(R.id.SPTouchActivation);
        setupEnumSpinner(spTouchActivation, TouchActivationMode.values(), currentProfile != null ? currentProfile.getTouchActivationMode() : TouchActivationMode.LOCK);
        spTouchActivation.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View v, int pos, long id) {
                if (currentProfile != null) {
                    TouchActivationMode newMode = (TouchActivationMode) spTouchActivation.getSelectedItem();
                    if (newMode != currentProfile.getTouchActivationMode()) {
                        currentProfile.setTouchActivationMode(newMode);
                        currentProfile.save();
                    }
                }
            }
            @Override public void onNothingSelected(AdapterView<?> parent) {}
        });

        view.findViewById(R.id.BTAddProfile).setOnClickListener((v) -> ContentDialog.prompt(context, R.string.profile_name, null, (name) -> {
            currentProfile = manager.createProfile(name);
            loadProfileSpinner(sProfile);
            updateLayout.run();
        }));

        view.findViewById(R.id.BTEditProfile).setOnClickListener((v) -> {
            if (currentProfile != null) {
                ContentDialog.prompt(context, R.string.profile_name, currentProfile.getName(), (name) -> {
                    currentProfile.setName(name);
                    currentProfile.save();
                    loadProfileSpinner(sProfile);
                });
            }
            else AppUtils.showToast(context, R.string.no_profile_selected);
        });

        view.findViewById(R.id.BTDuplicateProfile).setOnClickListener((v) -> {
            if (currentProfile != null) {
                ContentDialog.confirm(context, R.string.do_you_want_to_duplicate_this_profile, () -> {
                    currentProfile = manager.duplicateProfile(currentProfile);
                    loadProfileSpinner(sProfile);
                    updateLayout.run();
                });
            }
            else AppUtils.showToast(context, R.string.no_profile_selected);
        });

        view.findViewById(R.id.BTRemoveProfile).setOnClickListener((v) -> {
            if (currentProfile != null) {
                if (currentProfile.getName().equals("Default")) {
                    AppUtils.showToast(context, R.string.cannot_remove_default_profile);
                    return;
                }
                ContentDialog.confirm(context, R.string.do_you_want_to_remove_this_profile, () -> {
                    manager.removeProfile(currentProfile);
                    currentProfile = null;
                    loadProfileSpinner(sProfile);
                    updateLayout.run();
                });
            }
            else AppUtils.showToast(context, R.string.no_profile_selected);
        });

        view.findViewById(R.id.BTImportProfile).setOnClickListener((v) -> {
            android.widget.PopupMenu popupMenu = new PopupMenu(context, v);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) popupMenu.setForceShowIcon(true);
            popupMenu.inflate(R.menu.open_file_popup_menu);
            int tint = getResources().getColor(R.color.colorAccent, context.getTheme());
            if (popupMenu.getMenu().findItem(R.id.open_file) != null && popupMenu.getMenu().findItem(R.id.open_file).getIcon() != null) {
                popupMenu.getMenu().findItem(R.id.open_file).getIcon().setTint(tint);
            }
            if (popupMenu.getMenu().findItem(R.id.download_file) != null && popupMenu.getMenu().findItem(R.id.download_file).getIcon() != null) {
                popupMenu.getMenu().findItem(R.id.download_file).getIcon().setTint(tint);
            }
            popupMenu.setOnMenuItemClickListener((menuItem) -> {
                int itemId = menuItem.getItemId();
                if (itemId == R.id.open_file) {
                    openProfileFile(sProfile);
                }
                else if (itemId == R.id.download_file) {
                    downloadProfileList(sProfile);
                }
                return true;
            });
            popupMenu.show();
        });

        view.findViewById(R.id.BTExportProfile).setOnClickListener((v) -> {
            if (currentProfile != null) {
                File exportedFile = manager.exportProfile(currentProfile);
                if (exportedFile != null) {
                    String path = exportedFile.getPath();
                    AppUtils.showToast(context, context.getString(R.string.profile_exported_to)+" "+path);
                }
            }
            else AppUtils.showToast(context, R.string.no_profile_selected);
        });

        view.findViewById(R.id.BTControlsEditor).setOnClickListener((v) -> {
            if (currentProfile != null) {
                Intent intent = new Intent(context, ControlsEditorActivity.class);
                intent.putExtra("profile_id", currentProfile.id);
                startActivity(intent);
                getActivity().overridePendingTransition(R.anim.slide_in_up, R.anim.slide_out_down);
            } else {
                AppUtils.showToast(context, R.string.no_profile_selected);
            }
        });

        view.findViewById(R.id.BTGestureSettings).setOnClickListener((v) -> {
            if (currentProfile != null) {
                TouchscreenGestureSettingsDialog dialog = new TouchscreenGestureSettingsDialog(getContext(), currentProfile);
                dialog.setOnSaveListener((savedProfile) -> {
                    loadProfileSpinner(view.findViewById(R.id.SProfile));
                    XServerDisplayActivity.updateGestureConfig();
                    AppUtils.showToast(getContext(), "Gesture settings saved");
                });
                dialog.show();
            } else {
                AppUtils.showToast(context, R.string.no_profile_selected);
            }
        });

        view.findViewById(R.id.BTTouchpadGestureSettings).setOnClickListener((v) -> {
            if (currentProfile != null) {
                TouchpadGestureSettingsDialog dialog = new TouchpadGestureSettingsDialog(getContext(), currentProfile);
                dialog.setOnSaveListener((savedProfile) -> {
                    loadProfileSpinner(view.findViewById(R.id.SProfile));
                    XServerDisplayActivity.updateGestureConfig();
                    AppUtils.showToast(getContext(), "Touchpad gesture settings saved");
                });
                dialog.show();
            } else {
                AppUtils.showToast(context, R.string.no_profile_selected);
            }
        });

        view.findViewById(R.id.BTIconPackManager).setOnClickListener((v) -> {
            startActivity(new Intent(context, IconPackManagerActivity.class));
        });

        return view;
    }

    private void openProfileFile(Spinner sProfile) {
        importProfileCallback = (importedProfile) -> {
            currentProfile = importedProfile;
            loadProfileSpinner(sProfile);
            updateLayout.run();
        };
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        getActivity().startActivityFromFragment(this, intent, MainActivity.OPEN_FILE_REQUEST_CODE);
    }

    private void downloadSelectedProfiles(final Spinner sProfile, String[] items, final ArrayList<Integer> positions) {
        final MainActivity activity = (MainActivity)getActivity();
        activity.preloaderDialog.show(R.string.downloading_file);
        currentProfile = null;
        final AtomicInteger processedItemCount = new AtomicInteger();

        for (int position : positions) {
            HttpUtils.download(String.format(INPUT_CONTROLS_URL, items[position]), (content) -> {
                try {
                    if (content != null) manager.importProfile(new JSONObject(content));
                }
                catch (JSONException e) {}
                if (processedItemCount.incrementAndGet() == positions.size()) {
                    activity.runOnUiThread(() -> {
                        activity.preloaderDialog.close();
                        loadProfileSpinner(sProfile);
                        updateLayout.run();
                    });
                }
            });
        }
    }

    private void downloadProfileList(final Spinner sProfile) {
        final MainActivity activity = (MainActivity)getActivity();
        activity.preloaderDialog.show(R.string.loading);
        HttpUtils.download(String.format(INPUT_CONTROLS_URL, "index.txt"), (content) -> activity.runOnUiThread(() -> {
            activity.preloaderDialog.close();
            if (content != null) {
                final String[] items = content.split("\n");
                ContentDialog.showMultipleChoiceList(activity, R.string.import_profile, items, (positions) -> {
                    if (!positions.isEmpty()) {
                        ContentDialog.confirm(activity, R.string.do_you_want_to_download_the_selected_profiles, () -> downloadSelectedProfiles(sProfile, items, positions));
                    }
                });
            }
            else AppUtils.showToast(activity, R.string.unable_to_load_profile_list);
        }));
    }

    @Override
    public void onStart() {
        super.onStart();
        if (updateLayout != null) updateLayout.run();
    }

    private void loadProfileSpinner(Spinner spinner) {
        final ArrayList<ControlsProfile> profiles = manager.getProfiles();
        ArrayList<String> values = new ArrayList<>();
        values.add("-- "+getString(R.string.select_profile)+" --");

        int selectedPosition = 0;
        for (int i = 0; i < profiles.size(); i++) {
            ControlsProfile profile = profiles.get(i);
            if (profile == currentProfile) selectedPosition = i + 1;
            values.add(profile.getName());
        }

        spinner.setAdapter(new ArrayAdapter<>(getContext(), android.R.layout.simple_spinner_dropdown_item, values));
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                currentProfile = position > 0 ? profiles.get(position - 1) : null;
                updateLayout.run();
                if (currentProfile != null) {
                    AppUtils.setSpinnerSelectionFromValue(spMouseMode, currentProfile.getMouseMode().name());
                    sbBindingDelay.setProgress(currentProfile.getBindingDelay());
                    tvBindingDelay.setText(currentProfile.getBindingDelay() + " ms");
                    int lpDelay = currentProfile.getLongPressDelay();
                    sbLongPressDelay.setProgress(lpDelay);
                    tvLongPressDelay.setText(lpDelay + " ms");
                    spButtonLongPressHaptic.setSelection(currentProfile.getButtonLongPressHaptic());
                    spButtonGestureHaptic.setSelection(currentProfile.getButtonGestureHaptic());
                    spGestureLongPressHaptic.setSelection(currentProfile.getGestureLongPressHaptic());
                    int gThreshold = currentProfile.getGestureThreshold();
                    sbGestureThreshold.setProgress(gThreshold - 10);
                    tvGestureThreshold.setText(gThreshold + " px");
                    int ddDistance = currentProfile.getDoubleTapDistance();
                    sbDoubleTapDistance.setProgress(ddDistance - 10);
                    tvDoubleTapDistance.setText(ddDistance + " px");
                    float sw = currentProfile.getStrokeWidth();
                    sbStrokeWidth.setProgress(Math.round((sw - 0.05f) * 100));
                    tvStrokeWidth.setText(String.format("%.2fx", sw));
                    int fina = currentProfile.getFillAlphaInactive();
                    sbFillAlphaInactive.setProgress(fina);
                    tvFillAlphaInactive.setText(String.valueOf(fina));
                }
                updateStarIcon.run();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
                updateStarIcon.run();
            }
        });
        spinner.setSelection(selectedPosition, false);
    }

    private void loadExternalControllers(final View view) {
        LinearLayout container = view.findViewById(R.id.LLExternalControllers);
        container.removeAllViews();
        Context context = getContext();
        LayoutInflater inflater = LayoutInflater.from(context);
        ArrayList<ExternalController> connectedControllers = ExternalController.getControllers();

        ArrayList<ExternalController> controllers = currentProfile != null ? currentProfile.loadControllers() : new ArrayList<>();
        for (ExternalController controller : connectedControllers) {
            if (!controllers.contains(controller)) controllers.add(controller);
        }

        if (!controllers.isEmpty()) {
            view.findViewById(R.id.TVEmptyText).setVisibility(View.GONE);
            String bindingsText = context.getString(R.string.bindings);
            for (final ExternalController controller : controllers) {
                View itemView = inflater.inflate(R.layout.external_controller_list_item, container, false);
                ((TextView)itemView.findViewById(R.id.TVTitle)).setText(controller.getName());

                int controllerBindingCount = controller.getControllerBindingCount();
                ((TextView)itemView.findViewById(R.id.TVSubtitle)).setText(controllerBindingCount+" "+bindingsText);

                ImageView imageView = itemView.findViewById(R.id.ImageView);
                int tintColor = controller.isConnected() ? ContextCompat.getColor(context, R.color.colorAccent) : 0xffe57373;
                ImageViewCompat.setImageTintList(imageView, ColorStateList.valueOf(tintColor));

                if (controllerBindingCount > 0) {
                    ImageButton removeButton = itemView.findViewById(R.id.BTRemove);
                    removeButton.setVisibility(View.VISIBLE);
                    removeButton.setOnClickListener((v) -> ContentDialog.confirm(getContext(), R.string.do_you_want_to_remove_this_controller, () -> {
                        currentProfile.removeController(controller);
                        currentProfile.save();
                        loadExternalControllers(view);
                    }));
                }

                itemView.setOnClickListener((v) -> {
                    if (currentProfile != null) {
                        Intent intent = new Intent(getContext(), ExternalControllerBindingsActivity.class);
                        intent.putExtra("profile_id", currentProfile.id);
                        intent.putExtra("controller_id", controller.getId());
                        startActivity(intent);
                        getActivity().overridePendingTransition(R.anim.slide_in_up, R.anim.slide_out_down);  // Custom slide animations
                    }
                    else AppUtils.showToast(getContext(), R.string.no_profile_selected);
                });

                container.addView(itemView);
            }
        }
        else view.findViewById(R.id.TVEmptyText).setVisibility(View.VISIBLE);
    }

    private Spinner setupHapticSpinner(View view, int id, Function<ControlsProfile, Integer> getter, BiConsumer<ControlsProfile, Integer> setter) {
        String[] labels = new String[com.winlator.cmod.core.HapticUtils.getTypeCount()];
        for (int i = 0; i < labels.length; i++) {
            labels[i] = com.winlator.cmod.core.HapticUtils.getName(com.winlator.cmod.core.HapticUtils.getType(i));
        }
        Spinner spinner = view.findViewById(id);
        ArrayAdapter<String> adapter = new ArrayAdapter<>(getContext(), android.R.layout.simple_spinner_dropdown_item, labels);
        spinner.setAdapter(adapter);
        Context ctx = getContext();
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View v, int position, long id) {
                com.winlator.cmod.core.HapticUtils.perform(ctx, com.winlator.cmod.core.HapticUtils.getType(position));
                if (currentProfile != null) {
                    setter.accept(currentProfile, position);
                }
            }
            @Override public void onNothingSelected(AdapterView<?> parent) {}
        });
        if (currentProfile != null) {
            spinner.post(() -> spinner.setSelection(getter.apply(currentProfile), false));
        }
        return spinner;
    }

    private <T extends Enum<T>> void setupEnumSpinner(Spinner spinner, T[] values, T current) {
        int selectedIndex = 0;
        for (int i = 0; i < values.length; i++) {
            if (values[i] == current) selectedIndex = i;
        }
        spinner.setAdapter(new ArrayAdapter<>(getContext(), android.R.layout.simple_spinner_dropdown_item, values));
        spinner.setSelection(selectedIndex, false);
    }
}
