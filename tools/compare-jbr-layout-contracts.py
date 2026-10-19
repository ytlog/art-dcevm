#!/usr/bin/env python3
"""Pin superclass byte borrowing, raw offset history and DEX field-order erasure."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import struct
from evidence_paths import new_evidence_directory
ROOT=Path(__file__).resolve().parents[1]
REVISION='cf1f197170a0f04c6559feb873e362583be021d1'
SOURCES={
 'A.java':'public class A {}',
 'B.java':'public class B extends A { public int b=2; public int parentValue=2; public static int calls; public B(){calls++;} }',
 'C.java':'public class C extends A { public int c=3; public int guard=5; }',
 'Offsets.java':'public class Offsets { public volatile int aa=11; public volatile int zz=22; }',
}
HARNESS=r'''
import java.lang.instrument.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.concurrent.atomic.AtomicIntegerFieldUpdater;
public class LayoutContractOracle {
 static Instrumentation inst;
 public static void premain(String args,Instrumentation value){inst=value;}
 static void out(String key,Object value){System.out.println(key+"="+value);}
 static void change(Class<?> type,String path)throws Exception{
  inst.redefineClasses(new ClassDefinition(type,Files.readAllBytes(Paths.get(path))));
 }
 public static void main(String[] args)throws Exception{
  if(args[0].equals("parent")){
   Class<?> type=Class.forName("C"),parent=Class.forName("B");
   Object first=type.getConstructor().newInstance(),second=type.getConstructor().newInstance();
   type.getField("c").setInt(first,61);type.getField("guard").setInt(first,97);
   type.getField("c").setInt(second,67);type.getField("guard").setInt(second,101);
   change(type,args[1]);
   out("firstBorrowedB",parent.getField("b").getInt(first));
   out("firstBorrowedParentValue",parent.getField("parentValue").getInt(first));
   out("secondBorrowedB",parent.getField("b").getInt(second));
   out("secondBorrowedParentValue",parent.getField("parentValue").getInt(second));
   out("ownC",type.getField("c").getInt(first));out("ownGuard",type.getField("guard").getInt(second));
   out("parentConstructorsBeforeNew",parent.getField("calls").getInt(null));
   System.gc();out("afterGc",parent.getField("b").getInt(first));
   Object fresh=type.getConstructor().newInstance();out("freshB",parent.getField("b").getInt(fresh));
   out("freshParentValue",parent.getField("parentValue").getInt(fresh));
   out("parentConstructorsAfterNew",parent.getField("calls").getInt(null));
  } else {
   Class type=Class.forName("Offsets");Object value=type.getConstructor().newInstance();
   Field field=type.getField("zz");AtomicIntegerFieldUpdater updater=AtomicIntegerFieldUpdater.newUpdater(type,"zz");
   Class<?> unsafeType=Class.forName("sun.misc.Unsafe");Field singleton=unsafeType.getDeclaredField("theUnsafe");singleton.setAccessible(true);
   Object unsafe=singleton.get(null);Method offset=unsafeType.getMethod("objectFieldOffset",Field.class);
   long old=((Number)offset.invoke(unsafe,field)).longValue();Method get=unsafeType.getMethod("getInt",Object.class,long.class);
   out("rawBefore",get.invoke(unsafe,value,old));out("updaterBefore",updater.get(value));
   change(type,args[1]);
   long fresh=((Number)offset.invoke(unsafe,type.getField("zz"))).longValue();
   out("offsetMoved",old!=fresh);out("cachedReflection",field.getInt(value));
   out("rawAfter",get.invoke(unsafe,value,old));out("cachedUpdater",updater.get(value));
   out("freshUpdater",AtomicIntegerFieldUpdater.newUpdater(type,"zz").get(value));
   out("newField",type.getField("bb").getInt(value));System.gc();
   out("afterGc",get.invoke(unsafe,value,old));
  }
 }
}
'''
PARENT_EXPECTED={'firstBorrowedB':'61','firstBorrowedParentValue':'97','secondBorrowedB':'67','secondBorrowedParentValue':'101',
 'ownC':'61','ownGuard':'101','parentConstructorsBeforeNew':'0','afterGc':'61','freshB':'2','freshParentValue':'2','parentConstructorsAfterNew':'1'}
OFFSET_EXPECTED={'rawBefore':'22','updaterBefore':'22','offsetMoved':'true','cachedReflection':'0','rawAfter':'0',
 'cachedUpdater':'0','freshUpdater':'22','newField':'0','afterGc':'0'}

def declared_fields(data):
 """Read ClassFile field order without relying on constant-pool numbering."""
 offset=10;index=1;pool={};count=struct.unpack_from('>H',data,8)[0]
 while index<count:
  tag=data[offset];offset+=1
  if tag==1:
   length=struct.unpack_from('>H',data,offset)[0];offset+=2;pool[index]=data[offset:offset+length].decode();offset+=length
  elif tag in (3,4,9,10,11,12,17,18):offset+=4
  elif tag in (5,6):offset+=8;index+=1
  elif tag in (7,8,16,19,20):offset+=2
  elif tag==15:offset+=3
  else:raise ValueError('Unknown classfile constant')
  index+=1
 offset+=6;interfaces=struct.unpack_from('>H',data,offset)[0];offset+=2+2*interfaces
 count=struct.unpack_from('>H',data,offset)[0];offset+=2;fields=[]
 for _ in range(count):
  _,name,_,attributes=struct.unpack_from('>HHHH',data,offset);offset+=8;fields.append(pool[name])
  for _ in range(attributes):
   _,length=struct.unpack_from('>HI',data,offset);offset+=6+length
 return fields

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--java-home',required=True,type=Path);parser.add_argument('--sdk',type=Path)
 args=parser.parse_args();java=args.java_home.resolve();sdk=args.sdk
 if sdk is None:
  properties=dict(line.split('=',1) for line in (ROOT/'local.properties').read_text().splitlines() if '=' in line and not line.startswith('#'))
  sdk=Path(properties['sdk.dir'])
 evidence=new_evidence_directory(ROOT/'build/evidence/jbr-layout-contracts')
 manifest=dict(sourceOracleRevision=REVISION,runs=[],scope='Primitive-only raw reads stay inside the enlarged object; no reference-offset writes or unsafe VM defect reproduction.')
 def run(name,command):
  r=subprocess.run(list(map(str,command)),capture_output=True,text=True,cwd=evidence,env=dict(os.environ,JAVA_HOME=str(java)),timeout=120)
  (evidence/(name+'.log')).write_text(r.stdout+r.stderr)
  if r.returncode:raise RuntimeError(name+' failed; inspect ignored evidence')
  return r.stdout+r.stderr
 version=run('version',[java/'bin/java','-version'])
 if 'JBR-21.0.11+10-1163.116' not in version:raise RuntimeError('Requires pinned JBR')
 manifest['toolchain']={
  'jbrRuntime':'JBR-21.0.11+10-1163.116', 'buildTools':'35.0.0', 'minApi':30,
  'd8Version':run('d8-version',[sdk/'build-tools/35.0.0/d8','--version']).strip(),
  'd8JarSha256':hashlib.sha256((sdk/'build-tools/35.0.0/lib/d8.jar').read_bytes()).hexdigest(),
  'androidJarSha256':hashlib.sha256((sdk/'platforms/android-35/android.jar').read_bytes()).hexdigest(),
 }
 source=evidence/'source';source.mkdir();old=evidence/'old'
 for name,text in SOURCES.items():(source/name).write_text(text)
 (source/'LayoutContractOracle.java').write_text(HARNESS)
 run('compile-old',[java/'bin/javac','--release','11','-d',old,*sorted(source.glob('*.java'))])
 patches=evidence/'patches';patches.mkdir()
 for name,text in {'C.java':SOURCES['C.java'].replace('extends A','extends B'),
                   'Offsets.java':'public class Offsets { public volatile int aa=11; public volatile int bb=33; public volatile int zz=22; }'}.items():
  (patches/name).write_text(text)
 run('compile-patches',[java/'bin/javac','--release','11','-cp',old,'-d',patches,*sorted(patches.glob('*.java'))])
 mf=evidence/'manifest.mf';mf.write_text('Premain-Class: LayoutContractOracle\nCan-Redefine-Classes: true\n\n');agent=evidence/'agent.jar'
 run('agent',[java/'bin/jar','cfm',agent,mf,'-C',old,'LayoutContractOracle.class'])
 for mode,flags in [('interpreter',['-Xint']),('batch',['-Xbatch'])]:
  for case,klass,expected in [('parent','C',PARENT_EXPECTED),('offset','Offsets',OFFSET_EXPECTED)]:
   output=run(case+'-'+mode,[java/'bin/java',*flags,'-XX:+AllowEnhancedClassRedefinition','-javaagent:'+str(agent),'-cp',old,
       'LayoutContractOracle',case,patches/(klass+'.class')])
   observed=dict(re.findall(r'^(\w+)=(.*)$',output,re.M))
   if observed!=expected:raise RuntimeError(case+' differs from pinned layout expectations; inspect ignored evidence')
   manifest['runs'].append(dict(case=case,mode=mode,observations=observed,passed=True))
 # Keep source file name and source line count fixed: only declaration order changes.
 field_order=[]
 for label,fields in [('ab','public int a; public long b;'),('ba','public long b; public int a;')]:
  directory=evidence/label;directory.mkdir();p=directory/'FieldOrder.java';p.write_text('public class FieldOrder { '+fields+' }\n')
  run('compile-order-'+label,[java/'bin/javac','--release','11','-d',directory,p])
  dex=directory/'dex';dex.mkdir()
  run('dex-order-'+label,[sdk/'build-tools/35.0.0/d8','--min-api','30','--lib',sdk/'platforms/android-35/android.jar','--output',dex,directory/'FieldOrder.class'])
  names=declared_fields((directory/'FieldOrder.class').read_bytes())
  if names!=list(label):raise RuntimeError('Compiler did not preserve the requested ClassFile order')
  field_order.append(dict(variant=label,declaredFields=names,classSha256=hashlib.sha256((directory/'FieldOrder.class').read_bytes()).hexdigest(),dexSha256=hashlib.sha256((dex/'classes.dex').read_bytes()).hexdigest()))
 if field_order[0]['classSha256']==field_order[1]['classSha256'] or field_order[0]['dexSha256']!=field_order[1]['dexSha256']:
  raise RuntimeError('Pure declaration-order representation differs from the frozen D8 contract')
 manifest.update(fieldOrder=field_order,harnessSha256=hashlib.sha256(HARNESS.encode()).hexdigest(),toolSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),passed=True)
 (evidence/'summary.json').write_text(json.dumps(manifest,indent=2)+'\n')
 print('Pinned layout contracts PASS: 4 isolated processes, 40 observations; reordered ClassFiles produce identical DEX')
 print('Evidence: '+str(evidence.relative_to(ROOT)))
if __name__=='__main__':main()
