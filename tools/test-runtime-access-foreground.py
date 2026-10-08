#!/usr/bin/env python3
"""Foreground CLI evidence gates and termination cleanup, using a fake ADB boundary."""
import copy,hashlib,json,os,runpy,subprocess,sys,tempfile
from pathlib import Path
import xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[1]
full=runpy.run_path(str(root/'tools/test-owned-removal-report.py'))['full']
base={**full,'mainCallbackHits':128,'workerCallbackHits':[128,128],
      'privateEvolutionInitialized':False,'mainRoute':'new-global-ref','workerRoutes':['new-global-ref']*2,
      'mainContextChecks':384,'workerContextChecks':[384]*2,'mainMethodModel':12,'workerMethodModels':[12]*2,
      'methodDexDirectCount':4,'methodDexVirtualCount':1,'methodDexIndices':[1,2,3,4,5],
      'mainMethodTableCount':5,'workerMethodTableCounts':[5]*2,'runtimeServicesCalibrated':True,
      'exclusiveMonitorIterations':32,'exclusiveFailureRestoration':True,'exclusiveGcCollections':1,
      'nativeCacheClearedThreads':17,'jitServices':'jit,code-cache,heap','methodEntryGuardsCalibrated':True,
      'nativeFieldStorageCalibrated':True,'fieldEntryGuardsCalibrated':True,'methodHandleFieldGuardsCalibrated':True,
      'methodHandleFieldGuardChecks':12,'retirementCleanupCalibrated':True,
      'foregroundRequested':True,'foregroundActivity':'com.github.ytlog.artdcevm.testing.RuntimeAccessForegroundActivity',
      'foregroundInstrumentationPid':713,'foregroundActivityPid':713,'foregroundResumedBefore':True,
      'foregroundResumedAfter':True,'foregroundPauseCount':0,'foregroundFinished':True}
