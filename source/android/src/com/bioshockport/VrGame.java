package com.bioshockport;

import android.content.Intent;
import android.os.Bundle;
import android.content.SharedPreferences;
import android.preference.PreferenceManager;

import android.opengl.GLES20;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

import com.winlator.xr.XrActivity;
import com.winlator.xr.runtime.MetaQuest;

/**
 * Quest runtime. The DLL enters immersive SBS after the first game Present.
 * The native DLL writes exact, unfiltered 8-bit sync values (0,12,...,252).
 */
public class VrGame extends MetaQuest {
    private final ExitGuard exitGuard=new ExitGuard(this);
    @Override public void exitApp(){exitGuard.begin();super.exitApp();}
    @Override public synchronized void closeSession(){exitGuard.begin();super.closeSession();}
    @Override public android.content.Context getApplicationContext(){return exitGuard==null?super.getApplicationContext():exitGuard.application(super.getApplicationContext());}
    @Override public void startActivity(Intent intent){if(!exitGuard.block(intent))super.startActivity(intent);}
    @Override public void startActivity(Intent intent,Bundle options){if(!exitGuard.block(intent))super.startActivity(intent,options);}
    public static final String EXTRA_IMMERSIVE = "bioshockport_immersive";
    public static final String EXTRA_SBS = "bioshockport_sbs";
    private long syncReportAt;
    private int syncSamples, invalidSyncSamples;
    private float[] poseAxes;
    private boolean[] poseButtons;
    private int sourceWidth,sourceHeight;
    private int cinemaSink;
    private boolean cinemaSinkBound;
    private DatagramSocket poseSocket;
    private DatagramPacket posePacket;
    private long poseFrameId;
    private final ByteBuffer poseBytes = ByteBuffer.allocate(PoseWire.EXACT_BYTES).order(ByteOrder.LITTLE_ENDIAN);
    private boolean poseErrorLogged;
    private NativeCinema nativeCinema;
    private GamePerformance gamePerformance;
    private boolean syncPending;
    private int pendingR, pendingG, pendingB, pendingA;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        // A map calibrated against a previous ReShade/flat screen can point to
        // another historical pose, causing double images during head motion.
        SharedPreferences.Editor prefs = PreferenceManager.getDefaultSharedPreferences(this).edit();
        prefs.putInt("KEY_FRAMESYNC_MAPPING", 22);
        for (int i = 0; i < 22; ++i) prefs.putInt("KEY_FRAMESYNC_MAPPING" + i, i * 12);
        prefs.commit();
        Intent intent = getIntent();
        if (intent != null) {
            XrActivity.isImmersive = intent.getBooleanExtra(EXTRA_IMMERSIVE, true);
            XrActivity.isSBS = intent.getBooleanExtra(EXTRA_SBS, false);
        }
        super.onCreate(savedInstanceState);
        nativeCinema=new NativeCinema(this);
        gamePerformance=new GamePerformance();
    }

    @Override
    public void init(int width, int height, int refresh, int cpu, int gpu) {
        sourceWidth=width;sourceHeight=height;
        // XR_EXT_performance_settings uses 0/25/50/75, not the old VrApi 0..4 levels.
        PortLog.i("BioshockInit", "XR init " + width + "x" + height + " 72 Hz CPU=75 GPU=75");
        super.init(width, height, 72, 75, 75);
    }

    @Override
    public float[] getAxes() {
        float[] axes = super.getAxes();
        poseAxes = axes;
        poseFrameId=axes!=null&&axes.length>37?NativeCinema.capturePose((int)axes[37]):0;
        return axes;
    }

    @Override
    public boolean[] getButtons() {
        boolean[] buttons = super.getButtons();
        poseButtons=buttons;
        float[] axes = poseAxes;
        boolean exact=PoseWire.encodeExact(poseBytes,axes,buttons,poseFrameId);
        if (exact||PoseWire.encode(poseBytes, axes, buttons)) {
            try {
                if (poseSocket == null) {
                    poseSocket = new DatagramSocket();
                    posePacket = new DatagramPacket(poseBytes.array(), PoseWire.BYTES,
                            InetAddress.getByAddress(new byte[]{127,0,0,1}), PoseWire.PORT);
                }
                posePacket.setLength(exact?PoseWire.EXACT_BYTES:PoseWire.BYTES);
                poseSocket.send(posePacket);
            } catch (Exception error) {
                if (!poseErrorLogged) {
                    PortLog.e("BioshockPose", "Full-precision pose bridge failed", error);
                    poseErrorLogged = true;
                }
            }
        }
        return buttons;
    }

    @Override public synchronized void updateFrame() {
        if(nativeCinema!=null&&nativeCinema.ownsInput()) {
            // The DLL owns game input. Stock XrInput also moves the mouse and
            // races our ray; keep poses/buttons live without its X-server work.
            getAxes();getButtons();
            XrActivity.lastActive=System.currentTimeMillis();
        } else super.updateFrame();
    }

    @Override
    public void onDestroy() {
        if (gamePerformance != null) gamePerformance.close();
        if (poseSocket != null) poseSocket.close();
        if (nativeCinema != null) nativeCinema.close();
        super.onDestroy();
    }

    @Override
    public void endFrame() {
        int kind=NativeCinema.imageKind(syncPending,pendingR,pendingG,pendingA);
        if(!syncPending&&nativeCinema!=null&&nativeCinema.ownsWorld())kind=NativeCinema.WORLD;
        if (nativeCinema != null && nativeCinema.endFrame(poseAxes,poseButtons,kind,sourceWidth,sourceHeight)) {
            syncPending=false;
            return;
        }
        if (syncPending) {
            if(kind==NativeCinema.WORLD&&(pendingG==3||pendingG==4)){
                int pinned=XrActivity.isSBS?(pendingG==4?NativeCinema.retainExactPose((pendingR<<8)|pendingB):NativeCinema.retainWorldPose(pendingR,pendingB)):-1;
                if(pinned==0&&NativeCinema.holdWorldFrame()){syncPending=false;return;}
                super.nativeSetFramesync(pinned>0?pinned:pendingG==4?0:pendingR,0,0,pendingA);
            }else super.nativeSetFramesync(pendingR,pendingG,pendingB,pendingA);
            syncPending=false;
        }
        super.endFrame();
    }

    @Override public void onPause(){
        NativeCinema.trackingResumed();super.onPause();
    }
    @Override public void onResume(){
        super.onResume();NativeCinema.trackingResumed();
        if(gamePerformance!=null)gamePerformance.wake();
    }

    @Override public void bindFBO(int index) {
        cinemaSinkBound=false;
        if(index==0&&nativeCinema!=null&&nativeCinema.ownsInput()) {
            if(cinemaSink==0) {
                int[] ids=new int[1],binding=new int[1];
                GLES20.glGetIntegerv(GLES20.GL_TEXTURE_BINDING_2D,binding,0);
                GLES20.glGenTextures(1,ids,0);int texture=ids[0];
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D,texture);
                GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D,0,GLES20.GL_RGBA,1,1,0,GLES20.GL_RGBA,GLES20.GL_UNSIGNED_BYTE,null);
                GLES20.glGenFramebuffers(1,ids,0);cinemaSink=ids[0];
                GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER,cinemaSink);
                GLES20.glFramebufferTexture2D(GLES20.GL_FRAMEBUFFER,GLES20.GL_COLOR_ATTACHMENT0,GLES20.GL_TEXTURE_2D,texture,0);
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D,binding[0]);
            }
            GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER,cinemaSink);
            GLES20.glViewport(0,0,1,1);
            cinemaSinkBound=true;
            return;
        }
        super.bindFBO(index);
    }

    @Override
    public void nativeSetFramesync(int r, int g, int b, int a) {
        if(g==5&&a==255){if(nativeCinema!=null)nativeCinema.noteHudTag((r<<8)|b);return;}
        boolean valid = g==4?r>=0&&r<=255:(g == 0||g == 3) && r >= 0 && r <= 252 && r % 12 == 0;
        // preDrawable receives the actual image metadata after preFrame chose
        // its target. The first world image must not be drawn into the 1px
        // cinema sink merely because the previous frame was still a menu.
        if(valid&&a==255&&cinemaSinkBound&&(nativeCinema==null||!nativeCinema.ownsWorld())){
            super.bindFBO(0);NativeCinema.restoreWorldViewport();cinemaSinkBound=false;
            PortLog.i("BioshockFrameSync","First world image: restored stock target before draw");
        }
        // Malformed metadata must not index a random historical pose.
        pendingR=valid?r:0; pendingG=g; pendingB=b; pendingA=valid||g==1||g==2?a:0;
        if(valid&&g==4&&a==255&&nativeCinema!=null)nativeCinema.noteWorldTag((r<<8)|b);
        syncPending=true;
        if (PortLog.ENABLED) {
            ++syncSamples;
            if (!valid && g != 1 && g != 2 && a > 0) ++invalidSyncSamples;
            long now = android.os.SystemClock.elapsedRealtime();
            if (now - syncReportAt >= 5000) {
            PortLog.i("BioshockFrameSync", "sync=" + r + " rgba=" + r + "," + g + "," + b + "," + a
                    + " samples=" + syncSamples + " invalid=" + invalidSyncSamples);
            syncReportAt = now;
            syncSamples = invalidSyncSamples = 0;
            }
        }
    }
}
