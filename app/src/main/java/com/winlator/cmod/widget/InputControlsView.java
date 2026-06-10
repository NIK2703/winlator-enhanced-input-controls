package com.winlator.cmod.widget;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Point;
import android.graphics.PointF;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.Rect;
import android.os.Handler;
import android.os.Looper;

import android.util.Log;
import android.util.SparseArray;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.PointerIcon;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import androidx.preference.PreferenceManager;

import com.winlator.cmod.BuildConfig;
import com.winlator.cmod.R;
import com.winlator.cmod.inputcontrols.Binding;
import com.winlator.cmod.inputcontrols.ControlElement;
import com.winlator.cmod.inputcontrols.BindPackage;
import com.winlator.cmod.inputcontrols.InputMode;
import com.winlator.cmod.inputcontrols.ControlsProfile;
import com.winlator.cmod.inputcontrols.ExternalController;
import com.winlator.cmod.inputcontrols.NativeTouchProcessor;
import com.winlator.cmod.inputcontrols.ExternalControllerBinding;
import com.winlator.cmod.inputcontrols.GamepadState;
import com.winlator.cmod.math.Mathf;

import java.nio.ByteBuffer;
import com.winlator.cmod.winhandler.MouseEventFlags;
import com.winlator.cmod.winhandler.WinHandler;
import com.winlator.cmod.xserver.Pointer;
import com.winlator.cmod.xserver.XServer;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.List;
import java.util.Timer;
import java.util.TimerTask;

public class InputControlsView extends View {
    public static final float DEFAULT_OVERLAY_OPACITY = 0.4f;
    public static boolean skipDiskCache = false;
    private static final byte MOUSE_WHEEL_DELTA = 120;
    private boolean editMode = false;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final ColorFilter colorFilter = new PorterDuffColorFilter(0xffffffff, PorterDuff.Mode.SRC_IN);
    private final Point cursor = new Point();
    private boolean readyToDraw = false;
    private boolean moveCursor = false;
    private Runnable onEditActionListener;
    private int snappingSize;
    private float offsetX;
    private float offsetY;
    private ControlElement selectedElement;
    private ControlsProfile profile;
    private float overlayOpacity = DEFAULT_OVERLAY_OPACITY;
    private float cacheAlphaOverride = -1;
    private TouchpadView touchpadView;
    private XServer xServer;
    private NativeTouchProcessor nativeTouchProcessor;
    private com.winlator.cmod.renderer.ElementOverlayRenderer elementOverlayRenderer;
    private static final int VISUAL_STRIDE = 60;
    private static long visualThrottleMs = 0;
    private long lastInvalidateMs = 0;
    private final Bitmap[] icons = new Bitmap[40];
    private Timer mouseMoveTimer;
    private final PointF mouseMoveOffset = new PointF();
    private boolean showTouchscreenControls = true;
    private boolean cachesPreBuilt = false;
    private Handler timeoutHandler;
    private Runnable hideControlsRunnable;

    private SharedPreferences preferences;
    private boolean cachedRenderingEnabled;
    private final SharedPreferences.OnSharedPreferenceChangeListener prefListener = (prefs, key) -> {
        if ("cached_rendering".equals(key)) cachedRenderingEnabled = prefs.getBoolean(key, true);
    };

    public boolean isCachingEnabled() {
        return cachedRenderingEnabled;
    }

    private void initPreferences() {
        preferences = PreferenceManager.getDefaultSharedPreferences(getContext());
        cachedRenderingEnabled = preferences.getBoolean("cached_rendering", true);
        preferences.registerOnSharedPreferenceChangeListener(prefListener);
    }

    private ControlElement stickElement;

    private InputMode inputMode = InputMode.ABSOLUTE;
    private boolean focusOnStick = false;

    public void setInputMode(InputMode mode) {
        this.inputMode = mode;
    }

    public boolean isFocusedOnStick() {
        return focusOnStick;
    }

    public void setFocusOnStick(boolean focus) {
        this.focusOnStick = focus;
        invalidate();
    }

