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
sys.path.insert(0,str(Path(__file__).parent/'tools'))
from production_schema_report import CASE_PROOFS
args=sys.argv[3:];mode=os.environ['MODE'];saved=Path(os.environ['SAVED']);case=os.environ.get('CASE','field-types')
with open(os.environ['TRACE'],'a') as f:f.write(json.dumps(args)+'\n')
if 'path' in args:
    print('package:/data/app/'+('test' if args[-1].endswith('.test') else 'app')+'/base.apk')
elif args[:2]==['exec-out','cat']:
    sys.stdout.buffer.write(b'TEST' if '/test/' in args[-1] else b'APP')
elif 'instrument' in args:
    run=args[args.index('productionRunId')+1]
    scope=args[args.index('productionSchemaScope')+1]
    report=dict(runId=run,case=case,initializationScope=scope,
                route='DcevmRuntime/NativeBridge.apply',stage='finished',passed=True,
                sourceEngineUsed=False,lastApplyCode=0,
                capabilities=dict(backend='runtime-capabilities',partialEvolutionAvailable=True,evolutionAvailable=True),
                oldReflectionAndJniRetired=True,freshLongJniAndVarHandle=True,oldObjectGcState=True)
    report.update({key:True for key in CASE_PROOFS[case]})
    if case=='hierarchy-addition':report.update(successfulApplyCount=2,interfaceJniChecks=6)
    if case=='hierarchy-narrowing':report.update(successfulApplyCount=3,illegalTypedStorageChecks=3,illegalStorageCode0=98,illegalStorageCode1=98,illegalStorageCode2=98)
    if mode=='failed':report.update(stage='apply-field-types.before',passed=False,lastApplyCode=98)
    file_report=dict(report)
    if mode in ('stale','stale_live'):file_report['runId']='0'*32
    if mode in ('wrong_scope','wrong_scope_live'):file_report['initializationScope']='full' if scope=='common-owned' else 'common-owned'
    if mode=='mismatch':file_report['nativeDiagnostic']='different payload'
    saved.write_text('{' if mode=='corrupt_live' else '[]' if mode=='nonobject_live' else json.dumps(file_report))
    print('INSTRUMENTATION_RESULT: productionSchemaReport='+json.dumps(report))
    if args[-1].endswith('ProductionSchemaInstrumentation'):
        print('INSTRUMENTATION_RESULT: schemaRunnerCompleted='+('false' if mode in ('failed','incomplete') else 'true'))
        print('INSTRUMENTATION_CODE: '+('0' if mode=='wrong_code' else '-1'))
    else:print('FAILURES!!!' if mode=='failed' else 'OK (1 test)')
elif args[:3]==['exec-out','run-as','dev.artdcevm.lab']:
    if mode in ('missing','missing_live'):sys.exit(1)
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
    for case in ('field-types','hierarchy-addition','hierarchy-narrowing'):
      for self_route in (False,True):
        for scope in ('full','common-owned'):
          for mode in ('normal','failed','timeout','stale','wrong_scope','missing','mismatch','missing_live','corrupt_live','nonobject_live','stale_live','wrong_scope_live')+ (('incomplete','wrong_code') if self_route else ()):
              if case!='field-types' and mode not in ('normal','failed','timeout','mismatch'):continue
              trace=tmp/(case+'-'+('self-' if self_route else 'junit-')+scope+'-'+mode+'.jsonl');saved=tmp/(mode+'.json')
              env=dict(os.environ,CASE=case,MODE=mode,TRACE=str(trace),SAVED=str(saved))
              result=subprocess.run([sys.executable,str(wrapper),str(scripts/'run-production-schema.py'),
                                     '--serial','fixture','--adb',str(fake),'--apk',str(app),
                                     *(['--self-instrumentation'] if self_route else ['--test-apk',str(test)]),'--case',case,'--scope',scope],
                                    env=env,capture_output=True,text=True,timeout=10)
              metadata=json.loads(result.stdout.splitlines()[-2])
              assert metadata['passed']==(mode=='normal'),(mode,result.stdout,result.stderr)
              assert (result.returncode==0)==(mode=='normal')
              assert metadata['reportFileCaptured']==(mode in ('normal','failed','timeout','mismatch','incomplete','wrong_code'))
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
              if mode.endswith('_live'):
                  # Successful instrumentation alone must never hide a lost/corrupt/stale saved report.
                  assert 'instrumentationTimedOut' not in metadata
                  assert metadata['reportFileCaptured'] is False
                  assert 'Missing current persisted schema report:' in str(metadata['errors'])
                  assert json.loads((out/'report.json').read_text())['passed'] is True
              calls=[json.loads(line) for line in trace.read_text().splitlines()]
              instrument=next(i for i,c in enumerate(calls) if 'instrument' in c)
              assert not any('force-stop' in c or 'install' in c or 'instrument' in c for c in calls[instrument+1:])
              if self_route:
                  assert metadata['runner']=='self' and 'testSha256' not in metadata
                  assert not any('dev.artdcevm.lab.test' in c and 'path' in c for c in calls)
                  assert calls[instrument][-1].endswith('ProductionSchemaInstrumentation')
              print('Production schema recovery:',case, 'self' if self_route else 'junit',scope,mode,'PASS')
