#!/usr/bin/env python3
"""Exercise both actual CLIs: current-run phases, live thread snapshots, cleanup."""
import hashlib,json,os,shutil,subprocess,sys,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory);tools=tmp/'tools';tools.mkdir()
    for name in ['run-runtime-access.py','run-production-bootstrap.py','owned_removal_report.py','production_bootstrap_report.py','apk_verification.py','evidence_paths.py','timeout_diagnostics.py','test_phase_file.py','crash_diagnostics.py']:shutil.copy(root/'tools'/name,tools/name)
    app=tmp/'app.apk';app.write_bytes(b'correlated frozen app');test=tmp/'test.apk';test.write_bytes(b'correlated frozen test')
    appsha=hashlib.sha256(app.read_bytes()).hexdigest();testsha=hashlib.sha256(test.read_bytes()).hexdigest()
    fake=tmp/'adb';fake.write_text(r'''#!/usr/bin/env python3
import json,os,sys
args=sys.argv[3:]
with open(os.environ['TRACE'],'a') as f:f.write(json.dumps(args)+'\n')
if args[0]=='install':print('Success')
elif 'path' in args:print('package:/data/'+('test' if args[-1].endswith('.test') else 'app')+'/base.apk')
elif 'sha256sum' in args:print((os.environ['TEST_SHA'] if '/test/' in args[-1] else os.environ['APP_SHA'])+'  '+args[-1])
elif 'instrument' in args:
    key='productionRunId' if 'productionRunId' in args else 'runtimeRunId'
    open(os.environ['RUN_ID'],'w').write(args[args.index(key)+1]);print('PARTIAL instrument output')
elif 'pidof' in args:
    if os.environ['MODE']=='missing_pid':sys.exit(1)
    print('731')
elif 'ps' in args:
    assert args[-1]=='PID,TID,COMM,STAT,WCHAN'
    print('PID TID COMM STAT WCHAN\n731 731 main S ep_poll\n731 745 Instr:main R< 0\n731 799 RenderThread S futex_wait')
elif 'run-as' in args:
    if 'runtime-phases/' in args[-1]:
        if os.environ['MODE']=='phase_file':
            run=open(os.environ['RUN_ID']).read();print(json.dumps({'runId':run,'pid':731,'phases':[{'phase':'method-deletion.before','sequence':1,'threadId':745}]}));sys.exit(0)
        sys.exit(1)
    if os.environ['MODE']=='denied':print('Permission denied',file=sys.stderr);sys.exit(1)
    print('Name: Instrumentation\nState: D (disk sleep)\n__refrigerator')
elif 'logcat' in args:
    run=open(os.environ['RUN_ID']).read()
    for data in [{'runId':'f'*32,'pid':731,'phase':'stale'}, {'runId':run,'pid':999,'phase':'wrong-pid'},
                 {'runId':run,'pid':True,'phase':'bool-pid'}, {'runId':run,'pid':731,'phase':'foreground.start-activity.before'},
                 {'runId':run,'pid':731,'phase':'foreground.start-activity.after'}, {'runId':run,'pid':731,'phase':'foreground.wait-idle.before'}]:
        print('I ArtDcevmPhase: '+json.dumps(data))
    print('I ArtDcevmPhase: malformed JSON')
''');fake.chmod(0o755)
    wrapper=tmp/'inject.py';wrapper.write_text('''import importlib,os,runpy,subprocess,sys,types
real=subprocess.run
sys.argv=sys.argv[1:];sys.path.insert(0,os.path.dirname(sys.argv[0]))
if os.environ['MODE']=='budget':
 module=importlib.import_module('timeout_diagnostics');ticks=iter([0,0,16]);module.time=types.SimpleNamespace(monotonic=lambda:next(ticks,16))
def run(args,**kwargs):
 result=real(args,**kwargs)
 if 'instrument' in args:
  assert kwargs['timeout']==(120 if 'productionRunId' in args else 180)
  raise subprocess.TimeoutExpired(args,kwargs['timeout'],output=b'PARTIAL instrument output')
 if 'pidof' in args and os.environ['MODE']=='budget':raise subprocess.TimeoutExpired(args,kwargs['timeout'],output=b'partial PID query')
 if any(x in args for x in ['pidof','ps','run-as','logcat']):assert 0<kwargs['timeout']<=15
 return result
subprocess.run=run
runpy.run_path(sys.argv[0],run_name='__main__')
''')
    ids=[]
    for kind in ['runtime','production']:
        for mode in ['matching','denied','missing_pid','budget','background','phase_file']:
            name=kind+'-'+mode;trace=tmp/(name+'.trace');ridfile=tmp/(name+'.id')
            env=dict(os.environ,MODE=mode,TRACE=str(trace),RUN_ID=str(ridfile),APP_SHA=appsha,TEST_SHA=testsha)
            runner='run-runtime-access.py' if kind=='runtime' else 'run-production-bootstrap.py'
            cmd=[sys.executable,str(wrapper),str(tools/runner),'--serial','fixture','--adb',str(fake),'--apk',str(app)]
            if mode!='background':cmd.append('--foreground')
            if kind=='production':cmd+=['--test-apk',str(test),'--require-fields']
            result=subprocess.run(cmd,cwd=tmp,env=env,capture_output=True,text=True,timeout=10)
            assert result.returncode!=0,(name,result.stdout,result.stderr)
            pattern='runtime-access/*/run.json' if kind=='runtime' else 'production-bootstrap/*/report.json'
            rp=max((tmp/'build/evidence').glob(pattern),key=lambda p:p.stat().st_mtime_ns);report=json.loads(rp.read_text())
            rid=ridfile.read_text();assert len(rid)==32 and report['runId']==rid and rid not in ids;ids.append(rid)
            assert report['passed'] is False and 'timed out after' in report['error'] and 'timeoutDiagnosticError' not in report
            d=report['timeoutDiagnostics'];assert d['runId']==rid
            calls=[json.loads(l) for l in trace.read_text().splitlines()]
            assert calls[-1]==['shell','am','force-stop','dev.artdcevm.lab']
            instrument=next(i for i,c in enumerate(calls) if 'instrument' in c)
            snapshots=calls[instrument+1:-1];assert snapshots and 'pidof' in snapshots[0]
            assert not any('install'==c[0] or 'instrument' in c or 'kill' in c for c in snapshots)
            if mode=='budget':
                assert d['phases']==[] and len(snapshots)==1
                assert 'budget exhausted' in d['commands']['phases']['error'].lower()
                assert 'partial PID query' in (rp.parent/'timeout-pid.log').read_text()
            else:
                assert d['lastPhase']==({'runId':rid,'pid':731,'phase':'method-deletion.before','sequence':1,'threadId':745} if mode=='phase_file' else {'runId':rid,'pid':731,'phase':'foreground.wait-idle.before'})
                assert all(p['runId']==rid and type(p['pid']) is int for p in d['phases'])
                if mode!='missing_pid':
                    assert d['livePids']==[731] and len(d['phases'])==(1 if mode=='phase_file' else 3)
                    state=next(c for c in snapshots if 'run-as' in c and '/proc/731/status' in c);assert 'sh' not in state and '/proc/731/task/745/wchan' in state
                    assert d['commands']['state-731']['exitCode']==(1 if mode=='denied' else 0)
                    assert 'Instr:main R<' in (rp.parent/'timeout-threads-731.log').read_text()
                    assert '/proc/731/task/745/comm' in state and '/proc/731/task/799/status' in state
                else:assert d['livePids']==[] and not any('ps' in c or '/proc/731/status' in c for c in snapshots)
            print('Correlated timeout:',name,'passed')
