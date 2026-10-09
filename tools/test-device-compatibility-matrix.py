#!/usr/bin/env python3
"""Exercise actual matrix CLI fanout/order/refusal using fake ADB and child runners."""
import hashlib,json,os,shutil,subprocess,sys,tempfile,time
from pathlib import Path
root=Path(__file__).resolve().parents[1]
CHILD='''import argparse,hashlib,json,os,subprocess,sys,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);p.add_argument('--adb',required=True);p.add_argument('--apk',required=True);p.add_argument('--test-apk');p.add_argument('--require-fields',action='store_true');p.add_argument('--skip-install',action='store_true');p.add_argument('--foreground',action='store_true');p.add_argument('--active-frames',action='store_true');p.add_argument('--case');p.add_argument('--scope',default='full')
for flag in ['runtime-services','exclusive-roots','native-cache','jit-receivers','method-guards','field-storage']:p.add_argument('--'+flag,action='store_true')
a=p.parse_args();role='schema-'+a.case if 'schema' in Path(__file__).name else 'production' if 'production' in Path(__file__).name else 'runtime';mode=os.environ['MODE']
def event(phase):
 record={'serial':a.serial,'role':role,'phase':phase,'time':time.monotonic(),'argv':sys.argv[1:],'pid':os.getpid()}
 fd=os.open(os.environ['TRACE'],os.O_WRONLY|os.O_CREAT|os.O_APPEND,0o600);os.write(fd,(json.dumps(record)+'\\n').encode());os.close(fd)
assert hashlib.sha256(Path(a.apk).read_bytes()).hexdigest()==os.environ['APP_SHA']
if role=='production':
 assert a.require_fields and not a.skip_install and not a.active_frames
 assert a.foreground==(a.serial in json.loads(os.environ['FORE']))
 assert hashlib.sha256(Path(a.test_apk).read_bytes()).hexdigest()==os.environ['TEST_SHA']
elif role.startswith('schema-'):
 assert a.test_apk and not a.foreground and not a.skip_install and not a.require_fields and not a.active_frames
 assert hashlib.sha256(Path(a.test_apk).read_bytes()).hexdigest()==os.environ['TEST_SHA']
else:
 assert a.active_frames==(os.environ['ACTIVE']=='true')
 assert a.skip_install and not a.test_apk and not a.require_fields
 assert all(getattr(a,key.replace('-','_')) for key in ['runtime-services','exclusive-roots','native-cache','jit-receivers','method-guards','field-storage'])
 assert a.foreground==(a.serial in json.loads(os.environ['FORE']))
event('start')
if mode=='parallel' and role=='production':
 markers=Path(os.environ['MARKERS']);(markers/a.serial).write_text('active')
 deadline=time.monotonic()+4
 while len(list(markers.iterdir()))<int(os.environ['COUNT']):
  assert time.monotonic()<deadline,'Expected actual device fanout';time.sleep(.01)
if (mode=='timeout' and role=='production') or (mode=='schema_timeout' and role.startswith('schema-')):
 subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)'])
 print('PARTIAL formal output',flush=True);time.sleep(30)
passed=not (mode=='schema_fail' and role.startswith('schema-')) and not (mode in ('formal_fail','false_report') and role=='production') and not (mode=='runtime_fail' and role=='runtime')
out=Path('build/evidence/fake-'+role)/a.serial;out.mkdir(parents=True)
report={'passed':passed,'serial':a.serial,'applicationSha256':os.environ['APP_SHA'],'testSha256':os.environ['TEST_SHA']}
if role.startswith('schema-'):
 from production_schema_report import CASE_PROOFS
 report.update(runId='current-'+a.serial+'-'+a.case,case=a.case,scope=a.scope)
 (out/'result.json').write_text(json.dumps(report))
 actual={'runId':report['runId'],'case':a.case,'initializationScope':a.scope,'route':'DcevmRuntime/NativeBridge.apply','stage':'finished','passed':passed,'sourceEngineUsed':False,'lastApplyCode':0,'capabilities':{'evolutionAvailable':True,'backend':'runtime-capabilities','partialEvolutionAvailable':True},'retiredJniReadWriteChecks':12,'freshJniFields':4}
 actual.update({key:True for key in CASE_PROOFS[a.case]})
 if mode=='schema_false_proof':actual['lastApplyCode']=113
 (out/'report.json').write_text(json.dumps(actual))
else:(out/('report.json' if role=='production' else 'run.json')).write_text(json.dumps(report))
event('end');print(json.dumps(report));print(Path('tools') if mode=='outside_evidence' else out.resolve())
sys.exit(0 if passed or mode=='false_report' else 7)
'''
ADB='''#!/usr/bin/env python3
import json,os,sys,time
parts=sys.argv[3:];serial=sys.argv[2]
fd=os.open(os.environ['ADB_TRACE'],os.O_WRONLY|os.O_CREAT|os.O_APPEND,0o600);os.write(fd,(json.dumps({'serial':serial,'argv':parts,'time':time.monotonic()})+'\\n').encode());os.close(fd)
if parts==['shell','getprop','ro.build.version.sdk']:print(json.loads(os.environ['SDK'])[serial])
elif parts==['shell','am','force-stop','dev.artdcevm.lab']:pass
else:raise AssertionError('Unexpected real ADB action: '+str(parts))
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp)
    def run_case(name,devices,sdks,*,mode='matching',foreground=None,workers=7,timeout=10,expected=True,active=False,schemas=(),schema_scope='full'):
        case=tmp/name;(case/'tools').mkdir(parents=True);(case/'markers').mkdir()
        for file in ['run-device-compatibility-matrix.py','evidence_paths.py','production_schema_report.py']:shutil.copyfile(root/'tools'/file,case/'tools'/file)
        for file in ['run-production-bootstrap.py','run-runtime-access.py','run-production-schema.py']:(case/'tools'/file).write_text(CHILD)
        apk=case/'application.apk';apk.write_bytes(b'unchanged frozen application')
        test=case/'instrumentation.apk';test.write_bytes(b'unchanged frozen test')
        adb=case/'fake-adb';adb.write_text(ADB);adb.chmod(0o755)
        trace=case/'trace.jsonl';adb_trace=case/'adb.jsonl'
        selected=set(foreground or ['12']);fore=[serial for version,serial in devices if version in selected]
        env=dict(os.environ,ACTIVE=str(active).lower(),MODE=mode,SDK=json.dumps(sdks),FORE=json.dumps(fore),COUNT=str(len(devices)),MARKERS=str(case/'markers'),TRACE=str(trace),ADB_TRACE=str(adb_trace),APP_SHA=hashlib.sha256(apk.read_bytes()).hexdigest(),TEST_SHA=hashlib.sha256(test.read_bytes()).hexdigest())
        command=[sys.executable,str(case/'tools/run-device-compatibility-matrix.py'),'--apk',str(apk),'--test-apk',str(test),'--adb',str(adb),'--max-workers',str(workers),'--suite-timeout',str(timeout)]
        for schema in schemas:command.extend(['--schema-case',schema])
        command.extend(['--schema-scope',schema_scope])
        if active:command.append('--active-frames')
        for version,serial in devices:command.extend(['--device',version+'='+serial])
        for version in foreground or []:command.extend(['--foreground-version',version])
        start=time.monotonic();result=subprocess.run(command,cwd=case,env=env,capture_output=True,text=True,timeout=12)
        assert (result.returncode==0)==expected,(name,result.stdout,result.stderr)
        paths=list((case/'build/evidence/device-compatibility-matrix').glob('*/matrix.json'));assert len(paths)==1,(name,result.stdout,result.stderr)
        matrix=json.loads(paths[0].read_text());assert matrix['passed']==expected and matrix['activeFramesRequested']==active
        assert matrix['schemaCases']==list(schemas) and matrix['schemaScope']==schema_scope
        assert matrix['artifacts']['application']['sha256']==env['APP_SHA'] and matrix['artifacts']['instrumentation']['sha256']==env['TEST_SHA']
        events=[json.loads(line) for line in trace.read_text().splitlines()] if trace.exists() else []
        adb_events=[json.loads(line) for line in adb_trace.read_text().splitlines()]
        for version,serial in devices:
            own=[event for event in events if event['serial']==serial]
            report=next(item for item in matrix['devices'] if item['serial']==serial)
            if name=='sdk_mismatch':assert not own and report['production']['skipped'] and report['runtime']['skipped'];continue
            formal=[event for event in own if event['role']=='production'];runtime=[event for event in own if event['role']=='runtime']
            assert formal[0]['phase']=='start' and runtime[0]['phase']=='start'
            if mode!='timeout':assert formal[-1]['phase']=='end' and formal[-1]['time']<=runtime[0]['time']
            assert report['runtime']['command'].count('--skip-install')==1
            assert ('--foreground' in report['runtime']['command'])==(serial in fore)
            assert '--require-fields' in report['production']['command']
            assert '--active-frames' not in report['production']['command']
            assert ('--active-frames' in report['runtime']['command'])==active
            assert ('--foreground' in report['production']['command'])==(serial in fore)
            for label in ['production','runtime']:
                suite=report[label];assert Path(suite['stdout']).exists() and Path(suite['stderr']).exists() and 'exitCode' in suite
                if not suite.get('timedOut') and mode!='outside_evidence':assert Path(suite['reportPath']).exists()
            previous_end=runtime[-1]['time']
            for schema in schemas:
                events_for_case=[event for event in own if event['role']=='schema-'+schema]
                assert events_for_case[0]['time']>=previous_end
                item=report['schema'][schema]
                assert item['command'][item['command'].index('--case')+1]==schema
                assert item['command'][item['command'].index('--scope')+1]==schema_scope
                assert '--foreground' not in item['command'] and '--skip-install' not in item['command']
                if mode!='schema_timeout':
                    assert events_for_case[-1]['phase']=='end';previous_end=events_for_case[-1]['time']
                    assert item['passed']==(mode not in ('schema_fail','schema_false_proof'))
                else:
                    assert item['timedOut'] and not item['passed'] and item['timeoutCleanup']['exitCode']==0
        if workers==1 and len(devices)>1:
            first=[event['time'] for event in events if event['serial']==devices[0][1]]
            second=[event['time'] for event in events if event['serial']==devices[1][1]]
            assert max(first)<min(second)
        if mode=='parallel':
            starts=[event['time'] for event in events if event['role']=='production' and event['phase']=='start']
            ends=[event['time'] for event in events if event['role']=='production' and event['phase']=='end'];assert max(starts)<min(ends)
        if mode in ('formal_fail','false_report'):assert all(item['runtime']['passed'] and not item['production']['passed'] for item in matrix['devices'])
        if mode=='timeout':
            report=matrix['devices'][0];assert report['production']['timedOut'] and report['runtime']['passed']
            cleanup=next(item for item in adb_events if 'force-stop' in item['argv']);runtime_start=next(item['time'] for item in events if item['role']=='runtime' and item['phase']=='start')
            assert cleanup['time']<runtime_start and time.monotonic()-start<6
            assert 'PARTIAL formal output' in Path(report['production']['stdout']).read_text()
        print('Device matrix:',name,'passed')
        return case,command,env
    seven=[(str(n),'fixture-'+str(n)) for n in range(11,18)]
    run_case('seven_parallel',seven,{serial:str(int(version)+(19 if int(version)<=12 else 20)) for version,serial in seven},mode='parallel')
    cases=('field-types','field-staticness','method-signature','native-java')
    run_case('seven_parallel_schema',seven,{serial:str(int(version)+(19 if int(version)<=12 else 20)) for version,serial in seven},mode='parallel',schemas=cases)
    run_case('common_schema',[('17','fixture-17')],{'fixture-17':'37'},schemas=('field-types',),schema_scope='common-owned')
    run_case('schema_failure',[('11','fixture-11')],{'fixture-11':'30'},schemas=('field-types',),mode='schema_fail',expected=False)
    run_case('schema_false_proof',[('11','fixture-11')],{'fixture-11':'30'},schemas=('field-types',),mode='schema_false_proof',expected=False)
    run_case('schema_timeout',[('11','fixture-11')],{'fixture-11':'30'},schemas=('field-types',),mode='schema_timeout',timeout=.25,expected=False)
    run_case('active_frames_opt_in',[('11','fixture-11'),('17','fixture-17')],{'fixture-11':'30','fixture-17':'37'},active=True)
    run_case('minor_labels',[('12','fixture-12L'),('17','fixture-17minor')],{'fixture-12L':'32','fixture-17minor':'37.1'})
    run_case('same_version_devices',[('12','fixture-12a'),('12','fixture-12b')],{'fixture-12a':'31','fixture-12b':'32'})
    run_case('foreground_override',[('12','fixture-12'),('14','fixture-14')],{'fixture-12':'31','fixture-14':'34'},foreground=['14'])
    run_case('formal_failure',[('11','fixture-11'),('12','fixture-12')],{'fixture-11':'30','fixture-12':'31'},mode='formal_fail',expected=False)
    run_case('runtime_failure',[('11','fixture-11')],{'fixture-11':'30'},mode='runtime_fail',expected=False)
    run_case('false_report',[('11','fixture-11')],{'fixture-11':'30'},mode='false_report',expected=False)
    run_case('sdk_mismatch',[('11','fixture-11')],{'fixture-11':'31'},expected=False)
    run_case('outside_report',[('11','fixture-11')],{'fixture-11':'30'},mode='outside_evidence',expected=False)
    run_case('timeout_cleanup',[('11','fixture-11')],{'fixture-11':'30'},mode='timeout',timeout=.25,expected=False)
    case,command,env=run_case('workers_one',[('11','fixture-11'),('12','fixture-12')],{'fixture-11':'30','fixture-12':'31'},workers=1)
    for extra in [['--device','13=fixture-11'],['--max-workers','8'],['--device','18=unsupported'],['--suite-timeout','nan'],['--schema-case','field-types','--schema-case','field-types']]:
        result=subprocess.run(command+extra,cwd=case,env=env,capture_output=True,text=True);assert result.returncode==2
    print('Device matrix: duplicate serial, unsupported label, over-limit concurrency rejected')
