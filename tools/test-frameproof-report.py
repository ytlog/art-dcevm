#!/usr/bin/env python3
import copy,json,os,pathlib,subprocess,tempfile
from frameproof_report import validate_frameproof
root=pathlib.Path(__file__).resolve().parents[1]
positive={'passed':True,'protocol':1,'runId':'current','targetNondebuggable':True,'instrumentationNondebuggable':True,'sameSignature':True,
 'bootstrap':{'passed':True,'runtimeJavaDebuggableObserved':True,'runtimeJavaDebuggable':False,'runtimeReceiversMatched':True,'methodIdentitiesChecked':5,'carrierJitCompiled':True,'publicJvmtiInitialized':False,'warmCalls':100000,'enqueueCalls':1,'compileCompletionCalls':1,'heapCollectionCalls':1,'gcCompletionDelta':1,'error':''},
 'activeFrames':{'passed':True,'carrierJitCompiled':True,'inlineFlagNativeObserved':True,'blockedLiveWitnesses':True,'retainedLiveWitnesses':True,'frames':3,'integers':3,'references':3,'locationReads':6,'dexPcChecks':3,'warmCalls':100000,'error':'','runtimeJavaDebuggableObserved':True,'runtimeJavaDebuggable':False}}
def output(report,code=-1):return 'INSTRUMENTATION_RESULT: stream=FRAME_PROOF_REPORT='+json.dumps(report)+'\nINSTRUMENTATION_CODE: '+str(code)+'\n'
def reject(text):
 try:validate_frameproof(text,'current')
 except (RuntimeError,ValueError):return
 raise AssertionError('Insufficient proof accepted')
assert validate_frameproof(output(positive),'current')==positive
checks=0
for group in (None,'bootstrap','activeFrames'):
 values=positive if group is None else positive[group]
 for key,value in values.items():
  if type(value) is dict:continue
  for replacement in (None,str(value),False if type(value) is int else 0):
   report=copy.deepcopy(positive);(report if group is None else report[group])[key]=replacement
   if replacement==value and type(replacement)==type(value):continue
   reject(output(report));checks+=1
  report=copy.deepcopy(positive);del (report if group is None else report[group])[key];reject(output(report));checks+=1
for text in (output(positive,0),output(positive)+'INSTRUMENTATION_CODE: -1\n',output(positive)+output(positive),'INSTRUMENTATION_CODE: -1\n',output(positive).replace('current','old')):reject(text);checks+=1
with tempfile.TemporaryDirectory() as temp:
 p=pathlib.Path(temp);apk=p/'app.apk';test=p/'test.apk';apk.write_bytes(b'app proof');test.write_bytes(b'test proof');template=p/'proof.json';template.write_text(json.dumps(positive));fake=p/'adb'
 fake.write_text('''#!/usr/bin/env python3
import hashlib,json,os,sys
args=sys.argv[3:];scenario=os.environ.get('CASE','pass')
if args[:1]==['install']:
 print('Failure [INSTALL_FAILED]' if scenario=='install' else 'Success');sys.exit(0)
if args[:3]==['shell','pm','path']:
 package=args[3];print('package:/data/app/'+('test' if package.endswith('.test') else 'app')+'/base.apk');sys.exit(0)
if args[:2]==['shell','sha256sum']:
 name='TEST_APK' if '/test/' in args[2] else 'APP_APK'
 digest=hashlib.sha256(open(os.environ[name],'rb').read()).hexdigest()
 if scenario==('testhash' if name=='TEST_APK' else 'apphash'):digest='0'*64
 print(digest+'  '+args[2]);sys.exit(0)
if args[:3]==['shell','am','force-stop']:sys.exit(0)
if args[:3]==['shell','am','instrument']:
 report=json.load(open(os.environ['PROOF']));report['runId']=args[args.index('frameRunId')+1]
 if scenario=='old':report['runId']='old'
 if scenario=='debug':report['bootstrap']['runtimeJavaDebuggable']=True
 if scenario=='inline':report['activeFrames']['inlineFlagNativeObserved']=False
 print('INSTRUMENTATION_RESULT: stream=FRAME_PROOF_REPORT='+json.dumps(report))
 print('INSTRUMENTATION_CODE: '+('0' if scenario=='completion' else '-1'));sys.exit(0)
raise Exception(str(args))
''');fake.chmod(0o755)
 for scenario in ('pass','install','apphash','testhash','old','debug','inline','completion'):
  result=subprocess.run([os.sys.executable,str(root/'tools/run-nondebuggable-frameproof.py'),'--serial','fake','--adb',str(fake),'--apk',str(apk),'--test-apk',str(test)],text=True,capture_output=True,env=dict(os.environ,CASE=scenario,APP_APK=str(apk),TEST_APK=str(test),PROOF=str(template)))
  report,_=json.JSONDecoder().raw_decode(result.stdout);assert (result.returncode==0)==(scenario=='pass'),(scenario,result.stdout,result.stderr)
  assert report['passed']==(scenario=='pass')
  if scenario=='pass':assert report['applicationSha256']==report['applicationRequestedSha256'] and report['testSha256']==report['testRequestedSha256']
print(f'Nondebuggable full frame report: {checks} missing/type/actual-proof mutations rejected; actual runner fake-ADB 8 install/hash/fresh-run/debug/inline/completion cases passed')
