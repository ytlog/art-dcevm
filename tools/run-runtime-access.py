#!/usr/bin/env python3
"""Run the isolated owned JNI probe without initializing private evolution."""
import argparse,hashlib,json,re,subprocess,shutil,uuid
from evidence_paths import new_evidence_directory
from apk_verification import require_install_success,base_apk_path,verified_apk_sha256
from owned_removal_report import validate_owned_removal,validate_active_frames
from pathlib import Path
from timeout_diagnostics import collect_timeout_diagnostics
from crash_diagnostics import collect_crash_diagnostics
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial',required=True)
p.add_argument('--apk',type=Path,default=Path('app/build/outputs/apk/debug/app-debug.apk'))
p.add_argument('--adb',default=shutil.which('adb'))
p.add_argument('--skip-install',action='store_true')
p.add_argument('--foreground',action='store_true',help='Require the independent foreground carrier in the instrumentation process')
p.add_argument('--runtime-services',action='store_true',help='Require source-family Runtime service calibration')
p.add_argument('--exclusive-roots',action='store_true',help='Require actual exclusive strong-root and monitor/GC recovery')
p.add_argument('--native-cache',action='store_true',help='Require synchronous real native interpreter-cache clearing')
p.add_argument('--jit-receivers',action='store_true',help='Require actual Java/JVMTI JIT/cache/heap observation')
p.add_argument('--method-guards',action='store_true',help='Require real retired JNI and reflection invocation rejection')
p.add_argument('--field-storage',action='store_true',help='Require native raw field writes, card marking and monitor oracles')
p.add_argument('--active-frames',action='store_true',help='Require independent actual compiled carrier/two-inlinee map, DEX PC and live-root proof')
a=p.parse_args()
if not a.adb:p.error("adb not found; pass --adb")
out=new_evidence_directory(Path('build/evidence/runtime-access'))
run_id=uuid.uuid4().hex
metadata={'runId':run_id,'serial':a.serial,'apkSha256':hashlib.sha256(a.apk.read_bytes()).hexdigest(),'passed':False,'foregroundRequested':a.foreground,'activeFramesRequested':a.active_frames,'scope':'isolated-owned-JNI; no full evolution initialization'}
def adb(label,*args,timeout=180):
    try:
        r=subprocess.run([a.adb,'-s',a.serial,*args],capture_output=True,text=True,timeout=timeout)
    except subprocess.TimeoutExpired as error:
        def decoded(value):return value.decode(errors='replace') if isinstance(value,bytes) else value or ''
        (out/(label+'.txt')).write_text(decoded(error.stdout)+decoded(error.stderr)+'\n'+str(error)+'\n')
        raise
    (out/(label+'.txt')).write_text(r.stdout+r.stderr)
    if label=='install':require_install_success(r.returncode,r.stdout,r.stderr)
    r.check_returncode();return r.stdout
