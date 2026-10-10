#!/usr/bin/env python3
"""Verify exact installed artifacts and formal production method transactions."""
import argparse,hashlib,json,pathlib,subprocess,sys,uuid,re
from apk_verification import require_install_success,base_apk_path,verified_apk_sha256
from timeout_diagnostics import collect_timeout_diagnostics
from evidence_paths import new_evidence_directory
from production_bootstrap_report import SOURCE_EPISODES,validate_production_bootstrap_payload
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);p.add_argument('--adb',required=True);p.add_argument('--apk',required=True);p.add_argument('--test-apk');p.add_argument('--self-instrumentation',action='store_true',help='Run the identical bootstrap suite from only the lab application APK');p.add_argument('--foreground',action='store_true',help='Require same-process foreground carrier evidence');p.add_argument('--require-fields',action='store_true',help='Require formal field removal, cached JNI/reflection/MethodHandle/VarHandle coverage and retained state');p.add_argument('--require-source-episode',choices=SOURCE_EPISODES,action='append',default=[],help='Require a calibrated source episode and empty diagnostic in this exact run; repeat for multiple episodes');a=p.parse_args()
if not a.self_instrumentation and not a.test_apk:p.error('--test-apk is required for the default JUnit route')
root=pathlib.Path(__file__).resolve().parents[1];out=new_evidence_directory(root/'build/evidence/production-bootstrap','run')
run_id=uuid.uuid4().hex
instrumentation_started=False
instrumentation_timed_out=False
report={'runner':'self' if a.self_instrumentation else 'junit','serial':a.serial,'runId':run_id,'foregroundRequested':a.foreground,'requiredSourceEpisodes':a.require_source_episode,'passed':False,'scope':'production bootstrap and formal method deletion with four cached JNI invocation modes and reflection; field deletion and full evolution are not established by this suite'}
if a.require_fields:
 report['scope']='production bootstrap and formal method/field deletion with cached JNI/reflection/MethodHandle/VarHandle rejection and retained state; full evolution is not established by this suite'
def adb(label,*args,timeout=120):
 try:
  r=subprocess.run([a.adb,'-s',a.serial,*args],text=True,capture_output=True,timeout=timeout)
 except subprocess.TimeoutExpired as error:
  def decoded(value):return value.decode(errors='replace') if isinstance(value,bytes) else value or ''
  (out/(label+'.log')).write_text(decoded(error.stdout)+decoded(error.stderr)+'\n'+str(error)+'\n')
  raise
 (out/(label+'.log')).write_text(r.stdout+r.stderr)
 return r
def diagnose_install_timeout(label):
 # Bounded, read-only collection: never retry/abandon an installation session.
 diagnostics={'stage':label,'confirmationVisible':False,'commands':{}}
 for name,args in [('activity',('shell','dumpsys','activity','activities')),('sessions',('shell','dumpsys','package'))]:
  try:
   result=adb(label+'-timeout-'+name,*args,timeout=8)
   diagnostics['commands'][name]={'exitCode':result.returncode,'log':str(out/(label+'-timeout-'+name+'.log'))}
   if name=='activity' and result.returncode==0:
    diagnostics['confirmationVisible']='android.content.pm.action.CONFIRM_INSTALL' in result.stdout and re.search(r'^\s*(?:topResumedActivity|mResumedActivity|ResumedActivity)[=:].*com\.android\.packageinstaller/[^\n]*PackageInstallerActivity',result.stdout,re.M) is not None
  except (OSError,subprocess.SubprocessError) as error:
   diagnostics['commands'][name]={'error':str(error)}
 report['installTimeoutDiagnostics']=diagnostics
 return diagnostics
try:
 artifacts=[('application',a.apk,'dev.artdcevm.lab')]
 if not a.self_instrumentation:artifacts.append(('test',a.test_apk,'dev.artdcevm.lab.test'))
 for key,path,package in artifacts:
  expected=hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
  report[key+'RequestedSha256']=expected
  try:
   installed=adb(key+'-install','install','-r',path)
  except subprocess.TimeoutExpired as error:
   diagnostics=diagnose_install_timeout(key+'-install')
   suffix='; package installer confirmation is visible: confirm the existing installation on the device before another run' if diagnostics['confirmationVisible'] else '; inspect preserved install output and read-only timeout diagnostics'
   raise RuntimeError(str(error)+suffix) from error
  require_install_success(installed.returncode,installed.stdout,installed.stderr)
  paths=adb(key+'-path','shell','pm','path',package);paths.check_returncode()
  sha=adb(key+'-hash','shell','sha256sum',base_apk_path(paths.stdout));sha.check_returncode()
  report[key+'Sha256']=verified_apk_sha256(sha.stdout,expected)
 stopped=adb('stop','shell','am','force-stop','dev.artdcevm.lab');stopped.check_returncode()
 instrumentation_started=True
 command=['shell','am','instrument','-w','-r']
 if not a.self_instrumentation:command+=['-e','class','com.github.ytlog.artdcevm.testing.ProductionCapabilitiesTest']
 command+=['-e','productionRunId',run_id]+(['-e','foreground','true'] if a.foreground else [])
 command.append('dev.artdcevm.lab/com.github.ytlog.artdcevm.testing.ProductionCapabilitiesInstrumentation' if a.self_instrumentation else 'dev.artdcevm.lab.test/androidx.test.runner.AndroidJUnitRunner')
 result=adb('instrumentation',*command);result.check_returncode()
 persisted=adb('transaction-file','exec-out','run-as','dev.artdcevm.lab','cat','files/production-bootstrap/'+run_id+'.json',timeout=15);persisted.check_returncode()
 transaction=validate_production_bootstrap_payload(result.stdout,persisted.stdout,run_id,require_fields=a.require_fields,require_foreground=a.foreground,self_instrumentation=a.self_instrumentation,require_source_episodes=a.require_source_episode)
 report['reportFileCaptured']=True
 # Logcat is diagnostic only: large valid transaction JSON may be truncated by Android.
 try:
  logs=adb('device','logcat','-d','-s','ArtDcevmProduction:I','artdcevm:E','AndroidRuntime:E',timeout=15)
  report['deviceLogExitCode']=logs.returncode
 except (OSError,subprocess.SubprocessError) as log_error:report['deviceLogError']=str(log_error)
 report['transaction']=transaction
 (out/'transaction.json').write_text(json.dumps(transaction,indent=2)+'\n')
 report['passed']=True
except (RuntimeError,OSError,ValueError,subprocess.SubprocessError) as error:
 if isinstance(error,subprocess.TimeoutExpired) and instrumentation_started:
  instrumentation_timed_out=True
  try:report['timeoutDiagnostics']=collect_timeout_diagnostics(a.adb,a.serial,out,run_id)
  except Exception as diagnostic_error:report['timeoutDiagnosticError']=str(diagnostic_error)
 report['passed']=False;report['error']=str(error)
finally:
 if instrumentation_started and (a.foreground or instrumentation_timed_out):
  try:
   cleanup=adb('foreground-stop','shell','am','force-stop','dev.artdcevm.lab',timeout=15);cleanup.check_returncode()
  except (OSError,subprocess.SubprocessError) as error:
   report['passed']=False;report['foregroundCleanupError']=str(error)
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));print(out);sys.exit(0 if report['passed'] else 1)
