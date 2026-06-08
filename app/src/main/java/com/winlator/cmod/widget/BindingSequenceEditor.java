package com.winlator.cmod.widget;

import android.app.AlertDialog;
import android.content.Context;
import android.os.Build;
import android.view.LayoutInflater;
import android.view.MenuItem;
import android.view.View;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.SeekBar;
import android.widget.TextView;

import com.winlator.cmod.R;
import com.winlator.cmod.contentdialog.BindingPickerDialog;
import com.winlator.cmod.core.AppUtils;
import com.winlator.cmod.core.UnitUtils;
import com.winlator.cmod.inputcontrols.BindPackage;
import com.winlator.cmod.inputcontrols.Binding;

public class BindingSequenceEditor {
    public static View createView(Context context, String label, int helpTextResId,
                                   final BindPackage bp, final Runnable onChanged) {
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

        {
            ImageView btMenu = new ImageView(context);
            int menuSize = (int) UnitUtils.dpToPx(22);
            LinearLayout.LayoutParams lpMenu = new LinearLayout.LayoutParams(menuSize, menuSize);
            lpMenu.leftMargin = (int) UnitUtils.dpToPx(4);
            btMenu.setLayoutParams(lpMenu);
            btMenu.setImageResource(android.R.drawable.ic_menu_more);
            btMenu.setColorFilter(0xffe0e0e0);
            btMenu.setOnClickListener((v) -> {
                PopupMenu popupMenu = new PopupMenu(context, v);
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) popupMenu.setForceShowIcon(true);
                MenuItem toggleItem = popupMenu.getMenu().add(0, 1, 0, "Toggle Switch");
                toggleItem.setCheckable(true);
                toggleItem.setChecked(bp.isToggleSwitch());
                toggleItem.setOnMenuItemClickListener((menuItem) -> {
                    boolean newState = !bp.isToggleSwitch();
                    bp.setToggleSwitch(newState);
                    menuItem.setChecked(newState);
                    if (onChanged != null) onChanged.run();
                    return true;
                });

                MenuItem autoRepeatItem = popupMenu.getMenu().add(0, 2, 0, "Auto Repeat");
                autoRepeatItem.setCheckable(true);
                autoRepeatItem.setChecked(bp.isAutoRepeat());

                MenuItem intervalItem = popupMenu.getMenu().add(0, 3, 0, "Interval: " + bp.getAutoRepeatIntervalMs() + "ms");
                intervalItem.setEnabled(bp.isAutoRepeat());

                autoRepeatItem.setOnMenuItemClickListener((menuItem) -> {
                    boolean newState = !bp.isAutoRepeat();
                    bp.setAutoRepeat(newState);
                    menuItem.setChecked(newState);
                    intervalItem.setEnabled(newState);
                    if (onChanged != null) onChanged.run();
                    if (newState) showIntervalDialog(context, intervalItem, bp, onChanged);
                    return true;
                });

                intervalItem.setOnMenuItemClickListener((menuItem) -> {
                    showIntervalDialog(context, intervalItem, bp, onChanged);
                    return true;
                });

                popupMenu.show();
            });
            titleRow.addView(btMenu);
        }

        section.addView(titleRow);

        final LinearLayout llItems = new LinearLayout(context);
        llItems.setOrientation(LinearLayout.VERTICAL);
        section.addView(llItems);

        final int colorAccent = context.getResources().getColor(R.color.colorAccent, context.getTheme());

        final Runnable[] populateRef = new Runnable[1];
        populateRef[0] = () -> {
            llItems.removeAllViews();

            bp.sync();

            for (int i = 0; i < bp.size(); i++) {
                final Binding currentBinding = bp.get(i);
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

        final int MAX_BINDINGS = 8;

        int buttonHeight = (int) UnitUtils.dpToPx(36);
        Button btAdd = new Button(context, null, 0, R.style.ButtonNeutral);
        btAdd.setText("+ Add Binding");
        btAdd.setLayoutParams(new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, buttonHeight));
        btAdd.setOnClickListener((v) -> {
            bp.add(Binding.NONE);
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
            btAdd.setVisibility(bp.size() >= MAX_BINDINGS ? View.GONE : View.VISIBLE);
        };
        populateRef[0].run();

        return section;
    }

    private static void showIntervalDialog(Context context, MenuItem intervalItem, BindPackage bp, Runnable onChanged) {
        LinearLayout layout = new LinearLayout(context);
        layout.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) UnitUtils.dpToPx(20);
        layout.setPadding(pad, pad, pad, pad);

        SeekBar seekBar = new SeekBar(context);
        seekBar.setMax(95);
        int cur = bp.getAutoRepeatIntervalMs();
        seekBar.setProgress(Math.max(0, cur / 10 - 5));
        layout.addView(seekBar);

        TextView tvValue = new TextView(context);
        tvValue.setText(String.valueOf(cur) + "ms");
        tvValue.setGravity(android.view.Gravity.CENTER);
        tvValue.setTextSize(18);
        layout.addView(tvValue);

        seekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int progress, boolean fromUser) {
                tvValue.setText(String.valueOf((progress + 5) * 10) + "ms");
            }
            @Override public void onStartTrackingTouch(SeekBar s) {}
            @Override public void onStopTrackingTouch(SeekBar s) {}
        });

        new AlertDialog.Builder(context)
            .setTitle("Repeat Interval")
            .setView(layout)
            .setPositiveButton("OK", (dialog, which) -> {
                int value = (seekBar.getProgress() + 5) * 10;
                bp.setAutoRepeatIntervalMs(value);
                intervalItem.setTitle("Interval: " + value + "ms");
                if (onChanged != null) onChanged.run();
            })
            .setNegativeButton("Cancel", null)
            .show();
    }
}
