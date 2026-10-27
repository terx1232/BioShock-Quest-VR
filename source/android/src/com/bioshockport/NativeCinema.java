package com.bioshockport;

import android.os.SystemClock;

import com.winlator.xr.XrActivity;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketTimeoutException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Screen layout from the DLL; presentation and beam run on the XR thread. */
final class NativeCinema implements AutoCloseable {
    private static final boolean loaded;
    static {
        boolean ok=false;
        try { System.loadLibrary("bioshockcinema"); ok=true; }
        catch (UnsatisfiedLinkError error) { PortLog.e("BioshockCinema","Native layers unavailable",error); }
        loaded=ok;
    }
    private static native int frame(float[] axes,float[] layout,int hand,boolean submit,int texture,int width,int height,int menuWidth,int menuHeight,int sequence,boolean fullSource);
    static native void restoreWorldViewport();
    private static native int holdFrame();
    private static native int pinWorldPose(int sync,int serial);
    private static native void resetWorldPose();
    private static native long captureWorldPose(int sync);
    private static native void registerWorldImage(int tag,long frameId,int flags);
    private static native int pinExactPose(int tag);
    private static native int worldFrame(int texture,int width,int height,int cpuTag);
    private static native void prepareHud(int texture,int width,int height,int tag,float[] axes);
    static long capturePose(int sync){return loaded?captureWorldPose(sync):0;}
    static int retainExactPose(int tag){return loaded?pinExactPose(tag):-1;}
    static int retainWorldPose(int sync,int serial){return loaded?pinWorldPose(sync,serial):-1;}
    static boolean holdWorldFrame(){return loaded&&holdFrame()==2;}
    static void trackingResumed(){if(loaded)resetWorldPose();}
    static final int WORLD=0,MENU=1,HOLD=2;
    static int imageKind(boolean pending,int r,int g,int a){
        if(!pending||a!=255)return HOLD;
        if(g==MENU)return MENU;
        return (g==4?r>=0&&r<=255:(g==WORLD||g==3)&&r>=0&&r<=252&&r%12==0)?WORLD:HOLD;
    }
    static int route(int imageKind,int layoutMode){
        if(imageKind==WORLD&&layoutMode==0)return WORLD;
        if(imageKind==MENU&&(layoutMode==2||layoutMode==3))return MENU;
        return HOLD;
    }
    private static final class Layout {
        int mode,hand,sequence,width,height; float debugU=-1,debugV=-1;final float[] values=new float[14];
    }
    private volatile Layout layout;
    private volatile boolean closed;
    private DatagramSocket socket;
    private DatagramPacket acknowledgement;
    private final byte[] ack=new byte[20]; // BSQA, ready, client X/Y, button
    private long ackAt;
    private long capabilityAt;
    private final byte[] capability={'B','S','W','P',1,0,0,0};
    private final byte[] exactCapability={'B','S','W','P',2,0,0,0};
    private DatagramPacket capabilityPacket;
    private DatagramPacket exactCapabilityPacket;
    private boolean wasReady;
    private boolean ready;
    private boolean presenting;
    private boolean masking;
    private boolean activated;
    private boolean worldPresenting,worldDisabled;
    private int worldTag=-1;
    private int hudTag=-1;
    private final WindowSource hudSource=new WindowSource();
    private final byte[] hudCapability={'B','S','H','U',0,0,0,0};
    private DatagramPacket hudPacket;
    boolean ownsWorld(){return worldPresenting;}
    void noteWorldTag(int tag){worldTag=tag;}
    void noteHudTag(int tag){hudTag=tag;}
    private int lastRoute=-1;
    private final WindowSource source=new WindowSource();
    private int targetX=-1,targetY=-1;
    private boolean targetDown;
    private long reportAt;

