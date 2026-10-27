package com.bioshockport;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.Intent;
import android.os.Bundle;

/** Keep Winlator's normal shutdown, without its automatic launcher restart. */
final class ExitGuard {
    private final Activity owner;
    private volatile boolean exiting;
    private Context closingContext;
    ExitGuard(Activity owner){this.owner=owner;}
    void begin(){exiting=true;}
    static boolean isRestart(boolean exiting,String ownPackage,String targetPackage,String targetClass){
        return exiting&&ownPackage.equals(targetPackage)&&
            ("com.bioshockport.Launcher".equals(targetClass)||"com.winlator.cmod.MainActivity".equals(targetClass));
    }
    boolean block(Intent intent){
        ComponentName target=intent==null?null:intent.getComponent();
        if(target==null||!isRestart(exiting,owner.getPackageName(),target.getPackageName(),target.getClassName()))return false;
        owner.finishAndRemoveTask();return true;
    }
    Context application(Context original){
        if(!exiting||original==null)return original;
        if(closingContext==null)closingContext=new ContextWrapper(original){
            @Override public void startActivity(Intent intent){if(!block(intent))super.startActivity(intent);}
            @Override public void startActivity(Intent intent,Bundle options){if(!block(intent))super.startActivity(intent,options);}
        };
        return closingContext;
    }
}
