package com.winlator.cmod.xserver;
public class XServer {
    public enum Lockable {WINDOW_MANAGER,INPUT_DEVICE}
    public Pointer pointer;
    public XLock lock(Lockable... locks){return null;}
}
