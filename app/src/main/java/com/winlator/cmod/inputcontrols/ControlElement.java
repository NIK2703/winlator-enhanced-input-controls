package com.winlator.cmod.inputcontrols;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PointF;
import android.graphics.Rect;
import android.os.SystemClock;

import androidx.core.graphics.ColorUtils;

import com.winlator.cmod.core.CubicBezierInterpolator;
import com.winlator.cmod.inputcontrols.InputMode;
import com.winlator.cmod.math.Mathf;
import com.winlator.cmod.widget.InputControlsView;
import com.winlator.cmod.widget.TouchpadView;
import com.winlator.cmod.winhandler.MouseEventFlags;
import com.winlator.cmod.xserver.XServer;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

public class ControlElement {
    public static final float STICK_DEAD_ZONE = 0.15f;
    public static final float DPAD_DEAD_ZONE = 0.3f;
    public static final float STICK_SENSITIVITY = 2.0f;
    public static final float TRACKPAD_MIN_SPEED = 0.8f;
    public static final float TRACKPAD_MAX_SPEED = 20.0f;
    public static final byte TRACKPAD_ACCELERATION_THRESHOLD = 4;
    public static final short BUTTON_MIN_TIME_TO_KEEP_PRESSED = 300;
    public enum Type {
        BUTTON, D_PAD, RANGE_BUTTON, STICK, TRACKPAD;

        public static String[] names() {
            Type[] types = values();
            String[] names = new String[types.length];
            for (int i = 0; i < types.length; i++) names[i] = types[i].name().replace("_", "-");
            return names;
        }
    }
    public enum Shape {
        CIRCLE, RECT, ROUND_RECT, SQUARE;

        public static String[] names() {
            Shape[] shapes = values();
            String[] names = new String[shapes.length];
            for (int i = 0; i < shapes.length; i++) names[i] = shapes[i].name().replace("_", " ");
            return names;
        }
    }
    public enum Range {
        FROM_A_TO_Z(26), FROM_0_TO_9(10), FROM_F1_TO_F12(12), FROM_NP0_TO_NP9(10);
        public final byte max;

        Range(int max) {
            this.max = (byte)max;
        }

        public static String[] names() {
            Range[] ranges = values();
            String[] names = new String[ranges.length];
            for (int i = 0; i < ranges.length; i++) names[i] = ranges[i].name().replace("_", " ");
            return names;
        }
    }
    private final InputControlsView inputControlsView;
    private Type type = Type.BUTTON;
    private Shape shape = Shape.CIRCLE;
    private List<List<Binding>> bindings = new ArrayList<>();
    private float scale = 1.0f;
    private short x;
    private short y;
    private boolean selected = false;
    private boolean toggleSwitch = false;
    private int currentPointerId = -1;
    private final Rect boundingBox = new Rect();
    private boolean[] states = new boolean[4];
    private boolean boundingBoxNeedsUpdate = true;
    private String text = "";
    private byte iconId;
    private List<Binding> heldBindings;
    private List<Binding> longPressBindings = new ArrayList<>();
    private boolean longPressTriggered;
    private List<Binding> gestureBindings = new ArrayList<>();
    private boolean gestureTriggered;

    private float gestureDownX;

    private float gestureDownY;
    private boolean active;
    private android.os.Handler longPressHandler;
    private Range range;
    private byte orientation;
    private PointF currentPosition;
    private RangeScroller scroller;
    private CubicBezierInterpolator interpolator;
    private Object touchTime;

    public ControlElement(InputControlsView inputControlsView) {
        this.inputControlsView = inputControlsView;
        for (int i = 0; i < 4; i++) {
            bindings.add(new ArrayList<Binding>());
        }
    }

    private void reset() {
        for (List<Binding> seq : bindings) {
            seq.clear();
        }
        scroller = null;

        if (type == Type.STICK) {
            bindings.get(0).add(Binding.KEY_W);
            bindings.get(1).add(Binding.KEY_D);
            bindings.get(2).add(Binding.KEY_S);
            bindings.get(3).add(Binding.KEY_A);
        }
        else if(type == Type.D_PAD){
            bindings.get(0).add(Binding.GAMEPAD_DPAD_UP);
            bindings.get(1).add(Binding.GAMEPAD_DPAD_RIGHT);
            bindings.get(2).add(Binding.GAMEPAD_DPAD_DOWN);
            bindings.get(3).add(Binding.GAMEPAD_DPAD_LEFT);
        }
        else if (type == Type.TRACKPAD) {
            bindings.get(0).add(Binding.GAMEPAD_RIGHT_THUMB_UP);
            bindings.get(1).add(Binding.GAMEPAD_RIGHT_THUMB_RIGHT);
            bindings.get(2).add(Binding.GAMEPAD_RIGHT_THUMB_DOWN);
            bindings.get(3).add(Binding.GAMEPAD_RIGHT_THUMB_LEFT);
        }
        else if (type == Type.RANGE_BUTTON) {
            scroller = new RangeScroller(inputControlsView, this);
        }

        text = "";
        iconId = 0;
        range = null;
        boundingBoxNeedsUpdate = true;
    }

    public Type getType() {
        return type;
    }

    public void setType(Type type) {
        if (this.type == type) return;
        this.type = type;
        reset();
    }

    public int getBindingCount() {
        return bindings.size();
    }

    public void setBindingCount(int bindingCount) {
        while (bindings.size() < bindingCount) {
            List<Binding> seq = new ArrayList<>();
            seq.add(Binding.NONE);
            bindings.add(seq);
        }
        while (bindings.size() > bindingCount) {
            bindings.remove(bindings.size() - 1);
        }
        states = new boolean[bindingCount];
        boundingBoxNeedsUpdate = true;
    }

