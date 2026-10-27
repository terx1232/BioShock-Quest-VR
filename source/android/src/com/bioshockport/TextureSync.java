package com.bioshockport;

import android.opengl.GLES20;
import android.opengl.GLES30;
import android.opengl.EGL14;
import android.os.SystemClock;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.util.List;

/** Read metadata from the GPU image after WinlatorXR has sampled that image. */
final class TextureSync {
    private final ByteBuffer pixel = ByteBuffer.allocateDirect(4);
    private final int[] binding = new int[1], generated = new int[1];
    private Field rendererInstance, windows, content, renderLock, width, height;
    private Method getTexture, getTextureId;
    private int fbo;
    private long contextHandle;
    private boolean unavailable;
    private long reportAt, nanos;
    private int samples, mismatches, failures;

    /** Returns packed RGBA, or -1 when the texture is not available. */
    long read(int cpuR) {
        if (unavailable) return -1;
        try {
            if (rendererInstance == null) {
                Class<?> renderer = Class.forName("com.winlator.xr.io.XrRenderer");
                rendererInstance = renderer.getDeclaredField("instance");
                rendererInstance.setAccessible(true);
                windows = renderer.getSuperclass().getDeclaredField("renderableWindows");
                windows.setAccessible(true);
                content = Class.forName("com.winlator.cmod.renderer.RenderableWindow").getField("content");
                Class<?> drawable = Class.forName("com.winlator.cmod.xserver.Drawable");
                renderLock = drawable.getField("renderLock");
                width = drawable.getField("width"); height = drawable.getField("height");
                getTexture = drawable.getMethod("getTexture");
                getTextureId = Class.forName("com.winlator.cmod.renderer.Texture").getMethod("getTextureId");
            }
            Object renderer = rendererInstance.get(null);
            if (renderer == null) return -1;
            List<?> list = (List<?>)windows.get(renderer);
            if (list.isEmpty()) return -1;
            Object drawable = null;
            for (int i = list.size()-1; i >= 0; --i) {
                Object candidate = content.get(list.get(i));
                if (width.getShort(candidate) >= 1024 && height.getShort(candidate) >= 512) {
                    drawable = candidate; break;
                }
            }
            if (drawable == null) return -1;
            synchronized (renderLock.get(drawable)) {
                int texture = (Integer)getTextureId.invoke(getTexture.invoke(drawable));
                if (texture == 0) return -1;
                long currentContext=EGL14.eglGetCurrentContext().getNativeHandle();
                if (contextHandle != currentContext) { contextHandle=currentContext; fbo=0; }
                if (fbo == 0) { GLES30.glGenFramebuffers(1,generated,0); fbo=generated[0]; }
                GLES30.glGetIntegerv(GLES30.GL_READ_FRAMEBUFFER_BINDING,binding,0);
                long started = System.nanoTime();
                try {
                    GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER,fbo);
                    GLES30.glFramebufferTexture2D(GLES30.GL_READ_FRAMEBUFFER,GLES30.GL_COLOR_ATTACHMENT0,
                            GLES20.GL_TEXTURE_2D,texture,0);
                    int status=GLES30.glCheckFramebufferStatus(GLES30.GL_READ_FRAMEBUFFER);
                    if (status != GLES30.GL_FRAMEBUFFER_COMPLETE) {
                        unavailable=true;
                        PortLog.w("BioshockTextureSync","GPU image is not readable, FBO="+status+"; retaining stock metadata");
                        return -1;
                    }
                    pixel.clear();
                    GLES30.glReadPixels(0,0,1,1,GLES30.GL_RGBA,GLES30.GL_UNSIGNED_BYTE,pixel);
                    int error=GLES30.glGetError();
                    if (error != GLES30.GL_NO_ERROR) {
                        unavailable=true;
                        PortLog.w("BioshockTextureSync","GPU metadata read failed, GL="+error+"; retaining stock metadata");
                        return -1;
                    }
                } finally {
                    GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER,binding[0]);
                    nanos += System.nanoTime()-started;
                }
            }
            int c0=pixel.get(0)&255, g=pixel.get(1)&255, c2=pixel.get(2)&255, a=pixel.get(3)&255;
            // Shared BGRA buffers can expose either channel order. SBS uses blue=0.
            int r = c2 == 0 ? c0 : c0 == 0 ? c2 : -1;
            long result = -1;
            if (r >= 0 && r <= 252 && r%12 == 0 && g == 0 && a == 255) {
                ++samples; if (r != cpuR) ++mismatches;
                result = (long)r | ((long)a<<24);
            } else ++failures;
            long now=SystemClock.elapsedRealtime();
            if (now-reportAt>=5000) {
                PortLog.i("BioshockTextureSync","GPU samples="+samples+" cpuMismatch="+mismatches
                        +" failures="+failures+" readMs="+(samples>0?nanos/1e6/samples:0));
                reportAt=now; samples=mismatches=failures=0; nanos=0;
            }
            return result;
        } catch (ReflectiveOperationException | RuntimeException error) {
            unavailable=true;
            PortLog.e("BioshockTextureSync","GPU metadata unavailable; retaining stock metadata",error);
            return -1;
        }
    }
}