    @SuppressLint("ResourceType")
    public InputControlsView(Context context) {
        super(context);
        setClickable(true);
        setFocusable(true);
        setFocusableInTouchMode(true);
        requestFocus();
        setBackgroundColor(0x00000000);
        setPointerIcon(PointerIcon.load(getResources(), R.drawable.hidden_pointer_arrow));
        setLayoutParams(new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        initPreferences();
    }

    @SuppressLint("ResourceType")
    public InputControlsView(Context context, Handler timeoutHandler, Runnable hideControlsRunnable) {
        super(context);
        this.timeoutHandler = timeoutHandler;
        this.hideControlsRunnable = hideControlsRunnable;
        setClickable(true);
        setFocusable(true);
        setFocusableInTouchMode(true);
        requestFocus();
        setBackgroundColor(0x00000000);
        setPointerIcon(PointerIcon.load(getResources(), R.drawable.hidden_pointer_arrow));
        setLayoutParams(new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        initPreferences();
    }

    public InputControlsView(Context context, boolean focusOnStick) {
        super(context);
        setClickable(true);
        setFocusable(true);
        setFocusableInTouchMode(true);
        requestFocus();
        setBackgroundColor(0x00000000);
        setPointerIcon(PointerIcon.load(getResources(), R.drawable.hidden_pointer_arrow));

        if (focusOnStick) {
            setLayoutParams(new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        } else {
            setLayoutParams(new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        }

        initPreferences();
    }

    public void setEditMode(boolean editMode) {
        this.editMode = editMode;
    }

    private void scheduleOverlayRender() {
        if (elementOverlayRenderer != null && elementOverlayRenderer.isActive()) {
            elementOverlayRenderer.scheduleRender();
        }
    }

    public void setOverlayOpacity(float overlayOpacity) {
        this.overlayOpacity = overlayOpacity;
        if (profile != null) {
            for (ControlElement element : profile.getElements()) {
                element.invalidateElementCachesKeepDisk();
            }
        }
        ControlElement.clearSharedPool();
        cachesPreBuilt = false;
        invalidate();
        scheduleOverlayRender();
    }

    public void setProfileStrokeWidth(float strokeWidth) {
        if (profile != null) {
            profile.setStrokeWidth(strokeWidth);
            for (ControlElement element : profile.getElements()) {
                element.invalidateElementCachesKeepDisk();
            }
        }
        ControlElement.clearSharedPool();
        cachesPreBuilt = false;
        invalidate();
        scheduleOverlayRender();
    }

    public void setProfileFillAlphaInactive(int fillAlpha) {
        if (profile != null) {
            profile.setFillAlphaInactive(fillAlpha);
            for (ControlElement element : profile.getElements()) {
                element.invalidateElementCachesKeepDisk();
            }
        }
        ControlElement.clearSharedPool();
        cachesPreBuilt = false;
        invalidate();
        scheduleOverlayRender();
    }

    public void setProfileCornerRadius(float cornerRadius) {
        skipDiskCache = true;
        if (profile != null) {
            profile.setCornerRadius(cornerRadius);
            for (ControlElement element : profile.getElements()) {
                element.invalidateElementCachesKeepDisk();
            }
        }
        ControlElement.clearSharedPool();
        cachesPreBuilt = false;
        invalidate();
        scheduleOverlayRender();
    }

    public void invalidateCache() {
        if (profile != null) {
            for (ControlElement element : profile.getElements()) {
                element.invalidateElementCachesKeepDisk();
            }
        }
        ControlElement.clearSharedPool();
        cachesPreBuilt = false;
        invalidate();
        scheduleOverlayRender();
    }

    public int getSnappingSize() {
        return snappingSize;
    }

    public void setSnappingSize(int size) {
        this.snappingSize = size;
    }

    @Override
    protected synchronized void onDraw(Canvas canvas) {
        int width, height;

        if (stickElement != null && isFocusedOnStick()) {
            Rect boundingBox = stickElement.getBoundingBox();
            width = boundingBox.width();
            height = boundingBox.height();
        } else {
            width = getWidth();
            height = getHeight();
        }

        if (width == 0 || height == 0) {
            readyToDraw = false;
            return;
        }

        int newSnapSize = width / 100;
        if (newSnapSize != snappingSize) {
            snappingSize = newSnapSize;
            if (nativeTouchProcessor != null) {
                nativeTouchProcessor.setSnappingSize(snappingSize);
            }
        }
        readyToDraw = true;
        Log.w("Winlator_Controls", "onDraw: profile=" + (profile != null) + " showTouchscreenControls=" + showTouchscreenControls + " elements=" + (profile != null && profile.isElementsLoaded() ? profile.getElements().size() : "N/A") + " cachedRenderingEnabled=" + cachedRenderingEnabled);

        if (editMode) {
            drawGrid(canvas);
            drawCursor(canvas);
        }

        if (stickElement != null) {
            stickElement.draw(canvas);
        }

        if (profile != null && showTouchscreenControls && !isFocusedOnStick()) {
            if (!profile.isElementsLoaded()) profile.loadElements(this);
            if (cachedRenderingEnabled) {
                int renderedCount = 0;
                for (ControlElement element : profile.getElements()) {
                    try {
                        if (!cachesPreBuilt) element.buildCache();
                        element.drawCached(canvas);
                        renderedCount++;
                    } catch (Exception e) {
                        e.printStackTrace();
                    }
                }
                if (renderedCount > 0) cachesPreBuilt = true;
            }
            else {
                for (ControlElement element : profile.getElements()) {
                    try {
                        element.draw(canvas);
                    } catch (Exception e) {
                        e.printStackTrace();
                    }
                }
            }
            skipDiskCache = false;
        }

        super.onDraw(canvas);
    }

    public void resetStickPosition() {
        if (stickElement != null) {
            Rect boundingBox = stickElement.getBoundingBox();
            float centerX = boundingBox.centerX();
            float centerY = boundingBox.centerY();

            stickElement.setCurrentPosition(centerX, centerY);
            invalidate();
        }
    }

    public void initializeStickElement(float x, float y, float scale) {
        stickElement = new ControlElement(this);
        stickElement.setType(ControlElement.Type.STICK);
        stickElement.setX((int) x);
        stickElement.setY((int) y);
        stickElement.setScale(scale);
        invalidate();
    }

    public void updateStickPosition(float x, float y) {
        if (stickElement != null) {
            stickElement.getCurrentPosition().x = x;
            stickElement.getCurrentPosition().y = y;
            invalidate();
        }
    }

    public ControlElement getStickElement() {
        return stickElement;
    }

    private void drawGrid(Canvas canvas) {
        paint.setStyle(Paint.Style.FILL);
        paint.setStrokeWidth(snappingSize * 0.0625f);
        paint.setColor(0xff000000);
        canvas.drawColor(Color.BLACK);

        paint.setAntiAlias(false);
        paint.setColor(0xff303030);

        int width = getMaxWidth();
        int height = getMaxHeight();

        for (int i = 0; i < width; i += snappingSize) {
            canvas.drawLine(i, 0, i, height, paint);
            canvas.drawLine(0, i, width, i, paint);
        }

        float cx = Mathf.roundTo(width * 0.5f, snappingSize);
        float cy = Mathf.roundTo(height * 0.5f, snappingSize);
        paint.setColor(0xff424242);

        for (int i = 0; i < width; i += snappingSize * 2) {
            canvas.drawLine(cx, i, cx, i + snappingSize, paint);
            canvas.drawLine(i, cy, i + snappingSize, cy, paint);
        }

        paint.setAntiAlias(true);
    }

    private void drawCursor(Canvas canvas) {
        paint.setStyle(Paint.Style.FILL);
        paint.setStrokeWidth(snappingSize * 0.0625f);
        paint.setColor(0xffc62828);

        paint.setAntiAlias(false);
        canvas.drawLine(0, cursor.y, getMaxWidth(), cursor.y, paint);
        canvas.drawLine(cursor.x, 0, cursor.x, getMaxHeight(), paint);

        paint.setAntiAlias(true);
    }

    public synchronized boolean addElement() {
        if (editMode && profile != null) {
            ControlElement element = new ControlElement(this);
            element.setX(cursor.x);
            element.setY(cursor.y);
            profile.addElement(element);
            profile.save();
            invalidateCache();
            selectElement(element);
            if (onEditActionListener != null) onEditActionListener.run();
            return true;
        }
        else return false;
    }

    public synchronized boolean copyElement() {
        if (editMode && selectedElement != null && profile != null) {
            ControlElement element = new ControlElement(this);
            element.setType(selectedElement.getType());
            element.setBindingCount(selectedElement.getBindingCount());
            for (int i = 0; i < element.getBindingCount(); i++) {
                element.setSlotPackage(i, new BindPackage(selectedElement.getSlotPackage(i)));
            }
            element.setShape(selectedElement.getShape());
            element.setPassthroughTouch(selectedElement.isPassthroughTouch());
            element.setScale(selectedElement.getScale());
            element.setText(selectedElement.getText());
            element.setIconId(selectedElement.getIconId());
            element.setCustomIconData(selectedElement.getCustomIconData());
            if (selectedElement.getOpacity() >= 0) element.setOpacity(selectedElement.getOpacity());
            if (selectedElement.getRange() != null) element.setRange(selectedElement.getRange());
            element.setOrientation(selectedElement.getOrientation());
            element.setElementWidth(selectedElement.getElementWidth());
            element.setElementHeight(selectedElement.getElementHeight());
            element.setCornerRadius(selectedElement.getCornerRadius());
            element.setDpadCornerRadius(selectedElement.getDpadCornerRadius());
            element.setBindingPackage(ControlElement.BindingSection.LONG_PRESS, new BindPackage(selectedElement.getBindingPackage(ControlElement.BindingSection.LONG_PRESS)));
            element.setBindingPackage(ControlElement.BindingSection.GESTURE, new BindPackage(selectedElement.getBindingPackage(ControlElement.BindingSection.GESTURE)));
            element.setX(cursor.x);
            element.setY(cursor.y);
            profile.addElement(element);
            profile.save();
            invalidateCache();
            selectElement(element);
            if (onEditActionListener != null) onEditActionListener.run();
            return true;
        }
        else return false;
    }

    public synchronized boolean removeElement() {
        if (editMode && selectedElement != null && profile != null) {
            profile.removeElement(selectedElement);
            selectedElement = null;
            profile.save();
            invalidateCache();
            if (onEditActionListener != null) onEditActionListener.run();
            return true;
        }
        else return false;
    }

    public ControlElement getSelectedElement() {
        return selectedElement;
    }

    private synchronized void deselectAllElements() {
        selectedElement = null;
        if (profile != null) {
            for (ControlElement element : profile.getElements()) element.setSelected(false);
        }
    }

    private void selectElement(ControlElement element) {
        deselectAllElements();
        if (element != null) {
            selectedElement = element;
            selectedElement.setSelected(true);
        }
        invalidate();
    }

    public synchronized ControlsProfile getProfile() {
        return profile;
    }

    public synchronized void setProfile(ControlsProfile profile) {
        if (profile != null) {
            this.profile = profile;
            Log.w("Winlator_Controls", "setProfile: name=" + profile.getName() + " elements=" + (profile.isElementsLoaded() ? profile.getElements().size() : "not_loaded"));
            deselectAllElements();
        }
        else this.profile = null;
        cachesPreBuilt = false;
        invalidateCache();
    }

    public boolean isShowTouchscreenControls() {
        return showTouchscreenControls;
    }

    public void setOnEditActionListener(Runnable listener) {
        this.onEditActionListener = listener;
    }

    public void setShowTouchscreenControls(boolean showTouchscreenControls) {
        this.showTouchscreenControls = showTouchscreenControls;
        Log.w("Winlator_Controls", "setShowTouchscreenControls: " + showTouchscreenControls);
    }

    public float getOverlayOpacity() {
        return cacheAlphaOverride >= 0 ? cacheAlphaOverride : overlayOpacity;
    }

    public int getPrimaryColor() {
        float alpha = cacheAlphaOverride >= 0 ? cacheAlphaOverride : overlayOpacity;
        return Color.argb((int)(alpha * 255), 255, 255, 255);
    }

    public int getSecondaryColor() {
        float alpha = cacheAlphaOverride >= 0 ? cacheAlphaOverride : overlayOpacity;
        return Color.argb((int)(alpha * 255), 2, 119, 189);
    }

    public void setCacheAlphaOverride(float alpha) {
        this.cacheAlphaOverride = alpha;
    }

    private synchronized ControlElement intersectElement(float x, float y) {
        if (profile != null) {
            for (ControlElement element : profile.getElements()) {
                if (element.containsPoint(x, y)) return element;
            }
        }
        return null;
    }

    public Paint getPaint() {
        return paint;
    }

    public Path getPath() {
        return path;
    }

    public ColorFilter getColorFilter() {
        return colorFilter;
    }

    public TouchpadView getTouchpadView() {
        return touchpadView;
    }

    public void setTouchpadView(TouchpadView touchpadView) {
        this.touchpadView = touchpadView;
    }

    public void setNativeTouchProcessor(NativeTouchProcessor p) {
        this.nativeTouchProcessor = p;
    }

    public void setElementOverlayRenderer(com.winlator.cmod.renderer.ElementOverlayRenderer r) {
        this.elementOverlayRenderer = r;
        if (r != null) {
            r.setRenderCallback(canvas -> renderToOverlay(canvas));
        }
    }

    private void syncVisualStates() {
        if (profile == null) return;
        if (visualThrottleMs == 0) {
            float refreshRate = 60.0f;
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) {
                android.view.Display display = getDisplay();
                if (display != null) refreshRate = display.getMode().getRefreshRate();
            } else {
                android.view.WindowManager wm = (android.view.WindowManager)getContext().getSystemService(Context.WINDOW_SERVICE);
                if (wm != null) refreshRate = wm.getDefaultDisplay().getRefreshRate();
            }
            visualThrottleMs = (long)(1000.0f / refreshRate);
            if (visualThrottleMs < 4) visualThrottleMs = 4;
        }
        long now = System.currentTimeMillis();
        boolean shouldInvalidate = (now - lastInvalidateMs) >= visualThrottleMs;
        if (!shouldInvalidate) return;
        List<ControlElement> elements = profile.getElements();
        int count = elements.size();
        if (count == 0) {
            lastInvalidateMs = now;
            invalidate();
            return;
        }
        float[] pos = nativeTouchProcessor.syncPositions;
        int[] st = nativeTouchProcessor.syncStates;
        float[] scroll = nativeTouchProcessor.syncScrollOffsets;
        int n = nativeTouchProcessor.syncVisualState(pos, st, scroll);
        if (n > count) n = count;
        int activeCount = 0;
        for (int i = 0; i < n; i++) {
            ControlElement e = elements.get(i);
            int flags = st[i];
            if ((flags & 1) != 0) activeCount++;
            e.syncVisualState(
                (flags & 1) != 0,
                pos[i*2], pos[i*2+1],
                (flags & 2) != 0,
                (flags & 4) != 0,
                (flags & 8) != 0,
                (flags & 16) != 0,
                scroll[i]
            );
        }
        Log.w("Winlator_Controls", "syncVisualStates: synced=" + n + " active=" + activeCount);
        lastInvalidateMs = now;
        invalidate();
    }

    public void tick(long timeMs) {
        Log.w("Winlator_Controls", "tick: start timeMs=" + timeMs);
        if (nativeTouchProcessor != null) {
            nativeTouchProcessor.tick(timeMs);
            syncVisualStates();
        }
        if (elementOverlayRenderer != null && elementOverlayRenderer.isActive()) {
            int w = getWidth();
            int h = getHeight();
            if (w > 0 && h > 0) {
                elementOverlayRenderer.setViewSize(w, h);
            }
        }
    }

    public void renderToOverlay(Canvas canvas) {
        if (profile != null && showTouchscreenControls && !isFocusedOnStick()) {
            if (!profile.isElementsLoaded()) profile.loadElements(this);
            // Snapshot via toArray() to avoid ConcurrentModificationException
            // from UI thread modifications during background render
            Object[] snapshot = profile.getElements().toArray();
            if (cachedRenderingEnabled) {
                for (Object obj : snapshot) {
                    ControlElement element = (ControlElement) obj;
                    if (!cachesPreBuilt) element.buildCache();
                    element.drawCached(canvas);
                }
                cachesPreBuilt = true;
            }
            else {
                for (Object obj : snapshot) {
                    ((ControlElement) obj).draw(canvas);
                }
            }
        }
    }

    public XServer getXServer() {
        return xServer;
    }

    public void setXServer(XServer xServer) {
        this.xServer = xServer;
    }

    public int getMaxWidth() {
        return (int)Mathf.roundTo(getWidth(), snappingSize);
    }

    @Override
    protected void onDetachedFromWindow() {
        stopMouseMove();
        super.onDetachedFromWindow();
    }

    public int getMaxHeight() {
        return (int)Mathf.roundTo(getHeight(), snappingSize);
    }

    private void createMouseMoveTimer() {
        if (mouseMoveTimer != null) return;
        WinHandler winHandler = xServer != null ? xServer.getWinHandler() : null;
        if (winHandler == null) return;
        final float cursorSpeed = profile != null ? profile.getCursorSpeed() : 1.0f;
        mouseMoveTimer = new Timer();
        mouseMoveTimer.schedule(new TimerTask() {
            @Override
            public void run() {
                PointF offset;
                synchronized (InputControlsView.this) {
                    offset = new PointF(mouseMoveOffset.x, mouseMoveOffset.y);
                }
                if (offset.x != 0 || offset.y != 0) {
                    if (inputMode == InputMode.RELATIVE) {
                        WinHandler wh = xServer != null ? xServer.getWinHandler() : null;
                        if (wh != null)
                            wh.mouseEvent(MouseEventFlags.MOVE, (int) (offset.x * cursorSpeed * 10), (int) (offset.y * cursorSpeed * 10), 0);
                    } else if (xServer != null)
                        xServer.injectPointerMoveDelta(
                            (int) (offset.x * cursorSpeed * 10),
                            (int) (offset.y * cursorSpeed * 10)
                    );
                }
            }
        }, 0, 1000 / 60);
    }

    public void startMouseMove(int dx, int dy, boolean hold) {
        mouseMoveOffset.x = dx;
        mouseMoveOffset.y = dy;
        createMouseMoveTimer();
    }

    public void stopMouseMove() {
        mouseMoveOffset.x = 0;
        mouseMoveOffset.y = 0;
        if (mouseMoveTimer != null) {
            mouseMoveTimer.cancel();
            mouseMoveTimer = null;
        }
    }

    private void processJoystickInput(ExternalController controller) {
        final int[] axes = {
                MotionEvent.AXIS_X, MotionEvent.AXIS_Y,
                MotionEvent.AXIS_Z, MotionEvent.AXIS_RZ,
                MotionEvent.AXIS_HAT_X, MotionEvent.AXIS_HAT_Y
        };
        final float[] values = {
                controller.state.thumbLX, controller.state.thumbLY,
                controller.state.thumbRX, controller.state.thumbRY,
                controller.state.getDPadX(), controller.state.getDPadY()
        };

        for (int i = 0; i < axes.length; i++) {
            float value = values[i];
            if (Math.abs(value) > ControlElement.STICK_DEAD_ZONE) {
                byte sign = Mathf.sign(value);
                int keyCode = ExternalControllerBinding.getKeyCodeForAxis(axes[i], sign);
                ExternalControllerBinding controllerBinding = controller.getControllerBinding(keyCode);
                if (controllerBinding != null) {
                    handleInputEvent(controller, controllerBinding.getBinding(), true, value, false);
                }
            } else {
                for (byte sign = -1; sign <= 1; sign += 2) {
                    int keyCode = ExternalControllerBinding.getKeyCodeForAxis(axes[i], sign);
                    ExternalControllerBinding controllerBinding = controller.getControllerBinding(keyCode);
                    if (controllerBinding != null) {
                        handleInputEvent(controller, controllerBinding.getBinding(), false, value, false);
                    }
                }
            }
        }

        processTriggerInput(controller, controller.state.triggerL, KeyEvent.KEYCODE_BUTTON_L2, false);
        processTriggerInput(controller, controller.state.triggerR, KeyEvent.KEYCODE_BUTTON_R2, false);

        WinHandler winHandler = xServer != null ? xServer.getWinHandler() : null;
        if (winHandler != null) {
            winHandler.sendGamepadState(controller);
        }
    }

    private void processTriggerInput(ExternalController controller, float value, int keyCode, boolean sendUpdate) {
        ExternalControllerBinding binding = controller.getControllerBinding(keyCode);
        if (binding != null) {
            boolean isPressed = value > ControlElement.STICK_DEAD_ZONE;
            if (isPressed) {
                handleInputEvent(controller, binding.getBinding(), true, value, sendUpdate);
            } else {
                handleInputEvent(controller, binding.getBinding(), false, 0, sendUpdate);
            }
        }
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        if (!editMode && profile != null) {
            ExternalController controller = profile.getController(event.getDeviceId());

            if (controller != null && controller.updateStateFromMotionEvent(event)) {
                processJoystickInput(controller);

                return true;
            }
        }

        return super.onGenericMotionEvent(event);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        resetTouchscreenTimeout();
        {
            int actionIndex = event.getActionIndex();
            int pointerId = event.getPointerId(actionIndex);
            Log.w("Winlator_Controls", "onTouchEvent: action=" + event.getActionMasked() + " pointerId=" + pointerId + " x=" + event.getX(actionIndex) + " y=" + event.getY(actionIndex));
        }

        // Route through native processor
        if (nativeTouchProcessor != null && !editMode) {
            int action = event.getActionMasked();
            int actionIndex = event.getActionIndex();
            int pointerId = event.getPointerId(actionIndex);
            switch (action) {
                case MotionEvent.ACTION_DOWN:
                case MotionEvent.ACTION_POINTER_DOWN: {
                    float x = event.getX(actionIndex);
                    float y = event.getY(actionIndex);
                    nativeTouchProcessor.onFingerDown(pointerId, x, y, event.getEventTime());
                    syncVisualStates();
                    if (elementOverlayRenderer != null && elementOverlayRenderer.isActive()) {
                        elementOverlayRenderer.scheduleRender();
                    }
                    return true;
                }
                case MotionEvent.ACTION_MOVE: {
                    int historySize = event.getHistorySize();
                    for (int h = 0; h < historySize; h++) {
                        for (int i = 0; i < event.getPointerCount(); i++) {
                            int pid = event.getPointerId(i);
                            nativeTouchProcessor.onFingerMove(pid, event.getHistoricalX(i, h), event.getHistoricalY(i, h), event.getHistoricalEventTime(h));
                        }
                    }
                    for (int i = 0; i < event.getPointerCount(); i++) {
                        int pid = event.getPointerId(i);
                        nativeTouchProcessor.onFingerMove(pid, event.getX(i), event.getY(i), event.getEventTime());
                    }
                    syncVisualStates();
                    if (elementOverlayRenderer != null && elementOverlayRenderer.isActive()) {
                        elementOverlayRenderer.scheduleRender();
                    }
                    return true;
                }
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_POINTER_UP: {
                    float x = event.getX(actionIndex);
                    float y = event.getY(actionIndex);
                    nativeTouchProcessor.onFingerUp(pointerId, x, y, event.getEventTime());
                    syncVisualStates();
                    if (elementOverlayRenderer != null && elementOverlayRenderer.isActive()) {
                        elementOverlayRenderer.scheduleRender();
                    }
                    return true;
                }
                case MotionEvent.ACTION_CANCEL: {
                    nativeTouchProcessor.reset();
                    invalidate();
                    if (elementOverlayRenderer != null && elementOverlayRenderer.isActive()) {
                        elementOverlayRenderer.scheduleRender();
                    }
                    return true;
                }
            }
            return true;
        }

        if (editMode && readyToDraw) {
            switch (event.getAction()) {
                case MotionEvent.ACTION_DOWN: {
                    float x = event.getX();
                    float y = event.getY();
                    ControlElement element = intersectElement(x, y);
                    moveCursor = true;
                    if (element != null) {
                        offsetX = x - element.getX();
                        offsetY = y - element.getY();
                        moveCursor = false;
                        selectElement(element);
                    }
                    else {
                        deselectAllElements();
                    }
                    break;
                }
                case MotionEvent.ACTION_MOVE: {
                    if (selectedElement != null) {
                        selectedElement.setX((int) Mathf.roundTo(event.getX() - offsetX, snappingSize));
                        selectedElement.setY((int) Mathf.roundTo(event.getY() - offsetY, snappingSize));
                        invalidate();
                    }
                    break;
                }
                case MotionEvent.ACTION_UP: {
                    if (selectedElement != null && profile != null) {
                        profile.save();
                        if (onEditActionListener != null) onEditActionListener.run();
                    }
                    if (moveCursor) cursor.set((int) Mathf.roundTo(event.getX(), snappingSize), (int) Mathf.roundTo(event.getY(), snappingSize));
                    invalidate();
                    break;
                }
            }
        }

        return true;
    }

    private void resetTouchscreenTimeout() {

        if (timeoutHandler != null && hideControlsRunnable != null) {
            timeoutHandler.removeCallbacks(hideControlsRunnable);
            timeoutHandler.postDelayed(hideControlsRunnable, 5000);
        }
    }

    public boolean onKeyEvent(KeyEvent event) {
        if (profile != null && event.getRepeatCount() == 0) {
            ExternalController controller = profile.getController(event.getDeviceId());
            
            if (controller != null) {
                ExternalControllerBinding controllerBinding = controller.getControllerBinding(event.getKeyCode());
                
                if (controllerBinding != null) {
                    int action = event.getAction();

                    if (action == KeyEvent.ACTION_DOWN) {
                        handleInputEvent(controller, controllerBinding.getBinding(), true);
                    }
                    else if (action == KeyEvent.ACTION_UP) {
                        handleInputEvent(controller, controllerBinding.getBinding(), false);
                    }
                    return true;
                }
            }
        }
        return false;
    }

    public void handleInputEvent(Binding binding, boolean isActionDown) {
        handleInputEvent(null, binding, isActionDown, 0);
    }

    public void handleInputEvent(ExternalController controller, Binding binding, boolean isActionDown) {
        handleInputEvent(controller, binding, isActionDown, 0);
    }

    /**
     * Handle stick input with proper 2D axis management.
     * Use this for analog sticks to avoid per-direction axis conflicts.
     */
    public void handleStickInput(Binding firstBinding, float deltaX, float deltaY) {
        if (!firstBinding.isGamepad()) return;
        
        GamepadState state = profile.getGamepadState();
        WinHandler winHandler = xServer != null ? xServer.getWinHandler() : null;
        
        boolean isLeftStick = firstBinding == Binding.GAMEPAD_LEFT_THUMB_UP || 
                             firstBinding == Binding.GAMEPAD_LEFT_THUMB_DOWN ||
                             firstBinding == Binding.GAMEPAD_LEFT_THUMB_LEFT ||
                             firstBinding == Binding.GAMEPAD_LEFT_THUMB_RIGHT;
        
        if (isLeftStick) {
            state.thumbLX = deltaX;
            state.thumbLY = deltaY;
        } else {
            state.thumbRX = deltaX;
            state.thumbRY = deltaY;
        }
        
        if (winHandler != null) {
            winHandler.sendGamepadState();
        }
    }

    public void handleInputEvent(Binding binding, boolean isActionDown, float offset) {
        handleInputEvent(null, binding, isActionDown, offset);
    }

    public void handleInputEvent(ExternalController controller, Binding binding, boolean isActionDown, float offset) {
        handleInputEvent(controller, binding, isActionDown, offset, true);
    }

    public void handleInputEvent(ExternalController controller, Binding binding, boolean isActionDown, float offset, boolean sendUpdate) {
        WinHandler winHandler = xServer != null ? xServer.getWinHandler() : null;
        if (binding.isGamepad()) {
            GamepadState state = (controller != null) ? controller.remappedState : profile.getGamepadState();

            int buttonIdx = binding.ordinal() - Binding.GAMEPAD_BUTTON_A.ordinal();
            if (buttonIdx <= ExternalController.IDX_BUTTON_R2) {
                if (buttonIdx == ExternalController.IDX_BUTTON_L2)
                    state.triggerL = isActionDown ? (offset != 0 ? offset : 1.0f) : 0f;
                else if (buttonIdx == ExternalController.IDX_BUTTON_R2)
                    state.triggerR = isActionDown ? (offset != 0 ? offset : 1.0f) : 0f;
                else
                    state.setPressed(buttonIdx, isActionDown);
            }
            else if (binding == Binding.GAMEPAD_LEFT_THUMB_UP || binding == Binding.GAMEPAD_LEFT_THUMB_DOWN) {
                float val = (isActionDown && offset == 0) ? 1.0f : Math.abs(offset);
                state.thumbLY = isActionDown ? (binding == Binding.GAMEPAD_LEFT_THUMB_UP ? -val : val) : 0;
            }
            else if (binding == Binding.GAMEPAD_LEFT_THUMB_LEFT || binding == Binding.GAMEPAD_LEFT_THUMB_RIGHT) {
                float val = (isActionDown && offset == 0) ? 1.0f : Math.abs(offset);
                state.thumbLX = isActionDown ? (binding == Binding.GAMEPAD_LEFT_THUMB_LEFT ? -val : val) : 0;
            }
            else if (binding == Binding.GAMEPAD_RIGHT_THUMB_UP || binding == Binding.GAMEPAD_RIGHT_THUMB_DOWN) {
                float val = (isActionDown && offset == 0) ? 1.0f : Math.abs(offset);
                state.thumbRY = isActionDown ? (binding == Binding.GAMEPAD_RIGHT_THUMB_UP ? -val : val) : 0;
            }
            else if (binding == Binding.GAMEPAD_RIGHT_THUMB_LEFT || binding == Binding.GAMEPAD_RIGHT_THUMB_RIGHT) {
                float val = (isActionDown && offset == 0) ? 1.0f : Math.abs(offset);
                state.thumbRX = isActionDown ? (binding == Binding.GAMEPAD_RIGHT_THUMB_LEFT ? -val : val) : 0;
            }
            else if (binding == Binding.GAMEPAD_DPAD_UP || binding == Binding.GAMEPAD_DPAD_RIGHT ||
                     binding == Binding.GAMEPAD_DPAD_DOWN || binding == Binding.GAMEPAD_DPAD_LEFT) {
                state.dpad[binding.ordinal() - Binding.GAMEPAD_DPAD_UP.ordinal()] = isActionDown;
            }

            if (winHandler != null && sendUpdate) {
                if (controller != null)
                    winHandler.sendGamepadState(controller);
                else
                    winHandler.sendGamepadState();
            }
        }
        else {
            if (binding == Binding.MOUSE_MOVE_LEFT || binding == Binding.MOUSE_MOVE_RIGHT) {
                mouseMoveOffset.x = isActionDown ? (offset != 0 ? offset : (binding == Binding.MOUSE_MOVE_LEFT ? -1 : 1)) : 0;
                if (isActionDown) createMouseMoveTimer();
            }
            else if (binding == Binding.MOUSE_MOVE_DOWN || binding == Binding.MOUSE_MOVE_UP) {
                mouseMoveOffset.y = isActionDown ? (offset != 0 ? offset : (binding == Binding.MOUSE_MOVE_UP ? -1 : 1)) : 0;
                if (isActionDown) createMouseMoveTimer();
            }
            else {
                Pointer.Button pointerButton = binding.getPointerButton();
                if (isActionDown) {
                    if (pointerButton != null) {
                        if (inputMode == InputMode.RELATIVE) {
                            int wheelDelta = pointerButton == Pointer.Button.BUTTON_SCROLL_UP ? MOUSE_WHEEL_DELTA : (pointerButton == Pointer.Button.BUTTON_SCROLL_DOWN ? -MOUSE_WHEEL_DELTA : 0);
                            winHandler.mouseEvent(MouseEventFlags.getFlagFor(pointerButton, true), 0, 0, wheelDelta);
                        } else {
                            xServer.injectPointerButtonPress(pointerButton);
                        }
                    }
                    else xServer.injectKeyPress(binding.keycode);
                }
                else {
                    if (pointerButton != null) {
                        if (inputMode == InputMode.RELATIVE) {
                            winHandler.mouseEvent(MouseEventFlags.getFlagFor(pointerButton, false), 0, 0, 0);
                        } else {
                            xServer.injectPointerButtonRelease(pointerButton);
                        }
                    }
                    else xServer.injectKeyRelease(binding.keycode);
                }
            }
        }
    }

    public Bitmap getIcon(byte id) {
        if (id < 0 || id >= icons.length) return null;
        if (icons[id] == null) {
            Context context = getContext();
            try (InputStream is = context.getAssets().open("inputcontrols/icons/"+id+".png")) {
                icons[id] = BitmapFactory.decodeStream(is);
            }
            catch (IOException e) {}
        }
        return icons[id];
    }
}
