package com.winlator.cmod.inputcontrols;

import android.graphics.Rect;

import com.winlator.cmod.widget.InputControlsView;

public class RangeScroller {
    private final InputControlsView inputControlsView;
    private final ControlElement element;
    private float scrollOffset;
    private float currentOffset;
    private float lastPosition;
    private int pressedIndex = -1;
    private boolean isActionDown = false;
    private boolean scrolling = false;
    private byte rangeIndexFrom;
    private byte rangeIndexTo;

    public RangeScroller(InputControlsView inputControlsView, ControlElement element) {
        this.inputControlsView = inputControlsView;
        this.element = element;
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
                currentOffset = -cScrollOffset;
                scrollOffset = cScrollOffset;
                lastPosition = element.getOrientation() == 0 ? x : y;
                updateRangeIndex();
            } else {
                float position = element.getOrientation() == 0 ? x : y;
                float deltaPosition = position - lastPosition;
                if (Math.abs(deltaPosition) >= 10) {
                    if (!scrolling) {
                        scrolling = true;
                        pressedIndex = -1;
                    }
                    currentOffset += deltaPosition;
                    float scrollSize = getScrollSize();
                    scrollOffset = -currentOffset % scrollSize;
                    if (scrollOffset < 0) scrollOffset = scrollSize + scrollOffset;
                    updateRangeIndex();
                }
                lastPosition = position;
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
        int from = (int)Math.floor((scrollOffset / getElementSize()) % range.max);
        if (from < 0) from = range.max + from;
        int to = from + element.getBindingCount();
        rangeIndexFrom = (byte)from;
        rangeIndexTo = (byte)to;
    }

    private int getIndexByPosition(float x, float y) {
        Rect boundingBox = element.getBoundingBox();
        ControlElement.Range range = element.getRange();
        float offset = element.getOrientation() == 0 ? x - boundingBox.left - currentOffset : y - boundingBox.top - currentOffset;
        int index = (int)Math.floor((offset / getElementSize()) % range.max);
        if (index < 0) index = range.max + index;
        return index;
    }

}
