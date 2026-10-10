#!/usr/bin/env python3
"""Actual Java thread coordination and immutable D8 instruction witness."""
import hashlib, json, os, re, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
out=root/'build/evidence/host/receiver-super-coordinator';out.mkdir(parents=True,exist_ok=True)
java=Path(subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
sdk=Path(os.environ.get('ANDROID_HOME',str(Path.home()/'Library/Android/sdk')))
fixture=root/'artdcevm/src/bootstrap/receiver/com/github/ytlog/artdcevm/internal/ReceiverGuardFixture.java'
test=root/'tests/host/java/ReceiverSuperCoordinatorTest.java'
classes=out/'classes';classes.mkdir(exist_ok=True)
def run(cmd, name):
 r=subprocess.run(list(map(str,cmd)),text=True,capture_output=True,timeout=90)
 (out/name).write_text(r.stdout+r.stderr);r.check_returncode();return r.stdout
run([java/'bin/javac','--release','11','-d',classes,fixture,test],'javac.log')
print(run([java/'bin/java','-cp',classes,'ReceiverSuperCoordinatorTest'],'java.log').strip())
dex=out/'dex';dex.mkdir(exist_ok=True)
inputs=sorted((classes/'com/github/ytlog/artdcevm/internal').glob('*.class'))
run([sdk/'build-tools/35.0.0/d8','--min-api','30','--lib',sdk/'platforms/android-35/android.jar','--output',dex,*inputs],'d8.log')
dump=run([sdk/'build-tools/35.0.0/dexdump','-d',dex/'classes.dex'],'dexdump.log')
marker="name          : 'callSuper'"
assert dump.count(marker)==1
body=dump.split(marker,1)[1].split('    #',1)[0]
invokes=re.findall(r'\|[0-9a-f]+: (invoke-[^\n]+)',body)
super_calls=[i for i,x in enumerate(invokes) if x.startswith('invoke-super ') or x.startswith('invoke-super/range ')]
wait_calls=[i for i,x in enumerate(invokes) if 'awaitBeforeSuper:' in x]
assert len(super_calls)==2 and len(wait_calls)==1, invokes
assert super_calls[0]<wait_calls[0]<super_calls[1], invokes
assert all('ReceiverGuardFixture$Base;.body:' in invokes[i] for i in super_calls), invokes
record={'dex_sha256':hashlib.sha256((dex/'classes.dex').read_bytes()).hexdigest(),'invocations':invokes,'ordered_coordinated_super':True,'exit_code':0,'scope':'Actual public Java worker; immutable D8 after-wait invoke-super. Not ART migration or obsolete frame authority.'}
(out/'witness.json').write_text(json.dumps(record,indent=2)+'\n')
print('Immutable D8 witness: default invoke-super and coordinated awaitBeforeSuper -> original Base.body invoke-super PASS')
