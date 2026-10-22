#!/usr/bin/env python3
"""Verify existing interface direct declarations when parents are added on pinned JBR."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import runpy
import subprocess
from evidence_paths import new_evidence_directory
ROOT = Path(__file__).resolve().parents[1]
REVISION = 'cf1f197170a0f04c6559feb873e362583be021d1'
SOURCE = r'''
import java.lang.instrument.*;
import java.nio.file.*;
import com.github.ytlog.artdcevm.fixture.*;
public class InverseParentOracle {
  static Instrumentation inst;
  static int checks;
  public static void premain(String args,Instrumentation value){inst=value;}
  static void eq(String name,Object expected,Object actual){
    if(!expected.equals(actual))throw new AssertionError(name+": expected "+expected+", got "+actual);
    checks++;
  }
  public static void main(String[] args)throws Exception {
    InverseParentState state=new InverseParentState();
    boolean staticsOnly=args.length>1&&args[1].equals("static");
    if(staticsOnly) {
      java.util.concurrent.CountDownLatch open=new java.util.concurrent.CountDownLatch(0);
      for(int k=2;k<4;k++) {
        java.lang.reflect.Method method=state.children[k].getMethod("await",java.util.concurrent.CountDownLatch.class,java.util.concurrent.CountDownLatch.class);
        for(int i=0;i<30000;i++) if(!Integer.valueOf(11).equals(method.invoke(state.receivers[k],open,open)))throw new AssertionError("static await warmup");
      }
    }
    for(int i=0;i<30000;i++) for(int k=0;k<4;k++)
      if(InverseParentFixture.local(state.receivers[k],k)!=11)throw new AssertionError("warmup");
    for(int round=0;round<2;round++) for(int phase=0;phase<4;phase++) {
      boolean restoring=phase>=2,fail=phase%2==0,changed=phase==1||phase==2;
      Class<?>[] targets=state.targets(round!=0,staticsOnly);
      ClassDefinition[] definitions=new ClassDefinition[targets.length];
      for(int i=0;i<targets.length;i++) definitions[i]=new ClassDefinition(targets[i],
        fail&&i==targets.length-1?new byte[]{0,1}:Files.readAllBytes(Paths.get(args[0],
          restoring?"original":"added",targets[i].getName().replace('.','/')+".class")));
      InverseParentState.Active active=staticsOnly?state.startStaticActive():state.startActive();
      try {
        try{inst.redefineClasses(definitions);if(fail)throw new AssertionError("batch accepted");}
        catch(InternalError expected){if(!fail||expected.getClass()!=InternalError.class)throw expected;checks++;}
      } finally {active.close();}
      for(int k=staticsOnly?2:0;k<4;k++) System.out.println("active"+round+"/"+phase+"/"+k+"="+active.observation(k));
      state.verify(InverseParentOracle::eq,"",changed,false,staticsOnly);
      for(int k=staticsOnly?2:0;k<4;k++) {
        final int slot=k;
        System.out.println("cachedReflection"+round+"/"+phase+"/"+k+"="+
            InverseParentState.result(()->state.own[slot].invoke(state.receivers[slot])));
        System.out.println("cachedHandle"+round+"/"+phase+"/"+k+"="+state.cachedHandleResult(k));
      }
      System.gc();state.verify(InverseParentOracle::eq,"gc",changed,false,staticsOnly);
      System.out.println("state "+round+"/"+phase+" PASS");
    }
    System.out.println("PASS checks="+checks);
  }
}
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home',type=Path,required=True)
    args=parser.parse_args();java=args.java_home.resolve()
    evidence=new_evidence_directory(ROOT/'build/evidence/jbr-inverse-parents')
    manifest={'sourceOracleRevision':REVISION,'inputs':{},'runs':[]}
    def run(name,command):
        r=subprocess.run(list(map(str,command)),capture_output=True,text=True,cwd=evidence,timeout=120)
        (evidence/(name+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+' failed; inspect ignored evidence')
        return r.stdout+r.stderr
    version=run('version',[java/'bin/java','-version'])
    if 'JBR-21.0.11+10-1163.116' not in version:raise RuntimeError('Requires pinned JBR 21.0.11+10-b1163.116')
    builder=runpy.run_path(str(ROOT/'tools/build-fixtures.py'))
    original=ROOT/'app/src/debug/java/com/github/ytlog/artdcevm/fixture'
    sources=[original/(name+'.java') for name in ('InverseParentFixture','InverseParentState')]
    old=evidence/'old'
    run('compile-old',[java/'bin/javac','--release','9','-d',old,*sources])
    patches=evidence/'patches'
    for kind in ('original','added'):
        directory=ROOT/('fixtures/inverse-parent-'+kind);out=patches/kind
        inputs=sorted(directory.rglob('*.java'))
        run('compile-'+kind,[java/'bin/javac','--release','9','-d',out,*inputs])
        recipe=directory/'rename-strings.json'
        if recipe.exists():
            inputs.append(recipe)
            renames=json.loads(recipe.read_text())
            for path in out.rglob('*.class'):
                data=path.read_bytes()
                if b'valueY' in data:
                    path.write_bytes(builder['strip_class_methods'](data,[],renames=renames))
        sources.extend(inputs)
    source=evidence/'InverseParentOracle.java';source.write_text(SOURCE)
    harness=evidence/'harness'
    run('compile-oracle',[java/'bin/javac','--release','9','-cp',old,'-d',harness,source])
    mf=evidence/'manifest.mf';mf.write_text('Premain-Class: InverseParentOracle\nCan-Redefine-Classes: true\n\n')
    agent=evidence/'agent.jar'
    run('agent',[java/'bin/jar','cfm',agent,mf,'-C',harness,'InverseParentOracle.class'])
    for name,flags,statics in [('interpreter',['-Xint'],False),('jit',['-Xbatch','-XX:+PrintCompilation',
            '-XX:CompileCommand=compileonly,com.github.ytlog.artdcevm.fixture.InverseParentFixture::local'],False),
            ('static-interpreter',['-Xint'],True),('static-jit',['-Xbatch','-XX:+PrintCompilation',
            '-XX:CompileCommand=compileonly,com.github.ytlog.artdcevm.fixture.InverseParentFixture$Static*::await'],True)]:
        output=run(name,[java/'bin/java',*flags,'-Xcheck:jni','-XX:+AllowEnhancedClassRedefinition',
          '-javaagent:'+str(agent),'-cp',str(harness)+':'+str(old),'InverseParentOracle',patches,*(['static'] if statics else [])])
        if output.count(' PASS\n')!=8:raise RuntimeError('Incomplete inverse parent matrix')
        if name=='jit' and not any('InverseParentFixture::local' in line and 'CompileCommand:' not in line for line in output.splitlines()):
            raise RuntimeError('Missing compiled old caller evidence')
        if name=='static-jit':
            for child in ('StaticDefault','StaticAbstract'):
                if not any('InverseParentFixture$'+child+'::await' in line and 'CompileCommand:' not in line and 'made not compilable' not in line for line in output.splitlines()):
                    raise RuntimeError('Missing compiled static caller evidence')
        observations=dict(re.findall(r'^((?:active|cachedReflection|cachedHandle)\d/\d/\d)=(.*)$',output,re.M))
        expected={}
        for round in range(2):
            for phase in range(4):
                for slot in range(2 if statics else 0,4):
                    expected[f'active{round}/{phase}/{slot}']='java.lang.IncompatibleClassChangeError' if phase%2 and slot<2 else ('13' if phase==2 and slot<2 else '11')
                    for prefix in ('cachedReflection','cachedHandle'):
                        expected[f'{prefix}{round}/{phase}/{slot}']='java.lang.AbstractMethodError' if phase in (1,2) and slot%2 else ('13' if phase in (1,2) and slot<2 else '11')
        if observations != expected:raise RuntimeError('Pinned active-frame or cached-slot observations changed')
        manifest['runs'].append(dict(mode=name,staticsOnly=statics,states=8,checks=int(re.search(r'^PASS checks=(\d+)$',output,re.M)[1]),
            observations=observations,referenceDefects=['cached reflection/handle aliases inherited abstract']+([] if statics else ['obsolete private self call throws ICCE']),passed=True))
    manifest['inputs']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources+[Path(__file__)]}
    manifest['harnessSha256']=hashlib.sha256(SOURCE.encode()).hexdigest();manifest['passed']=True
    (evidence/'summary.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('Pinned JBR inverse parents PASS: '+str(manifest['runs']))
    print('Evidence: '+str(evidence.relative_to(ROOT)))
if __name__=='__main__':main()
