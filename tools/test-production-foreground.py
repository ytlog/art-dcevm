#!/usr/bin/env python3
"""Actual formal runner CLI: correlated foreground proofs and cleanup rejection."""
import hashlib,json,os,shutil,subprocess,sys,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
base={'productionBootstrap':True,'formalApplyMethodDeletion':True,'cachedJniRejectedModes':4,
      'cachedReflectionRejected':True,'retainedIdentity':True,'retainedCompute':35,'retainedValue':31,
      'formalApplyFieldDeletion':True,'fieldHandleRejectedPaths':8,'fieldJniRejectedPaths':36,
      'fieldReflectionRejectedPaths':36,'fieldFreshKeptJniChecks':4,'fieldRetainedValue':41,
      'fieldStaticRetainedValue':42,'fieldRetainedCompute':42,'fieldRetainedIdentity':True,
      'capabilities':{'nativeMethodDeletion':True,'fieldDeletion':True},
      'foregroundRequested':True,'foregroundActivity':'com.github.ytlog.artdcevm.testing.RuntimeAccessForegroundActivity',
      'foregroundInstrumentationPid':741,'foregroundActivityPid':741,'foregroundResumedBefore':True,
      'foregroundResumedAfter':True,'foregroundPauseCount':0,'foregroundFinished':True}
with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory);tools=tmp/'tools';tools.mkdir()
    for name in ['run-production-bootstrap.py','production_bootstrap_report.py','apk_verification.py','evidence_paths.py','timeout_diagnostics.py']:shutil.copy(root/'tools'/name,tools/name)
    app=tmp/'app.apk';app.write_bytes(b'frozenapp');test=tmp/'test.apk';test.write_bytes(b'frozentest')
    sha=hashlib.sha256(app.read_bytes()).hexdigest();test_sha=hashlib.sha256(test.read_bytes()).hexdigest()
    fake=tmp/'adb';fake.write_text(r'''#!/usr/bin/env python3
import json,os,sys
args=sys.argv[3:]
with open(os.environ['TRACE'],'a') as f:f.write(json.dumps(args)+'\n')
if args[0]=='install':print('Success')
elif 'path' in args:print('package:/data/'+('test' if args[-1].endswith('.test') else 'app')+'/base.apk')
elif 'sha256sum' in args:print(('0'*64 if os.environ['MODE']=='stale' else os.environ['TEST_SHA'] if '/test/' in args[-1] else os.environ['APP_SHA'])+'  '+args[-1])
elif 'instrument' in args:
    open(os.environ['RUN_ID'],'w').write(args[args.index('productionRunId')+1])
    if os.environ['MODE']=='crash':print('INSTRUMENTATION_RESULT: shortMsg=Process crashed.\nINSTRUMENTATION_CODE: 0')
    else:print('OK (1 test)\nINSTRUMENTATION_CODE: -1')
elif 'logcat' in args:
    report=json.loads(os.environ['REPORT']);report['runId']=open(os.environ['RUN_ID']).read()
    print('I ArtDcevmProduction: '+json.dumps(report))
elif 'force-stop' in args and os.environ['MODE']=='cleanup_failure':
    calls=[json.loads(l) for l in open(os.environ['TRACE'])]
    if sum('force-stop' in c for c in calls)>1:sys.exit(9)
''');fake.chmod(0o755)
    wrapper=tmp/'timeout.py';wrapper.write_text('''import os,runpy,subprocess,sys
real=subprocess.run
def run(args,**kwargs):
 result=real(args,**kwargs)
 if 'instrument' in args:raise subprocess.TimeoutExpired(args,kwargs['timeout'],output=b'PARTIAL formal progress')
 return result
subprocess.run=run
sys.argv=sys.argv[1:];sys.path.insert(0,os.path.dirname(sys.argv[0]));runpy.run_path(sys.argv[0],run_name='__main__')
''')
    cases=[('matching',{},True),('legacy',{},True),('wrong_pid',{'foregroundActivityPid':742},False),
           ('boolean_pid',{'foregroundInstrumentationPid':True,'foregroundActivityPid':True},False),
           ('missing',{'foregroundRequested':None},False),('not_resumed',{'foregroundResumedBefore':False},False),
           ('lost_resume',{'foregroundResumedAfter':False},False),('paused',{'foregroundPauseCount':1},False),
           ('bool_pauses',{'foregroundPauseCount':False},False),('not_finished',{'foregroundFinished':False},False),
           ('wrong_activity',{'foregroundActivity':'other.Activity'},False),('method_incomplete',{'cachedJniRejectedModes':3},False),
           ('field_incomplete',{'fieldFreshKeptJniChecks':0},False),('crash',{},False),('cleanup_failure',{},False),('timeout',{},False),('stale',{},False)]
    for mode,patch,passed in cases:
        trace=tmp/(mode+'.trace');env=dict(os.environ,MODE=mode,TRACE=str(trace),RUN_ID=str(tmp/(mode+'.id')),REPORT=json.dumps({**base,**patch}),APP_SHA=sha,TEST_SHA=test_sha)
        cmd=[sys.executable,*([str(wrapper)] if mode=='timeout' else []),str(tools/'run-production-bootstrap.py'),'--serial','fixture','--adb',str(fake),'--apk',str(app),'--test-apk',str(test),'--require-fields']
        if mode!='legacy':cmd.append('--foreground')
        result=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=10)
        assert (result.returncode==0)==passed,(mode,result.stdout,result.stderr)
        report_path=max((tmp/'build/evidence/production-bootstrap').glob('*/report.json'),key=lambda p:p.stat().st_mtime_ns)
        report=json.loads(report_path.read_text());assert report['passed']==passed and report['foregroundRequested']==(mode!='legacy')
        calls=[json.loads(l) for l in trace.read_text().splitlines()];instruments=[c for c in calls if 'instrument' in c]
        if mode=='stale':assert not instruments and not any('force-stop' in c for c in calls)
        else:
            assert len(instruments)==1
            assert ('foreground' in instruments[0])==(mode!='legacy')
            if mode!='legacy':assert instruments[0][instruments[0].index('foreground')+1]=='true'
            assert sum('force-stop' in c for c in calls)==(1 if mode=='legacy' else 2)
            if mode!='legacy':assert calls[-1]==['shell','am','force-stop','dev.artdcevm.lab']
        if passed:assert report['applicationSha256']==sha and report['testSha256']==test_sha
        if mode=='cleanup_failure':assert 'foregroundCleanupError' in report
        if mode=='timeout':assert 'PARTIAL formal progress' in (report_path.parent/'instrumentation.log').read_text()
        print('Production foreground:',mode,'passed')
