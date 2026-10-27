"""Self-contained terminal installer. No GUI or runtime downloads."""
from pathlib import Path
import argparse
import json
import os
import shutil
import subprocess
import sys
import threading
from urllib.parse import urlsplit
import webbrowser
from core import Installer,InstallError,Cancelled,TARGETS,PAYLOAD,verify_payload,fast_find_game

def clean(text):
    return ''.join(c if c.isprintable() else ' ' for c in str(text))

def donation_url(payload=PAYLOAD):
    config=json.loads((Path(payload)/'config/distribution.json').read_text(encoding='utf-8'))
    url=config.get('donation_url','').strip()
    if not url:return None
    parts=urlsplit(url)
    if parts.scheme!='https' or parts.hostname not in ('ko-fi.com','www.ko-fi.com') or parts.username or parts.password or parts.port not in (None,443) or parts.path in ('','/'):
        raise InstallError('The bundled Ko-fi link is invalid.')
    return url

class Terminal:
    def __init__(self,read=None,output=None,cancel=None):
        self.read=read or input;self.output=output or sys.stdout
        self.cancel=cancel or threading.Event();self.lock=threading.Lock()
        self.percent=None;self.label='';self.previous_width=0;self.animated=self.output.isatty()
    def line(self,text=''):
        print(text,file=self.output,flush=True)
    def ask(self,text):
        return self.read(text).strip()
    def progress(self,percent,text):
        with self.lock:
            self.percent=max(self.percent or 0,min(100,max(0,percent)));self.label=clean(text)
    def draw(self,index):
        if not self.animated:return
        with self.lock:percent,label=self.percent,self.label
        if percent is None:prefix=''
        else:
            filled=int(percent*24/100)
            prefix='['+'#'*filled+'-'*(24-filled)+'] %3d%% '%percent
        width=max(40,shutil.get_terminal_size((90,24)).columns-1)
        row=(prefix+'|/-\\'[index%4]+' '+label)[:width]
        self.output.write('\r'+row+' '*max(0,self.previous_width-len(row)))
        self.output.flush();self.previous_width=len(row)
    def work(self,operation,label,reset=True):
        with self.lock:
            self.label=label
            if reset:self.percent=None
        if not self.animated:self.line(label)
        result=[];failure=[]
        def worker():
            try:result.append(operation())
            except BaseException as error:failure.append(error)
        thread=threading.Thread(target=worker,daemon=False);frame=0
        try:
            thread.start()
            while thread.is_alive():
                try:
                    self.draw(frame);frame+=1;thread.join(.08)
                except KeyboardInterrupt:
                    self.cancel.set()
                    with self.lock:self.label='Stopping. Please wait for the current step and rollback to finish…'
        finally:
            if thread.is_alive():
                self.cancel.set()
                while thread.is_alive():
                    try:thread.join(.08)
                    except KeyboardInterrupt:self.cancel.set()
        if self.animated:
            self.draw(frame);self.line();self.previous_width=0
        if failure:raise failure[0]
        if self.cancel.is_set():raise Cancelled('Operation cancelled.')
        return result[0] if result else None

def select_target(ui):
    ui.line('1. Choose your headset')
    values=list(TARGETS)
    for index,key in enumerate(values,1):ui.line('   %d. %s'%(index,TARGETS[key]))
    while True:
        value=ui.ask('Headset number [1]: ') or '1'
        if value.isdigit() and 1<=int(value)<=len(values):return values[int(value)-1]
        ui.line('Enter a number from the list.')

def install_driver(ui,payload=PAYLOAD):
    import ctypes
    files=list((Path(payload)/'tools/MetaADB').rglob('android_winusb.inf'))
    if len(files)!=1:raise InstallError('The bundled USB driver was not found.')
    tool=Path(os.environ.get('SystemRoot','C:/Windows'))/'System32/pnputil.exe'
    args=subprocess.list2cmdline(['/add-driver',str(files[0]),'/install'])
    result=ctypes.windll.shell32.ShellExecuteW(None,'runas',str(tool),args,None,0)
    if result<=32:ui.line('Windows did not allow the driver installation.')
    else:ui.line('Wait for Windows to install the driver, then reconnect USB.')

