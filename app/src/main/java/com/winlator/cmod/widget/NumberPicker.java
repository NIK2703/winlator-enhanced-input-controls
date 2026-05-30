package com.winlator.cmod.widget;

import android.content.Context;
import android.content.res.TypedArray;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.AttributeSet;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.widget.EditText;
import android.widget.FrameLayout;

import com.winlator.cmod.R;
import com.winlator.cmod.math.Mathf;

public class NumberPicker extends FrameLayout implements View.OnTouchListener {
    private int value = 0;
    private int minValue = 0;
    private int maxValue = 100;
    private int step = 1;
    private final EditText editText;
    private OnValueChangeListener onValueChangeListener;

    public interface OnValueChangeListener {
        void onValueChange(NumberPicker numberPicker, int value);
    }

    public NumberPicker(Context context) {
        this(context, null);
    }

    public NumberPicker(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public NumberPicker(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);

        LayoutInflater.from(context).inflate(R.layout.number_picker, this, true);
        editText = findViewById(R.id.EditText);
        findViewById(R.id.BTDecrement).setOnTouchListener(this);
        findViewById(R.id.BTIncrement).setOnTouchListener(this);

        editText.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {}
            @Override public void afterTextChanged(Editable s) {
                String text = s.toString();
                if (!text.isEmpty()) {
                    try {
                        int newValue = Integer.parseInt(text);
                        if (newValue != NumberPicker.this.value) {
                            NumberPicker.this.value = Mathf.clamp(newValue, minValue, maxValue);
                            if (onValueChangeListener != null) {
                                onValueChangeListener.onValueChange(NumberPicker.this, NumberPicker.this.value);
                            }
                        }
                    }
                    catch (NumberFormatException ignored) {}
                }
            }
        });
        editText.setOnFocusChangeListener((v, hasFocus) -> {
            if (!hasFocus) {
                setValue(value);
            }
            else {
                editText.selectAll();
            }
        });

        if (attrs != null) {
            TypedArray ta = context.obtainStyledAttributes(attrs, R.styleable.NumberPicker);
            minValue = ta.getInt(R.styleable.NumberPicker_minValue, minValue);
            maxValue = ta.getInt(R.styleable.NumberPicker_maxValue, maxValue);
            setStep(ta.getInt(R.styleable.NumberPicker_step, step));
            int value = ta.getInt(R.styleable.NumberPicker_value, 0);
            ta.recycle();
            setValue(value);
        }
        else setStep(step);
    }

    public void setValue(int value) {
        this.value = Mathf.clamp(value, minValue, maxValue);
        editText.setText(String.valueOf(this.value));
    }

    public int getValue() {
        return value;
    }

    public int getMinValue() {
        return minValue;
    }

    public void setMinValue(int minValue) {
        this.minValue = minValue;
    }

    public int getMaxValue() {
        return maxValue;
    }

    public void setMaxValue(int maxValue) {
        this.maxValue = maxValue;
    }

    public void increment() {
        setValue(value+step);
    }

    public void decrement() {
        setValue(value-step);
    }

    public int getStep() {
        return step;
    }

    public void setStep(int step) {
        this.step = step;
    }

    public OnValueChangeListener getOnValueChangeListener() {
        return onValueChangeListener;
    }

    public void setOnValueChangeListener(OnValueChangeListener onValueChangeListener) {
        this.onValueChangeListener = onValueChangeListener;
    }

    private void onButtonClick(View v) {
        if (!isEnabled()) return;

        int id = v.getId();
        if (id == R.id.BTIncrement) {
            increment();
        }
        else if (id == R.id.BTDecrement) {
            decrement();
        }
        if (onValueChangeListener != null) {
            onValueChangeListener.onValueChange(NumberPicker.this, value);
        }
    }

    @Override
    public boolean onTouch(final View v, MotionEvent event) {
        int action = event.getAction();
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            onButtonClick(v);
        }
        return true;
    }
}
