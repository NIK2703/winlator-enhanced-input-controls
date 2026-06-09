package com.winlator.cmod.contentdialog;

import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.view.Display;
import android.view.View;
import android.view.WindowManager;
import android.widget.ArrayAdapter;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;

import com.winlator.cmod.R;
import com.winlator.cmod.core.AppUtils;

public class RendererOptionsDialog extends ContentDialog {

    private final boolean isNativeMode;

    private void setGroupVisibility(int id, int vis) {
        View v = findViewById(id);
        if (v != null) v.setVisibility(vis);
    }

    public interface Config {
        boolean getRendererNative();
        void setRendererNative(boolean v);

        String getRendererPresentMode();
        void setRendererPresentMode(String v);

        String getRendererDriverId();
        void setRendererDriverId(String v);

        int getRendererFilterMode();
        void setRendererFilterMode(int v);

        int getRendererRefreshRateLimit();
        void setRendererRefreshRateLimit(int v);

        boolean getRendererSwapRB();
        void setRendererSwapRB(boolean v);

        boolean getGridRendering();
        void setGridRendering(boolean v);
        float getGridDarken();
        void setGridDarken(float v);
        int getGridCycleInterval();
        void setGridCycleInterval(int v);
        String getScreenSize();
    }

    private static final String[] PRESENT_MODE_IDS    = {"fifo", "mailbox"};
    private static final String[] PRESENT_MODE_LABELS = {
        "Fifo",
        "Mailbox"
    };

    private static final String[] FILTER_LABELS = {
        "Bilinear",
        "Nearest neighbor"
    };
    private static final int[] REFRESH_RATE_VALUES = {60, 0};
    private static final String[] REFRESH_RATE_LABELS = {"60 Hz", "Device Refresh Rate"};

