#!/usr/bin/env python3
import os,subprocess,zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
out=root/'build/evidence/host/readonly-metadata-oracle';classes=out/'classes';dex=out/'dex'
classes.mkdir(parents=True,exist_ok=True);dex.mkdir(exist_ok=True)
java=root/'tests/host/java'
subprocess.run(['javac','-d',str(classes),str(java/'ReadonlyMetadataFixture.java'),str(java/'ReadonlyMetadataOracleTest.java'),str(root/'app/src/debug/java/com/github/ytlog/artdcevm/testing/MethodDexOracle.java')],check=True)
d8=Path(os.environ.get('ANDROID_HOME',str(Path.home()/'Library/Android/sdk')))/'build-tools/36.0.0/d8'
subprocess.run([str(d8),'--min-api','30','--output',str(dex),str(classes/'ReadonlyMetadataFixture.class')],check=True)
apk=out/'oracle.apk'
with zipfile.ZipFile(apk,'w') as archive:archive.write(dex/'classes.dex','classes.dex')
r=subprocess.run(['java','-cp',str(classes),'ReadonlyMetadataOracleTest',str(apk)],capture_output=True,text=True)
(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
