#!/usr/bin/env python3
"""Build library-owned calibration DEX assets from checked-in production sources."""
import argparse, os, pathlib, subprocess, tempfile
p=argparse.ArgumentParser();p.add_argument('--sdk',required=True);p.add_argument('--java',required=True);p.add_argument('--out',required=True);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];out=pathlib.Path(a.out)/'artdcevm/bootstrap';out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,JAVA_HOME=a.java);env['PATH']=str(pathlib.Path(a.java)/'bin')+os.pathsep+env['PATH']
for variant in ('original','additive'):
 with tempfile.TemporaryDirectory(prefix='artdcevm-bootstrap-') as work:
  classes=pathlib.Path(work)/'classes';classes.mkdir();dex=pathlib.Path(work)/'dex';dex.mkdir()
  source=root/'artdcevm/src/bootstrap'/variant/'com/github/ytlog/artdcevm/internal/BootstrapAdditive.java'
  subprocess.run([str(pathlib.Path(a.java)/'bin/javac'),'--release','8','-g','-d',str(classes),str(source)],check=True,env=env)
  subprocess.run([str(pathlib.Path(a.sdk)/'build-tools/35.0.0/d8'),'--min-api','30','--lib',str(pathlib.Path(a.sdk)/'platforms/android-35/android.jar'),'--output',str(dex),str(classes/'com/github/ytlog/artdcevm/internal/BootstrapAdditive.class')],check=True,env=env)
  (out/(variant+'.dex')).write_bytes((dex/'classes.dex').read_bytes())
