package com.winlator.cmod.inputcontrols;

import android.graphics.Rect;
import android.os.Handler;
import android.os.Looper;

import com.winlator.cmod.widget.InputControlsView;
import com.winlator.cmod.widget.TouchpadView;

public class RangeScroller {
    private final InputControlsView inputControlsView;
    private final ControlElement element;
    private float scrollOffset;
    private float currentOffset;
    private float lastPosition;
    private long touchTime;
    private Binding binding = Binding.NONE;
    private int pressedIndex = -1;
    private boolean isActionDown = false;
    private boolean scrolling = false;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable tapRunnable;
    private byte rangeIndexFrom;
    private byte rangeIndexTo;

    private static final Binding[][] RANGE_BINDINGS = new Binding[4][];
    static {
        for (ControlElement.Range range : ControlElement.Range.values()) {
            int max = range.max;
            Binding[] arr = new Binding[max];
            for (int i = 0; i < max; i++) {
                switch (range.ordinal()) {
                    case 0: arr[i] = Binding.valueOf("KEY_" + (char)(65 + i)); break;
                    case 1: arr[i] = Binding.valueOf("KEY_" + ((i + 1) % 10)); break;
                    case 2: arr[i] = Binding.valueOf("KEY_F" + (i + 1)); break;
                    case 3: arr[i] = Binding.valueOf("KEY_KP_" + ((i + 1) % 10)); break;
                }
            }
            RANGE_BINDINGS[range.ordinal()] = arr;
        }
    }

    public RangeScroller(InputControlsView inputControlsView, ControlElement element) {
        this.inputControlsView = inputControlsView;
        this.element = element;
        this.tapRunnable = () -> {
            if (!scrolling) inputControlsView.handleInputEvent(binding, true);
        };
    }

    public float getElementSize() {
        Rect boundingBox = element.getBoundingBox();
        return (float)Math.max(boundingBox.width(), boundingBox.height()) / element.getBindingCount();
    }

    public float getScrollSize() {
        return getElementSize() * element.getRange().max;
    }

    public float getScrollOffset() {
        return scrollOffset;
    }

    public boolean isActionDown() {
        return isActionDown;
    }

    public int getPressedIndex() {
        return pressedIndex;
    }

    public void updateVisualState(boolean isFingerDown, float x, float y, float cScrollOffset) {
        if (isFingerDown) {
            if (!isActionDown) {
                isActionDown = true;
                pressedIndex = getIndexByPosition(x, y);
                scrolling = false;
                currentOffset = 0;
                scrollOffset = cScrollOffset;
                lastPosition = element.getOrientation() == 0 ? x : y;
                updateRangeIndex();
            } else {
                float position = element.getOrientation() == 0 ? x : y;
                float deltaPosition = position - lastPosition;
                if (Math.abs(deltaPosition) >= TouchpadView.MAX_TAP_TRAVEL_DISTANCE) {
                    if (!scrolling) {
                        scrolling = true;
                        pressedIndex = -1;
                    }
                    currentOffset += deltaPosition;
                    float scrollSize = getScrollSize();
                    scrollOffset = -currentOffset % scrollSize;
                    if (scrollOffset < 0) scrollOffset = scrollSize + scrollOffset;
                    updateRangeIndex();
                    lastPosition = position;
                }
            }
        } else {
            isActionDown = false;
            pressedIndex = -1;
            scrolling = false;
        }
    }

    public int getRangeIndexFrom() {
        updateRangeIndex();
        return rangeIndexFrom;
    }

    public int getRangeIndexTo() {
        updateRangeIndex();
        return rangeIndexTo;
    }

    private void updateRangeIndex() {
        ControlElement.Range range = element.getRange();
        byte from = (byte)Math.floor((scrollOffset / getElementSize()) % range.max);
        if (from < 0) from = (byte)(range.max + from);
        byte to = (byte)(from + element.getBindingCount() + 1);
        rangeIndexFrom = from;
        rangeIndexTo = to;
    }

    private int getIndexByPosition(float x, float y) {
        Rect boundingBox = element.getBoundingBox();
        ControlElement.Range range = element.getRange();
        float offset = element.getOrientation() == 0 ? x - boundingBox.left - currentOffset : y - boundingBox.top - currentOffset;
        int index = (int)Math.floor((offset / getElementSize()) % range.max);
        if (index < 0) index = range.max + index;
        return index;
    }

    private Binding getBindingByPosition(float x, float y) {
        int index = getIndexByPosition(x, y);
        int ordinal = element.getRange().ordinal();
        if (ordinal < 0 || ordinal >= RANGE_BINDINGS.length) return Binding.NONE;
        Binding[] arr = RANGE_BINDINGS[ordinal];
        if (index < 0 || index >= arr.length) return Binding.NONE;
        return arr[index];
    }

    private boolean isTap() {
        return (System.currentTimeMillis() - touchTime) < TouchpadView.MAX_TAP_MILLISECONDS;
    }

    public void handleTouchDown(float x, float y) {
        handler.removeCallbacks(tapRunnable);

        scrolling = false;
        isActionDown = true;
        binding = getBindingByPosition(x, y);
        pressedIndex = binding != Binding.NONE ? getIndexByPosition(x, y) : -1;
        touchTime = System.currentTimeMillis();
        lastPosition = element.getOrientation() == 0 ? x : y;
        element.setBinding(Binding.NONE);

        handler.postDelayed(tapRunnable, TouchpadView.MAX_TAP_MILLISECONDS);
    }

    public void handleTouchMove(float x, float y) {
        if (isActionDown) {
            float position = element.getOrientation() == 0 ? x : y;
            float deltaPosition = position - lastPosition;

            if (Math.abs(deltaPosition) >= TouchpadView.MAX_TAP_TRAVEL_DISTANCE) {
                scrolling = true;
                pressedIndex = -1;
                handler.removeCallbacks(tapRunnable);
            }

            if (scrolling) {
                currentOffset += deltaPosition;

                float scrollSize = getScrollSize();
                scrollOffset = -currentOffset % scrollSize;
                if (scrollOffset < 0) scrollOffset = scrollSize + scrollOffset;

                updateRangeIndex();
                lastPosition = position;
                inputControlsView.invalidate();
            }
        }
    }

    public void handleTouchUp() {
        if (isActionDown) {
            handler.removeCallbacks(tapRunnable);
            if (isTap() && !scrolling) {
                inputControlsView.handleInputEvent(binding, true);
                final Binding finalBinding = binding;
                inputControlsView.postDelayed(() -> inputControlsView.handleInputEvent(finalBinding, false), 30);
            }
            else inputControlsView.handleInputEvent(binding, false);
        }
        isActionDown = false;
        pressedIndex = -1;
    }
}
