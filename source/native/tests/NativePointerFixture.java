package com.bioshockport;

import java.lang.reflect.*;
import java.nio.*;

/** Calls the actual Android pointer calculation without a headset or X server. */
public final class NativePointerFixture {
    static Field field(Class<?> type,String name)throws Exception{Field f=type.getDeclaredField(name);f.setAccessible(true);return f;}
    static void check(boolean ok,String what){if(!ok)throw new AssertionError(what);}
    public static void main(String[] args)throws Exception{
        NativeCinema cinema=new NativeCinema(null);
        Class<?> type=Class.forName("com.bioshockport.NativeCinema$Layout");
        Constructor<?> ctor=type.getDeclaredConstructor();ctor.setAccessible(true);Object layout=ctor.newInstance();
        field(type,"hand").setInt(layout,1);
        float[] l=(float[])field(type,"values").get(layout);
        l[1]=1.5f;l[2]=-3.5f;l[3]=1;l[7]=1;l[11]=1;l[12]=2;l[13]=1;
        float[] axes=new float[53];axes[18]=1;axes[22]=1.5f;
        boolean[] buttons=new boolean[19];buttons[18]=true;
        Method pointer=NativeCinema.class.getDeclaredMethod("pointer",float[].class,boolean[].class,type,int.class,int.class);pointer.setAccessible(true);
        pointer.invoke(cinema,axes,buttons,layout,3072,1536);
        check(field(NativeCinema.class,"targetX").getInt(cinema)==1536,"right hand center X");
        check(field(NativeCinema.class,"targetY").getInt(cinema)==768,"right hand center Y");
        check(field(NativeCinema.class,"targetDown").getBoolean(cinema),"right trigger");
        axes[21]=1;axes[22]=2;
        pointer.invoke(cinema,axes,buttons,layout,3072,1536);
        check(field(NativeCinema.class,"targetX").getInt(cinema)==2304,"right hand offset X");
        check(field(NativeCinema.class,"targetY").getInt(cinema)==384,"right hand offset Y");
        axes[21]=0;axes[22]=1.5f;
        for(float yaw:new float[]{-.35f,-.2f,0,.2f,.35f}){
            axes[16]=(float)Math.sin(yaw*.5);axes[18]=(float)Math.cos(yaw*.5);
            pointer.invoke(cinema,axes,buttons,layout,3072,1536);
            int expected=(int)((.5-3.5*Math.tan(yaw)/4)*3072);
            check(Math.abs(field(NativeCinema.class,"targetX").getInt(cinema)-expected)<=1,"ray angle to exact screen pixel");
        }
        field(type,"hand").setInt(layout,0);axes[6]=1;axes[10]=1.5f;buttons[7]=true;
        pointer.invoke(cinema,axes,buttons,layout,3072,1536);
        check(field(NativeCinema.class,"targetX").getInt(cinema)==1536,"left hand center");
        check(field(NativeCinema.class,"targetDown").getBoolean(cinema),"left trigger");
        field(NativeCinema.class,"ready").setBoolean(cinema,true);
        Method encode=NativeCinema.class.getDeclaredMethod("encodeAck");encode.setAccessible(true);encode.invoke(cinema);
        byte[] wire=(byte[])field(NativeCinema.class,"ack").get(cinema);
        ByteBuffer in=ByteBuffer.wrap(wire).order(ByteOrder.LITTLE_ENDIAN);
        check(wire.length==20&&in.getInt()==0x41515342&&in.getInt()==1,"pointer packet magic and ready");
        check(in.getInt()==1536&&in.getInt()==768&&in.getInt()==1,"exact cursor coordinates and trigger on the wire");
        axes[9]=10;
        pointer.invoke(cinema,axes,buttons,layout,3072,1536);
        check(!field(NativeCinema.class,"targetDown").getBoolean(cinema),"offscreen ray releases trigger");
        axes[9]=0;axes[4]=1;axes[6]=0;
        field(NativeCinema.class,"targetDown").setBoolean(cinema,true);
        pointer.invoke(cinema,axes,buttons,layout,3072,1536);
        check(!field(NativeCinema.class,"targetDown").getBoolean(cinema),"backward ray releases trigger");
        // Device probes must exercise Android's real input path, without clicks.
        ByteBuffer packet=ByteBuffer.allocate(76).order(ByteOrder.LITTLE_ENDIAN);
        packet.putInt(0x31515342).putInt(2);
        for(float value:l)packet.putFloat(value);
        packet.putInt(15).putFloat(.7f).putFloat(.6f);
        Method decode=NativeCinema.class.getDeclaredMethod("decode",byte[].class,int.class);decode.setAccessible(true);
        Object decoded=decode.invoke(null,packet.array(),76);
        pointer.invoke(cinema,axes,buttons,decoded,1280,720);
        check(field(NativeCinema.class,"targetX").getInt(cinema)==896,"1280x720 absolute X");
        check(field(NativeCinema.class,"targetY").getInt(cinema)==432,"1280x720 absolute Y");
        check(!field(NativeCinema.class,"targetDown").getBoolean(cinema),"device probe cannot click");
        check(decode.invoke(null,packet.array(),68)!=null,"legacy layout supported");
        check(decode.invoke(null,packet.array(),75)==null,"truncated layout rejected");
        ByteBuffer sized=ByteBuffer.allocate(84).order(ByteOrder.LITTLE_ENDIAN);
        sized.put(packet.array()).putInt(1280).putInt(720);
        Object cropped=decode.invoke(null,sized.array(),84);
        check(cropped!=null&&field(type,"width").getInt(cropped)==1280&&field(type,"height").getInt(cropped)==720,
                "independent menu rectangle survives transport");
        pointer.invoke(cinema,axes,buttons,cropped,1280,720);
        check(field(NativeCinema.class,"targetX").getInt(cinema)==896&&field(NativeCinema.class,"targetY").getInt(cinema)==432,
                "larger stereo transport does not rescale menu coordinates");
        sized.putInt(76,9000);
        check(decode.invoke(null,sized.array(),84)==null,"oversized menu rejected");
        sized.putInt(76,1280);sized.putInt(4,3);
        check(decode.invoke(null,sized.array(),84)!=null,"loading layout mode and fixed panel extent survive the same wire packet");
        check(NativeCinema.imageKind(false,0,0,255)==NativeCinema.HOLD,"missing image metadata never exposes a window");
        check(NativeCinema.imageKind(true,12,0,0)==NativeCinema.HOLD,"inactive metadata is masked");
        check(NativeCinema.imageKind(true,13,0,255)==NativeCinema.HOLD,"invalid world pose index is masked");
        check(NativeCinema.imageKind(true,12,2,255)==NativeCinema.HOLD,"explicit transition is masked");
        check(NativeCinema.imageKind(true,12,3,255)==NativeCinema.WORLD,"negotiated retained world pose uses the world route");
        check(NativeCinema.imageKind(true,13,3,255)==NativeCinema.HOLD,"retained world rejects invalid sync");
        check(NativeCinema.imageKind(true,12,3,0)==NativeCinema.HOLD,"sleep metadata cannot expose retained world");
        check(NativeCinema.imageKind(true,12,1,255)==NativeCinema.MENU,"full raw menu image recognised");
        check(NativeCinema.imageKind(true,12,0,255)==NativeCinema.WORLD,"completed stereo world recognised");
        check(NativeCinema.imageKind(true,255,4,255)==NativeCinema.WORLD,"exact world tag uses both red and blue bytes");
        check(NativeCinema.imageKind(true,255,4,0)==NativeCinema.HOLD,"unready exact image stays masked");
        check(NativeCinema.route(NativeCinema.MENU,0)==NativeCinema.HOLD,"early UDP disable cannot expose the older menu as world");
        check(NativeCinema.route(NativeCinema.WORLD,2)==NativeCinema.HOLD,"early pause layout cannot expose the older stereo image as menu");
        check(NativeCinema.route(NativeCinema.WORLD,0)==NativeCinema.WORLD,"matching world image and layout allow stock VR");
        check(NativeCinema.route(NativeCinema.MENU,2)==NativeCinema.MENU,"matching menu image and layout allow native quad");
        check(NativeCinema.route(NativeCinema.MENU,3)==NativeCinema.MENU,"confirmed level-loading image uses the independent quad and live ray");
        check(NativeCinema.route(NativeCinema.WORLD,3)==NativeCinema.HOLD,"loading layout cannot publish a previous stereo world window");
        check(NativeCinema.route(NativeCinema.HOLD,3)==NativeCinema.HOLD,"loading layout cannot override a transition mask");
        for(int mode:new int[]{-1,0,1,2})check(NativeCinema.route(NativeCinema.HOLD,mode)==NativeCinema.HOLD,"transition always stays masked");
        field(type,"mode").setInt(layout,2);field(NativeCinema.class,"layout").set(cinema,layout);
        check(!cinema.ownsInput(),"prepared panel must not stop stock mode negotiation");
        field(NativeCinema.class,"masking").setBoolean(cinema,true);
        check(!cinema.ownsInput(),"startup mask must keep stock mode negotiation alive until first real quad");
        field(NativeCinema.class,"masking").setBoolean(cinema,false);
        field(NativeCinema.class,"presenting").setBoolean(cinema,true);
        field(NativeCinema.class,"activated").setBoolean(cinema,true);
        check(cinema.ownsInput(),"submitted native panel owns menu input");
        field(type,"mode").setInt(layout,0);
        check(cinema.ownsInput(),"early UDP change must not release menu input before its image");
        field(NativeCinema.class,"presenting").setBoolean(cinema,false);
        field(NativeCinema.class,"masking").setBoolean(cinema,true);
        check(cinema.ownsInput(),"transition mask keeps duplicate stock mouse disabled");
        field(NativeCinema.class,"masking").setBoolean(cinema,false);
        check(!cinema.ownsInput(),"completed gameplay restores stock update cycle");
        field(NativeCinema.class,"worldPresenting").setBoolean(cinema,true);
        check(cinema.ownsInput()&&cinema.ownsWorld(),"GPU world owns its raw pose/input cycle without duplicate X-server mouse");
        field(NativeCinema.class,"worldPresenting").setBoolean(cinema,false);
        cinema.close();
        System.out.println("PASS: actual ray maps both hands, angles and triggers to 3072x1536 pixels; 20-byte pointer packet and offscreen release");
    }
}
