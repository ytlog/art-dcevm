#!/usr/bin/env python3
"""Verify exact installed artifacts and formal production method transactions."""
import argparse,hashlib,json,pathlib,subprocess,sys,uuid,re
from apk_verification import require_install_success,base_apk_path,verified_apk_sha256
from evidence_paths import new_evidence_directory
from production_bootstrap_report import validate_production_bootstrap
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);p.add_argument('--adb',required=True);p.add_argument('--apk',required=True);p.add_argument('--test-apk',required=True);p.add_argument('--foreground',action='store_true',help='Require same-process foreground carrier evidence');p.add_argument('--require-fields',action='store_true',help='Require formal field removal, cached JNI/reflection/MethodHandle/VarHandle coverage and retained state');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];out=new_evidence_directory(root/'build/evidence/production-bootstrap','run')
run_id=uuid.uuid4().hex
instrumentation_started=False
report={'serial':a.serial,'runId':run_id,'foregroundRequested':a.foreground,'passed':False,'scope':'production bootstrap and formal method deletion with four cached JNI invocation modes and reflection; field deletion and full evolution are not established by this suite'}
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
 for key,path,package in [('application',a.apk,'dev.artdcevm.lab'),('test',a.test_apk,'dev.artdcevm.lab.test')]:
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
 result=adb('instrumentation','shell','am','instrument','-w','-r','-e','class','com.github.ytlog.artdcevm.testing.ProductionCapabilitiesTest','-e','productionRunId',run_id,*(['-e','foreground','true'] if a.foreground else []),'dev.artdcevm.lab.test/androidx.test.runner.AndroidJUnitRunner');result.check_returncode()
 logs=adb('device','logcat','-d','-s','ArtDcevmProduction:I','artdcevm:E','AndroidRuntime:E');logs.check_returncode()
 transaction=validate_production_bootstrap(result.stdout,logs.stdout,run_id,require_fields=a.require_fields,require_foreground=a.foreground)
 report['transaction']=transaction
 (out/'transaction.json').write_text(json.dumps(transaction,indent=2)+'\n')
 report['passed']=True
except (RuntimeError,OSError,ValueError,subprocess.SubprocessError) as error:
 report['passed']=False;report['error']=str(error)
finally:
 if a.foreground and instrumentation_started:
  try:
   cleanup=adb('foreground-stop','shell','am','force-stop','dev.artdcevm.lab',timeout=15);cleanup.check_returncode()
  except (OSError,subprocess.SubprocessError) as error:
   report['passed']=False;report['foregroundCleanupError']=str(error)
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));print(out);sys.exit(0 if report['passed'] else 1)
