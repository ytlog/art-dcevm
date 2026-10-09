#!/usr/bin/env python3
"""Require independent actual frame evidence through the real runner/fake ADB boundary."""
import copy,hashlib,json,os,runpy,subprocess,sys,tempfile
from pathlib import Path
from owned_removal_report import validate_active_frames,validate_physical_active_frames
root=Path(__file__).resolve().parents[1]
proof={'passed':True,'carrierJitCompiled':True,'inlineFlagNativeObserved':True,
       'blockedLiveWitnesses':True,'retainedLiveWitnesses':True,'frames':3,'integers':3,
       'references':3,'locationReads':6,'dexPcChecks':3,'warmCalls':100000,'error':''}
report={'activeFramesRequested':True,'ownedActiveFrameProof':proof}
validate_active_frames(report)
def reject(candidate):
    try:validate_active_frames(candidate)
    except RuntimeError:return
    raise AssertionError('Unverified/mistyped actual frame evidence accepted')
for value in [None,[],True,'proof']:
    reject({'activeFramesRequested':True,'ownedActiveFrameProof':value})
for value in [False,1,'true',None]:reject({**report,'activeFramesRequested':value})
for key,value in proof.items():
    absent=copy.deepcopy(report);del absent['ownedActiveFrameProof'][key];reject(absent)
    wrongs=([False,1,'true',None] if type(value) is bool else
            [value-1,value+1,True,str(value),float(value),None] if type(value) is int else [None,False,'failure'])
    for wrong in wrongs:reject({**report,'ownedActiveFrameProof':{**proof,key:wrong}})
physical_proof={**proof,'inlineFlagNativeObserved':False,'frameShape':'three-physical',
                'wholeProcessFrameEligibility':False,'matchedMethodMask':7,'physicalMethodMatches':3,
                'inlineMethodMatches':0,'headerOwnershipChecks':3,
                'nativeReaderRoute':'original-canonical-optional-location-fast',
                'nativeLocationReads':6,'physicalComparisonReads':6,'rawReferenceReads':3,
                'nativeReferenceMaskChecks':3,'freshNativeStrongRootWitnesses':3,
                'nativeContextIdentityMask':7,'nativeContextGetterObserved':True,'nativeContextChecks':6,
                'nativeReferenceReadBarrierExecuted':False,'gcRetainedLiveWitnesses':True,
                'blockedGcCollections':1,'retainedGcCollections':1}
physical_report={'physicalActiveFramesRequested':True,'ownedPhysicalActiveFrameProof':physical_proof}
validate_physical_active_frames(physical_report)
for key,value in physical_proof.items():
    wrong=0 if key in ('blockedGcCollections','retainedGcCollections','nativeContextChecks') else not value if type(value) is bool else value+1 if type(value) is int else 'wrong-shape'
    candidate=copy.deepcopy(physical_report);candidate['ownedPhysicalActiveFrameProof'][key]=wrong
    try:validate_physical_active_frames(candidate)
    except RuntimeError:pass
    else:raise AssertionError('Physical proof accepted missing/wrong source frame evidence: '+key)
ordinary=runpy.run_path(str(root/'tools/test-owned-removal-report.py'))['full']
base={**ordinary,**report,'mainCallbackHits':128,'workerCallbackHits':[128,128],
      'privateEvolutionInitialized':False,'mainRoute':'new-global-ref','workerRoutes':['new-global-ref']*2,
      'mainContextChecks':384,'workerContextChecks':[384]*2,'mainMethodModel':12,'workerMethodModels':[12]*2,
      'methodDexDirectCount':4,'methodDexVirtualCount':1,'methodDexIndices':[1,2,3,4,5],
      'mainMethodTableCount':5,'workerMethodTableCounts':[5]*2,'runtimeServicesCalibrated':True,
      'exclusiveMonitorIterations':32,'exclusiveFailureRestoration':True,'exclusiveGcCollections':1,
      'nativeCacheClearedThreads':17,'jitServices':'jit,code-cache,heap','methodEntryGuardsCalibrated':True,
      'nativeFieldStorageCalibrated':True,'fieldEntryGuardsCalibrated':True,'methodHandleFieldGuardsCalibrated':True,
      'methodHandleFieldGuardChecks':12,'retirementCleanupCalibrated':True}
