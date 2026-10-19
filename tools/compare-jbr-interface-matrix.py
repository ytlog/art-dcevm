#!/usr/bin/env python3
"""Pin class-chain interface selection and isolated JNI diagnostics on fixed JBR."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import subprocess

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
REVISION = 'cf1f197170a0f04c6559feb873e362583be021d1'
SOURCE = r'''
import java.lang.instrument.*;
import java.lang.invoke.*;
import java.lang.reflect.*;
import java.net.*;
import java.nio.file.*;
import java.util.*;
public class InterfaceMatrixOracle {
  static Instrumentation inst;
  static Path patches;
  static int checks;
  static final boolean JNI = Boolean.getBoolean("matrixJni");
  static final boolean JNI_ADDITION = Boolean.getBoolean("matrixJniAddition");
  static final String P = "com.github.ytlog.artdcevm.fixture.selection.";
  static final String M = P + "Matrix$";
  static final String[] BASES = {"PrivateBase", "PackageBase", "ProtectedBase"};
  static final String[] NAMES = {"Plain", "OwnPrivate", "PrivateChild", "PrivateGrandchild",
    "OwnPackage", "PackageChild", "OwnProtected", "ProtectedChild", "OtherPackageChild",
    "PrivatePublic", "PackagePublic", "ProtectedPublic", "OtherPackagePublic",
    "OwnPrivateLate", "PrivateLate", "PackageLate", "ProtectedLate", "OtherPackageLate", "PublicLate", "RedeclaredPrivate", "OwnOnInherited", "OwnOnPlain"};
  static final int[] OLD = {100,101,102,102,111,112,121,122,131,102,142,143,131,103,102,112,122,131,145,104,105,106};
  static final int[] DECLARED = {0,101,102,102,111,112,121,122,131,141,142,143,144,103,102,112,122,131,145,104,105,106};
  public static void premain(String args, Instrumentation value) { inst = value; }
  static native void cache(int slot, Class<?> type);
  static native int invoke(Object receiver, int slot);
    interface Op { Object run() throws Throwable; }
  static void eq(String name, Object expected, Object actual) {
    ++checks;
    if (!expected.equals(actual)) throw new AssertionError(name + ": expected " + expected + ", got " + actual);
  }
  static void result(String name, Object expected, Op op) throws Throwable {
    Object actual;
    try { actual = op.run(); }
    catch (Throwable error) {
      if (error instanceof InvocationTargetException) error = error.getCause();
      actual = error.getClass().getName();
    }
    eq(name, expected, actual);
  }
  static Object expected(int row, boolean abstractKind, boolean parent) {
    if (row >= 9 && row <= 12) return 141 + row - 9;
    if (row == 18) return 145;
    if (row >= 4 && row <= 8 || row >= 15 && row <= 17) return "java.lang.IllegalAccessError";
    if (abstractKind || row == 3 || row == 14 || row >= 19) return "java.lang.AbstractMethodError";
    return parent ? 17 : 7;
  }
  static class ChildLoader extends URLClassLoader {
    ChildLoader(URL[] urls, ClassLoader parent) { super(urls, parent); }
    protected synchronized Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException {
      boolean base = name.equals(P + "other.PackageBase");
      for (String n : BASES) base |= name.equals(M + n);
      if (!name.startsWith(P) || base) return super.loadClass(name, resolve);
      Class<?> type = findLoadedClass(name);
      if (type == null) type = findClass(name);
      if (resolve) resolveClass(type);
      return type;
    }
  }
  static Method declaration(Class<?> type) {
    for (; type != null; type = type.getSuperclass()) {
      try { Method method = type.getDeclaredMethod("select"); method.setAccessible(true); return method; }
      catch (NoSuchMethodException absent) {}
    }
    return null;
  }
  static void change(String variant, Class<?> type) throws Exception {
    inst.redefineClasses(new ClassDefinition(type, Files.readAllBytes(patches.resolve("interface-selection-" + variant)
      .resolve(type.getName().replace('.', '/') + ".class"))));
    ++checks;
  }
  static void group(boolean split, String lane) throws Throwable {
    URL[] urls = {patches.resolve(JNI && !JNI_ADDITION ? "interface-selection-default" : "interface-selection-original").toUri().toURL(),
      patches.resolve("interface-selection-original").toUri().toURL(),
      patches.resolve("interface-selection-calls").toUri().toURL()};
    try (URLClassLoader parentLoader = new URLClassLoader(urls, ClassLoader.getPlatformClassLoader());
         URLClassLoader loader = split ? new ChildLoader(urls, parentLoader)
           : new URLClassLoader(urls, ClassLoader.getPlatformClassLoader())) {
      Class<?> contract = loader.loadClass(M + "Contract"), parent = loader.loadClass(M + "Parent");
      Object isolated = parentLoader.loadClass(M + "Plain").getConstructor().newInstance();
      Method isolatedStable = parentLoader.loadClass(M + "Contract").getMethod("stable");
      Object[] values = new Object[NAMES.length];
      Method[] old = new Method[NAMES.length], declared = new Method[NAMES.length];
      MethodHandle[] oldHandles = new MethodHandle[NAMES.length], declaredHandles = new MethodHandle[NAMES.length];
      int[] modifiers = new int[NAMES.length], identity = new int[NAMES.length];
      Method call = loader.loadClass(P + "Calls").getMethod("call", contract);
      Method hot = loader.loadClass(P + "Calls").getMethod("hot", contract, int.class);
      Method classCall = loader.loadClass(P + "Calls").getMethod("classCall", Object.class, int.class);
      Method stable = loader.loadClass(P + "Calls").getMethod("stable", contract);
      for (int round = 0; round < (JNI ? 1 : 2); ++round) {
        for (int phase = 0; phase < (JNI ? 1 : 4); ++phase) {
          String label = (split ? "split" : "local") + "/" + lane + "/" + round + "/" + phase;
          boolean restore = phase == 3, parentLane = lane.equals("parent");
          boolean abstractKind = parentLane ? phase == 1 : lane.equals("abstract") ? phase != 1 : phase == 1;
          String variant = restore ? "original" : parentLane ? phase == 1 ? "parent-abstract" : "parent"
            : abstractKind ? "abstract" : "default";
          // Old callers and the initial receiver shapes precede the first update.
          for (int row = 0; row < NAMES.length; ++row) {
            if (round == 0 && phase == 0 && row >= 13 && row < 19) continue;
            if (values[row] != null) continue;
            Class<?> type = loader.loadClass(M + NAMES[row]);
            values[row] = type.getConstructor().newInstance();
            old[row] = type.getMethod("oldCall"); oldHandles[row] = MethodHandles.publicLookup().unreflect(old[row]);
            declared[row] = declaration(type); identity[row] = System.identityHashCode(values[row]);
            if (declared[row] != null) {
              modifiers[row] = declared[row].getModifiers();
              declaredHandles[row] = MethodHandles.lookup().unreflect(declared[row]);
            }
          }
          eq(label + "/old caller loaded", 19, loader.loadClass(P + "Calls").getMethod("stable", contract).invoke(null, values[0]));
          if (JNI && !JNI_ADDITION) {
            cache(0, contract); eq("JNI baseline", 7, invoke(values[0], 0));
            System.out.println("JNI baseline PASS value=7");
          }
          change(variant, parentLane && phase != 0 && !restore ? parent : contract);
          if (phase == 0) {
            // Load genuinely new declarations and children only after evolution.
            for (int row = 13; row < NAMES.length; ++row) if (values[row] == null) {
              Class<?> type = loader.loadClass(M + NAMES[row]); values[row] = type.getConstructor().newInstance();
              old[row] = type.getMethod("oldCall"); oldHandles[row] = MethodHandles.publicLookup().unreflect(old[row]);
              declared[row] = declaration(type); identity[row] = System.identityHashCode(values[row]);
              modifiers[row] = declared[row].getModifiers(); declaredHandles[row] = MethodHandles.lookup().unreflect(declared[row]);
            }
          }
          Method freshCall = loader.loadClass(P + "NewCalls").getMethod("call", contract);
          if (phase == 0) { cached = contract.getMethod("select"); cachedHandle = MethodHandles.publicLookup().unreflect(cached); if (JNI) cache(0, cached.getDeclaringClass()); }
          Method current = restore ? null : contract.getMethod("select");
          for (int row = 0; row < NAMES.length; ++row) {
            final Object value = values[row]; final int index = row;
            Object selected = restore ? "java.lang.NoSuchMethodError" : expected(row, abstractKind, parentLane);
            {
              result(label + "/" + NAMES[row] + "/old site", selected, () -> call.invoke(null, value));
              result(label + "/" + NAMES[row] + "/new site", selected, () -> freshCall.invoke(null, value));
            }
            if (!restore) {
              result(label + "/reflection", selected, () -> current.invoke(value));
              result(label + "/cached reflection", selected, () -> cached.invoke(value));
              result(label + "/cached handle", selected, () -> cachedHandle.invokeWithArguments(value));
              if (JNI) result(label + "/cached JNI", selected, () -> invoke(value, 0));
              Object fresh = value.getClass().getConstructor().newInstance();
              result(label + "/new receiver", selected, () -> call.invoke(null, fresh));
            }
            int classResult = split && row == 10 ? 112 : OLD[row];
            oldHandles[row] = MethodHandles.publicLookup().findVirtual(value.getClass(), "oldCall", MethodType.methodType(int.class));
            if (declared[row] != null) {
              Method freshDeclaration = declared[row].getDeclaringClass().getDeclaredMethod("select");
              declaredHandles[row] = MethodHandles.privateLookupIn(freshDeclaration.getDeclaringClass(), MethodHandles.lookup())
                .findVirtual(freshDeclaration.getDeclaringClass(), "select", MethodType.methodType(int.class));
            }
            result(label + "/class bytecode", classResult, () -> classCall.invoke(null, value, index));
            result(label + "/class handle", classResult, () -> oldHandles[index].invokeWithArguments(value));
            if (declared[row] != null) {
              eq(label + "/access", modifiers[row], declared[row].getDeclaringClass().getDeclaredMethod("select").getModifiers());
              // Cached class reflection slots can alias after indirect JBR redefinition.
              // Use symbolic handles to pin the unchanged class declaration instead.
              result(label + "/declared handle", DECLARED[row], () -> declaredHandles[index].invokeWithArguments(value));
            }
            eq(label + "/identity", identity[row], System.identityHashCode(value));
            eq(label + "/stable", 19, stable.invoke(null, value));
          }
          if (!restore && phase == 2) for (int row : new int[]{2,3,5,11}) {
            Object expected = expected(row, abstractKind, parentLane);
            int perCall = expected instanceof Integer ? (Integer) expected
              : expected.equals("java.lang.IllegalAccessError") ? -1 : -2;
            eq(label + "/hot/" + NAMES[row], 30000 * perCall, hot.invoke(null, values[row], 30000));
          }
          System.gc();
          eq(label + "/isolated loader", 19, isolatedStable.invoke(isolated));
          eq(label + "/different contract", false, contract.isInstance(isolated));
          result(label + "/after GC", restore ? "java.lang.NoSuchMethodError" : expected(2, abstractKind, parentLane),
            () -> call.invoke(null, values[2]));
          System.out.println("matrix " + label + " PASS");
        }
      }
    }
  }
  static Method cached;
  static MethodHandle cachedHandle;
  public static void main(String[] args) throws Throwable {
    patches = Paths.get(args[0]); System.load(Paths.get(args[1]).toAbsolutePath().toString());
    if (JNI) group(false, "abstract");
    else for (boolean split : new boolean[]{false,true}) for (String lane : new String[]{"abstract","default","parent"}) group(split,lane);
    System.out.println("JBR interface matrix PASS checks=" + checks);
  }
}
'''
NATIVE = r'''
#include <jni.h>
static jmethodID methods[2];
JNIEXPORT void JNICALL Java_InterfaceMatrixOracle_cache(JNIEnv* e, jclass c, jint slot, jclass type) {
  (void)c;
  if (slot >= 0 && slot < 2) methods[slot] = (*e)->GetMethodID(e, type, "select", "()I");
}
JNIEXPORT jint JNICALL Java_InterfaceMatrixOracle_invoke(JNIEnv* e, jclass c, jobject receiver, jint slot) {
  (void)c;
  return (*e)->CallIntMethod(e, receiver, methods[slot]);
}
'''


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', type=Path, required=True)
    parser.add_argument('--probe-jni-failures', action='store_true',
                        help='Run isolated added-abstract and default-to-abstract JNI probes; record reference VM failures separately')
    args = parser.parse_args()
    jbr = args.java_home.resolve()
    evidence = new_evidence_directory(ROOT / 'build/evidence/jbr-interface-matrix')
    manifest = {'sourceOracleRevision': REVISION, 'toolSha256': sha(Path(__file__)),
                'harnessSha256': hashlib.sha256(SOURCE.encode()).hexdigest(), 'fixtureSha256': {}, 'runs': []}

    def run(name, command, expected_fatal=False):
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, cwd=evidence)
        except subprocess.TimeoutExpired as error:
            raw = lambda value: value.decode(errors='replace') if isinstance(value, bytes) else value or ''
            (evidence / (name + '.log')).write_text(raw(error.stdout) + raw(error.stderr))
            raise RuntimeError(name + ' timed out; inspect ignored evidence') from error
        output = result.stdout + result.stderr
        (evidence / (name + '.log')).write_text(output)
        if expected_fatal:
            if (result.returncode == 0 or 'Internal Error (javaCalls.cpp:' not in output
                    or 'wrong no. of arguments pushed' not in output):
                raise RuntimeError(name + ' did not reproduce the pinned reference failure; inspect ignored evidence')
        elif result.returncode:
            raise RuntimeError(name + ' failed; inspect ignored evidence')
        return output

    version = run('version', [str(jbr / 'bin/java'), '-version'])
    if 'JBR-21.0.11+10-1163.116' not in version:
        raise RuntimeError('Requires fixed JBR 21.0.11+10-b1163.116')
    manifest['runtimeVersion'] = version.strip()
    manifest['javaExecutableSha256'] = sha(jbr / 'bin/java')
    library = jbr / 'lib/server' / ('libjvm.dylib' if platform.system() == 'Darwin' else 'libjvm.so')
    manifest['jvmLibrarySha256'] = sha(library)
    patches = evidence / 'patches'
    for variant in ('original', 'abstract', 'default', 'parent', 'parent-abstract', 'calls'):
        sources = sorted((ROOT / ('fixtures/interface-selection-' + variant)).rglob('*.java'))
        for source in sources:
            manifest['fixtureSha256'][source.relative_to(ROOT).as_posix()] = sha(source)
        run('compile-' + variant, [str(jbr / 'bin/javac'), '--release', '9', '-d',
            str(patches / ('interface-selection-' + variant)), *map(str, sources)])
    source = evidence / 'InterfaceMatrixOracle.java'; source.write_text(SOURCE)
    classes = evidence / 'harness'
    run('compile-oracle', [str(jbr / 'bin/javac'), '--release', '11', '-d', str(classes), str(source)])
    native = evidence / 'native.c'; native.write_text(NATIVE)
    binary = evidence / ('libmatrix.dylib' if platform.system() == 'Darwin' else 'libmatrix.so')
    run('native', ['cc', '-shared', '-fPIC', '-I' + str(jbr / 'include'),
        '-I' + str(jbr / 'include' / ('darwin' if platform.system() == 'Darwin' else 'linux')),
        str(native), '-o', str(binary)])
    mf = evidence / 'manifest.mf'; mf.write_text('Premain-Class: InterfaceMatrixOracle\nCan-Redefine-Classes: true\n\n')
    agent = evidence / 'agent.jar'
    run('agent', [str(jbr / 'bin/jar'), 'cfm', str(agent), str(mf), '-C', str(classes), 'InterfaceMatrixOracle.class'])
    jit = ['-Xbatch', '-XX:+PrintCompilation',
           '-XX:CompileCommand=compileonly,com.github.ytlog.artdcevm.fixture.selection.Calls::hot']
    for mode, flags in [('interpreter', ['-Xint']), ('jit', jit)]:
        output = run(mode, [str(jbr / 'bin/java'), *flags, '-Xcheck:jni', '-XX:+AllowEnhancedClassRedefinition', '-XX:ErrorFile=' + str(evidence / 'hs_err_pid%p.log'),
            '-javaagent:' + str(agent), '-cp', str(classes), 'InterfaceMatrixOracle', str(patches), str(binary)])
        match = re.search(r'JBR interface matrix PASS checks=(\d+)', output)
        states = [f'matrix {loader}/{lane}/{round_}/{phase} PASS' for loader in ('local', 'split')
                  for lane in ('abstract', 'default', 'parent') for round_ in range(2) for phase in range(4)]
        observed = re.findall(r'^matrix (?:local|split)/(?:abstract|default|parent)/[01]/[0-3] PASS$', output, re.M)
        if not match or observed != states:
            raise RuntimeError(mode + ' is missing complete matrix evidence')
        if mode.endswith('jit') and not re.search(r'selection\.Calls::hot', output):
            raise RuntimeError('JIT run is missing hot caller compilation')
        manifest['runs'].append({'mode': mode, 'checks': int(match[1]), 'states': len(states), 'passed': True})
    manifest['jitScope'] = 'Only fixture Calls.hot is compiled; the harness stays interpreted.'
    manifest['jniProbes'] = []
    if args.probe_jni_failures:
        for addition in (False, True):
            label = 'jni-added-abstract' if addition else 'jni-default-to-abstract'
            output = run(label, [str(jbr / 'bin/java'), '-Xint', '-Xcheck:jni', '-DmatrixJni=true',
                '-DmatrixJniAddition=' + str(addition).lower(), '-XX:+AllowEnhancedClassRedefinition',
                '-XX:ErrorFile=' + str(evidence / 'hs_err_pid%p.log'), '-javaagent:' + str(agent),
                '-cp', str(classes), 'InterfaceMatrixOracle', str(patches), str(binary)], expected_fatal=True)
            if not addition and 'JNI baseline PASS value=7' not in output:
                raise RuntimeError('JNI baseline failed before redefinition; inspect ignored evidence')
            manifest['jniProbes'].append({'probe': label, 'outcome': 'reference-vm-fatal',
                'diagnostic': 'javaCalls.cpp: wrong no. of arguments pushed'})
    manifest['jniScope'] = 'No passing JBR JNI parity claim after interface structural redefinition; isolated probes record reference VM failures.'
    manifest['classReflectionScope'] = 'Indirect JBR rebuilds can alias cached reflection slots; class-call oracles use original bytecode and symbolic MethodHandles.'
    manifest['nativeSha256'] = hashlib.sha256(NATIVE.encode()).hexdigest()
    manifest['nativeBinarySha256'] = sha(binary)
    manifest['passed'] = True
    (evidence / 'summary.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('JBR interface matrix PASS checks=' + str(sum(run['checks'] for run in manifest['runs'])))
    print('Evidence: ' + str(evidence.relative_to(ROOT)))


if __name__ == '__main__':
    main()
