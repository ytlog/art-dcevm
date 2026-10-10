#!/usr/bin/env python3
"""Real scenario CLIs: both instrumentation routes require current foreground proof."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    scripts = root / 'tools'
    scripts.mkdir()
    for name in ('run-production-schema.py', 'run-production-permissions.py', 'production_schema_report.py',
                 'production_permission_report.py', 'production_foreground_report.py', 'timeout_diagnostics.py', 'test_phase_file.py'):
        shutil.copy(ROOT / 'tools' / name, scripts / name)
    app = root / 'app.apk'; app.write_bytes(b'APP')
    test = root / 'test.apk'; test.write_bytes(b'TEST')
    fake = root / 'adb'
    fake.write_text(r'''#!/usr/bin/env python3
import json,os,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent/'tools'))
args=sys.argv[3:];family=os.environ['FAMILY'];mode=os.environ['MODE']
with open(os.environ['TRACE'],'a') as stream:stream.write(json.dumps(args)+'\n')
if 'path' in args:print('package:/data/'+('test' if args[-1].endswith('.test') else 'app')+'/base.apk')
elif args[:2]==['exec-out','cat']:sys.stdout.buffer.write(b'TEST' if '/test/' in args[-1] else b'APP')
elif 'instrument' in args:
    run=args[args.index('productionRunId')+1];case='field-types' if family=='schema' else 'final-fields'
    payload=dict(runId=run,case=case,initializationScope='full',route='DcevmRuntime/NativeBridge.apply',
        stage='finished',passed=True,sourceEngineUsed=False,lastApplyCode=0,capabilities={'evolutionAvailable':True})
    if family=='schema':
        from production_schema_report import CASE_PROOFS
        payload.update(dict.fromkeys(CASE_PROOFS[case],True))
    else:
        from production_permission_report import PROOFS,VARIANTS
        payload.update(dict.fromkeys(PROOFS[case],True),applies=[{'variant':v,'code':0} for v in VARIANTS[case]])
    if mode!='missing':
        payload.update(foregroundRequested=True,foregroundActivity='com.github.ytlog.artdcevm.testing.RuntimeAccessForegroundActivity',
            foregroundInstrumentationPid=741,foregroundActivityPid=742 if mode=='otherpid' else 741,
            foregroundResumedBefore=True,foregroundResumedAfter=True,foregroundPauseCount=1 if mode=='paused' else 0,
            foregroundFinished=mode!='unfinished')
    Path(os.environ['SAVED']).write_text(json.dumps(payload))
    prefix='productionSchemaReport' if family=='schema' else 'productionPermissionReport'
    print('INSTRUMENTATION_RESULT: '+prefix+'='+json.dumps(payload))
    if args[-1].endswith('AndroidJUnitRunner'):print('OK (1 test)')
    else:print('INSTRUMENTATION_RESULT: '+('schemaRunnerCompleted' if family=='schema' else 'permissionRunnerCompleted')+'=true\nINSTRUMENTATION_CODE: -1')
elif args[:3]==['exec-out','run-as','dev.artdcevm.lab']:print(Path(os.environ['SAVED']).read_text())
''')
    fake.chmod(0o755)
    count = 0
    for family in ('schema', 'permissions'):
        for self_route in (False, True):
            for mode in ('matching', 'missing', 'otherpid', 'paused', 'unfinished', 'legacy'):
                trace = root / 'trace.jsonl'; trace.write_text('')
                command = [sys.executable, str(scripts / ('run-production-' + family + '.py')),
                           '--adb', str(fake), '--serial', 'fixture', '--apk', str(app), '--case',
                           'field-types' if family == 'schema' else 'final-fields']
                command += ['--self-instrumentation'] if self_route else ['--test-apk', str(test)]
                if mode != 'legacy': command += ['--foreground']
                env = dict(os.environ, FAMILY='schema' if family == 'schema' else 'permissions', MODE=mode,
                           TRACE=str(trace), SAVED=str(root / 'persisted.json'))
                result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=10)
                expected = mode in ('matching', 'legacy')
                assert (result.returncode == 0) == expected, (family, self_route, mode, result.stdout, result.stderr)
                evidence = Path(result.stdout.splitlines()[-1])
                metadata = json.loads((evidence / 'result.json').read_text())
                assert metadata['passed'] == expected and metadata['reportFileCaptured'] is True
                assert metadata['foregroundRequested'] == (mode != 'legacy')
                calls = [json.loads(line) for line in trace.read_text().splitlines()]
                instrument = [call for call in calls if 'instrument' in call]
                assert len(instrument) == 1 and ('foreground' in instrument[0]) == (mode != 'legacy')
                if mode != 'legacy': assert instrument[0][instrument[0].index('foreground') + 1] == 'true'
                count += 1
    print('Actual schema/permission CLI foreground routes and negative proofs PASS:', count)