flags=['--runtime-services','--exclusive-roots','--native-cache','--jit-receivers','--method-guards','--field-storage']
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp);apk=tmp/'fixture.apk';apk.write_bytes(b'foreground exact artifact');sha=hashlib.sha256(apk.read_bytes()).hexdigest()
    fake=tmp/'adb';fake.write_text('''#!/usr/bin/env python3
import json,os,sys
args=sys.argv[3:]
with open(os.environ['TRACE'],'a') as stream:stream.write(json.dumps(args)+'\\n')
if 'force-stop' in args and os.environ['MODE']=='cleanup_exit':
    calls=[json.loads(line) for line in open(os.environ['TRACE'])]
    if sum('force-stop' in call for call in calls)>1:sys.exit(9)
if args[0]=='install':print('Success')
elif 'path' in args:print('package:/data/app/base.apk')
elif 'sha256sum' in args:print(os.environ['SHA']+'  /data/app/base.apk')
elif 'instrument' in args:
    if os.environ['MODE']=='exit':sys.exit(7)
    if os.environ['MODE']=='crash':print('INSTRUMENTATION_RESULT: shortMsg=Process crashed.')
    elif os.environ['MODE']=='no_stream':pass
    elif os.environ['MODE']=='malformed':print('INSTRUMENTATION_RESULT: stream=broken JSON')
    else:print('INSTRUMENTATION_RESULT: stream='+os.environ['REPORT'])
    if os.environ['MODE']!='missing_code':print('INSTRUMENTATION_CODE: '+('0' if os.environ['MODE']=='code_zero' else '-1'))
''');fake.chmod(0o755)
    # Timeout injection exercises the real runner's subprocess exception/finally
    # path without making this host regression sleep for the device's 180s cap.
    wrapper=tmp/'timeout.py';wrapper.write_text('''import runpy,subprocess,sys
from pathlib import Path
real=subprocess.run
def run(args,**kwargs):
    result=real(args,**kwargs)
    if 'instrument' in args:raise subprocess.TimeoutExpired(args,kwargs['timeout'],output='PARTIAL instrument output',stderr='device frozen')
    return result
subprocess.run=run
sys.argv=sys.argv[1:]
sys.path.insert(0,str(Path(sys.argv[0]).parent))
runpy.run_path(sys.argv[0],run_name='__main__')
''')
    cases=[('matching',{},True),('legacy',{},True),('different_pid',{'foregroundActivityPid':714},False),
           ('boolean_pid',{'foregroundInstrumentationPid':True,'foregroundActivityPid':True},False),
           ('boolean_activity_pid',{'foregroundInstrumentationPid':1,'foregroundActivityPid':True},False),
           ('not_resumed',{'foregroundResumedBefore':False},False),('paused',{'foregroundPauseCount':1},False),
           ('lost_resume',{'foregroundResumedAfter':False},False),('not_finished',{'foregroundFinished':False},False),
           ('wrong_activity',{'foregroundActivity':'com.github.ytlog.artdcevm.MainActivity'},False),
           ('missing_proof',{'foregroundActivity':None},False),('incomplete_transaction',{'ownedFieldDeletion':False},False),
           ('code_zero',{},False),('missing_code',{},False),('malformed',{},False),('exit',{},False),('timeout',{},False),
           ('crash',{},False),('no_stream',{},False),('stale',{},False),('cleanup_exit',{},False)]
    for mode,changes,passed in cases:
        cwd=tmp/mode;cwd.mkdir();trace=cwd/'trace.jsonl';report={**base,**changes}
        env=dict(os.environ,TRACE=str(trace),SHA='0'*64 if mode=='stale' else sha,REPORT=json.dumps(report),MODE=mode)
        command=[sys.executable]
        if mode=='timeout':command.append(str(wrapper))
        command.extend([str(root/'tools/run-runtime-access.py'),'--serial','fixture','--adb',str(fake),'--apk',str(apk),*flags])
        if mode!='legacy':command.append('--foreground')
        result=subprocess.run(command,cwd=cwd,env=env,capture_output=True,text=True,timeout=20)
        assert (result.returncode==0)==passed,(mode,result.stdout,result.stderr)
        reports=list((cwd/'build/evidence/runtime-access').glob('*/run.json'));assert len(reports)==1,(mode,result.stdout,result.stderr)
        evidence=reports[0];run=json.loads(evidence.read_text());assert run['passed']==passed and run['foregroundRequested']==(mode!='legacy')
        calls=[json.loads(line) for line in trace.read_text().splitlines()]
        instrumentation=[call for call in calls if 'instrument' in call]
        if mode=='stale':assert not instrumentation and not any('force-stop' in call for call in calls)
        else:
            assert len(instrumentation)==1
            args=instrumentation[0]
            for key in ['runtimeServices','exclusiveRoots','nativeCache','jitReceivers','methodGuards','fieldStorage']:
                at=args.index(key);assert args[at-1]=='-e' and args[at+1]=='true'
            assert ('foreground' in args)==(mode!='legacy')
            if mode!='legacy':
                at=args.index('foreground');assert args[at-1]=='-e' and args[at+1]=='true'
            stops=[call for call in calls if 'force-stop' in call]
            assert len(stops)==(1 if mode=='legacy' else 2),(mode,calls)
            if mode!='legacy':assert calls[-1]==['shell','am','force-stop','dev.artdcevm.lab']
        if mode=='timeout':
            text=(evidence.parent/'instrumentation.txt').read_text();assert 'PARTIAL instrument output' in text and 'device frozen' in text
        if mode in ['crash','no_stream']:
            assert 'Instrumentation produced no JSON probe report' in run['error']
            assert ('shortMsg=Process crashed.' in run['error'])==(mode=='crash')
            assert not (evidence.parent/'probe.json').exists()
        if passed:assert run['installedApkSha256']==run['apkSha256']==sha
        print('Foreground runner:',mode,'passed')
ns='{http://schemas.android.com/apk/res/android}'
manifest=ET.parse(root/'app/src/debug/AndroidManifest.xml').getroot()
activity=[a for a in manifest.find('application').findall('activity') if a.get(ns+'name')==base['foregroundActivity']]
assert len(activity)==1 and activity[0].get(ns+'exported')=='false' and activity[0].get(ns+'process') is None
source=(root/'app/src/debug/java/com/github/ytlog/artdcevm/testing/RuntimeAccessForegroundActivity.java').read_text()
assert all(name not in source for name in ['DcevmRuntime','TestRuntimeBackend','NativeBridge','System.loadLibrary','MainActivity','ForegroundStressActivity'])
print('Foreground carrier: same default process, internal Activity, no backend/mutation ownership')
