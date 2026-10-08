#!/usr/bin/env python3
"""Prove failed installs and stale artifacts cannot reach device instrumentation."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
from apk_verification import require_install_success,base_apk_path,verified_apk_sha256

for code,out,err in [(0,"Failure [INSTALL_FAILED_ABORTED]",""),
    (0,"Success","Failure [INSTALL_FAILED_ABORTED]"),(0,"Performing Streamed Install",""),
    (1,"Success","")]:
    try:require_install_success(code,out,err)
    except RuntimeError:pass
    else:raise AssertionError("Rejected installation accepted")
require_install_success(0,"Performing Streamed Install\nSuccess\n","")
assert base_apk_path("package:/x/base.apk\npackage:/x/split_config.arm64.apk")=='/x/base.apk'
for out in ["package:/x/split.apk","package:/x/base.apk\npackage:/y/base.apk"]:
    try:base_apk_path(out)
    except RuntimeError:pass
    else:raise AssertionError("Ambiguous/missing artifact accepted")
assert verified_apk_sha256('A'*64+'  /x/base.apk','a'*64)=='a'*64
root=Path(__file__).resolve().parents[1]
report={'passed':True,'mainCallbackHits':128,'workerCallbackHits':[128,128],
    'privateEvolutionInitialized':False,'mainRoute':'new-global-ref','workerRoutes':['new-global-ref']*2,
    'mainContextChecks':384,'workerContextChecks':[384]*2,'mainMethodModel':12,
    'workerMethodModels':[12]*2,'methodDexDirectCount':4,'methodDexVirtualCount':1,
    'methodDexIndices':[1,2,3,4,5],'mainMethodTableCount':5,'workerMethodTableCounts':[5,5]}
with tempfile.TemporaryDirectory() as tmp:
    directory=Path(tmp);apk=directory/'fixture.apk';apk.write_bytes(b'owned frozen artifact')
    sha=hashlib.sha256(apk.read_bytes()).hexdigest()
    fake=directory/'adb';fake.write_text(r"""#!/usr/bin/env python3
import json,os,sys
parts=sys.argv[3:]
with open(os.environ['APK_TEST_TRACE'],'a') as trace:trace.write(json.dumps(parts)+'\n')
mode=os.environ['APK_TEST_MODE']
if parts[0]=='install':
    if mode=='rejected':print('Failure [INSTALL_FAILED_ABORTED: User rejected permissions]')
    elif mode=='stderr_rejected':
        print('Success');print('Failure [INSTALL_FAILED_ABORTED]',file=sys.stderr)
    elif mode=='no_success':print('Performing Streamed Install')
    else:print('Success')
elif 'path' in parts:print('package:/x/base.apk' if mode!='missing_path' else 'package:/x/split.apk')
elif 'sha256sum' in parts:
    print(('0'*64 if mode=='stale' else 'invalid' if mode=='bad_hash' else os.environ['APK_TEST_SHA'])+'  /x/base.apk')
elif 'instrument' in parts:
    print('INSTRUMENTATION_RESULT: stream='+os.environ['APK_TEST_REPORT'])
    print('INSTRUMENTATION_CODE: -1')
