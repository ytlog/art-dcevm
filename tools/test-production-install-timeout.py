#!/usr/bin/env python3
"""Exercise actual production CLI timeout recording at the fake ADB boundary."""
import hashlib,json,os,shutil,subprocess,sys,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory);tools=tmp/'tools';tools.mkdir()
    for name in ['run-production-bootstrap.py','production_bootstrap_report.py','production_foreground_report.py','apk_verification.py','evidence_paths.py','timeout_diagnostics.py','test_phase_file.py']:
        shutil.copy(root/'tools'/name,tools/name)
    app=tmp/'application.apk';app.write_bytes(b'application exact frozen');test=tmp/'instrumentation.apk';test.write_bytes(b'test exact frozen')
    sha=hashlib.sha256(app.read_bytes()).hexdigest();test_sha=hashlib.sha256(test.read_bytes()).hexdigest()
    fake=tmp/'adb';fake.write_text(r'''#!/usr/bin/env python3
import json,os,sys
args=sys.argv[3:]
with open(os.environ['TRACE'],'a') as f:f.write(json.dumps(args)+'\n')
if args[0]=='install':print('Performing Streamed Install\nSuccess');print('install diagnostic stderr',file=sys.stderr)
elif 'path' in args:print('package:/data/app/'+('test' if args[-1].endswith('.test') else 'application')+'/base.apk')
elif 'sha256sum' in args:print((os.environ['TEST_SHA'] if '/test/' in args[-1] else os.environ['APP_SHA'])+'  '+args[-1])
elif 'dumpsys' in args:
    if os.environ['MODE']=='diagnostic_failure':sys.exit(8)
    if 'activities' in args:
        if os.environ['MODE'] in ['pending','test_pending','sessions_unavailable']:
            print('topResumedActivity=ActivityRecord{x com.android.packageinstaller/.PackageInstallerActivity}\nIntent { act=android.content.pm.action.CONFIRM_INSTALL }')
        elif os.environ['MODE']=='historical_confirmation':
            print('topResumedActivity=ActivityRecord{x launcher/.Home}\nHist #2 com.android.packageinstaller/.PackageInstallerActivity\nIntent { act=android.content.pm.action.CONFIRM_INSTALL }')
        else:print('topResumedActivity=launcher/.Home')
    else:
        if os.environ['MODE']=='sessions_unavailable':sys.exit(9)
        print('Active Session 1370670889: mClientProgress=1.0 mCommitted=true mPermissionsManuallyAccepted=false')
elif 'instrument' in args:print('PARTIAL instrumentation progress')
''');fake.chmod(0o755)
    wrapper=tmp/'inject.py';wrapper.write_text('''import os,runpy,subprocess,sys
real=subprocess.run
def run(args,**kwargs):
 result=real(args,**kwargs)
 mode=os.environ['MODE']
 if 'dumpsys' in args:
  assert kwargs['timeout']==8
  if mode=='diagnostic_timeout':raise subprocess.TimeoutExpired(args,8,output=b'partial read-only diagnostic')
 hit=('instrument' in args) if mode=='instrumentation' else ('install' in args and args[-1].endswith('instrumentation.apk' if mode=='test_pending' else 'application.apk'))
 if hit:
  raw='PARTIAL install stream' if mode!='instrumentation' else result.stdout
  if mode=='generic_bytes':raw=raw.encode()
  raise subprocess.TimeoutExpired(args,kwargs['timeout'],output=raw,stderr=b'PARTIAL stderr')
 return result
subprocess.run=run
sys.argv=sys.argv[1:];sys.path.insert(0,os.path.dirname(sys.argv[0]));runpy.run_path(sys.argv[0],run_name='__main__')
''')
    for mode in ['pending','test_pending','sessions_unavailable','generic_bytes','diagnostic_failure','diagnostic_timeout','historical_confirmation','instrumentation']:
        trace=tmp/(mode+'.jsonl');env=dict(os.environ,MODE=mode,TRACE=str(trace),APP_SHA=sha,TEST_SHA=test_sha)
        command=[sys.executable,str(wrapper),str(tools/'run-production-bootstrap.py'),'--serial','fixture','--adb',str(fake),'--apk',str(app),'--test-apk',str(test),'--require-fields']
        result=subprocess.run(command,env=env,capture_output=True,text=True,timeout=10)
        assert result.returncode!=0,(mode,result.stdout,result.stderr)
        reports=list((tmp/'build/evidence/production-bootstrap').glob('*/report.json'));latest=max(reports,key=lambda p:p.stat().st_mtime_ns)
        report=json.loads(latest.read_text());assert report['passed'] is False and report['applicationRequestedSha256']==sha
        label='instrumentation' if mode=='instrumentation' else 'test-install' if mode=='test_pending' else 'application-install'
        partial=(latest.parent/(label+'.log')).read_text();assert 'PARTIAL' in partial and 'PARTIAL stderr' in partial and 'timed out after 120 seconds' in partial
        calls=[json.loads(line) for line in trace.read_text().splitlines()]
        assert sum(c[0]=='install' for c in calls)==(2 if mode in ['test_pending','instrumentation'] else 1),calls
        assert not any('install-abandon' in c or 'install-commit' in c for c in calls)
        if mode=='instrumentation':
            assert 'installTimeoutDiagnostics' not in report and not any('dumpsys' in c for c in calls)
        else:
            assert not any('instrument' in c or 'force-stop' in c for c in calls)
            d=report['installTimeoutDiagnostics'];assert d['stage']==label
            visible=mode in ['pending','test_pending','sessions_unavailable'];assert d['confirmationVisible']==visible,(mode,d)
            assert ('confirm the existing installation' in report['error'])==visible
            if mode=='diagnostic_failure':assert all(x['exitCode']==8 for x in d['commands'].values())
            if mode=='diagnostic_timeout':
                assert all('timed out after 8 seconds' in x['error'] for x in d['commands'].values())
                assert 'partial read-only diagnostic' in (latest.parent/(label+'-timeout-activity.log')).read_text()
            if mode=='sessions_unavailable':assert d['commands']['sessions']['exitCode']==9
        if mode in ['test_pending','instrumentation']:assert report['applicationSha256']==sha and report['testRequestedSha256']==test_sha
        print('Production timeout:',mode,'passed')
