#!/usr/bin/env python3
"""Run one required permission case on already installed, SHA-verified APKs; no install/upload."""
import argparse, datetime, hashlib, json, pathlib, subprocess, sys, uuid
from production_permission_report import CASE_METHODS, validate, parse_current_report
from timeout_diagnostics import collect_timeout_diagnostics
p=argparse.ArgumentParser(description=__doc__)
for name in ('adb','serial','apk'):p.add_argument('--'+name,required=True)
p.add_argument('--test-apk')
p.add_argument('--self-instrumentation',action='store_true',help='Use the identical scenario in the lab APK; no test APK required')
p.add_argument('--case',choices=CASE_METHODS,required=True)
p.add_argument('--scope',choices=('full','common-owned'),default='full')
p.add_argument('--timeout',type=int,default=300)
a=p.parse_args()
if not a.self_instrumentation and not a.test_apk:p.error('--test-apk is required for the JUnit route')
root=pathlib.Path(__file__).resolve().parents[1];run_id=uuid.uuid4().hex
out=root/'build/evidence/production-permissions'/run_id;out.mkdir(parents=True)
def adb(*args,timeout=30):
    return subprocess.run([a.adb,'-s',a.serial,*args],capture_output=True,timeout=timeout,check=True)
metadata={'runId':run_id,'case':a.case,'scope':a.scope,'serial':a.serial,'runner':'self' if a.self_instrumentation else 'junit','startedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
report=None
def capture_report_file():
    result=subprocess.run([a.adb,'-s',a.serial,'exec-out','run-as','dev.artdcevm.lab','cat',
                           'files/production-permissions/'+run_id+'.json'],capture_output=True,timeout=10)
    (out/'report-file.log').write_bytes(result.stdout+result.stderr)
    if result.returncode:
        raise RuntimeError('Current permission report file unavailable (exit '+str(result.returncode)+')')
    recovered=parse_current_report(result.stdout,run_id,a.case,a.scope)
    (out/'report-file.json').write_text(json.dumps(recovered,indent=2))
    metadata['reportFileCaptured']=True
    if report is not None and report!=recovered:
        metadata.update(passed=False,errors=metadata.get('errors',[])+['Persisted report differs from instrumentation result'])
    if report is None:
        (out/'report.json').write_text(json.dumps(recovered,indent=2))
        metadata['recoveredReportErrors']=validate(recovered,run_id,a.case,a.scope)
        # A saved report cannot prove that the JUnit runner finished correctly.
        # It supplies diagnosis after a timeout, never a successful test result.
try:
    installed=[('dev.artdcevm.lab',a.apk,'appSha256')]
    if not a.self_instrumentation:installed.append(('dev.artdcevm.lab.test',a.test_apk,'testSha256'))
    for package,path,key in installed:
        expected=hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
        paths=adb('shell','pm','path',package).stdout.decode().strip().splitlines()
        if len(paths)!=1 or not paths[0].startswith('package:/'):raise RuntimeError('Expected one installed base APK: '+package)
        actual=hashlib.sha256(adb('exec-out','cat',paths[0][8:]).stdout).hexdigest()
        if expected!=actual:raise RuntimeError('Installed APK SHA mismatch: '+package)
        metadata[key]=actual
    for package in ('dev.artdcevm.lab.test','dev.artdcevm.lab'):adb('shell','am','force-stop',package)
    command=['shell','am','instrument','-w','-r','-e','class','com.github.ytlog.artdcevm.testing.ProductionFinalPermissionTest#'+CASE_METHODS[a.case],'-e','productionPermissionScope',a.scope,'-e','productionRunId',run_id,'dev.artdcevm.lab.test/androidx.test.runner.AndroidJUnitRunner']
    if a.self_instrumentation:
        command=['shell','am','instrument','-w','-r','-e','productionPermissionCase',a.case,'-e','productionPermissionScope',a.scope,'-e','productionRunId',run_id,'dev.artdcevm.lab/com.github.ytlog.artdcevm.testing.ProductionPermissionInstrumentation']
    try:result=adb(*command,timeout=a.timeout)
    except subprocess.TimeoutExpired as error:
        (out/'instrumentation.log').write_bytes((error.stdout or b'')+(error.stderr or b''))
        metadata['instrumentationTimedOut']=True
        metadata['timeoutDiagnostics']=collect_timeout_diagnostics(a.adb,a.serial,out,run_id)
        raise
    raw=result.stdout.decode(errors='replace')+result.stderr.decode(errors='replace');(out/'instrumentation.log').write_text(raw)
    prefix='INSTRUMENTATION_RESULT: productionPermissionReport='
    lines=[line[len(prefix):] for line in raw.splitlines() if line.startswith(prefix)]
    if len(lines)!=1:raise RuntimeError('Missing unique required permission report')
    report=parse_current_report(lines[0],run_id,a.case,a.scope);(out/'report.json').write_text(json.dumps(report,indent=2))
    errors=validate(report,run_id,a.case,a.scope)
    if a.self_instrumentation:
        if raw.splitlines().count('INSTRUMENTATION_RESULT: permissionRunnerCompleted=true')!=1 or raw.splitlines().count('INSTRUMENTATION_CODE: -1')!=1:
            errors.append('Self instrumentation did not complete the required scenario')
    elif 'OK (1 test)' not in raw:errors.append('JUnit did not pass exactly one required test')
    metadata.update(passed=not errors,errors=errors)
except Exception as error:
    metadata.update(passed=False,errors=[str(error)])
finally:
    try:capture_report_file()
    except Exception as error:
        metadata['reportFileCaptured']=False;metadata['reportFileError']=str(error)
    (out/'result.json').write_text(json.dumps(metadata,indent=2));print(json.dumps(metadata));print(out)
sys.exit(0 if metadata.get('passed') else 1)