    public Shape getShape() {
        return shape;
    }

    public void setShape(Shape shape) {
        this.shape = shape;
        boundingBoxNeedsUpdate = true;
    }

    public Range getRange() {
        return range != null ? range : Range.FROM_A_TO_Z;
    }

    public void setRange(Range range) {
        this.range = range;
    }

    public byte getOrientation() {
        return orientation;
    }

    public void setOrientation(byte orientation) {
        this.orientation = orientation;
        boundingBoxNeedsUpdate = true;
    }

    public boolean isToggleSwitch() {
        return toggleSwitch;
    }

    public void setToggleSwitch(boolean toggleSwitch) {
        this.toggleSwitch = toggleSwitch;
    }

    public Binding getBindingAt(int index) {
        if (index >= bindings.size()) return Binding.NONE;
        List<Binding> seq = bindings.get(index);
        return seq.isEmpty() ? Binding.NONE : seq.get(0);
    }

    public void setBindingAt(int index, Binding binding) {
        while (index >= bindings.size()) {
            bindings.add(new ArrayList<Binding>());
        }
        List<Binding> seq = bindings.get(index);
        seq.clear();
        seq.add(binding);
    }

    public void setBinding(Binding binding) {
        for (List<Binding> seq : bindings) {
            seq.clear();
            seq.add(binding);
        }
    }

    public List<Binding> getBindingSequence(int index) {
        if (index >= bindings.size()) return Collections.emptyList();
        return Collections.unmodifiableList(bindings.get(index));
    }

    public void setBindingSequence(int index, List<Binding> sequence) {
        while (index >= bindings.size()) {
            bindings.add(new ArrayList<Binding>());
        }
        List<Binding> seq = bindings.get(index);
        seq.clear();
        seq.addAll(sequence);
    }

    public void addBindingToSequence(int index, Binding binding) {
        while (index >= bindings.size()) {
            bindings.add(new ArrayList<Binding>());
        }
        bindings.get(index).add(binding);
    }

    public void removeBindingFromSequence(int index, int seqIndex) {
        if (index < bindings.size()) {
            List<Binding> seq = bindings.get(index);
            if (seqIndex >= 0 && seqIndex < seq.size()) {
                seq.remove(seqIndex);
            }
        }
    }

    public void setBindingAtSequenceIndex(int slotIndex, int seqIndex, Binding binding) {
        if (slotIndex < bindings.size()) {
            List<Binding> seq = bindings.get(slotIndex);
            if (seqIndex >= 0 && seqIndex < seq.size()) {
                seq.set(seqIndex, binding);
            }
        }
    }

    public List<Binding> getLongPressBindings() {
        return longPressBindings;
    }

    public void setLongPressBindings(List<Binding> bindings) {
        longPressBindings.clear();
        longPressBindings.addAll(bindings);
    }

    public void addLongPressBinding(Binding binding) {
        longPressBindings.add(binding);
    }

    public void removeLongPressBinding(int index) {
        if (index >= 0 && index < longPressBindings.size()) {
            longPressBindings.remove(index);
        }
    }

    public boolean hasLongPressBinding() {
        if (longPressBindings == null || longPressBindings.isEmpty()) return false;
        if (longPressBindings.size() == 1 && longPressBindings.get(0) == Binding.NONE) return false;
        return true;
    }

    public List<Binding> getGestureBindings() {
        return gestureBindings;
    }

    public void setGestureBindings(List<Binding> bindings) {
        gestureBindings.clear();
        gestureBindings.addAll(bindings);
    }

    public void addGestureBinding(Binding binding) {
        gestureBindings.add(binding);
    }

    public void removeGestureBinding(int index) {
        if (index >= 0 && index < gestureBindings.size()) {
            gestureBindings.remove(index);
        }
    }

    public boolean hasGestureBinding() {
        if (gestureBindings == null || gestureBindings.isEmpty()) return false;
        if (gestureBindings.size() == 1 && gestureBindings.get(0) == Binding.NONE) return false;
        return true;
    }

    public void setGestureDownPosition(float x, float y) {
        gestureDownX = x;
        gestureDownY = y;
    }

    public boolean isGestureTriggered() {
        return gestureTriggered;
    }

    public boolean isLongPressTriggered() {
        return longPressTriggered;
    }

    public float getScale() {
        return scale;
    }

    public void setScale(float scale) {
        this.scale = scale;
        boundingBoxNeedsUpdate = true;
    }

    public short getX() {
        return x;
    }

    public void setX(int x) {
        this.x = (short)x;
        boundingBoxNeedsUpdate = true;
    }

    public short getY() {
        return y;
    }

    public void setY(int y) {
        this.y = (short)y;
        boundingBoxNeedsUpdate = true;
    }

    public boolean isSelected() {
        return selected;
    }

    public void setSelected(boolean selected) {
        this.selected = selected;
    }

    public String getText() {
        return text;
    }

    public void setText(String text) {
        this.text = text != null ? text : "";
    }

    public byte getIconId() {
        return iconId;
    }

    public void setIconId(int iconId) {
        this.iconId = (byte)iconId;
    }

    public Rect getBoundingBox() {
        if (boundingBoxNeedsUpdate) computeBoundingBox();
        return boundingBox;
    }

