#!/usr/bin/env python3
"""Reject stale, incomplete and unfinished formal transaction evidence."""
import json,hashlib,os,pathlib,subprocess,sys,tempfile
from production_bootstrap_report import validate_production_bootstrap,validate_production_bootstrap_payload
run_id='a'*32
transaction={'lastApplyCode':0,'runId':run_id,'productionBootstrap':True,'formalApplyMethodDeletion':True,
 'cachedJniRejectedModes':4,'cachedReflectionRejected':True,'retainedIdentity':True,
 'retainedCompute':35,'retainedValue':31,'capabilities':{'nativeMethodDeletion':True,'fieldDeletion':False,'fullDcevmComplete':False}}
output='INSTRUMENTATION_RESULT: stream=OK (1 test)\nINSTRUMENTATION_CODE: -1\n'
def logged(value):return 'I ArtDcevmProduction: '+json.dumps(value)+'\n'
assert validate_production_bootstrap(output,logged(transaction),run_id)==transaction
assert validate_production_bootstrap_payload(output,json.dumps(transaction),run_id)==transaction
for payload in ('', '{', '[]', json.dumps({**transaction,'runId':'b'*32}),json.dumps(transaction)*2,
                json.dumps(transaction)[:-1]+',"runId":"'+run_id+'"}'):
 try:validate_production_bootstrap_payload(output,payload,run_id)
 except (RuntimeError,ValueError):pass
 else:raise AssertionError('Malformed/duplicate/stale persisted payload accepted')
for stdout in (output+output,output+'OK (1 test)\n',output.replace('OK (1 test)','OK (2 tests)')):
 try:validate_production_bootstrap_payload(stdout,json.dumps(transaction),run_id)
 except RuntimeError:pass
 else:raise AssertionError('Ambiguous JUnit completion accepted')
for stdout,log in [(output.replace('-1','0'),logged(transaction)),('OK (1 test)',logged(transaction)),
 (output,logged({**transaction,'runId':'b'*32})),(output,''),(output,logged(transaction)*2),
 (output,logged({**transaction,'cachedJniRejectedModes':3})),
 (output,logged({**transaction,'lastApplyCode':98})),
 (output,logged({**transaction,'lastApplyCode':False})),
 (output,logged({**transaction,'cachedReflectionRejected':False})),
 (output,logged({**transaction,'retainedIdentity':False})),
 (output,logged({**transaction,'retainedCompute':34})),
 (output,logged({**transaction,'capabilities':{'nativeMethodDeletion':False}}))]:
 try:validate_production_bootstrap(stdout,log,run_id)
 except RuntimeError:pass
 else:raise AssertionError('Incomplete formal evidence accepted')
field={'formalApplyFieldDeletion':True,'fieldHandleRejectedPaths':8,'fieldJniRejectedPaths':36,
 'fieldReflectionRejectedPaths':36,'fieldFreshKeptJniChecks':4,'fieldRetainedValue':41,'fieldStaticRetainedValue':42,
 'fieldRetainedCompute':42,'fieldRetainedIdentity':True}
full={**transaction,**field,'capabilities':{**transaction['capabilities'],'fieldDeletion':True}}
assert validate_production_bootstrap(output,logged(full),run_id,require_fields=True)==full
for missing in field:
 candidate={**full};del candidate[missing]
 try:validate_production_bootstrap(output,logged(candidate),run_id,require_fields=True)
 except RuntimeError:pass
 else:raise AssertionError('Missing formal field evidence accepted: '+missing)
for key,value in [('fieldHandleRejectedPaths',7),('fieldJniRejectedPaths',35),('fieldReflectionRejectedPaths',35),
                  ('fieldRetainedValue',0),('fieldRetainedCompute',41),('fieldRetainedIdentity',1)]:
 try:validate_production_bootstrap(output,logged({**full,key:value}),run_id,require_fields=True)
 except RuntimeError:pass
 else:raise AssertionError('Incomplete formal field evidence accepted: '+key)
for invalid in [None,True,False,'4',4.0,0,3,5,-4]:
 try:validate_production_bootstrap(output,logged({**full,'fieldFreshKeptJniChecks':invalid}),run_id,require_fields=True)
 except RuntimeError:pass
 else:raise AssertionError('Invalid fresh retained-field JNI proof accepted: '+repr(invalid))