flags=['--runtime-services','--exclusive-roots','--native-cache','--jit-receivers','--method-guards','--field-storage']
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp);apk=tmp/'fixture.apk';apk.write_bytes(b'active frames frozen fixture');sha=hashlib.sha256(apk.read_bytes()).hexdigest()
    fake=tmp/'adb';fake.write_text('''#!/usr/bin/env python3
import json,os,sys
args=sys.argv[3:]
with open(os.environ['TRACE'],'a') as stream:stream.write(json.dumps(args)+'\\n')
if args[0]=='install':print('Success')
elif 'path' in args:print('package:/data/app/base.apk')
elif 'sha256sum' in args:print(os.environ['SHA']+'  /data/app/base.apk')
elif 'instrument' in args:
 print('INSTRUMENTATION_RESULT: stream='+os.environ['REPORT']);print('INSTRUMENTATION_CODE: '+os.environ['CODE'])
''');fake.chmod(0o755)
    missing=copy.deepcopy(base);missing.pop('ownedActiveFrameProof')
    failed={**base,'ownedActiveFrameProof':{'passed':False,'error':'Missing actual inlinees'}}
    physical_only={k:v for k,v in {**base,**physical_report}.items() if not k.startswith('owned') or k=='ownedPhysicalActiveFrameProof'}
    cases=[('physical_prerequisites_only',physical_only,False,False,True),
           ('physical_matching',{**base,**physical_report},False,False,True),
           ('physical_inline_alias',{**base,**physical_report,'ownedPhysicalActiveFrameProof':proof},False,False,False),
           ('physical_missing',{**base,'physicalActiveFramesRequested':True},False,False,False),
           ('matching',base,True,True,True),('frames_only',base,True,False,True),
           ('missing',missing,True,True,False),('failed',failed,True,True,False),
           ('legacy_unrequested_failure',failed,False,True,True),
           ('legacy_unrequested_missing',missing,False,True,True),
           ('bool_count',{**base,'ownedActiveFrameProof':{**proof,'frames':True}},True,True,False),
           ('wrong_pc',{**base,'ownedActiveFrameProof':{**proof,'dexPcChecks':2}},True,True,False),
           ('not_current',{**base,'activeFramesRequested':False},True,True,False),
           ('stale',base,True,True,False),('code_zero',base,True,True,False),
           ('missing_jit',{**base,'jitServices':''},True,False,False),
           ('ordinary_failure',{**base,'ownedFieldDeletion':False},True,True,False)]
    for name,candidate,active,removal,expected in cases:
        cwd=tmp/name;cwd.mkdir();trace=cwd/'trace.jsonl'
        env=dict(os.environ,TRACE=str(trace),SHA='0'*64 if name=='stale' else sha,REPORT=json.dumps(candidate),CODE='0' if name=='code_zero' else '-1')
        command=[sys.executable,str(root/'tools/run-runtime-access.py'),'--serial','fixture','--adb',str(fake),'--apk',str(apk)]
        if removal:command+=flags
        if active:command+=['--active-frames']
        if name.startswith('physical_'):command+=['--physical-active-frames']
        if name=='physical_prerequisites_only':command+=['--runtime-services','--jit-receivers']
        result=subprocess.run(command,cwd=cwd,env=env,capture_output=True,text=True,timeout=15)
        assert (result.returncode==0)==expected,(name,result.stdout,result.stderr)
        paths=list((cwd/'build/evidence/runtime-access').glob('*/run.json'));assert len(paths)==1
        run=json.loads(paths[0].read_text());assert run['passed']==expected and run['activeFramesRequested']==active
        calls=[json.loads(line) for line in trace.read_text().splitlines()]
        instruments=[call for call in calls if 'instrument' in call]
        if name=='stale':assert not instruments
        else:
            assert len(instruments)==1 and ('activeFrames' in instruments[0])==active
            if active:
                at=instruments[0].index('activeFrames');assert instruments[0][at-1]=='-e' and instruments[0][at+1]=='true'
        if name.startswith('physical_'):
            assert run['physicalActiveFrameProofPassed']==expected
            assert 'physicalActiveFrames' in instruments[0]
            if expected:assert run['wholeProcessFrameEligibility'] is False
            if name=='physical_prerequisites_only':assert 'ownedRemovalPassed' not in run
        if expected:assert run['installedApkSha256']==run['apkSha256']==sha
        if name in ('missing','failed','bool_count','wrong_pc','not_current'):
            assert run['ownedRemovalPassed'] is True and run['activeFrameProofPassed'] is False
            saved=json.loads((paths[0].parent/'probe.json').read_text());assert saved['ownedFieldDeletion'] is True
        if name=='missing_jit':assert run['activeFrameProofPassed'] is False and 'two-inlinee' not in run['scope']
        if expected and active:assert run['activeFrameProofPassed'] is True and 'two-inlinee' in run['scope']
        if expected and not active:assert 'activeFrameProofPassed' not in run and 'two-inlinee' not in run['scope']
        print('Actual frame runner:',name,'passed')
print('Strict actual frame report: missing/mistyped/incorrect real PC, roots, inlining and current execution evidence rejected')