    NativeCinema(XrActivity activity) {
        if(!loaded)return;
        try {
            socket=new DatagramSocket(7875); socket.setSoTimeout(1000);
            acknowledgement=new DatagramPacket(ack,ack.length,InetAddress.getByAddress(new byte[]{127,0,0,1}),7876);
            capabilityPacket=new DatagramPacket(capability,capability.length,acknowledgement.getAddress(),7876);
            exactCapabilityPacket=new DatagramPacket(exactCapability,exactCapability.length,acknowledgement.getAddress(),7876);
            hudPacket=new DatagramPacket(hudCapability,hudCapability.length,acknowledgement.getAddress(),7876);
            Thread receiver=new Thread(this::receive,"BioShock-Cinema");receiver.setDaemon(true);receiver.start();
        } catch(Exception error) { PortLog.e("BioshockCinema","Cinema layout bridge unavailable",error); }
    }
    private void receive(){
        byte[] bytes=new byte[128];DatagramPacket packet=new DatagramPacket(bytes,bytes.length);
        while(!closed){
            try {
                packet.setLength(bytes.length);socket.receive(packet);
                if(packet.getLength()==16||packet.getLength()==20){
                    ByteBuffer frame=ByteBuffer.wrap(bytes,0,packet.getLength()).order(ByteOrder.LITTLE_ENDIAN);
                    int magic=frame.getInt();
                    if((magic==0x32465342&&packet.getLength()==16)||(magic==0x33465342&&packet.getLength()==20)){int tag=frame.getInt();long id=frame.getLong();
                        int flags=packet.getLength()==20?frame.getInt():0;
                        if(tag>=0&&tag<=65535&&id!=0)registerWorldImage(tag,id,flags);continue;}
                }
                Layout next=decode(bytes,packet.getLength());if(next!=null)layout=next;
            } catch(SocketTimeoutException ignored) {}
            catch(Exception error){if(!closed)PortLog.e("BioshockCinema","Cinema layout read failed",error);break;}
        }
    }
    private static Layout decode(byte[] bytes,int length){
        if(length!=68&&length!=76&&length!=84)return null;
        ByteBuffer in=ByteBuffer.wrap(bytes,0,length).order(ByteOrder.LITTLE_ENDIAN);
        if(in.getInt()!=0x31515342)return null;
        Layout next=new Layout();next.mode=in.getInt();boolean valid=next.mode>=0&&next.mode<=3;
        for(int i=0;i<14;++i){next.values[i]=in.getFloat();valid &= Float.isFinite(next.values[i]);}
        int encoded=in.getInt();next.hand=encoded&1;next.sequence=encoded>>>1;
        if(length>=76){next.debugU=in.getFloat();next.debugV=in.getFloat();}
        if(length==84){
            next.width=in.getInt();next.height=in.getInt();
            valid &= (next.width==0&&next.height==0)||(next.width>=320&&next.width<=8192&&next.height>=240&&next.height<=8192);
        }
        return valid?next:null;
    }
    // Keep stock mode negotiation alive until our first actual submission.
    // Prepared resources alone cannot replace the stock frame/input cycle.
    boolean ownsInput(){return worldPresenting||presenting||(masking&&activated);}
    private void pointer(float[] axes,boolean[] buttons,Layout current,int width,int height){
        if(current.debugU>=0&&current.debugU<=1&&current.debugV>=0&&current.debugV<=1){
            targetX=Math.min(width-1,(int)(current.debugU*width));targetY=Math.min(height-1,(int)(current.debugV*height));
            targetDown=false;return; // hover-only device check
        }
        if(buttons==null)return;
        float[] l=current.values;int p=current.hand==1?21:9,q=current.hand==1?15:3;
        float x=axes[q],y=axes[q+1],z=axes[q+2],w=axes[q+3];
        float dx=-2*(x*z+y*w),dy=-2*(y*z-x*w),dz=-(1-2*(x*x+y*y));
        float denominator=dx*l[9]+dy*l[10]+dz*l[11];
        if(denominator>=-.0001f){releasePointer();return;}
        float distance=((l[0]-axes[p])*l[9]+(l[1]-axes[p+1])*l[10]+(l[2]-axes[p+2])*l[11])/denominator;
        if(distance<=0){releasePointer();return;}
        float rx=axes[p]+dx*distance-l[0],ry=axes[p+1]+dy*distance-l[1],rz=axes[p+2]+dz*distance-l[2];
        float u=((rx*l[3]+ry*l[4]+rz*l[5])/l[12]+1)*.5f;
        float v=(1-(rx*l[6]+ry*l[7]+rz*l[8])/l[13])*.5f;
        if(u<0||u>1||v<0||v>1){releasePointer();return;}
        targetX=Math.min(width-1,(int)(u*width));targetY=Math.min(height-1,(int)(v*height));
        targetDown=buttons[current.hand==1?18:7];
    }
    private void releasePointer(){targetDown=false;}
    private void encodeAck(){
        ByteBuffer.wrap(ack).order(ByteOrder.LITTLE_ENDIAN).putInt(0x41515342)
            .putInt(ready?1:0).putInt(targetX).putInt(targetY).putInt(targetDown?1:0);
    }
    boolean endFrame(float[] axes,boolean[] buttons,int kind,int width,int height){
        Layout current=layout;
        int selected=route(kind,current==null?-1:current.mode);
        int status=0,menuWidth=width,menuHeight=height;
        int[] image=null;
        boolean wasWorld=worldPresenting;worldPresenting=false;
        boolean hudReady=false;
        String hudState="none";
        if(loaded&&socket!=null&&selected==WORLD&&!worldDisabled&&worldTag>=0){
            image=source.current();
            if(image!=null){
                int[] hud=hudTag>=0?hudSource.currentHud():null;
                if(hud!=null)hudState=hud[0]+"/"+hud[1]+"x"+hud[2]+" tag="+hudTag;
                prepareHud(hud==null?0:hud[0],hud==null?0:hud[1],hud==null?0:hud[2],hudTag,axes);
                int worldStatus=worldFrame(image[0],image[1],image[2],worldTag);
                worldPresenting=worldStatus>=2;hudReady=worldStatus==3;
                if(worldStatus==0){worldDisabled=true;PortLog.e("BioshockCinema","World GPU presenter unavailable; stock rendering restored");}
            }
        }
        if(loaded&&socket!=null&&axes!=null&&current!=null&&current.mode>0){
            image=source.current();
            if(image!=null){
                menuWidth=current.width>0?current.width:image[1];
                menuHeight=current.height>0?current.height:image[2];
                status=frame(axes,current.values,current.hand,selected==MENU,image[0],image[1],image[2],menuWidth,menuHeight,current.sequence,current.mode==3);
            }
        }
        ready=status>0;presenting=status==2;
        if(presenting)activated=true;
        if(presenting)pointer(axes,buttons,current,menuWidth,menuHeight);
        else releasePointer();
        // An unready menu/transition never falls through to the stock window.
        // Empty OpenXR composition clears the display without sampling it.
        masking=!worldPresenting&&!presenting&&(selected!=WORLD||wasWorld)&&loaded&&holdFrame()==2;
        long now=SystemClock.elapsedRealtime();
        int key=(kind+1)*100+(current==null?0:current.mode+1)*10+(presenting?MENU:masking?HOLD:WORLD);
        if(PortLog.ENABLED&&(key!=lastRoute||now-reportAt>=5000)){
            PortLog.i("BioshockCinema","imageKind="+kind+" layout="+(current==null?-1:current.mode)+" route="+(worldPresenting?"world-GPU":presenting?"quad":masking?"mask":"world")+" status="+status+" sbs="+XrActivity.isSBS+" image="+(image==null?"none":image[0]+"/"+image[1]+"x"+image[2])+" hud="+hudState+" ready="+hudReady);
            lastRoute=key;
            reportAt=now;
        }
        if(ready||ready!=wasReady||now-ackAt>=500){
            encodeAck();
            try {socket.send(acknowledgement);}catch(Exception error){PortLog.e("BioshockCinema","Cinema acknowledgement failed",error);}
            wasReady=ready;ackAt=now;
        }
        // Separate optional packet: APK52/DLL60 retain their original protocol.
        if(loaded&&socket!=null&&now-capabilityAt>=500){
            try{socket.send(capabilityPacket);socket.send(exactCapabilityPacket);}catch(Exception ignored){}
            if(worldPresenting){hudCapability[4]=(byte)(hudReady?1:0);try{socket.send(hudPacket);}catch(Exception ignored){}}
            capabilityAt=now;
        }
        return worldPresenting||presenting||masking;
    }
    @Override public void close(){closed=true;if(socket!=null)socket.close();}
}
