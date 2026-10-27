"""One entry point for Quest builds, checks, installs and rollback."""
import argparse
import importlib.util
import json
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
SCRIPTS=ROOT/'scripts'/'quest'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=['auto','build','install','doctor','test','measure','diagnose','rollback','recover'],nargs='?',default='auto')
    parser.add_argument('--serial')
    parser.add_argument('--backup',type=Path)
    parser.add_argument('--adb')
    parser.add_argument('--seconds',type=int,default=20,help='measurement duration, at most 45 seconds')
    parser.add_argument('--tag',default='game',help='label saved with a measurement')
    args=parser.parse_args()
    spec=importlib.util.spec_from_file_location('quest_builder',SCRIPTS/'builder.py')
    builder=importlib.util.module_from_spec(spec); spec.loader.exec_module(builder)
    def run(path,*arguments): subprocess.run([sys.executable,'-I',str(path),*map(str,arguments)],check=True,cwd=ROOT)
    if args.command=='measure':
        options=['--seconds',args.seconds,'--tag',args.tag]
        if args.serial: options+=['--serial',args.serial]
        if args.adb: options+=['--adb',args.adb]
        run(SCRIPTS/'measure.py',*options); return
    if args.command=='diagnose':
        options=['--seconds','180','--wait-for-load','600']
        if args.serial:options+=['--serial',args.serial]
        run(SCRIPTS/'watch_memory.py',*options);return
    if args.command=='doctor':
        sdk=builder.find_sdk()
        print(json.dumps({'game':str(builder.find_game('auto')),'java':str(builder.find_java()),'adb':str(sdk['adb']),'cachedApk':(builder.WORK/'apktool_out.apk').is_file(),'config':str(ROOT/'config'/'quest'/'port.ini')},ensure_ascii=False,indent=2))
        subprocess.run([str(sdk['adb']),'devices','-l'],check=True); return
    if args.command=='test':
        run(SCRIPTS/'test_camera.py')
        run(SCRIPTS/'test_questcontrols.py')
        run(SCRIPTS/'test_flashpointer.py')
        run(SCRIPTS/'test_shiftcache.py')
        run(SCRIPTS/'test_gameperformance.py')
        run(SCRIPTS/'test_install.py');run(SCRIPTS/'test_plasmid.py')
        run(SCRIPTS/'test_flash.py');run(SCRIPTS/'test_weapon_geometry.py')
        run(SCRIPTS/'test_worldgpu.py','--shadows')
        run(SCRIPTS/'test_depth.py'); run(SCRIPTS/'test_reticle.py'); run(SCRIPTS/'test_gameplay.py'); run(SCRIPTS/'test_hud.py'); run(SCRIPTS/'test_hudpack.py'); run(SCRIPTS/'test_worldgpu.py','--hud'); run(SCRIPTS/'test_uistate.py'); run(SCRIPTS/'test_worldpose.py'); run(SCRIPTS/'test_worldgpu.py'); run(SCRIPTS/'test_pointer.py'); run(SCRIPTS/'test_transition.py'); run(SCRIPTS/'test_viewport.py'); run(SCRIPTS/'test_resolution.py'); return
    if args.command in ('build','auto'):
        options=['--yes']
        if (builder.WORK/'apktool_out.apk').exists(): options.append('--cached-apk')
        run(SCRIPTS/'builder.py',*options)
        run(SCRIPTS/'test_flashpointer.py')
        run(SCRIPTS/'test_depth.py')
        run(SCRIPTS/'test_reticle.py')
        run(SCRIPTS/'test_gameplay.py')
        run(SCRIPTS/'test_camera.py')
        run(SCRIPTS/'test_hud.py')
        run(SCRIPTS/'test_hudpack.py')
        run(SCRIPTS/'test_worldgpu.py','--hud')
        run(SCRIPTS/'test_uistate.py')
        run(SCRIPTS/'test_questcontrols.py')
        run(SCRIPTS/'test_flash.py')
        run(SCRIPTS/'test_weapon_geometry.py')
        run(SCRIPTS/'test_worldpose.py')
        run(SCRIPTS/'test_worldgpu.py')
        run(SCRIPTS/'test_worldgpu.py','--shadows')
        run(SCRIPTS/'test_pointer.py')
        run(SCRIPTS/'test_transition.py')
        run(SCRIPTS/'test_viewport.py')
        run(SCRIPTS/'test_resolution.py')
        run(SCRIPTS/'test_plasmid.py')
        run(SCRIPTS/'test_install.py')
        run(SCRIPTS/'test_gameperformance.py')
        run(SCRIPTS/'test_shiftcache.py')
    if args.command in ('auto','install','rollback','recover'):
        options=[]
        if args.serial: options+=['--serial',args.serial]
        if args.adb: options+=['--adb',args.adb]
        if args.command in ('rollback','recover'):
            backup=args.backup
            if not backup:
                if args.command=='recover':
                    info=json.loads((ROOT/'config'/'quest'/'recovery.json').read_text())
                    backup=ROOT/info['backup']
                else:
                    info=json.loads((ROOT/'dist'/'quest'/'last-install.json').read_text())
                    backup=Path(info['backup'])
            options+=['--rollback',backup]
        else:
            options+=['--fallback-current']
        run(SCRIPTS/'install.py',*options)

if __name__=='__main__':
    try: main()
    except (Exception,KeyboardInterrupt) as error:
        print(f'Quest operation stopped: {error}',file=sys.stderr); sys.exit(1)
