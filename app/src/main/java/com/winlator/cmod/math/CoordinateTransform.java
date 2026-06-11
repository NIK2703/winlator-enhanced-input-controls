package com.winlator.cmod.math;

public class CoordinateTransform {
    public float scaleX = 1.0f;
    public float scaleY = 1.0f;
    public float offsetX;
    public float offsetY;

    public CoordinateTransform compute(int outerW, int outerH, int innerW, int innerH, boolean fullscreen) {
        if (!fullscreen) {
            float aspect = Math.min((float) outerW / innerW, (float) outerH / innerH);
            scaleX = 1.0f / aspect;
            scaleY = 1.0f / aspect;
            offsetX = (outerW - innerW * aspect) / 2.0f;
            offsetY = (outerH - innerH * aspect) / 2.0f;
        } else {
            scaleX = (float) innerW / outerW;
            scaleY = (float) innerH / outerH;
            offsetX = 0;
            offsetY = 0;
        }
        return this;
    }

    public float[] applyToXform(float[] xform) {
        XForm.set(xform, offsetX, offsetY, scaleX, scaleY);
        return xform;
    }

    public void applyToNativeProcessor(com.winlator.cmod.inputcontrols.NativeTouchProcessor ntp) {
        if (ntp == null) return;
        ntp.setXformScale(scaleX, scaleY);
        ntp.setViewOffset(offsetX, offsetY);
    }

    public void applyToNativeConfig(com.winlator.cmod.inputcontrols.NativeTouchProcessor.NativeConfig config) {
        config.xformScaleX = scaleX;
        config.xformScaleY = scaleY;
        config.viewOffsetX = offsetX;
        config.viewOffsetY = offsetY;
    }
}