instrumentation_started=False
instrumentation_timed_out=False
instrumentation_report_received=False
try:
    if not a.skip_install:
        adb('install','install','-r',str(a.apk))
    paths=adb('package-path','shell','pm','path','dev.artdcevm.lab')
    file=base_apk_path(paths)
    metadata['installedApkSha256']=verified_apk_sha256(adb('installed-hash','shell','sha256sum',file),metadata['apkSha256'])
    adb('stop','shell','am','force-stop','dev.artdcevm.lab')
    instrumentation_started=True
    output=adb('instrumentation','shell','am','instrument','-w','-r','-e','runtimeRunId',run_id,*[item for key,enabled in [('runtimeServices',a.runtime_services),('exclusiveRoots',a.exclusive_roots),('nativeCache',a.native_cache),('jitReceivers',a.jit_receivers),('methodGuards',a.method_guards),('fieldStorage',a.field_storage),('activeFrames',a.active_frames),('foreground',a.foreground)] if enabled for item in ['-e',key,'true']],'dev.artdcevm.lab/com.github.ytlog.artdcevm.testing.RuntimeAccessInstrumentation')
    match=re.search(r'^INSTRUMENTATION_RESULT: stream=(.*)$',output,re.M)
    if not match:
        reason=re.search(r'^INSTRUMENTATION_(?:RESULT|FAILED): (.*)$',output,re.M)
        raise RuntimeError('Instrumentation produced no JSON probe report'+(': '+reason.group(1).strip() if reason else '; inspect instrumentation.txt'))
    report=json.loads(match.group(1));instrumentation_report_received=isinstance(report,dict);(out/'probe.json').write_text(json.dumps(report,indent=2)+'\n')
    code=re.search(r'^INSTRUMENTATION_CODE: (.*)$',output,re.M)
    if not code or code.group(1).strip()!='-1':
        raise RuntimeError(report.get('error') or 'Instrumentation did not report success')
    metadata['passed']=report.get('passed') is True and report.get('mainCallbackHits')==128 and report.get('workerCallbackHits')==[128,128] and report.get('privateEvolutionInitialized') is False
    routes={'from-reflected-method','new-global-ref'}
    metadata['passed']=metadata['passed'] and report.get('mainRoute') in routes and len(report.get('workerRoutes',[]))==2 and all(route in routes for route in report['workerRoutes'])
    metadata['passed']=metadata['passed'] and report.get('mainContextChecks')==384 and report.get('workerContextChecks')==[384,384]
    model=report.get('mainMethodModel')
    direct=report.get('methodDexDirectCount');virtual=report.get('methodDexVirtualCount')
    indices=report.get('methodDexIndices',[])
    complete=isinstance(direct,int) and isinstance(virtual,int) and direct>=0 and virtual>=0 and len(indices)==direct+virtual and len(indices)>=3 and len(set(indices))==len(indices)
    metadata['passed']=metadata['passed'] and complete and model in (11,12) and report.get('workerMethodModels')==[model,model] and report.get('mainMethodTableCount')==len(indices) and report.get('workerMethodTableCounts')==[len(indices),len(indices)]
    if a.runtime_services or a.native_cache or a.jit_receivers or a.method_guards or a.active_frames:metadata['passed']=metadata['passed'] and report.get('runtimeServicesCalibrated') is True
    if a.exclusive_roots:metadata['passed']=metadata['passed'] and report.get('exclusiveMonitorIterations')==32 and report.get('exclusiveFailureRestoration') is True and report.get('exclusiveGcCollections',0)>0
    if a.native_cache:metadata['passed']=metadata['passed'] and report.get('nativeCacheClearedThreads',0)>0
    if a.jit_receivers or a.active_frames:metadata['passed']=metadata['passed'] and report.get('jitServices')=='jit,code-cache,heap'
    if a.method_guards:metadata['passed']=metadata['passed'] and report.get('methodEntryGuardsCalibrated') is True
    if a.field_storage:metadata['passed']=metadata['passed'] and report.get('nativeFieldStorageCalibrated') is True
    if a.method_guards:
        metadata['passed']=metadata['passed'] and report.get('fieldEntryGuardsCalibrated') is True and report.get('methodHandleFieldGuardsCalibrated') is True and report.get('methodHandleFieldGuardChecks')==12 and report.get('retirementCleanupCalibrated') is True
    if a.foreground:
        pid=report.get('foregroundInstrumentationPid')
        if not (report.get('foregroundRequested') is True and report.get('foregroundActivity')=='com.github.ytlog.artdcevm.testing.RuntimeAccessForegroundActivity' and type(pid) is int and pid>0 and type(report.get('foregroundActivityPid')) is int and report.get('foregroundActivityPid')==pid and report.get('foregroundResumedBefore') is True and report.get('foregroundResumedAfter') is True and type(report.get('foregroundPauseCount')) is int and report.get('foregroundPauseCount')==0 and report.get('foregroundFinished') is True):
            raise RuntimeError('Foreground carrier lifecycle/process proof missing or inconsistent')
    if a.runtime_services and a.jit_receivers:
        require_fields=a.method_guards and a.field_storage
        metadata['scope']=('owned native method/field deletion and cached JNI/reflection/MethodHandle/VarHandle rejection' if require_fields else 'owned native method deletion and cached JNI/reflection/MethodHandle rejection')+'; no full evolution initialization'
        validate_owned_removal(report,require_fields=require_fields)
        metadata['ownedRemovalPassed']=True
    if a.active_frames:
        metadata['activeFrameProofPassed']=False
        if report.get('runtimeServicesCalibrated') is not True or report.get('jitServices')!='jit,code-cache,heap':
            raise RuntimeError('Missing independent runtime/JIT receiver prerequisites for requested active-frame proof')
        validate_active_frames(report)
        metadata['activeFrameProofPassed']=True
        metadata['scope']=metadata['scope'].replace('; no full evolution initialization','; owned compiled carrier/two-inlinee map, DEX PC and live-root proof; no full evolution initialization')
except Exception as error:
    if isinstance(error,subprocess.TimeoutExpired) and instrumentation_started:
        instrumentation_timed_out=True
        try:metadata['timeoutDiagnostics']=collect_timeout_diagnostics(a.adb,a.serial,out,run_id)
        except Exception as diagnostic_error:metadata['timeoutDiagnosticError']=str(diagnostic_error)
    elif instrumentation_started and not instrumentation_report_received:
        try:metadata['crashDiagnostics']=collect_crash_diagnostics(a.adb,a.serial,out,run_id)
        except Exception as diagnostic_error:metadata['crashDiagnosticError']=str(diagnostic_error)
    metadata['passed']=False
    metadata['error']=str(error)
finally:
    if instrumentation_started and (a.foreground or instrumentation_timed_out):
        try:adb('foreground-stop','shell','am','force-stop','dev.artdcevm.lab',timeout=15)
        except Exception as error:
            metadata['passed']=False
            metadata['foregroundCleanupError']=str(error)
(out/'run.json').write_text(json.dumps(metadata,indent=2)+'\n')
print(json.dumps(metadata,indent=2));print(out)
raise SystemExit(0 if metadata['passed'] else 1)
