package com.winlator.xr;

// Compile-time stub only: the real class ships inside the WinlatorXR APK.
public class XrActivity extends android.app.Activity {
    public static boolean isImmersive;
    public static boolean isSBS;
    public static int headTurnSensitivity;
    public static long lastActive;
    public synchronized void updateFrame(){}
    public void exitApp(){}
    public synchronized void closeSession(){}
    public native void nativeSetFramesync(int r, int g, int b, int a);
    public native float[] getAxes();
    public native boolean[] getButtons();
    public native void init(int width, int height, int refresh, int cpu, int gpu);
    public native void endFrame();
    public native void bindFBO(int index);
    public com.winlator.cmod.xserver.XServer getXServer(){return null;}
}
