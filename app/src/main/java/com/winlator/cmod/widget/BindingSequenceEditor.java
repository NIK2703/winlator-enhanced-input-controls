package com.winlator.cmod.widget;

import android.app.AlertDialog;
import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

import com.winlator.cmod.R;
import com.winlator.cmod.contentdialog.BindingPickerDialog;
import com.winlator.cmod.core.AppUtils;
import com.winlator.cmod.core.UnitUtils;
import com.winlator.cmod.inputcontrols.BindPackage;
import com.winlator.cmod.inputcontrols.Bind;

import java.util.HashMap;
import java.util.function.BiConsumer;

public class BindingSequenceEditor {

    public static final String EXTRA_SCROLL_BINDINGS = "scrollBindings";
    public static final String EXTRA_IS_SCROLL_MODE = "isScrollMode";

    public static View createView(Context context, String label, int helpTextResId,
                                   final BindPackage bp, final Runnable onChanged) {
        return createView(context, label, helpTextResId, bp, onChanged, true);
    }

    public static View createView(Context context, String label, int helpTextResId,
                                   final BindPackage bp, final Runnable onChanged, boolean showMenu) {
        return createViewInternal(context, label, helpTextResId, bp, onChanged, showMenu, false, null);
    }

    public static View createDragView(Context context, String label, int helpTextResId,
                                       final BindPackage bp, final Runnable onChanged,
                                       HashMap<String, BindPackage> scrollBindings,
                                       boolean initialScrollActive) {
        return createViewInternal(context, label, helpTextResId, bp, onChanged, true, true, scrollBindings, initialScrollActive, null);
    }

    public static View createDragView(Context context, String label, int helpTextResId,
                                       final BindPackage bp, final Runnable onChanged,
                                       HashMap<String, BindPackage> scrollBindings,
                                       boolean initialScrollActive,
                                       java.util.function.BiConsumer<String, Boolean> onHoldChanged) {
        return createViewInternal(context, label, helpTextResId, bp, onChanged, true, true, scrollBindings, initialScrollActive, onHoldChanged);
    }

    private static View createViewInternal(Context context, String label, int helpTextResId,
                                            final BindPackage bp, final Runnable onChanged, boolean showMenu,
                                            boolean isDragGesture, HashMap<String, BindPackage> scrollBindings) {
        return createViewInternal(context, label, helpTextResId, bp, onChanged, showMenu, isDragGesture, scrollBindings, false, null);
    }

