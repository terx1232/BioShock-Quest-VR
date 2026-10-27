package com.bioshockport;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Exact axes from WinlatorXR's native frame, before decimal formatting. */
public final class PoseWire {
    public static final int PORT = 7874;
    public static final int BYTES = 160;
    public static final int EXACT_BYTES = 168;
    private static final int[] AXES = {
            3,4,5,6,7,8,9,10,11,15,16,17,18,19,20,21,22,23,
            27,28,29,30,31,32,33,34,35,36};
    private PoseWire() {}
    public static boolean encodeExact(ByteBuffer out,float[] axes,boolean[] buttons,long frameId){
        if(frameId==0||out.capacity()<EXACT_BYTES||!encode(out,axes,buttons))return false;
        out.putInt(0,0x02565342);out.putLong(160,frameId);return true;
    }

    public static boolean encode(ByteBuffer out, float[] axes, boolean[] buttons) {
        if (axes == null || axes.length < 48 || buttons == null || buttons.length < 19
                || out.capacity() < BYTES) return false;
        out.clear();
        out.order(ByteOrder.LITTLE_ENDIAN);
        out.putInt(0x01565342); // B S V 1
        for (int axis : AXES) out.putFloat(axes[axis]);
        out.putInt((int)axes[37]);
        int mask = 0;
        for (int i = 0; i < 19; ++i) if (buttons[i]) mask |= 1 << i;
        out.putInt(mask);
        for (int i = 39; i < 48; ++i) out.putFloat(axes[i]);
        return true;
    }
}
