"""Exercise NativeCinema's actual pointer mapping with the host JDK."""
import os
import importlib.util
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('quest_builder',ROOT/'scripts/quest/builder.py')
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
java=builder.find_java()
android=builder.find_sdk()['android_jar']

# Host mocks cannot detect class/interface changes in the real APK. Verify
# every X-server entry point used by the absolute pointer against its DEX.
sdk_root=builder.find_sdk()['build_tools'].parents[1]
analyzers=sorted((sdk_root/'cmdline-tools').glob('*/bin/apkanalyzer.bat'))
base=builder.WORK/'apktool_out.apk'
if not analyzers or not base.is_file():
    raise RuntimeError('Pointer ABI check needs Android cmdline-tools and the prepared WinlatorXR APK')
api={
    'XLock':('.class public interface abstract Lcom/winlator/cmod/xserver/XLock;',
             '.method public abstract close()V'),
    'XServer':('.class public Lcom/winlator/cmod/xserver/XServer;',
               '.field public final pointer:Lcom/winlator/cmod/xserver/Pointer;',
               '.method public varargs lock([Lcom/winlator/cmod/xserver/XServer$Lockable;)Lcom/winlator/cmod/xserver/XLock;'),
    'Pointer':('.class public Lcom/winlator/cmod/xserver/Pointer;',
               '.method public setX(I)V','.method public setY(I)V',
               '.method public triggerOnPointerMove(SS)V',
               '.method public setButton(Lcom/winlator/cmod/xserver/Pointer$Button;Z)V')}
env={**os.environ,'JAVA_HOME':str(java.parent)}
for name,declarations in api.items():
    dex=subprocess.run([str(analyzers[-1]),'dex','code','--class',
                        'com.winlator.cmod.xserver.'+name,str(base)],
                       check=True,capture_output=True,text=True,env=env).stdout
    for declaration in declarations:
        if declaration not in dex:
            raise RuntimeError('Incompatible WinlatorXR pointer API: '+declaration)
    stub=(ROOT/'source/android/stubs/com/winlator/cmod/xserver'/f'{name}.java').read_text()
    kind='interface' if ' interface ' in declarations[0] else 'class'
    if f'public {kind} {name} ' not in stub:
        raise RuntimeError(f'Pointer stub must declare {name} as {kind}')
print('PASS: pointer declarations match the real WinlatorXR DEX (including XLock interface)')
target=ROOT/'build'/'tests'/'quest-pointer'
target.mkdir(parents=True,exist_ok=True)
sources=[ROOT/'source/android/src/com/bioshockport/NativeCinema.java',
         ROOT/'source/android/src/com/bioshockport/WindowSource.java',
         ROOT/'source/native/tests/NativePointerFixture.java',
         ROOT/'source/native/tests/android/util/Log.java',
         *sorted((ROOT/'source/android/stubs').rglob('*.java'))]
subprocess.run([str(java/'javac.exe'),'--release','11','-cp',str(android),'-d',str(target),*map(str,sources)],check=True)
result=subprocess.run([str(java/'java.exe'),'-cp',os.pathsep.join(map(str,(target,android))),
                       'com.bioshockport.NativePointerFixture'],check=True,capture_output=True,text=True)
(target/'result.log').write_text(result.stdout,encoding='utf-8')
print(result.stdout,end='')
