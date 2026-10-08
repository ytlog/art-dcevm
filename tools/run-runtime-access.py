#!/usr/bin/env python3
"""Run the isolated owned JNI probe without initializing private evolution."""
import argparse,hashlib,json,re,subprocess,shutil
from evidence_paths import new_evidence_directory
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial',required=True)
p.add_argument('--apk',type=Path,default=Path('app/build/outputs/apk/debug/app-debug.apk'))
p.add_argument('--adb',default=shutil.which('adb'))
p.add_argument('--skip-install',action='store_true')
p.add_argument('--runtime-services',action='store_true',help='Require source-family Runtime service calibration')
p.add_argument('--exclusive-roots',action='store_true',help='Require actual exclusive strong-root and monitor/GC recovery')
p.add_argument('--native-cache',action='store_true',help='Require synchronous real native interpreter-cache clearing')
p.add_argument('--jit-receivers',action='store_true',help='Require actual Java/JVMTI JIT/cache/heap observation')
p.add_argument('--method-guards',action='store_true',help='Require real retired JNI and reflection invocation rejection')
p.add_argument('--field-storage',action='store_true',help='Require native raw field writes, card marking and monitor oracles')
a=p.parse_args()
if not a.adb:p.error("adb not found; pass --adb")
out=new_evidence_directory(Path('build/evidence/runtime-access'))
metadata={'serial':a.serial,'apkSha256':hashlib.sha256(a.apk.read_bytes()).hexdigest(),'passed':False,'scope':'isolated-owned-JNI; no full evolution initialization'}
def adb(label,*args):
    r=subprocess.run([a.adb,'-s',a.serial,*args],capture_output=True,text=True,timeout=180)
    (out/(label+'.txt')).write_text(r.stdout+r.stderr);r.check_returncode();return r.stdout
try:
    if a.skip_install:
        paths=adb('package-path','shell','pm','path','dev.artdcevm.lab')
        file=next(s.removeprefix('package:') for s in paths.splitlines() if s.endswith('/base.apk'))
        metadata['installedApkSha256']=adb('installed-hash','shell','sha256sum',file).split()[0]
        assert metadata['installedApkSha256']==metadata['apkSha256']
    else:adb('install','install','-r',str(a.apk))
    adb('stop','shell','am','force-stop','dev.artdcevm.lab')
    output=adb('instrumentation','shell','am','instrument','-w','-r',*[item for key,enabled in [('runtimeServices',a.runtime_services),('exclusiveRoots',a.exclusive_roots),('nativeCache',a.native_cache),('jitReceivers',a.jit_receivers),('methodGuards',a.method_guards),('fieldStorage',a.field_storage)] if enabled for item in ['-e',key,'true']],'dev.artdcevm.lab/com.github.ytlog.artdcevm.testing.RuntimeAccessInstrumentation')
    match=re.search(r'^INSTRUMENTATION_RESULT: stream=(.*)$',output,re.M);assert match
    report=json.loads(match.group(1));(out/'probe.json').write_text(json.dumps(report,indent=2)+'\n')
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
    if a.runtime_services or a.native_cache or a.jit_receivers or a.method_guards:metadata['passed']=metadata['passed'] and report.get('runtimeServicesCalibrated') is True
    if a.exclusive_roots:metadata['passed']=metadata['passed'] and report.get('exclusiveMonitorIterations')==32 and report.get('exclusiveFailureRestoration') is True and report.get('exclusiveGcCollections',0)>0
    if a.native_cache:metadata['passed']=metadata['passed'] and report.get('nativeCacheClearedThreads',0)>0
    if a.jit_receivers:metadata['passed']=metadata['passed'] and report.get('jitServices')=='jit,code-cache,heap'
    if a.method_guards:metadata['passed']=metadata['passed'] and report.get('methodEntryGuardsCalibrated') is True
    if a.field_storage:metadata['passed']=metadata['passed'] and report.get('nativeFieldStorageCalibrated') is True
except Exception as error:metadata['error']=str(error)
(out/'run.json').write_text(json.dumps(metadata,indent=2)+'\n')
print(json.dumps(metadata,indent=2));print(out)
raise SystemExit(0 if metadata['passed'] else 1)