try:validate_production_bootstrap(output,logged(transaction),run_id,require_fields=True)
except RuntimeError:pass
else:raise AssertionError('Method-only report accepted as formal field transaction')
root=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 p=pathlib.Path(tmp);app=p/'app.apk';test=p/'test.apk';app.write_bytes(b'app');test.write_bytes(b'test')
 fake=p/'adb';fake.write_text(r'''#!/usr/bin/env python3
import json,os,sys
args=sys.argv[3:];mode=os.environ['PRODUCTION_TEST_MODE']
with open(os.environ['PRODUCTION_TEST_TRACE'],'a') as f:f.write(json.dumps(args)+'\n')
if args[0]=='install':print('Success')
elif 'path' in args:print('package:/test/base.apk' if args[-1].endswith('.test') else 'package:/app/base.apk')
elif 'sha256sum' in args:
 expected=os.environ['PRODUCTION_TEST_TEST_SHA'] if '/test/' in args[-1] else os.environ['PRODUCTION_TEST_APP_SHA']
 if mode=='bad_app_hash' and '/app/' in args[-1] or mode=='bad_test_hash' and '/test/' in args[-1]:expected='0'*64
 print(expected+'  '+args[-1])
elif 'instrument' in args:
 run_id=args[args.index('productionRunId')+1]
 with open(os.environ['PRODUCTION_TEST_RUN_ID'],'w') as out:out.write(run_id)
 print('INSTRUMENTATION_RESULT: stream=OK (1 test)')
 print('INSTRUMENTATION_CODE: '+('0' if mode=='unfinished' else '-1'))
elif args[:3]==['exec-out','run-as','dev.artdcevm.lab']:
 if mode=='missing_file':sys.exit(1)
 if mode=='corrupt_file':print('{');sys.exit(0)
 if mode=='nonobject_file':print('[]');sys.exit(0)
 run_id=open(os.environ['PRODUCTION_TEST_RUN_ID']).read()
 report=json.loads(os.environ['PRODUCTION_TEST_REPORT']);report['runId']='b'*32 if mode=='stale' else run_id
 if mode=='incomplete':report['cachedJniRejectedModes']=3
 if mode=='missing_fresh_jni':del report['fieldFreshKeptJniChecks']
 if mode=='mistyped_fresh_jni':report['fieldFreshKeptJniChecks']='4'
 if mode in ('large_truncated','large_log_failure'):report['diagnostic']='x'*16000
 payload=json.dumps(report)
 if mode=='duplicate_file':payload+=payload
 if mode=='duplicate_keys':payload=payload[:-1]+',"runId":"'+run_id+'"}'
 print(payload)
elif 'logcat' in args:
 if mode=='large_log_failure':sys.exit(1)
 print('I ArtDcevmProduction: {"runId":"'+open(os.environ['PRODUCTION_TEST_RUN_ID']).read()+'","diagnostic":"'+'x'*4000)

''');fake.chmod(0o755)
 for mode in ['matching','stale','unfinished','incomplete','full_matching','missing_fields','missing_fresh_jni','mistyped_fresh_jni','missing_file','corrupt_file','nonobject_file','duplicate_file','duplicate_keys','large_truncated','large_log_failure','bad_app_hash','bad_test_hash']:
  env=dict(os.environ,PRODUCTION_TEST_MODE=mode,PRODUCTION_TEST_RUN_ID=str(p/'run-id'),
   PRODUCTION_TEST_TRACE=str(p/(mode+'.jsonl')),PRODUCTION_TEST_REPORT=json.dumps(full if mode in ('full_matching','missing_fresh_jni','mistyped_fresh_jni') else transaction),PRODUCTION_TEST_APP_SHA=hashlib.sha256(app.read_bytes()).hexdigest(),
   PRODUCTION_TEST_TEST_SHA=hashlib.sha256(test.read_bytes()).hexdigest())
  command=[sys.executable,str(root/'tools/run-production-bootstrap.py'),'--serial','mock','--adb',str(fake),
   '--apk',str(app),'--test-apk',str(test)]
  if mode in ('full_matching','missing_fields','missing_fresh_jni','mistyped_fresh_jni'):command.append('--require-fields')
  result=subprocess.run(command,env=env,capture_output=True,text=True)
  assert (result.returncode==0)==(mode in ('matching','full_matching','large_truncated','large_log_failure')),(mode,result.stdout,result.stderr)
  path=pathlib.Path(result.stdout.splitlines()[-1])
  report=json.loads((path/'report.json').read_text());assert report['passed']==(mode in ('matching','full_matching','large_truncated','large_log_failure'))
  if mode in ('bad_app_hash','bad_test_hash'):
   assert not any('instrument' in json.loads(line) for line in (p/(mode+'.jsonl')).read_text().splitlines())
  else:assert report['applicationSha256']==report['applicationRequestedSha256'] and report['testSha256']==report['testRequestedSha256']
  if mode in ('matching','full_matching','large_truncated','large_log_failure'):assert (path/'transaction.json').exists() and report['transaction']['runId']==report['runId']
  print('Production runner:',mode,'passed')
print('Production report: current-run structured evidence, successful terminal code and retained-state proofs enforced')
