#!/usr/bin/env python3
"""Measure class/interface/annotation/enum transitions on the pinned JBR.

Each matrix cell runs in its own VM, including rejected definitions. The optional
unsafe probe deliberately isolates a known live class-to-enum GC failure; a
successful redefine alone is not evidence that this conversion is supported.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
REVISION = 'cf1f197170a0f04c6559feb873e362583be021d1'
SOURCES = {
    'class': '''public class Target {
      public int marker=19; public Object reference="kept";
      public int value(){return 7;} public static int stable=23;
    }''',
    'interface': '''public interface Target {
      default int value(){return 11;} int stable=23;
    }''',
    'annotation': '''public @interface Target { int value() default 13; }''',
    'enum': '''public enum Target {
      ONE; public int marker=19; public Object reference="kept";
      public int value(){return 17;} public static int stable=23;
    }''',
}
HARNESS = r'''
import java.lang.instrument.*;
import java.lang.invoke.*;
import java.lang.reflect.*;
import java.nio.file.*;
public class ClassKindOracle {
  static Instrumentation inst;
  public static void premain(String args, Instrumentation value){inst=value;}
  interface Op {Object run() throws Throwable;}
  static void out(String name, Op op) {
    try {System.out.println(name+"="+op.run());}
    catch(Throwable error) {
      while(error instanceof InvocationTargetException)error=error.getCause();
      System.out.println(name+"=ERROR:"+error.getClass().getName());
    }
  }
  static void change(Class<?> target, Path file) throws Throwable {
    inst.redefineClasses(new ClassDefinition(target, Files.readAllBytes(file)));
  }
  public static void main(String[] args) throws Throwable {
    Path patch=Path.of(args[0]), original=Path.of(args[1]);
    String mode=args[2]; Class<?> type=Class.forName("Target"); Object instance=null;
    if(mode.equals("subtype")) Class.forName("Impl");
    if(mode.equals("instance") || mode.equals("subtype-instance") || mode.equals("constant")) {
      if(mode.equals("constant"))instance=type.getField("ONE").get(null);
      else if(type.isEnum())instance=type.getEnumConstants()[0];
      else if(type.isInterface() || mode.equals("subtype-instance"))
        instance=Class.forName("Impl").getConstructor().newInstance();
      else instance=type.getConstructor().newInstance();
    }
    final Object receiver=instance;
    Class<?> caller=Class.forName("Caller");
    Method call=caller.getMethod("call",Object.class), is=caller.getMethod("is",Object.class),
      cast=caller.getMethod("cast",Object.class), cached=type.getMethod("value");
    MethodHandle handle=MethodHandles.publicLookup().unreflect(cached);
    Field marker=null;
    try {marker=type.getField("marker");}catch(NoSuchFieldException ignored){}
    final Field cachedField=marker;
    if(receiver!=null) {
      out("before",()->call.invoke(null,receiver));
      out("fieldBefore",()->cachedField==null?"absent":cachedField.get(receiver));
    }
    // Failure of any member of a batch must preserve both target and peer.
    Class<?> peer=Class.forName("Peer");
    try {
      inst.redefineClasses(new ClassDefinition(type,Files.readAllBytes(patch)),
        new ClassDefinition(peer,new byte[]{0,1}));
      System.out.println("batch=UNEXPECTED_SUCCESS"); return;
    }catch(Throwable error){System.out.println("batch=ERROR:"+error.getClass().getName());}
    out("batchKind",()->type.isInterface()+":"+type.isAnnotation()+":"+type.isEnum());
    out("batchPeer",()->peer.getMethod("value").invoke(null));
    if(receiver!=null)out("batchCall",()->call.invoke(null,receiver));
    for(int round=0;round<2;round++) {
      String suffix=""+round;
      try {change(type,patch);System.out.println("redefine"+suffix+"=OK");}
      catch(Throwable error){System.out.println("redefine"+suffix+"=ERROR:"+error.getClass().getName());return;}
      out("kind"+suffix,()->type.isInterface()+":"+type.isAnnotation()+":"+type.isEnum());
      out("nullCall"+suffix,()->call.invoke(null,(Object)null));
      out("cachedNullReflection"+suffix,()->cached.invoke(null));
      out("cachedNullHandle"+suffix,()->handle.invoke((Object)null));
      out("freshNullReflection"+suffix,()->type.getMethod("value").invoke(null));
      if(args[3].equals("class") && type.isInterface())
        out("oldConstructor"+suffix,()->caller.getMethod("construct").invoke(null));
      out("isNull"+suffix,()->is.invoke(null,(Object)null));
      out("castNull"+suffix,()->cast.invoke(null,(Object)null)==null);
      out("castOther"+suffix,()->cast.invoke(null,new Object()));
      out("default"+suffix,()->type.getMethod("value").getDefaultValue());
      out("enumConstants"+suffix,()->type.getEnumConstants()==null?"null":type.getEnumConstants().length);
      if(receiver!=null) {
        out("is"+suffix,()->is.invoke(null,receiver));
        out("cast"+suffix,()->cast.invoke(null,receiver)!=null);
        out("oldCall"+suffix,()->call.invoke(null,receiver));
        out("cachedReflection"+suffix,()->cached.invoke(receiver));
        out("freshReflection"+suffix,()->type.getMethod("value").invoke(receiver));
        out("cachedHandle"+suffix,()->handle.invoke(receiver));
        out("cachedField"+suffix,()->cachedField==null?"absent":cachedField.get(receiver));
        if(receiver instanceof Enum) {
          out("enumName"+suffix,()->((Enum<?>)receiver).name());
          out("enumOrdinal"+suffix,()->((Enum<?>)receiver).ordinal());
        }
      }
      System.gc();out("gc"+suffix,()->receiver==null||type.isInstance(receiver));
      try {change(type,original);System.out.println("restore"+suffix+"=OK");}
      catch(Throwable error){System.out.println("restore"+suffix+"=ERROR:"+error.getClass().getName());return;}
      if(receiver!=null)out("restoredCall"+suffix,()->call.invoke(null,receiver));
    }
  }
}
'''
PAIRED = r'''
import java.lang.instrument.*;
import java.nio.file.*;
public class PairedKindOracle {
  static Instrumentation inst;
  public static void premain(String args, Instrumentation value){inst=value;}
  public static void main(String[] args) throws Exception {
    Class<?> target=Class.forName("Target"), child=Class.forName("Impl");
    Object receiver=child.getConstructor().newInstance();
    try {
      inst.redefineClasses(new ClassDefinition(target,Files.readAllBytes(Path.of(args[0]))),
        new ClassDefinition(child,Files.readAllBytes(Path.of(args[1]))));
      System.out.println("paired=OK");
    }catch(Throwable error){System.out.println("paired=ERROR:"+error.getClass().getName());}
    System.out.println("oldCall="+child.getMethod("value").invoke(receiver));
    System.gc();System.out.println("gc=true");
  }
}
'''
CONSTRUCTORS = r'''
import java.lang.instrument.*;
import java.lang.invoke.*;
import java.lang.reflect.*;
import java.nio.file.*;
public class ConstructorKindOracle {
  static Instrumentation inst;
  public static void premain(String args, Instrumentation value){inst=value;}
  interface Op {Object run() throws Throwable;}
  static void out(String name, Op op) {
    try {op.run();System.out.println(name+"=OK");}
    catch(Throwable error) {
      String chain=error.getClass().getName();
      if(error.getCause()!=null)chain+=":"+error.getCause().getClass().getName();
      System.out.println(name+"=ERROR:"+chain);
    }
  }
  public static void main(String[] args) throws Throwable {
    Class<?> type=Class.forName("Target");
    Constructor<?> cached=type.getConstructor();
    MethodHandle handle=MethodHandles.publicLookup().unreflectConstructor(cached);
    if(args[2].equals("warm"))out("warm",()->cached.newInstance());
    System.gc();
    inst.redefineClasses(new ClassDefinition(type,Files.readAllBytes(Path.of(args[0]))));
    out("reflection",()->cached.newInstance());
    out("handle",()->handle.invoke());
    inst.redefineClasses(new ClassDefinition(type,Files.readAllBytes(Path.of(args[1]))));
    out("restoredReflection",()->cached.newInstance());
    out("restoredHandle",()->handle.invoke());
  }
}
'''


def class_flags(data, clear):
    """Clear only ClassFile.access_flags; retain Enum superclass and layout."""
    result = bytearray(data)
    offset, index = 10, 1
    count = struct.unpack_from('>H', result, 8)[0]
    while index < count:
        tag = result[offset]
        offset += 1
        if tag == 1:
            offset += 2 + struct.unpack_from('>H', result, offset)[0]
        elif tag in (3, 4, 9, 10, 11, 12, 17, 18):
            offset += 4
        elif tag in (5, 6):
            offset += 8
            index += 1
        elif tag in (7, 8, 16, 19, 20):
            offset += 2
        elif tag == 15:
            offset += 3
        else:
            raise ValueError('Unknown constant pool tag')
        index += 1
    flags = struct.unpack_from('>H', result, offset)[0]
    struct.pack_into('>H', result, offset, flags & ~clear)
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', required=True, type=Path)
    parser.add_argument('--include-unsafe-live-enum', action='store_true')
    args = parser.parse_args()
    java = args.java_home.resolve() / 'bin'
    evidence = new_evidence_directory(ROOT / 'build/evidence/jbr-class-kinds')
    work = evidence / 'work'
    work.mkdir()
    manifest = {'sourceOracleRevision': REVISION, 'cells': [], 'fixtureSha256': {},
                'toolSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'harnessSha256': hashlib.sha256(HARNESS.encode()).hexdigest(),
                'pairedHarnessSha256': hashlib.sha256(PAIRED.encode()).hexdigest(),
                'constructorHarnessSha256': hashlib.sha256(CONSTRUCTORS.encode()).hexdigest()}

    def run(name, command, required=True):
        try:
            result = subprocess.run(list(map(str, command)), capture_output=True, timeout=45,
                                    cwd=work)
            output = result.stdout + result.stderr
            code = result.returncode
        except subprocess.TimeoutExpired as error:
            output = (error.stdout or b'') + (error.stderr or b'')
            code = -99
        (evidence / (name + '.log')).write_bytes(output)
        if required and code:
            raise RuntimeError(name + ' failed; inspect ignored evidence')
        return code, output.decode(errors='replace')

    _, version = run('version', [java / 'java', '-version'])
    if 'JBR-21.0.11+10-1163.116' not in version:
        raise RuntimeError('Requires fixed JBR 21.0.11+10-b1163.116')
    manifest['runtimeVersion'] = version.strip()
    manifest['javaExecutableSha256'] = hashlib.sha256((java / 'java').read_bytes()).hexdigest()
    for kind, source in SOURCES.items():
        directory = work / kind
        directory.mkdir()
        (directory / 'Target.java').write_text(source)
        files = [directory / 'Target.java']
        if kind != 'enum':
            impl = ('public class Impl extends Target {}' if kind == 'class' else
                    'public class Impl implements Target {public int value(){return 29;} '
                    'public Class<? extends java.lang.annotation.Annotation> annotationType(){return null;}}')
            (directory / 'Impl.java').write_text(impl)
            files.append(directory / 'Impl.java')
        run('compile-' + kind, [java / 'javac', '--release', '11', '-d', directory, *files])
        caller = ('public class Caller {public static int call(Object x){return ((Target)x).value();}'
                  'public static boolean is(Object x){return x instanceof Target;}'
                  'public static Object cast(Object x){return (Target)x;}' +
                  ('public static Object construct(){return new Target();}' if kind == 'class' else '') + '}')
        (directory / 'Caller.java').write_text(caller)
        run('caller-' + kind, [java / 'javac', '--release', '11', '-cp', directory,
                              '-d', directory, directory / 'Caller.java'])
        manifest['fixtureSha256'][kind] = hashlib.sha256((directory / 'Target.class').read_bytes()).hexdigest()
    ordinary_enum = work / 'enum-class'
    ordinary_enum.mkdir()
    (ordinary_enum / 'Target.class').write_bytes(class_flags((work / 'enum/Target.class').read_bytes(), 0x4000))
    for name in ('Caller.class',):
        (ordinary_enum / name).write_bytes((work / 'enum' / name).read_bytes())
    (work / 'ClassKindOracle.java').write_text(HARNESS)
    (work / 'Peer.java').write_text('public class Peer {public static int value(){return 41;}}')
    run('harness', [java / 'javac', '--release', '11', '-d', work,
                    work / 'ClassKindOracle.java', work / 'Peer.java'])
    (work / 'manifest.mf').write_text('Premain-Class: ClassKindOracle\nCan-Redefine-Classes: true\n\n')
    agent = work / 'agent.jar'
    run('agent', [java / 'jar', 'cfm', agent, work / 'manifest.mf', '-C', work, 'ClassKindOracle.class'])
    checks = 0
    for before in SOURCES:
        for after in SOURCES:
            modes = ('none', 'instance') if before == 'enum' else ('none', 'subtype', 'instance', 'subtype-instance')
            for mode in modes:
                unsafe = before == 'class' and after == 'enum' and mode == 'instance'
                if unsafe and not args.include_unsafe_live_enum:
                    continue
                name = before + '-' + after + '-' + mode
                code, output = run(name, [java / 'java', '-XX:+AllowEnhancedClassRedefinition',
                    '-XX:ErrorFile=hs_err_' + name + '.log', '-javaagent:' + str(agent),
                    '-cp', str(work) + ':' + str(work / before), 'ClassKindOracle',
                    work / after / 'Target.class', work / before / 'Target.class', mode, before], False)
                observations = dict(line.split('=', 1) for line in output.splitlines()
                                    if line.split('=', 1)[0] in {'before', 'fieldBefore', 'batch', 'batchKind', 'batchPeer', 'batchCall'}
                                    or any(line.startswith(prefix + str(i) + '=') for i in range(2)
                                           for prefix in ('redefine', 'kind', 'nullCall', 'oldConstructor', 'cachedNullReflection', 'cachedNullHandle',
                                                          'freshNullReflection', 'isNull', 'castNull', 'castOther',
                                                          'default', 'enumConstants', 'is', 'cast', 'oldCall',
                                                          'cachedReflection', 'freshReflection', 'cachedHandle', 'cachedField', 'enumName', 'enumOrdinal',
                                                          'gc', 'restore', 'restoredCall')))
                # Pin the matrix independently of the VM's observations.
                expected = ('ERROR:java.lang.UnsupportedOperationException' if before == 'enum' and after != 'enum'
                            else 'ERROR:java.lang.VerifyError' if before != after and
                            ((before == 'class' and mode.startswith('subtype')) or
                             (before in ('interface', 'annotation') and after in ('class', 'enum') and mode != 'none'))
                            else 'OK')
                if observations.get('redefine0') != expected or (code != 0 and not unsafe):
                    raise RuntimeError(name + ' differs from the pinned matrix; inspect ignored evidence')
                if not observations.get('batch', '').startswith('ERROR:') or observations.get('batchPeer') != '41':
                    raise RuntimeError(name + ' did not preserve the failed batch')
                if unsafe:
                    if code == 0:
                        raise RuntimeError('Live class-to-enum GC outcome changed; re-audit this boundary')
                elif expected == 'OK':
                    for key, value in {'isNull0': 'false', 'castNull0': 'true',
                                       'castOther0': 'ERROR:java.lang.ClassCastException', 'gc0': 'true'}.items():
                        if observations.get(key) != value:
                            raise RuntimeError(name + ': unexpected ' + key)
                    before_interface = before in ('interface', 'annotation')
                    after_interface = after in ('interface', 'annotation')
                    null_error = ('ERROR:java.lang.IncompatibleClassChangeError' if before_interface != after_interface
                                  else 'ERROR:java.lang.NullPointerException')
                    if observations.get('nullCall0') != null_error:
                        raise RuntimeError(name + ': unexpected old invocation error')
                    if before == 'class' and after_interface and observations.get('oldConstructor0') != 'ERROR:java.lang.InstantiationError':
                        raise RuntimeError(name + ': unexpected old constructor error')
                    for key in ('cachedNullReflection0', 'cachedNullHandle0', 'freshNullReflection0'):
                        if observations.get(key) != 'ERROR:java.lang.NullPointerException':
                            raise RuntimeError(name + ': unexpected ' + key)
                    if mode in ('instance', 'subtype-instance') and not (before == 'class' and after_interface):
                        for key in ('oldCall0', 'cachedReflection0', 'freshReflection0', 'cachedHandle0'):
                            if observations.get(key) != observations.get('before'):
                                raise RuntimeError(name + ': cached invocation changed: ' + key)
                    if after != 'enum' or before == 'enum':
                        if observations.get('restore1') != 'OK':
                            raise RuntimeError(name + ' did not survive repeated conversion')
                manifest['cells'].append({'before': before, 'after': after, 'mode': mode,
                                          'exitCode': code, 'observations': observations,
                                          'redefinitionAccepted': expected == 'OK',
                                          'completed': code == 0,
                                          'stableJbrProbe': code == 0 and expected == 'OK' and not
                                          (before == 'class' and after in ('interface', 'annotation') and mode == 'instance')})
                checks += 1
    # An enum bit is independent of the superclass. Keeping Enum admits both
    # directions, with live constants, instance fields and cached members.
    for before, after in (('enum', 'enum-class'), ('enum-class', 'enum')):
        name = before + '-' + after
        code, output = run(name, [java / 'java', '-XX:+AllowEnhancedClassRedefinition',
            '-javaagent:' + str(agent), '-cp', str(work) + ':' + str(work / before),
            'ClassKindOracle', work / after / 'Target.class', work / before / 'Target.class',
            'constant', before])
        observations = dict(line.split('=', 1) for line in output.splitlines() if '=' in line)
        if observations.get('restore1') != 'OK' or observations.get('gc1') != 'true':
            raise RuntimeError(name + ' failed preserved-Enum-superclass round trip')
        for key, value in {'enumName0': 'ONE', 'enumOrdinal0': '0', 'cachedField0': '19',
                           'oldCall0': '17', 'cachedReflection0': '17', 'cachedHandle0': '17'}.items():
            if observations.get(key) != value:
                raise RuntimeError(name + ' did not preserve ' + key)
        manifest['cells'].append({'before': before, 'after': after, 'exitCode': code,
                                  'observations': observations, 'supported': True})
        checks += 1
    manifest.update(passed=True, cellsChecked=checks)
    (work / 'PairedKindOracle.java').write_text(PAIRED)
    run('paired-harness', [java / 'javac', '--release', '11', '-d', work, work / 'PairedKindOracle.java'])
    (work / 'paired.mf').write_text('Premain-Class: PairedKindOracle\nCan-Redefine-Classes: true\n\n')
    paired_agent = work / 'paired.jar'
    run('paired-agent', [java / 'jar', 'cfm', paired_agent, work / 'paired.mf', '-C', work, 'PairedKindOracle.class'])
    for before, after in (('class', 'interface'), ('interface', 'class'),
                          ('annotation', 'class'), ('class', 'annotation')):
        name = 'paired-' + before + '-' + after
        directory = work / name
        directory.mkdir()
        relationship = 'implements' if after in ('interface', 'annotation') else 'extends'
        (directory / 'Impl.java').write_text('public class Impl ' + relationship + ' Target {'
            'public int value(){return 29;}'
            'public Class<? extends java.lang.annotation.Annotation> annotationType(){return null;}}')
        run('compile-' + name, [java / 'javac', '--release', '11', '-cp', work / after,
                                '-d', directory, directory / 'Impl.java'])
        _, output = run(name, [java / 'java', '-XX:+AllowEnhancedClassRedefinition',
            '-javaagent:' + str(paired_agent), '-cp', str(work) + ':' + str(work / before),
            'PairedKindOracle', work / after / 'Target.class', directory / 'Impl.class'])
        observations = dict(line.split('=', 1) for line in output.splitlines() if '=' in line)
        if observations.get('paired') != 'ERROR:java.lang.InternalError' or observations.get('gc') != 'true':
            raise RuntimeError(name + ' differs from the pinned paired-subtype boundary')
        if observations.get('oldCall') != ('7' if before == 'class' else '29'):
            raise RuntimeError(name + ' failed to preserve the rejected subtype')
        manifest['cells'].append({'before': before, 'after': after, 'mode': 'paired-subtype',
                                  'observations': observations, 'redefinitionAccepted': False})
        checks += 1
    (work / 'ConstructorKindOracle.java').write_text(CONSTRUCTORS)
    run('constructor-harness', [java / 'javac', '--release', '11', '-d', work, work / 'ConstructorKindOracle.java'])
    (work / 'constructor.mf').write_text('Premain-Class: ConstructorKindOracle\nCan-Redefine-Classes: true\n\n')
    constructor_agent = work / 'constructor.jar'
    run('constructor-agent', [java / 'jar', 'cfm', constructor_agent, work / 'constructor.mf',
                              '-C', work, 'ConstructorKindOracle.class'])
    manifest['constructorProbes'] = []
    for after in ('interface', 'annotation'):
        for mode in ('cold', 'warm'):
            name = 'constructor-' + after + '-' + mode
            _, output = run(name, [java / 'java', '-XX:+AllowEnhancedClassRedefinition',
                '-javaagent:' + str(constructor_agent), '-cp', str(work) + ':' + str(work / 'class'),
                'ConstructorKindOracle', work / after / 'Target.class', work / 'class/Target.class', mode])
            observations = dict(line.split('=', 1) for line in output.splitlines() if '=' in line)
            wrapped = 'java.lang.reflect.InvocationTargetException:' if mode == 'warm' else ''
            expected = {'reflection': 'ERROR:' + wrapped + 'java.lang.InstantiationException',
                        'handle': 'ERROR:java.lang.InstantiationException',
                        'restoredReflection': 'ERROR:' + wrapped +
                        ('java.lang.NoSuchMethodError' if mode == 'warm' else 'java.lang.InstantiationException'),
                        'restoredHandle': 'ERROR:java.lang.NoSuchMethodError'}
            if mode == 'warm':
                expected['warm'] = 'OK'
            if any(observations.get(key) != value for key, value in expected.items()):
                raise RuntimeError(name + ' differs from pinned constructor-cache semantics')
            manifest['constructorProbes'].append({'after': after, 'mode': mode, 'observations': observations})
    manifest['cellsChecked'] = checks
    (evidence / 'summary.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('JBR class-kind oracle PASS cells=' + str(checks))
    print('Evidence: ' + str(evidence.relative_to(ROOT)))


if __name__ == '__main__':
    main()
