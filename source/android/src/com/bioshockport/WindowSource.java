package com.bioshockport;


import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.List;

/** Access the already updated game GPU image, without a CPU readback. */
final class WindowSource {
    private Field instance,windows,content,width,height;
    private Method texture,textureId;
    private boolean failed;
    private final int[] result=new int[3];
    int[] current(){return find(false);}
    int[] currentHud(){return find(true);}
    private int[] find(boolean hud){
        if(failed)return null;
        try {
            if(instance==null){
                Class<?> renderer=Class.forName("com.winlator.xr.io.XrRenderer");
                instance=renderer.getDeclaredField("instance");instance.setAccessible(true);
                windows=renderer.getSuperclass().getDeclaredField("renderableWindows");windows.setAccessible(true);
                content=Class.forName("com.winlator.cmod.renderer.RenderableWindow").getField("content");
                Class<?> drawable=Class.forName("com.winlator.cmod.xserver.Drawable");
                width=drawable.getField("width");height=drawable.getField("height");
                texture=drawable.getMethod("getTexture");
                textureId=Class.forName("com.winlator.cmod.renderer.Texture").getMethod("getTextureId");
            }
            Object renderer=instance.get(null);if(renderer==null)return null;
            List<?> list=(List<?>)windows.get(renderer);
            for(int i=list.size()-1;i>=0;--i){
                Object drawable=content.get(list.get(i));
                int w=width.getShort(drawable),h=height.getShort(drawable);
                if(w<1024||h<512)continue;
                if(((h&1)!=0)!=hud)continue; // Dedicated RGB/coverage HUD transport has odd height.
                Object image=texture.invoke(drawable);if(image==null)return null;
                int id=(Integer)textureId.invoke(image);if(id==0)return null;
                result[0]=id;result[1]=w;result[2]=h;return result;
            }
        } catch(ReflectiveOperationException | RuntimeException error){
            PortLog.e("BioshockCinema","Game GPU image unavailable; retaining fallback",error);failed=true;
        }
        return null;
    }
}