    private static View createViewInternal(Context context, String label, int helpTextResId,
                                            final BindPackage bp, final Runnable onChanged, boolean showMenu,
                                            boolean isDragGesture, HashMap<String, BindPackage> scrollBindings,
                                            boolean initialScrollActive,
                                            java.util.function.BiConsumer<String, Boolean> onHoldChanged) {
        final LinearLayout section = new LinearLayout(context);
        section.setOrientation(LinearLayout.VERTICAL);
        section.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        LinearLayout titleRow = new LinearLayout(context);
        titleRow.setOrientation(LinearLayout.HORIZONTAL);
        titleRow.setGravity(android.view.Gravity.CENTER_VERTICAL);
        titleRow.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        LinearLayout leftGroup = new LinearLayout(context);
        leftGroup.setOrientation(LinearLayout.HORIZONTAL);
        leftGroup.setGravity(android.view.Gravity.CENTER_VERTICAL);
        leftGroup.setLayoutParams(new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        TextView tvTitle = new TextView(context);
        tvTitle.setText(label);
        leftGroup.addView(tvTitle);

        if (helpTextResId != 0) {
            ImageView btHelp = new ImageView(context);
            int helpSize = (int) UnitUtils.dpToPx(22);
            LinearLayout.LayoutParams lpHelp = new LinearLayout.LayoutParams(helpSize, helpSize);
            lpHelp.leftMargin = (int) UnitUtils.dpToPx(4);
            btHelp.setLayoutParams(lpHelp);
            btHelp.setImageResource(R.drawable.icon_help);
            btHelp.setColorFilter(0xffe0e0e0);
            btHelp.setOnClickListener((v) -> AppUtils.showHelpBox(context, v, helpTextResId));
            leftGroup.addView(btHelp);
        }
        titleRow.addView(leftGroup);

        final int colorAccent = context.getResources().getColor(R.color.colorAccent, context.getTheme());
        int iconSize = (int) UnitUtils.dpToPx(28);

        // Scroll mode toggle for drag gestures
        if (showMenu && isDragGesture) {
            final boolean[] scrollModeActive = {initialScrollActive};
            final LinearLayout[] scrollContainer = {null};

            // Auto repeat (first)
            LinearLayout autoRepeatGroup = new LinearLayout(context);
            autoRepeatGroup.setOrientation(LinearLayout.HORIZONTAL);
            autoRepeatGroup.setGravity(android.view.Gravity.CENTER_VERTICAL);

            ImageView btAutoRepeat = new ImageView(context);
            btAutoRepeat.setLayoutParams(new LinearLayout.LayoutParams(iconSize, iconSize));
            btAutoRepeat.setScaleType(ImageView.ScaleType.CENTER);
            btAutoRepeat.setImageResource(R.drawable.icon_autorepeat);
            btAutoRepeat.setColorFilter(bp.isAutoRepeat() ? colorAccent : 0xff888888);
            autoRepeatGroup.addView(btAutoRepeat);

            TextView tvInterval = new TextView(context);
            tvInterval.setText("Repeat" + (bp.isAutoRepeat() ? " (" + bp.getAutoRepeatIntervalMs() + "ms)" : ""));
            tvInterval.setTextColor(bp.isAutoRepeat() ? colorAccent : 0xff888888);
            tvInterval.setTextSize(12);
            tvInterval.setGravity(android.view.Gravity.CENTER_VERTICAL);
            LinearLayout.LayoutParams lpInterval = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    LinearLayout.LayoutParams.MATCH_PARENT);
            lpInterval.leftMargin = (int) UnitUtils.dpToPx(2);
            tvInterval.setLayoutParams(lpInterval);
            autoRepeatGroup.addView(tvInterval);

            autoRepeatGroup.setOnClickListener((v) -> {
                boolean newState = !bp.isAutoRepeat();
                bp.setAutoRepeat(newState);
                btAutoRepeat.setColorFilter(newState ? colorAccent : 0xff888888);
                tvInterval.setTextColor(newState ? colorAccent : 0xff888888);
                tvInterval.setText("Repeat" + (newState ? " (" + bp.getAutoRepeatIntervalMs() + "ms)" : ""));
                if (newState) {
                    showIntervalDialog(context, bp, () -> {
                        tvInterval.setText("Repeat (" + bp.getAutoRepeatIntervalMs() + "ms)");
                        if (onChanged != null) onChanged.run();
                    });
                }
                if (onChanged != null) onChanged.run();
            });
            titleRow.addView(autoRepeatGroup);

            // Scroll mode toggle (second)
            LinearLayout scrollToggleGroup = new LinearLayout(context);
            scrollToggleGroup.setOrientation(LinearLayout.HORIZONTAL);
            scrollToggleGroup.setGravity(android.view.Gravity.CENTER_VERTICAL);

            ImageView btScrollMode = new ImageView(context);
            btScrollMode.setLayoutParams(new LinearLayout.LayoutParams(iconSize, iconSize));
            btScrollMode.setScaleType(ImageView.ScaleType.CENTER);
            btScrollMode.setImageResource(R.drawable.icon_scroll_mode);
            btScrollMode.setColorFilter(0xff888888);
            scrollToggleGroup.addView(btScrollMode);

            TextView tvScrollLabel = new TextView(context);
            tvScrollLabel.setText("Scroll");
            tvScrollLabel.setTextColor(0xff888888);
            tvScrollLabel.setTextSize(12);
            tvScrollLabel.setGravity(android.view.Gravity.CENTER_VERTICAL);
            LinearLayout.LayoutParams lpScrollLabel = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    LinearLayout.LayoutParams.MATCH_PARENT);
            lpScrollLabel.leftMargin = (int) UnitUtils.dpToPx(2);
            tvScrollLabel.setLayoutParams(lpScrollLabel);
            scrollToggleGroup.addView(tvScrollLabel);

            // Container for normal bindings and add button (hidden when scroll mode is on)
            final LinearLayout normalContent = new LinearLayout(context);
            normalContent.setOrientation(LinearLayout.VERTICAL);
            normalContent.setVisibility(initialScrollActive ? View.GONE : View.VISIBLE);

            // Set initial toggle button colors
            if (initialScrollActive) {
                btScrollMode.setColorFilter(colorAccent);
                tvScrollLabel.setTextColor(colorAccent);
                autoRepeatGroup.setVisibility(View.GONE);
            }

            // Container for scroll bindings (hidden by default, unless initialScrollActive)
            scrollContainer[0] = new LinearLayout(context);
            scrollContainer[0].setOrientation(LinearLayout.VERTICAL);
            scrollContainer[0].setVisibility(initialScrollActive ? View.VISIBLE : View.GONE);

            scrollToggleGroup.setOnClickListener((v) -> {
                scrollModeActive[0] = !scrollModeActive[0];
                if (scrollModeActive[0]) {
                    btScrollMode.setColorFilter(colorAccent);
                    tvScrollLabel.setTextColor(colorAccent);
                    normalContent.setVisibility(View.GONE);
                    scrollContainer[0].setVisibility(View.VISIBLE);
                    autoRepeatGroup.setVisibility(View.GONE);
                } else {
                    btScrollMode.setColorFilter(0xff888888);
                    tvScrollLabel.setTextColor(0xff888888);
                    normalContent.setVisibility(View.VISIBLE);
                    scrollContainer[0].setVisibility(View.GONE);
                    autoRepeatGroup.setVisibility(View.VISIBLE);
                    // Clear all scroll bindings when toggling off (in-place, so
                    // GestureSettingsDialog.scrollBindingValues is also cleared)
                    if (scrollBindings != null) {
                        for (String dirKey : new String[]{"up", "down", "left", "right"}) {
                            BindPackage sbp = scrollBindings.get(dirKey);
                            if (sbp != null) sbp.clear();
                        }
                    }
                }
                if (onChanged != null) onChanged.run();
            });
            titleRow.addView(scrollToggleGroup);

            section.addView(titleRow);

            // Normal bindings content (button and rows go inside normalContent)
            populateNormalBindings(context, normalContent, normalContent, bp, onChanged);
            section.addView(normalContent);

            // Scroll bindings content (4 directional rows)
            populateScrollBindings(context, scrollContainer[0], scrollBindings, colorAccent, onChanged, onHoldChanged);
            section.addView(scrollContainer[0]);

        } else {
            section.addView(titleRow);

            if (showMenu) {
                // Auto repeat (first)
                LinearLayout autoRepeatGroup = new LinearLayout(context);
                autoRepeatGroup.setOrientation(LinearLayout.HORIZONTAL);
                autoRepeatGroup.setGravity(android.view.Gravity.CENTER_VERTICAL);

                ImageView btAutoRepeat = new ImageView(context);
                btAutoRepeat.setLayoutParams(new LinearLayout.LayoutParams(iconSize, iconSize));
                btAutoRepeat.setScaleType(ImageView.ScaleType.CENTER);
                btAutoRepeat.setImageResource(R.drawable.icon_autorepeat);
                btAutoRepeat.setColorFilter(bp.isAutoRepeat() ? colorAccent : 0xff888888);
                autoRepeatGroup.addView(btAutoRepeat);

                TextView tvInterval = new TextView(context);
                tvInterval.setText("Repeat" + (bp.isAutoRepeat() ? " (" + bp.getAutoRepeatIntervalMs() + "ms)" : ""));
                tvInterval.setTextColor(bp.isAutoRepeat() ? colorAccent : 0xff888888);
                tvInterval.setTextSize(12);
                tvInterval.setGravity(android.view.Gravity.CENTER_VERTICAL);
                LinearLayout.LayoutParams lpInterval = new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.WRAP_CONTENT,
                        LinearLayout.LayoutParams.MATCH_PARENT);
                lpInterval.leftMargin = (int) UnitUtils.dpToPx(2);
                tvInterval.setLayoutParams(lpInterval);
                autoRepeatGroup.addView(tvInterval);

                autoRepeatGroup.setOnClickListener((v) -> {
                    boolean newState = !bp.isAutoRepeat();
                    bp.setAutoRepeat(newState);
                    btAutoRepeat.setColorFilter(newState ? colorAccent : 0xff888888);
                    tvInterval.setTextColor(newState ? colorAccent : 0xff888888);
                    tvInterval.setText("Repeat" + (newState ? " (" + bp.getAutoRepeatIntervalMs() + "ms)" : ""));
                    if (newState) {
                        showIntervalDialog(context, bp, () -> {
                            tvInterval.setText("Repeat (" + bp.getAutoRepeatIntervalMs() + "ms)");
                            if (onChanged != null) onChanged.run();
                        });
                    }
                    if (onChanged != null) onChanged.run();
                });
                titleRow.addView(autoRepeatGroup);

                // Toggle switch (second)
                LinearLayout toggleGroup = new LinearLayout(context);
                toggleGroup.setOrientation(LinearLayout.HORIZONTAL);
                toggleGroup.setGravity(android.view.Gravity.CENTER_VERTICAL);

                ImageView btToggle = new ImageView(context);
                btToggle.setLayoutParams(new LinearLayout.LayoutParams(iconSize, iconSize));
                btToggle.setScaleType(ImageView.ScaleType.CENTER);
                btToggle.setImageResource(R.drawable.icon_toggle);
                btToggle.setColorFilter(bp.isToggleSwitch() ? colorAccent : 0xff888888);
                toggleGroup.addView(btToggle);

                TextView tvToggleLabel = new TextView(context);
                tvToggleLabel.setText("Switch");
                tvToggleLabel.setTextColor(bp.isToggleSwitch() ? colorAccent : 0xff888888);
                tvToggleLabel.setTextSize(12);
                tvToggleLabel.setGravity(android.view.Gravity.CENTER_VERTICAL);
                LinearLayout.LayoutParams lpToggleLabel = new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.WRAP_CONTENT,
                        LinearLayout.LayoutParams.MATCH_PARENT);
                lpToggleLabel.leftMargin = (int) UnitUtils.dpToPx(2);
                tvToggleLabel.setLayoutParams(lpToggleLabel);
                toggleGroup.addView(tvToggleLabel);

