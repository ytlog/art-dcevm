#!/usr/bin/env python3
"""Exercise actual runtime CLI crash collection, correlation, budget and cleanup."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
from unittest.mock import patch
from crash_diagnostics import collect_crash_diagnostics,correlated_crash_lines
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temporary:
    tmp=Path(temporary);apk=tmp/'app.apk';apk.write_bytes(b'crash fixture');sha=hashlib.sha256(apk.read_bytes()).hexdigest()
    fake=tmp/'adb';fake.write_text(r'''#!/usr/bin/env python3
import json,os,sys
args=sys.argv[3:];mode=os.environ['MODE']
with open(os.environ['TRACE'],'a') as f:f.write(json.dumps(args)+'\n')
if args[0]=='install':
 if mode=='install_failed':print('Failure [INSTALL_FAILED]');sys.exit(1)
 print('Success')
elif 'path' in args:print('package:/data/app/base.apk')
elif 'sha256sum' in args:print(os.environ['SHA']+'  /data/app/base.apk')
elif 'instrument' in args:
 rid=args[args.index('runtimeRunId')+1];open(os.environ['RID'],'w').write(rid)
 if mode=='exit':sys.exit(7)
 if mode=='proof_failed':print('INSTRUMENTATION_RESULT: stream='+json.dumps({'passed':False,'error':'actual proof failed'}))
 elif mode=='crash':print('INSTRUMENTATION_RESULT: shortMsg=Process crashed.')
 elif mode=='malformed':print('INSTRUMENTATION_RESULT: stream=broken')
 print('INSTRUMENTATION_CODE: -1')
elif 'run-as' in args:
 rid=open(os.environ['RID']).read()
 if mode=='fallback':sys.exit(1)
 print(json.dumps({'runId':rid,'pid':731,'phases':[{'phase':'frames.before','sequence':0,'threadId':745}]}))
elif 'logcat' in args:
 rid=open(os.environ['RID']).read()
 if 'ArtDcevmPhase:I' in args:
  print('I ArtDcevmPhase: '+json.dumps({'runId':'f'*32,'pid':999,'phase':'stale'}))
  print('I ArtDcevmPhase: '+json.dumps({'runId':rid,'pid':731,'phase':'frames.before'}))
 else:
  print('10-09 11:00:00.000 999 999 E AndroidRuntime: unrelated secret')
  print('10-09 11:00:00.001 731 745 E AndroidRuntime: correlated failure')
  print('10-09 11:00:00.002 900 900 F DEBUG: *** *** ***')
  print('10-09 11:00:00.003 900 900 F DEBUG: pid: 731, tid: 745, name: test >>> dev.artdcevm.lab <<<')
  print('10-09 11:00:00.004 900 900 F DEBUG: #00 pc 123 libart.so')
  print('10-09 11:00:00.004 999 999 E AndroidRuntime: interleaved secret')
  print('10-09 11:00:00.005 901 901 F DEBUG: *** *** ***')
  print('10-09 11:00:00.006 901 901 F DEBUG: pid: 999, tid: 999, name: other >>> other.app <<<')
  print('10-09 11:00:00.007 901 901 F DEBUG: unrelated tombstone secret')
''');fake.chmod(0o755)
    for mode in ['crash','missing','malformed','exit','fallback','proof_failed','install_failed']:
        cwd=tmp/mode;cwd.mkdir();trace=cwd/'trace';rid=cwd/'rid'
        result=subprocess.run([sys.executable,str(root/'tools/run-runtime-access.py'),'--serial','fixture','--adb',str(fake),'--apk',str(apk),'--foreground'],cwd=cwd,env=dict(os.environ,MODE=mode,TRACE=str(trace),RID=str(rid),SHA=sha),capture_output=True,text=True,timeout=20)
        assert result.returncode==1,(mode,result.stdout,result.stderr)
        evidence=next((cwd/'build/evidence/runtime-access').glob('*/run.json'));run=json.loads(evidence.read_text())
        calls=[json.loads(line) for line in trace.read_text().splitlines()]
        diagnostics=mode not in ['proof_failed','install_failed']
        assert ('crashDiagnostics' in run)==diagnostics,(mode,run)
        assert any('logcat' in call for call in calls)==diagnostics
        if diagnostics:
            summary=run['crashDiagnostics'];assert summary['correlatedPids']==[731]
            assert summary['lastPhase']['phase']=='frames.before'
            text=(evidence.parent/'crash-logcat.log').read_text()
            assert 'correlated failure' in text and '#00 pc 123 libart.so' in text
            assert 'secret' not in text
            assert calls[-1]==['shell','am','force-stop','dev.artdcevm.lab']
            assert max(i for i,call in enumerate(calls) if 'logcat' in call)<len(calls)-1
        if mode=='crash':assert 'shortMsg=Process crashed.' in run['error']
        if mode=='exit':assert 'returned non-zero exit status 7' in run['error']
        print('Crash CLI:',mode,'passed')
    run_id='a'*32
    ticks=iter([0,0,11,11]);commands=[]
    def timeout(args,**kwargs):
        commands.append((args,kwargs));raise subprocess.TimeoutExpired(args,kwargs['timeout'])
    with patch('crash_diagnostics.time.monotonic',side_effect=lambda:next(ticks,11)),patch('crash_diagnostics.subprocess.run',side_effect=timeout):
        summary=collect_crash_diagnostics('adb','fixture',tmp,run_id,budget=100)
    assert len(commands)==1 and commands[0][1]['timeout']==2
    assert summary['commands']['crash-logcat']['error']=='Diagnostic budget exhausted'
    assert summary['correlatedPids']==[] and summary['crashLines']==0
    assert correlated_crash_lines('10-09 11:00:00.000 731 745 E AndroidRuntime: unrelated',[])==[]
    print('Crash hard budget and absence of PID correlation passed')