    public RendererOptionsDialog(View anchorView, Config config, boolean isNativeMode) {
        super(anchorView.getContext(), R.layout.renderer_options_dialog);
        this.isNativeMode = isNativeMode;
        setTitle("Renderer Options");
        setIcon(R.drawable.icon_monitor);

        Context ctx = anchorView.getContext();

        Spinner  spPresent = findViewById(R.id.SPRendererPresentMode);
        Spinner  spFilter  = findViewById(R.id.SPRendererFilter);
        Spinner  spRefresh = findViewById(R.id.SPRendererRefreshRate);
        CheckBox cbSwapRB  = findViewById(R.id.CBRendererSwapRB);
        CheckBox cbGridRendering = findViewById(R.id.CBGridRendering);
        LinearLayout groupGridDarken = findViewById(R.id.GroupGridDarken);
        SeekBar sbGridDarken = findViewById(R.id.SBGridDarken);
        TextView tvGridDarken = findViewById(R.id.TVGridDarken);
        SeekBar sbGridCycleInterval = findViewById(R.id.SBGridCycleInterval);
        TextView tvGridCycleInterval = findViewById(R.id.TVGridCycleInterval);

        cbGridRendering.setChecked(config.getGridRendering());
        updateGridPreviewText(cbGridRendering, config);
        int darkenProgress = Math.round(config.getGridDarken() * 100);
        sbGridDarken.setProgress(darkenProgress);
        tvGridDarken.setText(darkenProgress + "%");
        boolean oled = isOledDisplay(ctx);
        int cycleInterval = config.getGridCycleInterval();
        if (cycleInterval < 0) cycleInterval = oled ? 240 : 0;
        sbGridCycleInterval.setProgress(cycleInterval);
        tvGridCycleInterval.setText(cycleInterval > 0 ? cycleInterval + "s" : "Off");
        groupGridDarken.setVisibility(cbGridRendering.isChecked() ? View.VISIBLE : View.GONE);

        cbGridRendering.setOnCheckedChangeListener((buttonView, isChecked) -> {
            groupGridDarken.setVisibility(isChecked ? View.VISIBLE : View.GONE);
        });

        sbGridDarken.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                tvGridDarken.setText(progress + "%");
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });

        sbGridCycleInterval.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                tvGridCycleInterval.setText(progress > 0 ? progress + "s" : "Off");
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });

        setGroupVisibility(R.id.GroupDriver,  View.GONE);
        setGroupVisibility(R.id.GroupFilter,  View.VISIBLE);

        spPresent.setAdapter(new ArrayAdapter<>(ctx,
            android.R.layout.simple_spinner_dropdown_item, PRESENT_MODE_LABELS));
        int pmSel = 0;
        String curPm = config.getRendererPresentMode();
        for (int i = 0; i < PRESENT_MODE_IDS.length; i++) {
            if (PRESENT_MODE_IDS[i].equals(curPm)) { pmSel = i; break; }
        }
        spPresent.setSelection(pmSel);

        String forcedDriverId = "";

        spFilter.setAdapter(new ArrayAdapter<>(ctx,
            android.R.layout.simple_spinner_dropdown_item, FILTER_LABELS));
        int filterSel = config.getRendererFilterMode();
        if (filterSel < 0 || filterSel >= FILTER_LABELS.length) filterSel = 0;
        spFilter.setSelection(filterSel);

        spRefresh.setAdapter(new ArrayAdapter<>(ctx,
            android.R.layout.simple_spinner_dropdown_item, REFRESH_RATE_LABELS));
        int rrSel = 0;
        int currentRefresh = config.getRendererRefreshRateLimit();
        for (int i = 0; i < REFRESH_RATE_VALUES.length; i++) {
            if (REFRESH_RATE_VALUES[i] == currentRefresh) { rrSel = i; break; }
        }
        spRefresh.setSelection(rrSel);
        cbSwapRB.setChecked(config.getRendererSwapRB());

        setOnConfirmCallback(() -> {
            config.setRendererPresentMode(PRESENT_MODE_IDS[spPresent.getSelectedItemPosition()]);
            config.setRendererDriverId(forcedDriverId);
            config.setRendererFilterMode(spFilter.getSelectedItemPosition());
            config.setRendererRefreshRateLimit(REFRESH_RATE_VALUES[spRefresh.getSelectedItemPosition()]);
            config.setRendererSwapRB(cbSwapRB.isChecked());
            config.setGridRendering(cbGridRendering.isChecked());
            config.setGridDarken(sbGridDarken.getProgress() / 100.0f);
            config.setGridCycleInterval(sbGridCycleInterval.getProgress());
        });
    }

    private static String computeGridResolution(String screenSize) {
        try {
            String[] parts = screenSize.split("x");
            if (parts.length != 2) return null;
            int origW = Integer.parseInt(parts[0]);
            int origH = Integer.parseInt(parts[1]);
            int deviceWidth = AppUtils.getScreenWidth();
            int deviceHeight = AppUtils.getScreenHeight();
            int landscapeW = Math.max(deviceWidth, deviceHeight);
            int landscapeH = Math.min(deviceWidth, deviceHeight);
            int gridH = landscapeH / 2;
            int gridW = (int)((float)gridH * origW / origH);
            int maxW = landscapeW / 2;
            if (gridW > maxW) gridW = maxW;
            if ((gridW & 1) != 0) gridW++;
            if ((gridH & 1) != 0) gridH++;
            return gridW + "x" + gridH;
        } catch (Exception e) { return null; }
    }

    private static void updateGridPreviewText(CheckBox cb, Config config) {
        String screenSize = config.getScreenSize();
        if (screenSize != null && !screenSize.isEmpty()) {
            String gridRes = computeGridResolution(screenSize);
            if (gridRes != null) {
                cb.setText("Half Resolution Grid Rendering (" + gridRes + ")");
                return;
            }
        }
        cb.setText("Half Resolution Grid Rendering");
    }

    public static boolean isOledDisplay(Context context) {
        try {
            Display display = ((WindowManager)context.getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay();
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                Display.HdrCapabilities hdrCaps = display.getHdrCapabilities();
                if (hdrCaps != null && hdrCaps.getSupportedHdrTypes().length > 0) return true;
            }
            String name = display.getName();
            if (name != null && (name.contains("OLED") || name.contains("AMOLED") || name.contains("POLED"))) return true;
        } catch (Exception e) {}
        return false;
    }

    public static int toVkPresentMode(String mode) {
        if (mode == null) return 2;
        switch (mode) {
            case "mailbox":       return 1;
            default:              return 2;
        }
    }
}