    private Rect computeBoundingBox() {
        int snappingSize = inputControlsView.getSnappingSize();
        int halfWidth = 0;
        int halfHeight = 0;

        switch (type) {
            case BUTTON:
                switch (shape) {
                    case RECT:
                    case ROUND_RECT:
                        halfWidth = snappingSize * 4;
                        halfHeight = snappingSize * 2;
                        break;
                    case SQUARE:
                        halfWidth = (int)(snappingSize * 2.5f);
                        halfHeight = (int)(snappingSize * 2.5f);
                        break;
                    case CIRCLE:
                        halfWidth = snappingSize * 3;
                        halfHeight = snappingSize * 3;
                        break;
                }
                break;
            case D_PAD: {
                halfWidth = snappingSize * 7;
                halfHeight = snappingSize * 7;
                break;
            }
            case TRACKPAD:
            case STICK: {
                halfWidth = snappingSize * 6;
                halfHeight = snappingSize * 6;
                break;
            }
            case RANGE_BUTTON: {
                halfWidth = snappingSize * ((bindings.size() * 4) / 2);
                halfHeight = snappingSize * 2;

                if (orientation == 1) {
                    int tmp = halfWidth;
                    halfWidth = halfHeight;
                    halfHeight = tmp;
                }
                break;
            }
        }

        halfWidth *= scale;
        halfHeight *= scale;
        boundingBox.set(x - halfWidth, y - halfHeight, x + halfWidth, y + halfHeight);
        boundingBoxNeedsUpdate = false;
        return boundingBox;
    }



    private String getDisplayText() {
        if (text != null && !text.isEmpty()) {
            return text;
        }
        else {
            Binding binding = getBindingAt(0);
            String text = binding.toString().replace("NUMPAD ", "NP").replace("BUTTON ", "");
            if (text.length() > 7) {
                String[] parts = text.split(" ");
                StringBuilder sb = new StringBuilder();
                for (String part : parts) sb.append(part.charAt(0));
                return (binding.isMouse() ? "M" : "")+ sb;
            }
            else return text;
        }
    }

    private static float getTextSizeForWidth(Paint paint, String text, float desiredWidth) {
        final byte testTextSize = 48;
        paint.setTextSize(testTextSize);
        return testTextSize * desiredWidth / paint.measureText(text);
    }

    private static String getRangeTextForIndex(Range range, int index) {
        String text = "";
        switch (range) {
            case FROM_A_TO_Z:
                text = String.valueOf((char)(65 + index));
                break;
            case FROM_0_TO_9:
                text = String.valueOf((index + 1) % 10);
                break;
            case FROM_F1_TO_F12:
                text = "F"+(index + 1);
                break;
            case FROM_NP0_TO_NP9:
                text = "NP"+((index + 1) % 10);
                break;
        }
        return text;
    }

    private boolean isEngaged() {
        return currentPointerId != -1 || active || (toggleSwitch && selected);
    }

