package com.winlator.cmod.inputcontrols;

import android.os.Handler;
import android.view.View;

public class TouchTimeoutManager {
    private static final int DEFAULT_TIMEOUT_MS = 5000;

    private final Handler handler;
    private final Runnable hideRunnable;
    private View targetView;

    public TouchTimeoutManager(Handler handler, Runnable onTimeout) {
        this.handler = handler;
        this.hideRunnable = onTimeout;
    }

    public void setTargetView(View targetView) {
        this.targetView = targetView;
    }

    public void reset() {
        reset(DEFAULT_TIMEOUT_MS);
    }

    public void reset(int timeoutMs) {
        if (handler != null && hideRunnable != null) {
            handler.removeCallbacks(hideRunnable);
            handler.postDelayed(hideRunnable, timeoutMs);
        }
    }

    public void cancel() {
        if (handler != null && hideRunnable != null) {
            handler.removeCallbacks(hideRunnable);
        }
    }

    public void setVisibility(boolean visible) {
        if (targetView != null) {
            targetView.setVisibility(visible ? View.VISIBLE : View.GONE);
        }
    }

    public boolean isVisible() {
        return targetView != null && targetView.getVisibility() == View.VISIBLE;
    }
}
