package com.winlator.cmod.contentdialog;

import android.app.Dialog;
import android.content.Context;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.GridLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.winlator.cmod.R;
import com.winlator.cmod.core.AppUtils;
import com.winlator.cmod.inputcontrols.Binding;

public class BindingPickerDialog extends Dialog {
    private final View contentView;

    public BindingPickerDialog(Context context, Binding currentBinding, final Callback<Binding> onSelected) {
        super(context, R.style.ContentDialog);
        contentView = LayoutInflater.from(context).inflate(R.layout.content_dialog_binding_picker, null);
        setContentView(contentView);

        setupTab();
        populateTab(context, R.id.FLTabMouse, Binding.mouseBindingValues(), Binding.mouseBindingLabels(), currentBinding, onSelected);
        populateTab(context, R.id.FLTabKeyboard, Binding.keyboardBindingValues(), Binding.keyboardBindingLabels(), currentBinding, onSelected);
        populateTab(context, R.id.FLTabGamepad, Binding.gamepadBindingValues(), Binding.gamepadBindingLabels(), currentBinding, onSelected);

        contentView.findViewById(R.id.BTSelectNone).setOnClickListener((v) -> {
            if (onSelected != null) onSelected.call(Binding.NONE);
            dismiss();
        });
    }

    private void setupTab() {
        AppUtils.setupTabLayout(contentView, R.id.TabLayout,
            R.id.FLTabMouse, R.id.FLTabKeyboard, R.id.FLTabGamepad);
    }

    private void populateTab(Context context, int containerId, Binding[] values, String[] labels, Binding current, final Callback<Binding> onSelected) {
        FrameLayout container = findViewById(containerId);
        LinearLayout outer = new LinearLayout(context);
        outer.setOrientation(LinearLayout.VERTICAL);
        outer.setLayoutParams(new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        int cols = 4;
        LinearLayout row = null;
        for (int i = 0; i < labels.length; i++) {
            if (i % cols == 0) {
                row = new LinearLayout(context);
                row.setOrientation(LinearLayout.HORIZONTAL);
                outer.addView(row);
            }

            final Binding binding = values[i];
            TextView cell = (TextView) LayoutInflater.from(context).inflate(R.layout.content_dialog_binding_picker_item, outer, false);
            cell.setText(labels[i]);
            if (binding == current) {
                cell.setTextColor(0xff3377ff);
            }
            cell.setLayoutParams(new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
            cell.setOnClickListener((v) -> {
                if (onSelected != null) onSelected.call(binding);
                dismiss();
            });
            row.addView(cell);
        }
        container.addView(outer);
    }

    public interface Callback<T> {
        void call(T value);
    }
}
