package com.winlator.cmod.renderer;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Message;
import android.util.Log;

import java.nio.ByteBuffer;

public class ElementOverlayRenderer {
    private static final String TAG = "Winlator_Overlay";
    private static final int MSG_RENDER = 1;
    private static final int MSG_SHUTDOWN = 2;

    private final VulkanRenderer vulkanRenderer;
    private final long vulkanHandle;
    private HandlerThread renderThread;
    private Handler renderHandler;
    private Bitmap overlayBitmap;
    private Canvas overlayCanvas;
    private ByteBuffer pixelBuffer;
    private int width;
    private int height;
    private boolean active = false;
    private volatile boolean needsRender = false;

    public interface RenderCallback {
        void renderOverlay(Canvas canvas);
    }

    private RenderCallback callback;

    public ElementOverlayRenderer(VulkanRenderer vulkanRenderer, long vulkanHandle) {
        this.vulkanRenderer = vulkanRenderer;
        this.vulkanHandle = vulkanHandle;
    }

    public void setRenderCallback(RenderCallback cb) {
        this.callback = cb;
    }

    public void start() {
        if (active) return;
        active = true;
        renderThread = new HandlerThread("ElementOverlay");
        renderThread.start();
        renderHandler = new Handler(renderThread.getLooper(), msg -> {
            switch (msg.what) {
                case MSG_RENDER:
                    doRender();
                    return true;
                case MSG_SHUTDOWN:
                    shutdownInternal();
                    return true;
            }
            return false;
        });
    }

    public void stop() {
        if (!active) return;
        active = false;
        if (renderHandler != null) {
            renderHandler.sendEmptyMessage(MSG_SHUTDOWN);
        }
    }

    public void scheduleRender() {
        if (renderHandler != null && active) {
            needsRender = true;
            renderHandler.removeMessages(MSG_RENDER);
            renderHandler.sendEmptyMessage(MSG_RENDER);
        }
    }

    public void setViewSize(int w, int h) {
        if (w == width && h == height && overlayBitmap != null) return;
        Log.d(TAG, "setViewSize: " + w + "x" + h + " (was " + width + "x" + height + ")");
        this.width = w;
        this.height = h;
        if (overlayBitmap != null) {
            overlayBitmap.recycle();
            overlayBitmap = null;
            overlayCanvas = null;
        }
        if (w > 0 && h > 0) {
            overlayBitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            overlayCanvas = new Canvas(overlayBitmap);
            int size = w * h * 4;
            if (pixelBuffer == null || pixelBuffer.capacity() < size) {
                pixelBuffer = ByteBuffer.allocateDirect(size);
                Log.d(TAG, "allocated pixelBuffer: " + size + " bytes");
            }
            Log.d(TAG, "overlay bitmap ready: " + w + "x" + h);
        }
    }

    public boolean isActive() {
        return active;
    }

    public boolean isReady() {
        return overlayCanvas != null;
    }

    private void doRender() {
        if (!needsRender) return;
        needsRender = false;
        if (overlayCanvas == null) {
            Log.w(TAG, "doRender: overlayCanvas null, skipping");
            return;
        }
        if (callback == null) {
            Log.w(TAG, "doRender: callback null, skipping");
            return;
        }

        // Clear to transparent
        overlayBitmap.eraseColor(0x00000000);

        // Invoke the callback to render elements onto the background canvas
        callback.renderOverlay(overlayCanvas);

        // Upload pixels to Vulkan
        uploadToVulkan();
    }

    private void uploadToVulkan() {
        if (overlayBitmap == null || pixelBuffer == null || vulkanHandle == 0) return;
        try {
            overlayBitmap.copyPixelsToBuffer(pixelBuffer);
            pixelBuffer.rewind();
            vulkanRenderer.updateElementOverlay(vulkanHandle, pixelBuffer, width, height);
        } catch (Exception e) {
            Log.e(TAG, "upload failed: " + e.getMessage());
        }
    }

    private void shutdownInternal() {
        if (overlayBitmap != null) {
            overlayBitmap.recycle();
            overlayBitmap = null;
            overlayCanvas = null;
        }
        pixelBuffer = null;
        if (renderThread != null) {
            renderThread.quitSafely();
            renderThread = null;
        }
        renderHandler = null;
    }
}
