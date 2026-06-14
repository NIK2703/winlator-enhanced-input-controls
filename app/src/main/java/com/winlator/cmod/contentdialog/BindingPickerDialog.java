package com.winlator.cmod.contentdialog;

import android.app.AlertDialog;
import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.graphics.Typeface;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.google.android.material.tabs.TabLayout;

import com.winlator.cmod.R;
import com.winlator.cmod.core.AppUtils;
import com.winlator.cmod.core.UnitUtils;
import com.winlator.cmod.inputcontrols.Bind;

import java.util.function.Consumer;

public class BindingPickerDialog {
    public static void show(Context context, Bind currentBinding, final Consumer<Bind> onSelected) {
        View view = LayoutInflater.from(context).inflate(R.layout.content_dialog_binding_picker, null);

        AlertDialog dialog = new AlertDialog.Builder(context)
                .setTitle("Select binding")
                .setView(view)
                .setPositiveButton("Cancel", (d, which) -> d.dismiss())
                .setNegativeButton("None", (d, which) -> {
                    if (onSelected != null) onSelected.accept(Bind.NONE);
                    d.dismiss();
                })
                .create();

        TabLayout tabLayout = view.findViewById(R.id.TabLayout);
        AppUtils.setupTabLayout(view, R.id.TabLayout,
            R.id.FLTabMouse, R.id.FLTabKeyboard, R.id.FLTabGamepad);

        populateTab(context, view, R.id.FLTabMouse, Bind.mouseBindingValues(), Bind.mouseBindingLabels(), currentBinding, onSelected, dialog);
        populateTab(context, view, R.id.FLTabKeyboard, Bind.keyboardBindingValues(), Bind.keyboardBindingLabels(), currentBinding, onSelected, dialog);
        populateTab(context, view, R.id.FLTabGamepad, Bind.gamepadBindingValues(), Bind.gamepadBindingLabels(), currentBinding, onSelected, dialog);

        dialog.show();

        if (currentBinding != null) {
            int tabIndex = -1;
            if (currentBinding.isMouse()) tabIndex = 0;
            else if (currentBinding.isKeyboard()) tabIndex = 1;
            else if (currentBinding.isGamepad()) tabIndex = 2;
            if (tabIndex >= 0 && tabLayout.getTabAt(tabIndex) != null) {
                tabLayout.getTabAt(tabIndex).select();
            }
        }
    }

    private static void populateTab(Context context, View root, int containerId, Bind[] values, String[] labels, Bind current, final Consumer<Bind> onSelected, final AlertDialog dialog) {
        FrameLayout container = root.findViewById(containerId);
        LinearLayout outer = new LinearLayout(context);
        outer.setOrientation(LinearLayout.VERTICAL);
        int padH = (int) UnitUtils.dpToPx(12);
        outer.setPadding(padH, 0, padH, 0);

        final int activeColor = context.getResources().getColor(R.color.colorAccent, context.getTheme());
        int cols = 4;
        LinearLayout row = null;
        int itemsInRow = 0;

        for (int i = 0; i < labels.length; i++) {
            if (itemsInRow == 0) {
                row = new LinearLayout(context);
                row.setOrientation(LinearLayout.HORIZONTAL);
                row.setBaselineAligned(false);
                outer.addView(row);
            }

            final Bind binding = values[i];
            TextView cell = (TextView) LayoutInflater.from(context).inflate(R.layout.content_dialog_binding_picker_item, outer, false);
            cell.setText(labels[i]);
            if (binding == current) {
                cell.setTextColor(activeColor);
                cell.setTypeface(cell.getTypeface(), Typeface.BOLD);
            }
            cell.setLayoutParams(new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1));
            cell.setOnClickListener((v) -> {
                if (onSelected != null) onSelected.accept(binding);
                dialog.dismiss();
            });
            row.addView(cell);
            itemsInRow++;

            if (itemsInRow == cols || i == labels.length - 1) {
                if (itemsInRow > 0 && itemsInRow < cols) {
                    for (int p = itemsInRow; p < cols; p++) {
                        View placeholder = new View(context);
                        placeholder.setLayoutParams(new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1));
                        row.addView(placeholder);
                    }
                }
                itemsInRow = 0;
            }
        }
        ScrollView scrollView = new ScrollView(context);
        scrollView.setLayoutParams(new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        scrollView.addView(outer, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        container.addView(scrollView);
    }
}
