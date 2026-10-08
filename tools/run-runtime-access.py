#!/usr/bin/env python3
"""Run the isolated owned JNI probe without initializing private evolution."""
import argparse,hashlib,json,re,subprocess,shutil
from datetime import datetime,timezone
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial',required=True)
p.add_argument('--apk',type=Path,default=Path('app/build/outputs/apk/debug/app-debug.apk'))
p.add_argument('--adb',default=shutil.which('adb'))
p.add_argument('--skip-install',action='store_true')
a=p.parse_args()
if not a.adb:p.error("adb not found; pass --adb")
out=Path('build/evidence/runtime-access')/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ');out.mkdir(parents=True)
metadata={'serial':a.serial,'apkSha256':hashlib.sha256(a.apk.read_bytes()).hexdigest(),'passed':False,'scope':'isolated-owned-JNI; no full evolution initialization'}
def adb(label,*args):
    r=subprocess.run([a.adb,'-s',a.serial,*args],capture_output=True,text=True,timeout=180)
    (out/(label+'.txt')).write_text(r.stdout+r.stderr);r.check_returncode();return r.stdout
try:
    if a.skip_install:
        paths=adb('package-path','shell','pm','path','dev.artdcevm.lab')
        file=next(s.removeprefix('package:') for s in paths.splitlines() if s.endswith('/base.apk'))
        metadata['installedApkSha256']=adb('installed-hash','shell','sha256sum',file).split()[0]
        assert metadata['installedApkSha256']==metadata['apkSha256']
    else:adb('install','install','-r',str(a.apk))
    adb('stop','shell','am','force-stop','dev.artdcevm.lab')
    output=adb('instrumentation','shell','am','instrument','-w','-r','dev.artdcevm.lab/com.github.ytlog.artdcevm.testing.RuntimeAccessInstrumentation')
    match=re.search(r'^INSTRUMENTATION_RESULT: stream=(.*)$',output,re.M);assert match
    report=json.loads(match.group(1));(out/'probe.json').write_text(json.dumps(report,indent=2)+'\n')
    metadata['passed']=report.get('passed') is True and report.get('mainCallbackHits')==128 and report.get('workerCallbackHits')==[128,128] and report.get('privateEvolutionInitialized') is False
except Exception as error:metadata['error']=str(error)
(out/'run.json').write_text(json.dumps(metadata,indent=2)+'\n')
print(json.dumps(metadata,indent=2));print(out)
raise SystemExit(0 if metadata['passed'] else 1)