""")
    fake.chmod(0o755)
    for mode in ['rejected','stderr_rejected','no_success','stale','missing_path','bad_hash','matching','skip_stale','skip_matching']:
        cwd=directory/mode;cwd.mkdir();trace=cwd/'trace.jsonl'
        actual_mode='stale' if mode=='skip_stale' else 'matching' if mode=='skip_matching' else mode
        env=dict(os.environ,APK_TEST_TRACE=str(trace),APK_TEST_MODE=actual_mode,APK_TEST_SHA=sha,APK_TEST_REPORT=json.dumps(report))
        command=[sys.executable,str(root/'tools/run-runtime-access.py'),'--serial','mock','--adb',str(fake),'--apk',str(apk)]
        if mode.startswith('skip_'):command.append('--skip-install')
        result=subprocess.run(command,cwd=cwd,env=env,capture_output=True,text=True)
        calls=[json.loads(line) for line in trace.read_text().splitlines()]
        positive=mode in ['matching','skip_matching']
        assert (result.returncode==0)==positive,(mode,result.stdout,result.stderr)
        assert any('instrument' in call for call in calls)==positive,(mode,calls)
        runs=list((cwd/'build/evidence/runtime-access').glob('*/run.json'))
        assert len(runs)==1
        run=json.loads(runs[0].read_text());assert run['passed']==positive
        if positive:assert run['installedApkSha256']==run['apkSha256']==sha
        else:assert not any('force-stop' in call for call in calls),(mode,calls)
        print('APK verification:',mode,'passed')
    # An old/bootstrap-only report from the exact requested artifact must not
    # satisfy a new request for native transactions merely because passed=true.
    cwd=directory/'missing_transaction';cwd.mkdir();trace=cwd/'trace.jsonl'
    bootstrap={**report,'runtimeServicesCalibrated':True,'jitServices':'jit,code-cache,heap'}
    env=dict(os.environ,APK_TEST_TRACE=str(trace),APK_TEST_MODE='matching',APK_TEST_SHA=sha,APK_TEST_REPORT=json.dumps(bootstrap))
    result=subprocess.run([sys.executable,str(root/'tools/run-runtime-access.py'),'--serial','mock',
        '--adb',str(fake),'--apk',str(apk),'--runtime-services','--jit-receivers'],
        cwd=cwd,env=env,capture_output=True,text=True)
    assert result.returncode!=0,(result.stdout,result.stderr)
    run=json.loads(next((cwd/'build/evidence/runtime-access').glob('*/run.json')).read_text())
    assert run['passed'] is False and 'actual deletion proof' in run['error'],run
    assert run['installedApkSha256']==sha
    assert any('instrument' in json.loads(line) for line in trace.read_text().splitlines())
    print('APK verification: exact artifact bootstrap-only report cannot pass native deletion request')
    cwd=directory/'missing_field_transaction';cwd.mkdir();trace=cwd/'trace.jsonl'
    method_only={**bootstrap,
        'methodEntryGuardsCalibrated':True,'fieldEntryGuardsCalibrated':True,
        'methodHandleFieldGuardsCalibrated':True,'methodHandleFieldGuardChecks':12,
        'retirementCleanupCalibrated':True,'nativeFieldStorageCalibrated':True,
        'ownedMethodDeletion':True,'ownedMethodDeletionError':'',
        'ownedMethodDeletionJniRejected':True,'ownedMethodDeletionReflectionRejected':True,
        'ownedMethodDeletionKeptValue':35,'ownedMethodDeletionIdentityPreserved':True,
        'ownedMethodHandleTargetProof':True,'ownedMethodDeletionMethodHandleRejected':True,
        'ownedMethodDeletionMethodHandleChecks':2,'ownedMethodDeletionKeptMethodHandleValue':35,
        'ownedMethodDeletionAfterGcChecks':1,'ownedMethodDeletionGcCollections':1,'ownedMethodDeletionDeclaringRootsRestored':True,'ownedMethodDeletionCacheClearedThreads':17}
    env=dict(os.environ,APK_TEST_TRACE=str(trace),APK_TEST_MODE='matching',APK_TEST_SHA=sha,APK_TEST_REPORT=json.dumps(method_only))
    result=subprocess.run([sys.executable,str(root/'tools/run-runtime-access.py'),'--serial','mock',
        '--adb',str(fake),'--apk',str(apk),'--runtime-services','--jit-receivers','--method-guards','--field-storage'],
        cwd=cwd,env=env,capture_output=True,text=True)
    assert result.returncode!=0,(result.stdout,result.stderr)
    run=json.loads(next((cwd/'build/evidence/runtime-access').glob('*/run.json')).read_text())
    assert run['passed'] is False and 'ownedFieldDeletion' in run['error'],run
    assert run['installedApkSha256']==sha
    print('APK verification: method-only exact artifact cannot pass full method/field transaction request')

print('APK verification: failure/stale guards prevent instrumentation; matching and skip-install execute only the verified artifact')
