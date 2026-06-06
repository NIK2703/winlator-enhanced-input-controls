package com.winlator.cmod.widget;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
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
}
