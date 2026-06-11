package com.winlator.cmod.inputcontrols;

import com.winlator.cmod.winhandler.MouseEventFlags;
import com.winlator.cmod.winhandler.WinHandler;
import com.winlator.cmod.xserver.Pointer;
import com.winlator.cmod.xserver.XServer;

public class InputDispatcher {
    private final XServer xServer;
    private InputMode inputMode = InputMode.ABSOLUTE;

    public InputDispatcher(XServer xServer) {
        this.xServer = xServer;
    }

    public void setInputMode(InputMode mode) {
        this.inputMode = mode;
    }

    public InputMode getInputMode() {
        return inputMode;
    }

    public void dispatchPointerButton(Pointer.Button button, boolean pressed) {
        WinHandler wh = xServer.getWinHandler();
        if (inputMode == InputMode.RELATIVE && wh != null) {
            wh.mouseEvent(MouseEventFlags.getFlagFor(button, pressed), 0, 0, 0);
        } else {
            if (pressed) xServer.injectPointerButtonPress(button);
            else xServer.injectPointerButtonRelease(button);
        }
    }

    public void dispatchPointerButton(Pointer.Button button, boolean pressed, int wheelDelta) {
        WinHandler wh = xServer.getWinHandler();
        if (inputMode == InputMode.RELATIVE && wh != null) {
            wh.mouseEvent(MouseEventFlags.getFlagFor(button, pressed), 0, 0, wheelDelta);
        } else {
            if (pressed) xServer.injectPointerButtonPress(button);
            else xServer.injectPointerButtonRelease(button);
        }
    }

    public void dispatchPointerMove(int x, int y) {
        WinHandler wh = xServer.getWinHandler();
        if (inputMode == InputMode.RELATIVE && wh != null) {
            wh.mouseEvent(MouseEventFlags.MOVE, x, y, 0);
        } else {
            xServer.injectPointerMove(x, y);
        }
    }

    public void dispatchPointerMoveDelta(int dx, int dy) {
        xServer.injectPointerMoveDelta(dx, dy);
    }

    public void dispatchScroll(float scrollY) {
        WinHandler wh = xServer.getWinHandler();
        if (scrollY <= -1.0f) {
            if (inputMode == InputMode.RELATIVE && wh != null) {
                wh.mouseEvent(MouseEventFlags.WHEEL, 0, 0, (int) scrollY);
            } else {
                xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_DOWN);
                xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_DOWN);
            }
        } else if (scrollY >= 1.0f) {
            if (inputMode == InputMode.RELATIVE && wh != null) {
                wh.mouseEvent(MouseEventFlags.WHEEL, 0, 0, (int) scrollY);
            } else {
                xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_UP);
                xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_UP);
            }
        }
    }

    public void dispatchMouseEvent(int flags, int dx, int dy) {
        dispatchMouseEvent(flags, dx, dy, 0);
    }

    public void dispatchMouseEvent(int flags, int dx, int dy, int wheelDelta) {
        WinHandler wh = xServer.getWinHandler();
        if (wh != null) {
            wh.mouseEvent(flags, dx, dy, wheelDelta);
        }
    }

    public void dispatchKeyPress(com.winlator.cmod.xserver.XKeycode keycode) {
        xServer.injectKeyPress(keycode);
    }

    public void dispatchKeyRelease(com.winlator.cmod.xserver.XKeycode keycode) {
        xServer.injectKeyRelease(keycode);
    }
}