def select_device(ui,engine,driver=None):
    ui.line('\n2. Choose a USB device')
    ui.line('Connect your Quest and allow USB debugging inside the headset.')
    while True:
        devices=ui.work(engine.devices,'Looking for connected devices…')
        ready=[device for device in devices if device['state']=='device']
        for index,device in enumerate(ready,1):ui.line('   %d. %s  [%s]'%(index,clean(device['model']),clean(device['serial'])))
        if any(device['state']=='unauthorized' for device in devices):ui.line('A device needs authorization: allow USB debugging inside the headset.')
        if not ready:
            ui.line('No devices are ready. Check the USB cable and Developer Mode.')
            answer=ui.ask('Enter — scan again; D — install the bundled USB driver: ')
        else:
            default=' [1]' if len(ready)==1 else ''
            answer=ui.ask('Device number%s; R — refresh: '%default)
            if not answer and len(ready)==1:answer='1'
            if answer.isdigit() and 1<=int(answer)<=len(ready):return ready[int(answer)-1]['serial']
        if answer.lower() in ('d','д'):(driver or (lambda:install_driver(ui,engine.payload)))()
        elif answer and answer.lower() not in ('r','к'):ui.line('Choose a device number or scan again.')

def run_installation(ui,engine,finder=None,browser=None,driver=None):
    engine.cancel=ui.cancel;engine.progress=ui.progress
    url=donation_url(engine.payload)
    ui.line('BioShock VR — Meta Quest Installer')
    ui.line('For the original BioShock 2007. All required tools are included.')
    ui.line('Press Ctrl+C to cancel.\n')
    target=select_target(ui)
    serial=select_device(ui,engine,driver)
    game=ui.work(finder or fast_find_game,'Looking for BioShock 2007 on this PC…')
    while game is None:
        ui.line('Game not found automatically. Select the original BioShock 2007, not Remastered.')
        path=ui.ask('Game folder: ').strip('"')
        if not path:continue
        candidate=Path(path).expanduser()
        from core import builder
        if builder.is_game_dir(candidate):game=candidate
        else:ui.line('This folder does not contain BioShock 2007.')
    ui.line('\nGame: '+str(game))
    ui.line('Headset: '+TARGETS[target]+'; USB: '+serial)
    ui.line('\n3. Preparing and installing the game')
    ui.work(lambda:engine.prepare(game,target),'Preparing the game…')
    receipt=ui.work(lambda:engine.install(serial),'Transferring and installing…',reset=False)
    if receipt.get('status')!='installed':raise InstallError('Installation did not finish. The support page was not opened.')
    engine.serial=serial
    try:
        ui.work(lambda:engine.shell('am','start','-n','com.winlator.cmod/com.bioshockport.Launcher'),'Starting BioShock VR…')
        ui.line('\nInstallation complete. BioShock VR has started on your headset.')
    except (InstallError,Cancelled) as error:
        ui.line('\nInstallation complete. Open BioShock VR under Unknown Sources on your headset.')
        ui.line('Automatic launch failed: '+clean(error))
    ui.line('On first launch, wait for the Windows environment to finish setting up.')
    if url and not ui.cancel.is_set():
        choice=ui.ask('\nSupport the project on Ko-fi? [y/N]: ').lower()
        if choice in ('д','да','y','yes','1'):
            try:
                opened=(browser or webbrowser.open)(url,new=2)
                if not opened:ui.line('Open this link in your browser: '+url)
            except Exception:ui.line('Open this link in your browser: '+url)
    return receipt

def main():
    for stream in (sys.stdin,sys.stdout,sys.stderr):
        if hasattr(stream,'reconfigure'):
            try:stream.reconfigure(encoding='utf-8',errors='replace')
            except (OSError,ValueError):pass
    parser=argparse.ArgumentParser(description='Automatic BioShock VR installation over USB')
    parser.add_argument('--self-check',type=Path);parser.add_argument('--prepare-only',type=Path);parser.add_argument('--workspace',type=Path)
    args=parser.parse_args()
    if args.self_check:
        report={'payload':False,'adb':False,'terminal':True,'donationOptIn':True}
        try:
            manifest=verify_payload();report.update(payload=True,version=manifest['version'],embeddedFiles=len(manifest['files']))
            report['adb']=Installer().run([PAYLOAD/'tools/adb.exe','version']).returncode==0
            report['donationUrl']=donation_url()
        except Exception as error:report['error']=str(error)
        args.self_check.write_text(json.dumps(report,indent=2),encoding='utf-8')
        return 0 if report['payload'] and report['adb'] and 'error' not in report else 1
    if args.prepare_only:
        Installer(workspace=args.workspace).prepare(args.prepare_only,'quest3');return 0
    ui=Terminal();engine=Installer(workspace=args.workspace,cancel=ui.cancel)
    try:run_installation(ui,engine);return 0
    except (KeyboardInterrupt,EOFError,Cancelled):ui.line('\nInstallation cancelled.');return 2
    except Exception as error:
        ui.line('\nInstallation could not be completed: '+clean(error))
        try:ui.ask('Press Enter to close…')
        except (KeyboardInterrupt,EOFError):pass
        return 1

if __name__=='__main__':sys.exit(main())
