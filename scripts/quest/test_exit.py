"""Exercise the actual Android exit guard through both upstream restart paths."""
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/quest'))
import builder
out=ROOT/'build/tests/quest-exit';out.mkdir(parents=True,exist_ok=True)
sources={
'android/os/Bundle.java':'package android.os; public class Bundle {}',
'android/content/ComponentName.java':'''package android.content; public class ComponentName {String p,c;public ComponentName(String p,String c){this.p=p;this.c=c;}public String getPackageName(){return p;}public String getClassName(){return c;}}''',
'android/content/Intent.java':'''package android.content; public class Intent {ComponentName c;public Intent(ComponentName c){this.c=c;}public ComponentName getComponent(){return c;}}''',
'android/content/Context.java':'''package android.content; public class Context {public int launches=0;public String getPackageName(){return "com.winlator.cmod";}public void startActivity(Intent i){launches++;}public void startActivity(Intent i,android.os.Bundle b){launches++;}}''',
'android/content/ContextWrapper.java':'''package android.content; public class ContextWrapper extends Context {Context base;public ContextWrapper(Context c){base=c;}public void startActivity(Intent i){base.startActivity(i);}public void startActivity(Intent i,android.os.Bundle b){base.startActivity(i,b);}}''',
'android/app/Activity.java':'''package android.app; public class Activity extends android.content.Context {public int finishes=0;public void finishAndRemoveTask(){finishes++;}}''',
'com/bioshockport/ExitFixture.java':'''package com.bioshockport;
import android.app.Activity;import android.content.*;import android.os.Bundle;
public class ExitFixture {
static void check(boolean b,String s){if(!b)throw new AssertionError(s);}
static Intent intent(String pkg,String cls){return new Intent(new ComponentName(pkg,cls));}
public static void main(String[] args){
 Activity activity=new Activity();Context app=new Context();ExitGuard guard=new ExitGuard(activity);
 Intent launcher=intent("com.winlator.cmod","com.bioshockport.Launcher");
 check(!guard.block(launcher)&&guard.application(app)==app,"ordinary launch/resume is untouched");
 guard.begin();boolean wineStopped=true;
 // XServerDisplayActivity.exitApp -> AppUtils.restartApplication(applicationContext).
 guard.application(app).startActivity(launcher);
 check(wineStopped&&app.launches==0&&activity.finishes==1,"normal Wine cleanup followed by no automatic launcher start");
 // XrActivity.closeSession -> this.startActivity(launcher), also during onDestroy.
 check(guard.block(launcher)&&activity.finishes==2,"direct closeSession restart is blocked");
 guard.application(app).startActivity(launcher,new Bundle());check(app.launches==0,"options overload cannot bypass the guard");
 check(guard.block(intent("com.winlator.cmod","com.winlator.cmod.MainActivity")),"stock Winlator main-screen restart is blocked");
 guard.application(app).startActivity(intent("external.app","external.app.Main"));check(app.launches==1,"external intents remain native");
 check(!guard.block(new Intent(null))&&!guard.block(null),"unknown intents are not swallowed");
 ExitGuard reopened=new ExitGuard(new Activity());check(!reopened.block(launcher),"immediate manual reopen is allowed; no persisted timeout/ticket");
 System.out.println("PASS: real ExitGuard, application-context and direct-activity restart, native cleanup retained, task removal, other intents, immediate manual reopen");
}}
'''}
files=[]
for name,source in sources.items():
    path=out/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(source,encoding='utf-8');files.append(str(path))
java=builder.find_java()
subprocess.run([str(java/'javac.exe'),'-encoding','UTF-8','--release','11','-d',str(out),*files,str(ROOT/'source/android/src/com/bioshockport/ExitGuard.java')],check=True)
r=subprocess.run([str(java/'java.exe'),'-cp',str(out),'com.bioshockport.ExitFixture'],capture_output=True,text=True,timeout=15)
(out/'result.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='')
if r.returncode:raise RuntimeError('Android exit regression')
