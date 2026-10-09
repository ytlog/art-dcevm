#!/usr/bin/env python3
"""Run the real schema CLI against delayed and stale fake-ADB reports."""
import json, os, shutil, subprocess, sys, tempfile
from pathlib import Path

root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory);scripts=tmp/'tools';scripts.mkdir()
    for name in ('run-production-schema.py','production_schema_report.py','timeout_diagnostics.py','test_phase_file.py'):
        shutil.copy(root/'tools'/name,scripts/name)
    app=tmp/'app.apk';app.write_bytes(b'APP')
    test=tmp/'test.apk';test.write_bytes(b'TEST')
    fake=tmp/'adb'
    fake.write_text(r'''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
args=sys.argv[3:];mode=os.environ['MODE'];saved=Path(os.environ['SAVED'])
with open(os.environ['TRACE'],'a') as f:f.write(json.dumps(args)+'\n')
if 'path' in args:
    print('package:/data/app/'+('test' if args[-1].endswith('.test') else 'app')+'/base.apk')
elif args[:2]==['exec-out','cat']:
    sys.stdout.buffer.write(b'TEST' if '/test/' in args[-1] else b'APP')
elif 'instrument' in args:
    run=args[args.index('productionRunId')+1]
    report=dict(runId=run,case='field-types',initializationScope='common-owned',
                route='DcevmRuntime/NativeBridge.apply',stage='finished',passed=True,
                sourceEngineUsed=False,lastApplyCode=0,
                capabilities=dict(backend='runtime-capabilities',partialEvolutionAvailable=True),
                oldReflectionAndJniRetired=True,freshLongJniAndVarHandle=True,oldObjectGcState=True)
    if mode=='failed':report.update(stage='apply-field-types.before',passed=False,lastApplyCode=98)
    file_report=dict(report)
    if mode=='stale':file_report['runId']='0'*32
    if mode=='wrong_scope':file_report['initializationScope']='full'
    if mode=='mismatch':file_report['nativeDiagnostic']='different payload'
    saved.write_text(json.dumps(file_report))
    print('INSTRUMENTATION_RESULT: productionSchemaReport='+json.dumps(report))
    print('FAILURES!!!' if mode=='failed' else 'OK (1 test)')
elif args[:3]==['exec-out','run-as','dev.artdcevm.lab']:
    if mode=='missing':sys.exit(1)
    print(saved.read_text())
elif 'pidof' in args:print('1234')
''')
    fake.chmod(0o755)
    wrapper=tmp/'inject.py'
    wrapper.write_text('''import os,runpy,subprocess,sys
real=subprocess.run
def run(args,**kwargs):
    result=real(args,**kwargs)
    if 'instrument' in args and os.environ['MODE'] in ('timeout','stale','wrong_scope','missing'):
        raise subprocess.TimeoutExpired(args,kwargs['timeout'],output=result.stdout,stderr=result.stderr)
    return result
subprocess.run=run
sys.argv=sys.argv[1:];sys.path.insert(0,os.path.dirname(sys.argv[0]));runpy.run_path(sys.argv[0],run_name='__main__')
''')
    for mode in ('normal','failed','timeout','stale','wrong_scope','missing','mismatch'):
        trace=tmp/(mode+'.jsonl');saved=tmp/(mode+'.json')
        env=dict(os.environ,MODE=mode,TRACE=str(trace),SAVED=str(saved))
        result=subprocess.run([sys.executable,str(wrapper),str(scripts/'run-production-schema.py'),
                               '--serial','fixture','--adb',str(fake),'--apk',str(app),
                               '--test-apk',str(test),'--case','field-types','--scope','common-owned'],
                              env=env,capture_output=True,text=True,timeout=10)
        metadata=json.loads(result.stdout.splitlines()[-2])
        assert metadata['passed']==(mode=='normal'),(mode,result.stdout,result.stderr)
        assert (result.returncode==0)==(mode=='normal')
        assert metadata['reportFileCaptured']==(mode in ('normal','failed','timeout','mismatch'))
        out=Path(result.stdout.splitlines()[-1])
        if mode in ('timeout','stale','wrong_scope','missing'):
            assert metadata['instrumentationTimedOut'] is True
            assert metadata['timeoutDiagnostics']['livePids']==[1234]
            assert metadata['errors'],metadata
        if mode=='timeout':
            # A complete, successful saved payload still cannot turn timeout green.
            assert metadata['recoveredReportErrors']==[]
            assert json.loads((out/'report.json').read_text())['passed'] is True
        if mode in ('stale','wrong_scope','missing'):
            assert not (out/'report.json').exists()
        if mode=='mismatch':assert 'Persisted report differs' in str(metadata['errors'])
        calls=[json.loads(line) for line in trace.read_text().splitlines()]
        instrument=next(i for i,c in enumerate(calls) if 'instrument' in c)
        assert not any('force-stop' in c or 'install' in c or 'instrument' in c for c in calls[instrument+1:])
        print('Production schema recovery:',mode,'PASS')