                toggleGroup.setOnClickListener((v) -> {
                    boolean newState = !bp.isToggleSwitch();
                    bp.setToggleSwitch(newState);
                    btToggle.setColorFilter(newState ? colorAccent : 0xff888888);
                    tvToggleLabel.setTextColor(newState ? colorAccent : 0xff888888);
                    if (onChanged != null) onChanged.run();
                });
                titleRow.addView(toggleGroup);
            }

            final LinearLayout llItems = new LinearLayout(context);
            llItems.setOrientation(LinearLayout.VERTICAL);
            section.addView(llItems);

            populateNormalBindings(context, section, llItems, bp, onChanged);
        }

        return section;
    }

    private static void populateNormalBindings(Context context, LinearLayout section,
                                                LinearLayout llItems, final BindPackage bp,
                                                final Runnable onChanged) {
        final int colorAccent = context.getResources().getColor(R.color.colorAccent, context.getTheme());
        final int MAX_BINDINGS = 8;

        final Runnable[] populateRef = new Runnable[1];
        populateRef[0] = () -> {
            llItems.removeAllViews();
            bp.sync();

            for (int i = 0; i < bp.size(); i++) {
                final Bind currentBinding = bp.get(i);
                final int seqIndex = i;
                View row = LayoutInflater.from(context).inflate(R.layout.binding_sequence_item, llItems, false);
                final TextView btBinding = row.findViewById(R.id.BTBinding);
                btBinding.setText(currentBinding.toString());

                btBinding.setOnClickListener((v) -> {
                    BindingPickerDialog.show(context, bp.get(seqIndex), (newBinding) -> {
                        if (seqIndex < bp.size() && newBinding != bp.get(seqIndex)) {
                            bp.set(seqIndex, newBinding);
                            btBinding.setText(newBinding.toString());
                            if (onChanged != null) onChanged.run();
                        }
                    });
                });

                final ImageView btPushpin = row.findViewById(R.id.BTPushpin);
                boolean isSticky = bp.isSticky(seqIndex);
                btPushpin.setColorFilter(isSticky ? colorAccent : 0xff888888);
                btPushpin.setOnClickListener((v) -> {
                    boolean newVal = !bp.isSticky(seqIndex);
                    bp.setSticky(seqIndex, newVal);
                    btPushpin.setColorFilter(newVal ? colorAccent : 0xff888888);
                    if (onChanged != null) onChanged.run();
                });

                row.findViewById(R.id.BTRemove).setOnClickListener((v) -> {
                    if (seqIndex < bp.size()) {
                        bp.remove(seqIndex);
                    }
                    if (onChanged != null) onChanged.run();
                    if (populateRef[0] != null) populateRef[0].run();
                });

                llItems.addView(row);
            }
        };

        int buttonHeight = (int) UnitUtils.dpToPx(36);
        Button btAdd = new Button(context, null, 0, R.style.ButtonNeutral);
        btAdd.setText("+ Add Bind");
        btAdd.setLayoutParams(new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, buttonHeight));
        btAdd.setOnClickListener((v) -> {
            bp.add(Bind.NONE);
            if (onChanged != null) onChanged.run();
            if (populateRef[0] != null) populateRef[0].run();

            int lastIndex = bp.size() - 1;
            if (lastIndex >= 0 && lastIndex < llItems.getChildCount()) {
                View lastRow = llItems.getChildAt(lastIndex);
                TextView btBinding = lastRow.findViewById(R.id.BTBinding);
                if (btBinding != null) btBinding.performClick();
            }
        });
        section.addView(btAdd);

        final Runnable origPopulate = populateRef[0];
        populateRef[0] = () -> {
            origPopulate.run();
            if (btAdd.getParent() instanceof android.view.ViewGroup)
                ((android.view.ViewGroup) btAdd.getParent()).removeView(btAdd);
            section.addView(btAdd);
            btAdd.setVisibility(bp.size() >= MAX_BINDINGS ? View.GONE : View.VISIBLE);
        };
        populateRef[0].run();
    }

    private static void populateScrollBindings(Context context, LinearLayout container,
                                                HashMap<String, BindPackage> scrollBindings,
                                                int colorAccent, final Runnable onChanged,
                                                java.util.function.BiConsumer<String, Boolean> onHoldChanged) {
        if (scrollBindings == null) return;

        android.graphics.Paint paint = new android.graphics.Paint();
        paint.setTextSize(UnitUtils.dpToPx(14));
        float maxLabelWidth = 0;
        for (String label : new String[]{"Up", "Down", "Left", "Right"}) {
            maxLabelWidth = Math.max(maxLabelWidth, paint.measureText(label));
        }
        int labelWidthPx = (int) Math.ceil(maxLabelWidth) + (int) UnitUtils.dpToPx(12);

        // Vertical section with hold toggle
        LinearLayout verticalHeader = new LinearLayout(context);
        verticalHeader.setOrientation(LinearLayout.HORIZONTAL);
        verticalHeader.setGravity(android.view.Gravity.CENTER_VERTICAL);
        verticalHeader.setPadding(0, (int) UnitUtils.dpToPx(8), 0, 0);

        TextView tvVertical = new TextView(context);
        tvVertical.setText("Vertical");
        tvVertical.setTextSize(14);
        tvVertical.setTextColor(0xcccccccc);
        LinearLayout.LayoutParams vLp = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        tvVertical.setLayoutParams(vLp);
        verticalHeader.addView(tvVertical);

        android.widget.Switch swHoldV = new android.widget.Switch(context);
        swHoldV.setText("Hold");
        swHoldV.setTextSize(12);
        swHoldV.setTextColor(0xcccccccc);
        swHoldV.setChecked(scrollBindings.containsKey("hold_v") && scrollBindings.get("hold_v") != null);
        swHoldV.setOnCheckedChangeListener((btn, checked) -> {
            scrollBindings.put("hold_v", checked ? BindPackage.fromSingle(Bind.NONE) : null);
            if (onHoldChanged != null) onHoldChanged.accept("hold_v", checked);
            if (onChanged != null) onChanged.run();
        });
        verticalHeader.addView(swHoldV);
        container.addView(verticalHeader);

        addScrollDirRow(context, container, scrollBindings, "up", "Up", labelWidthPx, colorAccent, onChanged);
        addScrollDirRow(context, container, scrollBindings, "down", "Down", labelWidthPx, colorAccent, onChanged);

        // Horizontal section with hold toggle
        LinearLayout horizontalHeader = new LinearLayout(context);
        horizontalHeader.setOrientation(LinearLayout.HORIZONTAL);
        horizontalHeader.setGravity(android.view.Gravity.CENTER_VERTICAL);
        horizontalHeader.setPadding(0, (int) UnitUtils.dpToPx(8), 0, 0);

        TextView tvHorizontal = new TextView(context);
        tvHorizontal.setText("Horizontal");
        tvHorizontal.setTextSize(14);
        tvHorizontal.setTextColor(0xcccccccc);
        LinearLayout.LayoutParams hLp = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        tvHorizontal.setLayoutParams(hLp);
        horizontalHeader.addView(tvHorizontal);

        android.widget.Switch swHoldH = new android.widget.Switch(context);
        swHoldH.setText("Hold");
        swHoldH.setTextSize(12);
        swHoldH.setTextColor(0xcccccccc);
        swHoldH.setChecked(scrollBindings.containsKey("hold_h") && scrollBindings.get("hold_h") != null);
        swHoldH.setOnCheckedChangeListener((btn, checked) -> {
            scrollBindings.put("hold_h", checked ? BindPackage.fromSingle(Bind.NONE) : null);
            if (onHoldChanged != null) onHoldChanged.accept("hold_h", checked);
            if (onChanged != null) onChanged.run();
        });
        horizontalHeader.addView(swHoldH);
        container.addView(horizontalHeader);

        addScrollDirRow(context, container, scrollBindings, "left", "Left", labelWidthPx, colorAccent, onChanged);
        addScrollDirRow(context, container, scrollBindings, "right", "Right", labelWidthPx, colorAccent, onChanged);
    }

    private static void addScrollDirRow(Context context, LinearLayout container,
                                         HashMap<String, BindPackage> scrollBindings,
                                         String dirKey, String label, int labelWidthPx,
                                         int colorAccent, final Runnable onChanged) {
        BindPackage dirBp = scrollBindings.get(dirKey);
        if (dirBp == null) {
            dirBp = new BindPackage();
            scrollBindings.put(dirKey, dirBp);
        }
        final BindPackage finalDirBp = dirBp;
        final Bind[] currentBind = {finalDirBp.size() > 0 ? finalDirBp.get(0) : Bind.NONE};

        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(android.view.Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams rowLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        rowLp.topMargin = (int) UnitUtils.dpToPx(4);
        row.setLayoutParams(rowLp);

        TextView tvTitle = new TextView(context);
        tvTitle.setText(label);
        tvTitle.setTextSize(14);
        tvTitle.setWidth(labelWidthPx);
        tvTitle.setGravity(android.view.Gravity.LEFT | android.view.Gravity.CENTER_VERTICAL);
        row.addView(tvTitle);

        TextView tvBind = new TextView(context);
        tvBind.setText(currentBind[0] != null && currentBind[0] != Bind.NONE ? currentBind[0].toString() : "None");
        tvBind.setTextSize(16);
        tvBind.setTextColor(0xffffffff);
        tvBind.setBackgroundResource(R.drawable.combo_box);
        tvBind.setPadding((int) UnitUtils.dpToPx(12), 0, (int) UnitUtils.dpToPx(36), 0);
        tvBind.setGravity(android.view.Gravity.LEFT | android.view.Gravity.CENTER_VERTICAL);
        tvBind.setMaxLines(1);
        tvBind.setEllipsize(android.text.TextUtils.TruncateAt.END);
        LinearLayout.LayoutParams bindLp = new LinearLayout.LayoutParams(
                0, (int) UnitUtils.dpToPx(42), 1);
        tvBind.setLayoutParams(bindLp);
        tvBind.setOnClickListener((v) -> {
            BindingPickerDialog.show(context, currentBind[0] != null ? currentBind[0] : Bind.NONE, (newBinding) -> {
                if (newBinding != currentBind[0]) {
                    currentBind[0] = newBinding;
                    tvBind.setText(newBinding != null && newBinding != Bind.NONE ? newBinding.toString() : "None");
                    finalDirBp.clear();
                    if (newBinding != null && newBinding != Bind.NONE) {
                        finalDirBp.add(newBinding);
                    }
                    if (onChanged != null) onChanged.run();
                }
            });
        });
        row.addView(tvBind);

        container.addView(row);
    }

    private static void showIntervalDialog(Context context, BindPackage bp, Runnable onOk) {
        LinearLayout layout = new LinearLayout(context);
        layout.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) UnitUtils.dpToPx(20);
        layout.setPadding(pad, pad, pad, pad);

        SeekBar seekBar = new SeekBar(context);
        seekBar.setMax(90);
        int cur = bp.getAutoRepeatIntervalMs();
        seekBar.setProgress(Math.max(0, cur / 10 - 10));
        layout.addView(seekBar);

        TextView tvValue = new TextView(context);
        tvValue.setText(String.valueOf(cur) + "ms");
        tvValue.setGravity(android.view.Gravity.CENTER);
        tvValue.setTextSize(18);
        layout.addView(tvValue);

        seekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int progress, boolean fromUser) {
                tvValue.setText(String.valueOf((progress + 10) * 10) + "ms");
            }
            @Override public void onStartTrackingTouch(SeekBar s) {}
            @Override public void onStopTrackingTouch(SeekBar s) {}
        });

        new AlertDialog.Builder(context)
            .setTitle("Repeat Interval")
            .setView(layout)
            .setPositiveButton("OK", (dialog, which) -> {
                int value = (seekBar.getProgress() + 10) * 10;
                bp.setAutoRepeatIntervalMs(value);
                if (onOk != null) onOk.run();
            })
            .setNegativeButton("Cancel", null)
            .show();
    }
}
