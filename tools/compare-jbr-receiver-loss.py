#!/usr/bin/env python3
"""Pin cached calls after an ordinary receiver class loses its interface.

Checked-JNI termination probes are opt-in, isolated, and recorded as reference
invalid-call diagnostics rather than successful invocation semantics.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess

from evidence_paths import new_evidence_directory
from method_handle_adapter_expectations import expected_graphs

ROOT = Path(__file__).resolve().parents[1]
REVISION = 'cf1f197170a0f04c6559feb873e362583be021d1'
JAVA = r'''

import java.lang.instrument.*;import java.lang.invoke.*;import java.lang.reflect.*;import java.nio.file.*;
public class LossProbe {
 static Instrumentation inst;static I typed;static Target object;static MethodHandle classHandle;
 public static int recovered(IncompatibleClassChangeError ignored){return 31;}
 public static void premain(String s,Instrumentation i){inst=i;}
 static native void cache(Class<?> i,Class<?> c);static native int call(Object o,int owner,int kind,int form);
 interface Op {Object run()throws Throwable;}
 static void out(String name,Op op){try{System.out.println(name+"="+op.run());}catch(Throwable e){System.out.println(name+"="+e.getClass().getName()+(e instanceof InvocationTargetException?":"+e.getCause().getClass().getName():""));}}
 static int hotInterface(int count){int sum=0;for(int i=0;i<count;i++)try{sum+=typed.value();}catch(IncompatibleClassChangeError e){sum--;}return sum;}
 static int hotClass(int count)throws Throwable{int sum=0;for(int i=0;i<count;i++)try{sum+=(int)classHandle.invokeExact(object);}catch(AbstractMethodError e){sum-=2;}return sum;}
 static void change(Path p)throws Exception{inst.redefineClasses(new ClassDefinition(Target.class,Files.readAllBytes(p)));}
 public static void main(String[] args)throws Throwable{
  System.load(args[0]);object=new Target();typed=object;final int identity=System.identityHashCode(object);
  MethodHandle ih=MethodHandles.publicLookup().findVirtual(I.class,"value",MethodType.methodType(int.class));
  MethodHandle ch=MethodHandles.publicLookup().findVirtual(Target.class,"value",MethodType.methodType(int.class));
  classHandle=ch;MethodHandle boundI=ih.bindTo(typed),boundC=ch.bindTo(object),adapted=ih.asType(MethodType.methodType(int.class,Object.class));
  Method im=I.class.getMethod("value"),cm=Target.class.getMethod("value");
  MethodHandle nullBound=ih.bindTo(null),bodyBound=MethodHandles.publicLookup().findVirtual(I.class,"bodyError",MethodType.methodType(int.class)).bindTo(typed);
  MethodHandle parameters=MethodHandles.publicLookup().findVirtual(I.class,"arguments",MethodType.methodType(int.class,int.class,String.class)).bindTo(typed);
  MethodHandle boxed=boundI.asType(MethodType.methodType(Object.class)),spread=boundI.asSpreader(Object[].class,0);
  MethodHandle caught=MethodHandles.catchException(boundI,IncompatibleClassChangeError.class,MethodHandles.publicLookup().findStatic(LossProbe.class,"recovered",MethodType.methodType(int.class,IncompatibleClassChangeError.class)));
  MethodHandle dropped=MethodHandles.dropArguments(boundI,0,String.class);
  com.github.ytlog.artdcevm.fixture.ReceiverLossAdapters adapters=new com.github.ytlog.artdcevm.fixture.ReceiverLossAdapters(typed);
  cache(I.class,Target.class);int id=Integer.parseInt(args[3]),kind=Integer.parseInt(args[4]),form=Integer.parseInt(args[5]);
  if(id>=0)out("beforeJni",()->call(object,id,kind,form));
  else {out("beforeHotI",()->hotInterface(120000));out("beforeHotC",()->hotClass(120000));}
  for(int round=0;round<(id<0?2:1);round++)for(int phase=0;phase<2;phase++){
   change(Paths.get(args[phase==0?1:2]));System.gc();String p="r"+round+"p"+phase;
   System.out.println("STATE "+p);out(p+"isInterface",()->I.class.isInstance(object));
   if(id>=0){out(p+"jni",()->call(object,id,kind,form));continue;}
   for(java.util.Map.Entry<String,String> e:adapters.observations().entrySet())System.out.println(p+"Graph"+e.getKey()+"="+e.getValue());
   out(p+"kept",()->object.kept);out(p+"self",()->object.self==object);out(p+"identity",()->identity==System.identityHashCode(object));
   out(p+"direct",()->typed.value());out(p+"interfaceExact",()->(int)ih.invokeExact(typed));
   out(p+"interfaceArgs",()->ih.invokeWithArguments(object));out(p+"interfaceBound",()->(int)boundI.invokeExact());
   out(p+"interfaceAdapter",()->(int)adapted.invokeExact((Object)object));
   out(p+"interfaceBoundArgs",()->boundI.invokeWithArguments());out(p+"boundWrongReturn",()->(long)boundI.invokeExact());
   out(p+"boundWrongArgument",()->(int)boundI.invokeExact(1));out(p+"boundNull",()->(int)nullBound.invokeExact());out(p+"boundBody",()->(int)bodyBound.invokeExact());
   out(p+"boundArgsList",()->boundI.invokeWithArguments(java.util.Collections.emptyList()));
   out(p+"boundArgsNullArray",()->boundI.invokeWithArguments((Object[])null));out(p+"boundArgsWrongArity",()->boundI.invokeWithArguments(1));
   out(p+"boundArgsNullList",()->boundI.invokeWithArguments((java.util.List<?>)null));
   out(p+"boundBoxed",()->(Object)boxed.invokeExact());out(p+"boundSpread",()->(int)spread.invokeExact(new Object[0]));
   out(p+"boundSpreadWrongLength",()->(int)spread.invokeExact(new Object[]{1}));out(p+"boundCaught",()->caught.invokeWithArguments());
   out(p+"boundDropped",()->dropped.invokeWithArguments("ignored"));out(p+"boundDroppedWrongReference",()->dropped.invokeWithArguments(new Object()));
   out(p+"boundNullArgs",()->nullBound.invokeWithArguments());out(p+"boundBodyArgs",()->bodyBound.invokeWithArguments());
   out(p+"boundParameters",()->parameters.invokeWithArguments(5,"abc"));out(p+"boundWidening",()->parameters.invokeWithArguments((short)5,"abc"));
   out(p+"boundBadReference",()->parameters.invokeWithArguments(5,new Object()));out(p+"boundBadPrimitive",()->parameters.invokeWithArguments(5.5,"abc"));
   out(p+"boundNullPrimitive",()->parameters.invokeWithArguments(null,"abc"));out(p+"boundNullReference",()->parameters.invokeWithArguments(5,null));
   out(p+"boundParameterArity",()->parameters.invokeWithArguments(5));
   out(p+"classExact",()->(int)ch.invokeExact(object));out(p+"classArgs",()->ch.invokeWithArguments(object));
   out(p+"classBound",()->(int)boundC.invokeExact());out(p+"classBoundArgs",()->boundC.invokeWithArguments());out(p+"interfaceReflection",()->im.invoke(object));
   out(p+"classReflection",()->cm.invoke(object));out(p+"freshClassHandle",()->MethodHandles.publicLookup().findVirtual(Target.class,"value",MethodType.methodType(int.class)).invokeWithArguments(object));
   out(p+"hotInterface",()->hotInterface(40000));out(p+"hotClass",()->hotClass(40000));
  }
 }
}
'''
NATIVE = r'''

#include <jni.h>
#include <stdarg.h>
static jmethodID ids[2];static jclass types[2];
JNIEXPORT void JNICALL Java_LossProbe_cache(JNIEnv* e,jclass unused,jclass i,jclass c){(void)unused;types[0]=(*e)->NewGlobalRef(e,i);types[1]=(*e)->NewGlobalRef(e,c);for(int x=0;x<2;x++)ids[x]=(*e)->GetMethodID(e,types[x],"value","()I");}
static jint callV(JNIEnv* e,jobject o,int owner,int kind,jmethodID method,...){va_list args;va_start(args,method);jint result=kind?(*e)->CallNonvirtualIntMethodV(e,o,types[owner],method,args):(*e)->CallIntMethodV(e,o,method,args);va_end(args);return result;}
JNIEXPORT jint JNICALL Java_LossProbe_call(JNIEnv* e,jclass unused,jobject o,jint owner,jint kind,jint form){(void)unused;if(form==1)return callV(e,o,owner,kind,ids[owner]);if(form==2)return kind?(*e)->CallNonvirtualIntMethodA(e,o,types[owner],ids[owner],NULL):(*e)->CallIntMethodA(e,o,ids[owner],NULL);return kind?(*e)->CallNonvirtualIntMethod(e,o,types[owner],ids[owner]):(*e)->CallIntMethod(e,o,ids[owner]);}
'''

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def expected_java(own):
    value = '11' if own else '7'
    expected = {'beforeHotI':str(int(value)*120000), 'beforeHotC':str(int(value)*120000)}
    for round_ in range(2):
        for phase in range(2):
            p = f'r{round_}p{phase}'
            values = {key:value for key in ('direct','interfaceExact','interfaceArgs',
                'interfaceBound','interfaceAdapter','interfaceBoundArgs','classExact','classArgs','classBound','classBoundArgs',
                'interfaceReflection','classReflection','freshClassHandle')}
            values.update(isInterface='true', kept='23' if own else '19', self='true', identity='true',
                boundWrongReturn='java.lang.invoke.WrongMethodTypeException', boundWrongArgument='java.lang.invoke.WrongMethodTypeException',
                boundNull='java.lang.NullPointerException', boundBody='java.lang.IllegalStateException')
            values.update(boundArgsList=value,boundArgsNullArray=value,boundBoxed=value,boundSpread=value,boundCaught=value,boundDropped=value,
                boundArgsWrongArity='java.lang.invoke.WrongMethodTypeException',boundArgsNullList='java.lang.NullPointerException',
                boundSpreadWrongLength='java.lang.IllegalArgumentException',boundDroppedWrongReference='java.lang.ClassCastException',
                boundNullArgs='java.lang.NullPointerException',boundBodyArgs='java.lang.IllegalStateException',boundParameters='8',boundWidening='8',
                boundBadReference='java.lang.ClassCastException',boundBadPrimitive='java.lang.ClassCastException',boundNullPrimitive='java.lang.NullPointerException',
                boundNullReference='java.lang.NullPointerException',boundParameterArity='java.lang.invoke.WrongMethodTypeException')
            values['hotInterface'] = values['hotClass'] = str(int(value)*40000)
            if phase == 0:
                values.update(isInterface='false', direct='java.lang.IncompatibleClassChangeError',
                    interfaceExact='java.lang.IncompatibleClassChangeError',
                    interfaceBound='java.lang.IncompatibleClassChangeError', interfaceBoundArgs='java.lang.IncompatibleClassChangeError',
                    boundBody='java.lang.IncompatibleClassChangeError',
                    interfaceArgs='java.lang.ClassCastException', interfaceAdapter='java.lang.ClassCastException',
                    interfaceReflection='java.lang.IllegalArgumentException', hotInterface='-40000')
                for key in ('boundArgsList','boundArgsNullArray','boundBoxed','boundSpread','boundDropped','boundBodyArgs','boundParameters','boundWidening','boundNullReference'):
                    values[key]='java.lang.IncompatibleClassChangeError'
                values['boundCaught']='31'
                if not own:
                    for key in ('classExact','classArgs','classBound','classBoundArgs'):
                        values[key] = 'java.lang.AbstractMethodError'
                    values.update(classReflection='java.lang.IllegalArgumentException',
                        freshClassHandle='java.lang.NoSuchMethodException', hotClass='-80000')
            values.update({'Graph'+key:result for key,result in expected_graphs(phase == 0, int(value)).items()})
            expected.update({p+key:result for key,result in values.items()})
    return expected


def observations(output):
    return dict(re.findall(r'^((?:r[01]p[01]|before)[A-Za-z]+)=(.*)$', output, re.M))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', type=Path, required=True)
    parser.add_argument('--probe-jni-boundaries', action='store_true',
                        help='Also isolate invalid-receiver CheckJNI diagnostics and unchecked JNI behavior')
    args = parser.parse_args()
    java = args.java_home.resolve()
    evidence = new_evidence_directory(ROOT/'build/evidence/jbr-receiver-loss')
    manifest = {'sourceOracleRevision':REVISION, 'toolSha256':sha(Path(__file__)),
                'adapterExpectationsSha256':sha(ROOT/'tools/method_handle_adapter_expectations.py'),
                'harnessTemplateSha256':hashlib.sha256(JAVA.encode()).hexdigest(),
                'nativeSha256':hashlib.sha256(NATIVE.encode()).hexdigest(), 'runs':[]}

    def run(name, command, directory, required=True):
        try:
            result = subprocess.run(list(map(str, command)), cwd=directory,
                                    capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired as error:
            raw = lambda value: value.decode(errors='replace') if isinstance(value,bytes) else value or ''
            (directory/(name+'.log')).write_text(raw(error.stdout)+raw(error.stderr))
            raise RuntimeError(name+' timed out; inspect ignored evidence') from error
        (directory/(name+'.log')).write_text(result.stdout+result.stderr)
        if required and result.returncode:
            raise RuntimeError(name+' failed; inspect ignored evidence')
        return result

    version = run('version', [java/'bin/java','-version'], evidence)
    if 'JBR-21.0.11+10-1163.116' not in version.stdout+version.stderr:
        raise RuntimeError('Requires pinned JBR')
    extension = 'dylib' if platform.system() == 'Darwin' else 'so'
    manifest.update(javaExecutableSha256=sha(java/'bin/java'),
                    jvmLibrarySha256=sha(java/'lib/server'/('libjvm.'+extension)))
    for own in (False,True):
        work = evidence/('own' if own else 'default');work.mkdir()
        old = work/'old';patch = work/'patch';old.mkdir();patch.mkdir()
        fixture = Path('app/src/debug/java/com/github/ytlog/artdcevm/fixture/ReceiverLossFixture.java')
        replacement = Path('fixtures/receiver-loss-drop/com/github/ytlog/artdcevm/fixture/ReceiverLossFixture.java')
        name = 'Own' if own else 'DefaultOnly'
        target = 'com.github.ytlog.artdcevm.fixture.ReceiverLossFixture.'+name
        contract = 'com.github.ytlog.artdcevm.fixture.ReceiverLossFixture.Contract'
        graphs = ROOT/'app/src/debug/java/com/github/ytlog/artdcevm/fixture/ReceiverLossAdapters.java'
        source = re.sub(r'\bTarget\b', target, re.sub(r'\bI\b', contract, JAVA))
        (old/'ReceiverLossFixture.java').write_text((ROOT/fixture).read_text())
        (patch/'ReceiverLossFixture.java').write_text((ROOT/replacement).read_text())
        (old/'LossProbe.java').write_text(source)
        (old/'ReceiverLossAdapters.java').write_text(graphs.read_text())
        manifest.setdefault('fixtureSha256', {}).update({fixture.as_posix():sha(ROOT/fixture),
                                                       replacement.as_posix():sha(ROOT/replacement),
                                                       graphs.relative_to(ROOT).as_posix():sha(graphs)})
        relative = Path('com/github/ytlog/artdcevm/fixture/ReceiverLossFixture$'+name+'.class')
        run('compile-old', [java/'bin/javac','--release','11','-d',old,*sorted(old.glob('*.java'))],work)
        run('compile-patch', [java/'bin/javac','--release','11','-d',patch,patch/'ReceiverLossFixture.java'],work)
        (work/'native.c').write_text(NATIVE);library=work/('libloss.'+extension)
        run('compile-native', [shutil.which('cc') or 'cc','-shared','-fPIC','-I'+str(java/'include'),
            '-I'+str(java/'include'/('darwin' if platform.system()=='Darwin' else 'linux')),
            work/'native.c','-o',library],work)
        (work/'manifest.mf').write_text('Premain-Class: LossProbe\nCan-Redefine-Classes: true\n\n')
        agent=work/'agent.jar'
        run('agent',[java/'bin/jar','cfm',agent,work/'manifest.mf','-C',old,'LossProbe.class'],work)
        for check in (False,True):
            checked=['-Xcheck:jni'] if check else []
            base=[java/'bin/java',*checked,'-XX:+AllowEnhancedClassRedefinition',
                  '-XX:-CreateCoredumpOnCrash','-javaagent:'+str(agent),'-cp',old]
            tail=['LossProbe',library,patch/relative,old/relative]
            for mode,flags in [('interpreter',['-Xint']),('jit',['-Xbatch','-XX:+PrintCompilation',
                    '-XX:CompileCommand=compileonly,LossProbe::hot*'])]:
                label=f'java-{mode}-check{int(check)}'
                result=run(label,[*base,*flags,*tail,-1,0,0],work)
                actual=observations(result.stdout)
                if actual!=expected_java(own):
                    raise RuntimeError(label+' differs from pinned receiver-loss expectations; inspect ignored evidence')
                if mode=='jit':
                    states=re.split(r'^STATE r[01]p[01]$',result.stdout,flags=re.M)
                    compiled=lambda part,name:any(re.search(r'^\s*\d+\s+\d+\s.*LossProbe::'+name+r'\s',line)
                        and 'made ' not in line and 'excluded' not in line for line in part.splitlines())
                    if len(states)!=5 or any(not all(compiled(part,name)
                            for name in ('hotInterface','hotClass')) for part in states):
                        raise RuntimeError(label+' lacks initial/post-transition hot compilation evidence')
                manifest['runs'].append(dict(ownMethod=own,checkJni=check,mode=mode,
                    scope='java-caches',checks=len(actual),passed=True))
            if not args.probe_jni_boundaries:
                continue
            for owner,kind,form in ((owner,kind,form) for owner in (0,1) for kind in (0,1) for form in (0,1,2)):
                label=f'jni-check{int(check)}-owner{owner}-kind{kind}-form{form}'
                result=run(label,[*base,'-Xint','-XX:ErrorFile='+str(work/(label+'-hs_err.log')),
                                 *tail,owner,kind,form],work,False)
                actual=observations(result.stdout)
                before='7' if kind and owner==0 else ('11' if own else '7')
                diagnostic=check and not (own and owner==1)
                expected={'beforeJni':before,'r0p0isInterface':'false'}
                if diagnostic:
                    if result.returncode==0 or 'FATAL ERROR in native method: Wrong object class or methodID passed to JNI call' not in result.stdout+result.stderr:
                        raise RuntimeError(label+' lacks the pinned invalid-receiver CheckJNI diagnostic')
                else:
                    after=('11' if own and owner==1 else '7') if kind else (
                        '11' if own and owner==1 else 'java.lang.IncompatibleClassChangeError')
                    expected.update(r0p0jni=after,r0p1isInterface='true',r0p1jni=before)
                    if result.returncode:
                        raise RuntimeError(label+' unexpectedly failed; inspect ignored evidence')
                if actual!=expected:
                    raise RuntimeError(label+' changed JNI baseline/transition/restoration observations')
                manifest['runs'].append(dict(ownMethod=own,checkJni=check,mode='interpreter',
                    scope='jni-virtual' if kind==0 else 'jni-nonvirtual',idLookupOwner='interface' if owner==0 else 'class',
                    referenceDiagnostic=diagnostic,form=("plain","V","A")[form],checks=len(actual),passed=True))
    manifest.update(passed=True,jitScope='Only hotInterface/hotClass; each initial and post-transition interval requires compilation evidence.',
        scope='Ordinary class removes/restores one interface; default-only and retained own implementation. JNI plain/V/A virtual/nonvirtual use cached GetMethodID results; JBR does not establish ART pointer/index mode behavior.')
    (evidence/'summary.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('Pinned receiver loss PASS: '+str(len(manifest['runs']))+' isolated processes; CheckJNI invalid-call diagnostics are separate')
    print('Evidence: '+str(evidence.relative_to(ROOT)))


if __name__=='__main__':
    main()
