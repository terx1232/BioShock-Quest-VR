"""Actual terminal flow with controlled USB/install/browser edges."""
from pathlib import Path
import io,json,sys,tempfile,unittest,threading,time,_thread
sys.path.insert(0,str(Path(__file__).parent))
import installer
import core
URL='https://ko-fi.com/kirillbaal'

class Engine:
 def __init__(self,payload,events,devices=None,failure=None):
  self.payload=payload;self.events=events;self.failure=failure
  self.scans=iter(devices or [[{'serial':'first','model':'Quest 3','state':'device'},{'serial':'chosen','model':'Quest 2','state':'device'}]])
 def devices(self):self.events.append(('devices',));return next(self.scans)
 def prepare(self,game,target):
  self.events.append(('prepare',str(game),target));self.progress(45,'Game prepared')
  if self.failure=='prepare':raise core.InstallError('Preparation failed')
  if self.failure=='cancel':raise core.Cancelled('Cancelled')
 def install(self,serial):
  self.events.append(('install',serial));self.progress(100,'Installed')
  if self.failure=='install':raise core.InstallError('Install failed')
  return {'status':'installed'}
 def shell(self,*args):
  self.events.append(('launch',self.serial,*args))
  if self.failure=='launch':raise core.InstallError('Launch failed')

class Tests(unittest.TestCase):
 def test_ctrl_c_waits_for_worker_cancellation(self):
  ui=installer.Terminal(output=io.StringIO());acknowledged=threading.Event()
  def operation():
   timer=threading.Timer(.05,_thread.interrupt_main);timer.start()
   try:
    limit=time.monotonic()+2
    while time.monotonic()<limit:
     if ui.cancel.is_set():acknowledged.set();raise core.Cancelled('Stopped safely')
     time.sleep(.01)
    raise AssertionError('Worker did not receive cancellation')
   finally:timer.cancel()
  with self.assertRaises(core.Cancelled):ui.work(operation,'Cancelling')
  self.assertTrue(acknowledged.is_set())

 def execute(self,folder,answers,**kwargs):
  payload=Path(folder);(payload/'config').mkdir(exist_ok=True);(payload/'config/distribution.json').write_text(json.dumps({'donation_url':URL}))
  events=[];output=io.StringIO();values=iter(answers)
  def read(prompt):output.write(prompt);return next(values)
  ui=installer.Terminal(read=read,output=output);engine=Engine(payload,events,**kwargs)
  def browser(url,**options):events.append(('browser',url,options));return True
  return ui,engine,events,output,browser

 def test_explicit_usb_choice_automatic_install_then_opt_in_browser(self):
  with tempfile.TemporaryDirectory() as folder:
   ui,engine,events,out,browser=self.execute(folder,['2','2','yes'])
   receipt=installer.run_installation(ui,engine,finder=lambda:Path('D:/StockBioShock'),browser=browser)
   self.assertEqual(receipt['status'],'installed')
   self.assertEqual(events[1:3],[('prepare',str(Path('D:/StockBioShock')),'quest3s'),('install','chosen')])
   self.assertEqual(events[-1],('browser',URL,{'new':2}));self.assertEqual(events[-2][0:2],('launch','chosen'))
   self.assertIn('Quest 2',out.getvalue())
   self.assertNotRegex(out.getvalue(),'[А-Яа-яЁё]')
   self.assertIn('1. Choose your headset',out.getvalue())
   self.assertNotIn('Pico',out.getvalue())

 def test_donation_default_and_no_never_open_browser(self):
  for answer in ('','нет','n'):
   with self.subTest(answer=answer),tempfile.TemporaryDirectory() as folder:
    ui,engine,events,out,browser=self.execute(folder,['1','1',answer])
    installer.run_installation(ui,engine,finder=lambda:Path('D:/StockBioShock'),browser=browser)
    self.assertFalse(any(event[0]=='browser' for event in events))
    self.assertIn('Installation complete',out.getvalue());self.assertIn('[y/N]',out.getvalue())

 def test_failed_or_cancelled_install_never_opens_donation_or_launches(self):
  for failure in ('prepare','install','cancel'):
   with self.subTest(failure=failure),tempfile.TemporaryDirectory() as folder:
    ui,engine,events,out,browser=self.execute(folder,['1','1'],failure=failure)
    with self.assertRaises(core.InstallError):installer.run_installation(ui,engine,finder=lambda:Path('D:/StockBioShock'),browser=browser)
    self.assertFalse(any(event[0] in ('browser','launch') for event in events));self.assertNotIn('Support the project',out.getvalue())

 def test_usb_wait_authorization_and_driver_precede_all_writes(self):
  with tempfile.TemporaryDirectory() as folder:
   scans=[[],[{'serial':'later','model':'Quest 2','state':'unauthorized'}],[{'serial':'later','model':'Quest 2','state':'device'}]]
   ui,engine,events,out,browser=self.execute(folder,['1','d','','',''],devices=scans)
   installer.run_installation(ui,engine,finder=lambda:Path('D:/StockBioShock'),browser=browser,driver=lambda:events.append(('driver',)))
   self.assertEqual(events[:4],[('devices',),('driver',),('devices',),('devices',)])
   self.assertEqual(events[5],('install','later'));self.assertIn('allow USB debugging',out.getvalue())

 def test_launch_failure_keeps_install_success(self):
  with tempfile.TemporaryDirectory() as folder:
   ui,engine,events,out,browser=self.execute(folder,['1','1',''],failure='launch')
   receipt=installer.run_installation(ui,engine,finder=lambda:Path('D:/StockBioShock'),browser=browser)
   self.assertEqual(receipt['status'],'installed');self.assertFalse(any(e[0]=='browser' for e in events));self.assertIn('Open BioShock VR',out.getvalue())

 def test_exact_https_kofi_destination(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);(root/'config').mkdir()
   for url in ('http://ko-fi.com/kirillbaal','https://ko-fi.com.evil.test/kirillbaal','https://user@ko-fi.com/kirillbaal','https://ko-fi.com/'):
    (root/'config/distribution.json').write_text(json.dumps({'donation_url':url}))
    with self.assertRaises(core.InstallError):installer.donation_url(root)
   (root/'config/distribution.json').write_text(json.dumps({'donation_url':URL}));self.assertEqual(installer.donation_url(root),URL)

if __name__=='__main__':unittest.main(verbosity=2)
