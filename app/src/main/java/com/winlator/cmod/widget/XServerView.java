package com.winlator.cmod.widget;

import android.annotation.SuppressLint;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import androidx.annotation.NonNull;

import com.winlator.cmod.renderer.VulkanRenderer;
import com.winlator.cmod.xserver.XServer;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

@SuppressLint("ViewConstructor")
public class XServerView extends SurfaceView implements SurfaceHolder.Callback {
    private static final long HEARTBEAT_INTERVAL_MS = 3000;
    private static final long WATCHDOG_TIMEOUT_MS = 12000;
    private volatile long lastHeartbeatTime = System.nanoTime();
    private volatile boolean eventExecutorHung = false;
    private final Object executorLock = new Object();
    private volatile ExecutorService eventExecutor;
    private final Handler watchdogHandler = new Handler(Looper.getMainLooper());
    private final Runnable heartbeatRunnable = new Runnable() {
        @Override
        public void run() {
            try {
                if (!eventExecutor.isShutdown() && !eventExecutor.isTerminated()) {
                    eventExecutor.execute(() -> {
                        lastHeartbeatTime = System.nanoTime();
                    });
                }
            } catch (java.util.concurrent.RejectedExecutionException e) {
                // race: executor was shut down between check and execute
            } finally {
                watchdogHandler.postDelayed(this, HEARTBEAT_INTERVAL_MS);
            }
        }
    };

    private final VulkanRenderer renderer;

    public XServerView(Context context, XServer xServer) {
        super(context);
        setLayoutParams(new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        getHolder().addCallback(this);
        this.eventExecutor = createEventExecutor();
        renderer = new VulkanRenderer(this, xServer);
        watchdogHandler.postDelayed(heartbeatRunnable, HEARTBEAT_INTERVAL_MS);
    }

    private ExecutorService createEventExecutor() {
        ThreadPoolExecutor executor = new ThreadPoolExecutor(
            1, 1, 5, TimeUnit.SECONDS,
            new LinkedBlockingQueue<>(),
            r -> {
                Thread t = new Thread(r, "xserver-event-executor");
                t.setDaemon(true);
                return t;
            }
        );
        executor.allowCoreThreadTimeOut(true);
        return executor;
    }

    public VulkanRenderer getRenderer() {
        return renderer;
    }

    @Override
    public void surfaceCreated(@NonNull SurfaceHolder holder) {
        renderer.onSurfaceCreated(holder.getSurface());
    }

    @Override
    public void surfaceChanged(@NonNull SurfaceHolder holder, int format, int width, int height) {
        renderer.onSurfaceChanged(width, height);
    }

    @Override
    public void surfaceDestroyed(@NonNull SurfaceHolder holder) {
        renderer.onSurfaceDestroyed();
    }

    public void queueEvent(Runnable r) {
        if (eventExecutorHung) {
            synchronized (executorLock) {
                if (eventExecutorHung) {
                    eventExecutor.shutdownNow();
                    eventExecutor = createEventExecutor();
                    eventExecutorHung = false;
                }
            }
        }
        try {
            eventExecutor.execute(() -> {
                try {
                    r.run();
                } catch (Exception e) {
                }
            });
        } catch (java.util.concurrent.RejectedExecutionException e) {
            eventExecutorHung = true;
        }
    }

    public boolean isEventExecutorHealthy() {
        if (eventExecutorHung) return false;
        if (eventExecutor.isShutdown()) return false;
        long now = System.nanoTime();
        return (now - lastHeartbeatTime) < WATCHDOG_TIMEOUT_MS * 1_000_000L;
    }

    public void markEventExecutorHung() {
        eventExecutorHung = true;
    }

    public void restartEventExecutor() {
        synchronized (executorLock) {
            if (!eventExecutor.isShutdown()) {
                eventExecutor.shutdownNow();
            }
            eventExecutor = createEventExecutor();
            eventExecutorHung = false;
        }
    }

    public void stopHeartbeat() {
        watchdogHandler.removeCallbacks(heartbeatRunnable);
    }

    public void onPause() {
        stopHeartbeat();
    }

    public void onResume() {
        watchdogHandler.postDelayed(heartbeatRunnable, HEARTBEAT_INTERVAL_MS);
    }
}
