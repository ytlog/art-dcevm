#!/usr/bin/env python3
"""Run one required schema case on already installed, SHA-verified APKs; no install/upload."""
import argparse, datetime, hashlib, json, pathlib, subprocess, sys, uuid
from production_schema_report import CASE_METHODS, validate
p=argparse.ArgumentParser(description=__doc__)
for name in ('adb','serial','apk','test-apk'):p.add_argument('--'+name,required=True)
p.add_argument('--case',choices=CASE_METHODS,required=True)
p.add_argument('--timeout',type=int,default=300)
a=p.parse_args();root=pathlib.Path(__file__).resolve().parents[1];run_id=uuid.uuid4().hex
out=root/'build/evidence/production-schema'/run_id;out.mkdir(parents=True)
def adb(*args,timeout=30):
    return subprocess.run([a.adb,'-s',a.serial,*args],capture_output=True,timeout=timeout,check=True)
metadata={'runId':run_id,'case':a.case,'serial':a.serial,'startedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
try:
    for package,path,key in [('dev.artdcevm.lab',a.apk,'appSha256'),('dev.artdcevm.lab.test',a.test_apk,'testSha256')]:
        expected=hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
        paths=adb('shell','pm','path',package).stdout.decode().strip().splitlines()
        if len(paths)!=1 or not paths[0].startswith('package:/'):raise RuntimeError('Expected one installed base APK: '+package)
        actual=hashlib.sha256(adb('exec-out','cat',paths[0][8:]).stdout).hexdigest()
        if expected!=actual:raise RuntimeError('Installed APK SHA mismatch: '+package)
        metadata[key]=actual
    for package in ('dev.artdcevm.lab.test','dev.artdcevm.lab'):adb('shell','am','force-stop',package)
    command=['shell','am','instrument','-w','-r','-e','class','com.github.ytlog.artdcevm.testing.ProductionSchemaEvolutionTest#'+CASE_METHODS[a.case],'-e','productionRunId',run_id,'dev.artdcevm.lab.test/androidx.test.runner.AndroidJUnitRunner']
    try:result=adb(*command,timeout=a.timeout)
    except subprocess.TimeoutExpired as error:
        (out/'instrumentation.log').write_bytes((error.stdout or b'')+(error.stderr or b''));raise
    raw=result.stdout.decode(errors='replace')+result.stderr.decode(errors='replace');(out/'instrumentation.log').write_text(raw)
    prefix='INSTRUMENTATION_RESULT: productionSchemaReport='
    lines=[line[len(prefix):] for line in raw.splitlines() if line.startswith(prefix)]
    if len(lines)!=1:raise RuntimeError('Missing unique required schema report')
    report=json.loads(lines[0]);(out/'report.json').write_text(json.dumps(report,indent=2))
    errors=validate(report,run_id,a.case)
    if 'OK (1 test)' not in raw:errors.append('JUnit did not pass exactly one required test')
    metadata.update(passed=not errors,errors=errors)
except Exception as error:
    metadata.update(passed=False,errors=[str(error)])
finally:
    (out/'result.json').write_text(json.dumps(metadata,indent=2));print(json.dumps(metadata));print(out)
sys.exit(0 if metadata.get('passed') else 1)