    public void draw(Canvas canvas) {
        int snappingSize = inputControlsView.getSnappingSize();
        Paint paint = inputControlsView.getPaint();
        int primaryColor = inputControlsView.getPrimaryColor();

        paint.setColor(selected ? inputControlsView.getSecondaryColor() : primaryColor);
        paint.setStyle(Paint.Style.STROKE);
        float strokeWidth = snappingSize * 0.2f;
        paint.setStrokeWidth(strokeWidth);
        Rect boundingBox = getBoundingBox();
        boolean engaged = isEngaged();
        int fillAlpha = engaged ? 80 : 0;
        int fillColor = ColorUtils.setAlphaComponent(primaryColor, fillAlpha);

        switch (type) {
            case BUTTON: {
                float cx = boundingBox.centerX();
                float cy = boundingBox.centerY();

                if (engaged) {
                    paint.setStyle(Paint.Style.FILL);
                    paint.setColor(fillColor);
                    switch (shape) {
                        case CIRCLE:
                            canvas.drawCircle(cx, cy, boundingBox.width() * 0.5f, paint);
                            break;
                        case RECT:
                            canvas.drawRect(boundingBox, paint);
                            break;
                        case ROUND_RECT: {
                            float r = boundingBox.height() * 0.5f;
                            canvas.drawRoundRect(boundingBox.left, boundingBox.top, boundingBox.right, boundingBox.bottom, r, r, paint);
                            break;
                        }
                        case SQUARE: {
                            float r = snappingSize * 0.75f * scale;
                            canvas.drawRoundRect(boundingBox.left, boundingBox.top, boundingBox.right, boundingBox.bottom, r, r, paint);
                            break;
                        }
                    }
                    paint.setStyle(Paint.Style.STROKE);
                    paint.setColor(selected ? inputControlsView.getSecondaryColor() : primaryColor);
                    paint.setStrokeWidth(strokeWidth);
                }
                switch (shape) {
                    case CIRCLE:
                        canvas.drawCircle(cx, cy, boundingBox.width() * 0.5f, paint);
                        break;
                    case RECT:
                        canvas.drawRect(boundingBox, paint);
                        break;
                    case ROUND_RECT: {
                        float radius = boundingBox.height() * 0.5f;
                        canvas.drawRoundRect(boundingBox.left, boundingBox.top, boundingBox.right, boundingBox.bottom, radius, radius, paint);
                        break;
                    }
                    case SQUARE: {
                        float radius = snappingSize * 0.75f * scale;
                        canvas.drawRoundRect(boundingBox.left, boundingBox.top, boundingBox.right, boundingBox.bottom, radius, radius, paint);
                        break;
                    }
                }

                if (iconId > 0) {
                    drawIcon(canvas, cx, cy, boundingBox.width(), boundingBox.height(), iconId);
                }
                else {
                    String text = getDisplayText();
                    paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, boundingBox.width() - strokeWidth * 2), snappingSize * 2 * scale));
                    paint.setTextAlign(Paint.Align.CENTER);
                    paint.setStyle(Paint.Style.FILL);
                    paint.setColor(primaryColor);
                    canvas.drawText(text, x, (y - ((paint.descent() + paint.ascent()) * 0.5f)), paint);
                }
                break;
            }
            case D_PAD: {
                float cx = boundingBox.centerX();
                float cy = boundingBox.centerY();
                float offsetX = snappingSize * 2 * scale;
                float offsetY = snappingSize * 3 * scale;
                float start = snappingSize * scale;

                Path upPath = new Path();
                upPath.moveTo(cx, cy - start);
                upPath.lineTo(cx - offsetX, cy - offsetY);
                upPath.lineTo(cx - offsetX, boundingBox.top);
                upPath.lineTo(cx + offsetX, boundingBox.top);
                upPath.lineTo(cx + offsetX, cy - offsetY);
                upPath.close();

                Path rightPath = new Path();
                rightPath.moveTo(cx + start, cy);
                rightPath.lineTo(cx + offsetY, cy - offsetX);
                rightPath.lineTo(boundingBox.right, cy - offsetX);
                rightPath.lineTo(boundingBox.right, cy + offsetX);
                rightPath.lineTo(cx + offsetY, cy + offsetX);
                rightPath.close();

                Path downPath = new Path();
                downPath.moveTo(cx, cy + start);
                downPath.lineTo(cx - offsetX, cy + offsetY);
                downPath.lineTo(cx - offsetX, boundingBox.bottom);
                downPath.lineTo(cx + offsetX, boundingBox.bottom);
                downPath.lineTo(cx + offsetX, cy + offsetY);
                downPath.close();

                Path leftPath = new Path();
                leftPath.moveTo(cx - start, cy);
                leftPath.lineTo(cx - offsetY, cy - offsetX);
                leftPath.lineTo(boundingBox.left, cy - offsetX);
                leftPath.lineTo(boundingBox.left, cy + offsetX);
                leftPath.lineTo(cx - offsetY, cy + offsetX);
                leftPath.close();

                canvas.drawPath(upPath, paint);
                canvas.drawPath(rightPath, paint);
                canvas.drawPath(downPath, paint);
                canvas.drawPath(leftPath, paint);

                paint.setStyle(Paint.Style.FILL);
                paint.setColor(fillColor);
                if (states[0]) canvas.drawPath(upPath, paint);
                if (states[1]) canvas.drawPath(rightPath, paint);
                if (states[2]) canvas.drawPath(downPath, paint);
                if (states[3]) canvas.drawPath(leftPath, paint);
                paint.setStyle(Paint.Style.STROKE);
                paint.setColor(selected ? inputControlsView.getSecondaryColor() : primaryColor);
                paint.setStrokeWidth(strokeWidth);
                break;
            }
            case RANGE_BUTTON: {
                Range range = getRange();
                int oldColor = paint.getColor();
                float radius = snappingSize * 0.75f * scale;
                float elementSize = scroller.getElementSize();
                float minTextSize = snappingSize * 2 * scale;
                float scrollOffset = scroller.getScrollOffset();
                byte[] rangeIndex = scroller.getRangeIndex();
                Path path = inputControlsView.getPath();
                path.reset();

                if (orientation == 0) {
                    float lineTop = boundingBox.top + strokeWidth * 0.5f;
                    float lineBottom = boundingBox.bottom - strokeWidth * 0.5f;
                    float startX = boundingBox.left;
                    canvas.drawRoundRect(startX, boundingBox.top, boundingBox.right, boundingBox.bottom, radius, radius, paint);

                    canvas.save();
                    path.addRoundRect(startX, boundingBox.top, boundingBox.right, boundingBox.bottom, radius, radius, Path.Direction.CW);
                    canvas.clipPath(path);
                    startX -= scrollOffset % elementSize;

                    for (byte i = rangeIndex[0]; i < rangeIndex[1]; i++) {
                        int index = i % range.max;
                        paint.setStyle(Paint.Style.STROKE);
                        paint.setColor(oldColor);

                        if (startX > boundingBox.left && startX  < boundingBox.right) canvas.drawLine(startX, lineTop, startX, lineBottom, paint);
                        String text = getRangeTextForIndex(range, index);

                        if (startX < boundingBox.right && startX + elementSize > boundingBox.left) {
                            paint.setStyle(Paint.Style.FILL);
                            paint.setColor(primaryColor);
                            paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, elementSize - strokeWidth * 2), minTextSize));
                            paint.setTextAlign(Paint.Align.CENTER);
                            canvas.drawText(text, startX + elementSize * 0.5f, (y - ((paint.descent() + paint.ascent()) * 0.5f)), paint);
                        }
                        startX += elementSize;
                    }

                    paint.setStyle(Paint.Style.STROKE);
                    paint.setColor(oldColor);
                    canvas.restore();
                }
                else {
                    float lineLeft = boundingBox.left + strokeWidth * 0.5f;
                    float lineRight = boundingBox.right - strokeWidth * 0.5f;
                    float startY = boundingBox.top;
                    canvas.drawRoundRect(boundingBox.left, startY, boundingBox.right, boundingBox.bottom, radius, radius, paint);

                    canvas.save();
                    path.addRoundRect(boundingBox.left, startY, boundingBox.right, boundingBox.bottom, radius, radius, Path.Direction.CW);
                    canvas.clipPath(inputControlsView.getPath());
                    startY -= scrollOffset % elementSize;

                    for (byte i = rangeIndex[0]; i < rangeIndex[1]; i++) {
                        paint.setStyle(Paint.Style.STROKE);
                        paint.setColor(oldColor);

                        if (startY > boundingBox.top && startY < boundingBox.bottom) canvas.drawLine(lineLeft, startY, lineRight, startY, paint);
                        String text = getRangeTextForIndex(range, i);

                        if (startY < boundingBox.bottom && startY + elementSize > boundingBox.top) {
                            paint.setStyle(Paint.Style.FILL);
                            paint.setColor(primaryColor);
                            paint.setTextSize(Math.min(getTextSizeForWidth(paint, text, boundingBox.width() - strokeWidth * 2), minTextSize));
                            paint.setTextAlign(Paint.Align.CENTER);
                            canvas.drawText(text, x, startY + elementSize * 0.5f - ((paint.descent() + paint.ascent()) * 0.5f), paint);
                        }
                        startY += elementSize;
                    }

                    paint.setStyle(Paint.Style.STROKE);
                    paint.setColor(oldColor);
                    canvas.restore();
                }
                break;
            }
            case STICK: {
                int cx = boundingBox.centerX();  // Fixed outer circle center
                int cy = boundingBox.centerY();  // Fixed outer circle center
                int oldColor = paint.getColor();

                canvas.drawCircle(cx, cy, boundingBox.height() * 0.5f, paint);

                float thumbstickX = getCurrentPosition().x;
                float thumbstickY = getCurrentPosition().y;

                short thumbRadius = (short) (snappingSize * 3.5f * scale); // Radius of the thumbstick
                paint.setStyle(Paint.Style.FILL);
                paint.setColor(ColorUtils.setAlphaComponent(primaryColor, engaged ? 120 : 50));
                canvas.drawCircle(thumbstickX, thumbstickY, thumbRadius, paint); // Draw thumbstick

                paint.setStyle(Paint.Style.STROKE);
                paint.setColor(oldColor);
                canvas.drawCircle(thumbstickX, thumbstickY, thumbRadius + strokeWidth * 0.5f, paint);
                break;
            }

            case TRACKPAD: {
                float radius = boundingBox.height() * 0.15f;
                canvas.drawRoundRect(boundingBox.left, boundingBox.top, boundingBox.right, boundingBox.bottom, radius, radius, paint);
                float offset = strokeWidth * 2.5f;
                float innerStrokeWidth = strokeWidth * 2;
                float innerHeight = boundingBox.height() - offset * 2;
                radius = (innerHeight / boundingBox.height()) * radius - (innerStrokeWidth * 0.5f + strokeWidth * 0.5f);
                paint.setStrokeWidth(innerStrokeWidth);
                canvas.drawRoundRect(boundingBox.left + offset, boundingBox.top + offset, boundingBox.right - offset, boundingBox.bottom - offset, radius, radius, paint);
                break;
            }
        }
    }

    private void drawIcon(Canvas canvas, float cx, float cy, float width, float height, int iconId) {
        Paint paint = inputControlsView.getPaint();
        Bitmap icon = inputControlsView.getIcon((byte)iconId);
        paint.setColorFilter(inputControlsView.getColorFilter());
        int margin = (int)(inputControlsView.getSnappingSize() * (shape == Shape.CIRCLE || shape == Shape.SQUARE ? 2.0f : 1.0f) * scale);
        int halfSize = (int)((Math.min(width, height) - margin) * 0.5f);

        Rect srcRect = new Rect(0, 0, icon.getWidth(), icon.getHeight());
        Rect dstRect = new Rect((int)(cx - halfSize), (int)(cy - halfSize), (int)(cx + halfSize), (int)(cy + halfSize));
        canvas.drawBitmap(icon, srcRect, dstRect, paint);
        paint.setColorFilter(null);
    }

    public JSONObject toJSONObject() {
        try {
            JSONObject elementJSONObject = new JSONObject();
            elementJSONObject.put("type", type.name());
            elementJSONObject.put("shape", shape.name());

            JSONArray bindingsJSONArray = new JSONArray();
            for (List<Binding> seq : bindings) {
                JSONArray seqArray = new JSONArray();
                for (Binding b : seq) {
                    if (b != null && b != Binding.NONE) seqArray.put(b.name());
                }
                bindingsJSONArray.put(seqArray);
            }

            elementJSONObject.put("bindings", bindingsJSONArray);
            elementJSONObject.put("scale", Float.valueOf(scale));
            elementJSONObject.put("x", (float)x / inputControlsView.getMaxWidth());
            elementJSONObject.put("y", (float)y / inputControlsView.getMaxHeight());
            elementJSONObject.put("toggleSwitch", toggleSwitch);
            elementJSONObject.put("text", text);
            elementJSONObject.put("iconId", iconId);

            if (hasLongPressBinding()) {
                JSONArray lpArray = new JSONArray();
                for (Binding b : longPressBindings) {
                    if (b != null && b != Binding.NONE) lpArray.put(b.name());
                }
                elementJSONObject.put("longPressBindings", lpArray);
            }

            if (hasGestureBinding()) {
                JSONArray gArray = new JSONArray();
                for (Binding b : gestureBindings) {
                    if (b != null && b != Binding.NONE) gArray.put(b.name());
                }
                elementJSONObject.put("gestureBindings", gArray);
            }

            if (type == Type.RANGE_BUTTON && range != null) {
                elementJSONObject.put("range", range.name());
                if (orientation != 0) elementJSONObject.put("orientation", orientation);
            }
            return elementJSONObject;
        }
        catch (JSONException e) {
            return null;
        }
    }

    public boolean containsPoint(float x, float y) {
        return getBoundingBox().contains((int)(x + 0.5f), (int)(y + 0.5f));
    }

    private boolean isKeepButtonPressedAfterMinTime() {
        Binding binding = getBindingAt(0);
        return !toggleSwitch && (binding == Binding.GAMEPAD_BUTTON_L3 || binding == Binding.GAMEPAD_BUTTON_R3);
    }

    private void pressBindings(List<Binding> seq) {
        ControlsProfile profile = inputControlsView.getProfile();
        int delay = profile != null ? profile.getBindingDelay() : 0;
        int size = seq.size();

        for (int i = 0; i < size; i++) {
            Binding b = seq.get(i);
            if (b == null || b == Binding.NONE) continue;
            Binding kb = b.toKeyboardBinding();
            if (kb != null) inputControlsView.handleInputEvent(kb, true);
        }

        for (int i = 0; i < size; i++) {
            Binding b = seq.get(i);
            if (b == null || b == Binding.NONE) continue;
            if (b.isModifier()) continue;
            inputControlsView.handleInputEvent(b, true);
            if (delay > 0 && i < size - 1) SystemClock.sleep(delay);
        }
    }

    private void releaseHeldBindings() {
        if (heldBindings == null) return;
        int size = heldBindings.size();

        for (int i = size - 1; i >= 0; i--) {
            Binding b = heldBindings.get(i);
            if (b == null || b == Binding.NONE) continue;
            if (b.isModifier()) continue;
            inputControlsView.handleInputEvent(b, false);
        }

        for (int i = size - 1; i >= 0; i--) {
            Binding b = heldBindings.get(i);
            if (b == null || b == Binding.NONE) continue;
            Binding kb = b.toKeyboardBinding();
            if (kb != null) inputControlsView.handleInputEvent(kb, false);
        }

        heldBindings = null;
    }

    public void activate() {
        if (bindings.isEmpty() || bindings.get(0).isEmpty()) return;
        active = true;
        heldBindings = new ArrayList<>(bindings.get(0));
        pressBindings(bindings.get(0));
        inputControlsView.invalidate();
    }

    public void startLongPressTimer(int delay) {
        if (!hasLongPressBinding() || toggleSwitch || delay <= 0) return;
        if (longPressHandler == null) longPressHandler = new android.os.Handler(android.os.Looper.getMainLooper());
        longPressTriggered = false;
        longPressHandler.postDelayed(() -> {
            longPressTriggered = true;
            android.os.Vibrator vib = (android.os.Vibrator) inputControlsView.getContext().getSystemService(Context.VIBRATOR_SERVICE);
            if (vib != null && vib.hasVibrator()) {
                if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.Q) {
                    try {
                        vib.vibrate(android.os.VibrationEffect.createPredefined(android.os.VibrationEffect.EFFECT_HEAVY_CLICK));
                    } catch (IllegalArgumentException e) {
                        vib.vibrate(android.os.VibrationEffect.createOneShot(50, 255));
                    }
                } else {
                    vib.vibrate(50);
                }
            }
            heldBindings = new ArrayList<>(longPressBindings);
            pressBindings(longPressBindings);
            inputControlsView.invalidate();
        }, delay);
    }

    public void deactivate() {
        active = false;
        currentPointerId = -1;
        cancelLongPress();
        releaseHeldBindings();
        inputControlsView.invalidate();
    }

    public void releaseTapBindings() {
        active = false;
        releaseHeldBindings();
        inputControlsView.invalidate();
    }

    public void cancelLongPress() {
        if (longPressHandler != null) {
            longPressHandler.removeCallbacksAndMessages(null);
            longPressHandler = null;
        }
        currentPointerId = -1;
        longPressTriggered = false;
        gestureTriggered = false;
    }

    public boolean handleTouchDown(int pointerId, float x, float y) {
        if (currentPointerId == -1 && containsPoint(x, y)) {
            currentPointerId = pointerId;
            if (type == Type.BUTTON) {
                gestureDownX = x;
                gestureDownY = y;
                if (isKeepButtonPressedAfterMinTime()) touchTime = System.currentTimeMillis();
                if (toggleSwitch && selected) {
                    releaseHeldBindings();
                }
                else if (hasLongPressBinding() && !toggleSwitch) {
                    longPressTriggered = false;
                    ControlsProfile localProfile = inputControlsView.getProfile();
                    int delay = localProfile != null ? localProfile.getLongPressDelay() : 0;
                    if (delay > 0) {
                        if (longPressHandler == null) longPressHandler = new android.os.Handler(android.os.Looper.getMainLooper());
                        longPressHandler.postDelayed(() -> {
                            longPressTriggered = true;
                            android.os.Vibrator vib = (android.os.Vibrator) inputControlsView.getContext().getSystemService(Context.VIBRATOR_SERVICE);
                            if (vib != null && vib.hasVibrator()) {
                                if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.Q) {
                                    try {
                                        vib.vibrate(android.os.VibrationEffect.createPredefined(android.os.VibrationEffect.EFFECT_HEAVY_CLICK));
                                    } catch (IllegalArgumentException e) {
                                        vib.vibrate(android.os.VibrationEffect.createOneShot(50, 255));
                                    }
                                } else {
                                    vib.vibrate(50);
                                }
                            }
                            heldBindings = new ArrayList<>(longPressBindings);
                            pressBindings(longPressBindings);
                            inputControlsView.invalidate();
                        }, delay);
                    }
                }
                else {
                    heldBindings = bindings.get(0);
                    pressBindings(bindings.get(0));
                }
                inputControlsView.invalidate();
                return true;
            }
            else if (type == Type.RANGE_BUTTON) {
                scroller.handleTouchDown(x, y);
                return true;
            }
            else {
                if (type == Type.TRACKPAD) {
                    if (currentPosition == null) currentPosition = new PointF();
                    currentPosition.set(x, y);
                }
                return handleTouchMove(pointerId, x, y);
            }
        }
        else return false;
    }

    public boolean handleTouchMove(int pointerId, float x, float y) {
        if (pointerId == currentPointerId) {
            if (type == Type.BUTTON && hasGestureBinding() && !gestureTriggered) {
                float dx = x - gestureDownX;
                float dy = y - gestureDownY;
                ControlsProfile p = inputControlsView.getProfile();
                int threshold = p != null ? p.getGestureThreshold() : 20;
                float distSq = dx * dx + dy * dy;
                if (distSq > threshold * threshold) {
                    gestureTriggered = true;
                    if (longPressHandler != null) {
                        longPressHandler.removeCallbacksAndMessages(null);
                        longPressHandler = null;
                    }
                    longPressTriggered = false;
                    android.os.Vibrator vib = (android.os.Vibrator) inputControlsView.getContext().getSystemService(Context.VIBRATOR_SERVICE);
                    if (vib != null && vib.hasVibrator()) {
                        vib.vibrate(android.os.VibrationEffect.createOneShot(20, 200));
                    }
                    heldBindings = new ArrayList<>(gestureBindings);
                    pressBindings(gestureBindings);
                    inputControlsView.invalidate();
                    return true;
                }
            }
            if (type == Type.BUTTON && hasLongPressBinding() && !longPressTriggered && longPressHandler != null && !containsPoint(x, y)) {
                longPressHandler.removeCallbacksAndMessages(null);
                longPressHandler = null;
                longPressTriggered = false;
            }
            if (type == Type.D_PAD || type == Type.STICK || type == Type.TRACKPAD) {
            float deltaX, deltaY;
            Rect boundingBox = getBoundingBox();
            float radius = boundingBox.width() * 0.5f;
            TouchpadView touchpadView =  inputControlsView.getTouchpadView();

            if (type == Type.TRACKPAD) {
                if (currentPosition == null) currentPosition = new PointF();
                float[] deltaPoint = touchpadView.computeDeltaPoint(currentPosition.x, currentPosition.y, x, y);
                deltaX = deltaPoint[0];
                deltaY = deltaPoint[1];
                currentPosition.set(x, y);
            }
            else {
                float localX = x - boundingBox.left;
                float localY = y - boundingBox.top;
                float offsetX = localX - radius;
                float offsetY = localY - radius;

                float distance = Mathf.lengthSq(radius - localX, radius - localY);
                if (distance > radius * radius) {
                    float angle = (float)Math.atan2(offsetY, offsetX);
                    offsetX = (float)(Math.cos(angle) * radius);
                    offsetY = (float)(Math.sin(angle) * radius);
                }

                deltaX = Mathf.clamp(offsetX / radius, -1, 1);
                deltaY = Mathf.clamp(offsetY / radius, -1, 1);
            }

            if (type == Type.STICK) {
                if (currentPosition == null) currentPosition = new PointF();
                currentPosition.x = boundingBox.left + deltaX * radius + radius;
                currentPosition.y = boundingBox.top + deltaY * radius + radius;
                
                Binding firstBinding = getBindingAt(0);
                if (firstBinding.isGamepad()) {
                    float magnitude = (float)Math.sqrt(deltaX * deltaX + deltaY * deltaY);
                    
                    float finalX = 0;
                    float finalY = 0;
                    
                    if (magnitude > STICK_DEAD_ZONE) {
                        float normalizedX = deltaX / magnitude;
                        float normalizedY = deltaY / magnitude;
                        
                        float scaledMagnitude = Math.max(0, magnitude - 0.01f) * STICK_SENSITIVITY;
                        scaledMagnitude = Math.min(scaledMagnitude, 1.0f);
                        
                        finalX = normalizedX * scaledMagnitude;
                        finalY = normalizedY * scaledMagnitude;
                    }
                    
                    inputControlsView.handleStickInput(firstBinding, finalX, finalY);
                    
                    for (byte i = 0; i < 4; i++) {
                        this.states[i] = true;
                    }
                } else {
                    final boolean[] states = {deltaY <= -STICK_DEAD_ZONE, deltaX >= STICK_DEAD_ZONE, deltaY >= STICK_DEAD_ZONE, deltaX <= -STICK_DEAD_ZONE};
                    for (byte i = 0; i < 4; i++) {
                        float value = i == 1 || i == 3 ? deltaX : deltaY;
                        List<Binding> seq = bindings.get(i);
                        for (int j = 0, sz = seq.size(); j < sz; j++) {
                            Binding binding = seq.get(j);
                            boolean state = binding.isMouseMove() ? (states[i] || states[(i+2)%4]) : states[i];
                            inputControlsView.handleInputEvent(binding, state, value);
                            this.states[i] = state;
                        }
                    }
                }

                inputControlsView.invalidate();
            }
            else if (type == Type.TRACKPAD) {
                Binding firstBinding = getBindingAt(0);
                if (firstBinding.isGamepad()) {
                    if (interpolator == null) interpolator = new CubicBezierInterpolator();
                    interpolator.set(0.075f, 0.95f, 0.45f, 0.95f);
                    
                    float valueX = deltaX;
                    float valueY = deltaY;
                    if (Math.abs(valueX) > TRACKPAD_ACCELERATION_THRESHOLD) valueX *= STICK_SENSITIVITY;
                    if (Math.abs(valueY) > TRACKPAD_ACCELERATION_THRESHOLD) valueY *= STICK_SENSITIVITY;
                    
                    float interpX = interpolator.getInterpolation(Math.min(1.0f, Math.abs(valueX / TRACKPAD_MAX_SPEED)));
                    float interpY = interpolator.getInterpolation(Math.min(1.0f, Math.abs(valueY / TRACKPAD_MAX_SPEED)));
                    
                    float finalX = Mathf.clamp(interpX * Mathf.sign(valueX), -1, 1);
                    float finalY = Mathf.clamp(interpY * Mathf.sign(valueY), -1, 1);
                    
                    inputControlsView.handleStickInput(firstBinding, finalX, finalY);
                    
                    for (byte i = 0; i < 4; i++) {
                        this.states[i] = true;
                    }
                } else {
                    final boolean[] states = {deltaY <= -TRACKPAD_MIN_SPEED, deltaX >= TRACKPAD_MIN_SPEED, deltaY >= TRACKPAD_MIN_SPEED, deltaX <= -TRACKPAD_MIN_SPEED};
                    int cursorDx = 0;
                    int cursorDy = 0;

                    for (byte i = 0; i < 4; i++) {
                        float value = (i == 1 || i == 3 ? deltaX : deltaY);
                        if (Math.abs(value) > TouchpadView.CURSOR_ACCELERATION_THRESHOLD) value *= TouchpadView.CURSOR_ACCELERATION;
                        List<Binding> seq = bindings.get(i);
                        for (int j = 0, sz = seq.size(); j < sz; j++) {
                            Binding binding = seq.get(j);
                            if (binding == Binding.MOUSE_MOVE_LEFT || binding == Binding.MOUSE_MOVE_RIGHT) {
                                cursorDx = Mathf.roundPoint(value);
                            }
                            else if (binding == Binding.MOUSE_MOVE_UP || binding == Binding.MOUSE_MOVE_DOWN) {
                                cursorDy = Mathf.roundPoint(value);
                            }
                            else {
                                inputControlsView.handleInputEvent(binding, states[i], value);
                            }
                        }
                        this.states[i] = states[i];
                    }

                    if (cursorDx != 0 || cursorDy != 0)  {
                        XServer xServer = inputControlsView.getXServer();
                        if (xServer.getInputMode() == InputMode.RELATIVE)
                            xServer.getWinHandler().mouseEvent(MouseEventFlags.MOVE, cursorDx, cursorDy, 0);
                        else
                            inputControlsView.getXServer().injectPointerMoveDelta(cursorDx, cursorDy);
                    }
                }
            }
            else {
                final boolean[] states = {deltaY <= -DPAD_DEAD_ZONE, deltaX >= DPAD_DEAD_ZONE, deltaY >= DPAD_DEAD_ZONE, deltaX <= -DPAD_DEAD_ZONE};

                for (byte i = 0; i < 4; i++) {
                    float value = i == 1 || i == 3 ? deltaX : deltaY;
                    List<Binding> seq = bindings.get(i);
                    for (int j = 0, sz = seq.size(); j < sz; j++) {
                        Binding binding = seq.get(j);
                        boolean state = binding.isMouseMove() ? (states[i] || states[(i+2)%4]) : states[i];
                        inputControlsView.handleInputEvent(binding, state, value);
                        this.states[i] = state;
                    }
                }
                inputControlsView.invalidate();
                return true;
            }
            }
            if (type == Type.RANGE_BUTTON) {
                scroller.handleTouchMove(x, y);
                return true;
            }
        }
        return false;
    }

    public boolean handleTouchUp(int pointerId) {
        if (pointerId == currentPointerId) {
            if (type == Type.BUTTON) {
                if (isKeepButtonPressedAfterMinTime() && touchTime != null) {
                    selected = (System.currentTimeMillis() - (long)touchTime) > BUTTON_MIN_TIME_TO_KEEP_PRESSED;
                    touchTime = null;
                }

                if (toggleSwitch) {
                    selected = !selected;
                    if (selected) return true;
                }

                if (longPressTriggered) {
                    releaseHeldBindings();
                    longPressTriggered = false;
                }
                else if (gestureTriggered) {
                    releaseHeldBindings();
                    gestureTriggered = false;
                }
                else if (hasLongPressBinding()) {
                    if (longPressHandler != null) longPressHandler.removeCallbacksAndMessages(null);
                    List<Binding> seq = bindings.get(0);
                    heldBindings = new ArrayList<>(seq);
                    pressBindings(seq);
                    releaseHeldBindings();
                }
                else {
                    releaseHeldBindings();
                }
                inputControlsView.invalidate();
            }
            else if (type == Type.RANGE_BUTTON || type == Type.D_PAD || type == Type.STICK || type == Type.TRACKPAD) {
                for (byte i = 0; i < states.length; i++) {
                    if (states[i]) {
                        for (Binding b : bindings.get(i)) {
                            if (b != Binding.NONE) inputControlsView.handleInputEvent(b, false);
                        }
                    }
                    states[i] = false;
                }

                if (type == Type.RANGE_BUTTON) {
                    scroller.handleTouchUp();
                }
                else if (type == Type.STICK || type == Type.D_PAD) {
                    inputControlsView.invalidate();
                }

                if (currentPosition != null) currentPosition = null;
            }
            currentPointerId = -1;
            return true;
        }
        return false;
    }

    public PointF getCurrentPosition() {
        if (currentPosition == null) {
            currentPosition = new PointF(x, y); // Initialize to the center (same as outer circle)
        }
        return currentPosition;
    }

    public void setCurrentPosition(float x, float y) {
        if (currentPosition == null) {
            currentPosition = new PointF();
        }
        currentPosition.set(x, y);
        inputControlsView.invalidate();
    }
}
