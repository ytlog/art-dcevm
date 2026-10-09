#!/usr/bin/env python3
"""Actual JNI observation TU host tests and Java/target wiring, no APK/device."""
import os,pathlib,subprocess,xml.etree.ElementTree as ET
root=pathlib.Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
out=root/'build/evidence/host/nondebuggable-frameproof';out.mkdir(parents=True,exist_ok=True)
java=pathlib.Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
common=[os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra','-Werror','-fno-rtti','-DARTDCEVM_TESTING=1','-I',str(cpp),'-I',str(java/'include'),'-I',str(java/'include'/platform),'-I',str(root/'third_party/jvmti')]
link=['-Wl,-dead_strip'] if platform=='darwin' else ['-Wl,--gc-sections']
exe=out/'observation-test'
result=subprocess.run(common+['-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections',str(root/'tests/host/frameproof_observation_test.cc'),*link,'-o',str(exe)],text=True,capture_output=True);(out/'native-compile.log').write_text(result.stdout+result.stderr);result.check_returncode()
result=subprocess.run([str(exe)],text=True,capture_output=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'native-observation.log').write_text(result.stdout+result.stderr);result.check_returncode();print(result.stdout.strip())
# Actual Android public APIs and actual selected oracle sources, not alternate Java replicas.
sdk=pathlib.Path(os.environ.get('ANDROID_HOME',str(pathlib.Path.home()/'Library/Android/sdk')))
android=next((path for path in (sdk/'platforms/android-37/android.jar',sdk/'platforms/android-37.0/android.jar') if path.is_file()),None)
if android is None:raise RuntimeError('Missing compileSdk37 android.jar')
src=root/'app/src/debug/java/com/github/ytlog/artdcevm/testing'
sources=[src/name for name in ['MethodDexOracle.java','ActiveFrameDexOracle.java','OwnedActiveFrameFixture.java','OwnedActiveFrameCalibration.java']]
sources.extend([root/'frameproof/src/main/java/com/github/ytlog/artdcevm/testing/FrameProofBootstrap.java',root/'frameproof/src/androidTest/java/com/github/ytlog/artdcevm/frameproof/FrameProofInstrumentation.java',root/'artdcevm/src/main/java/com/github/ytlog/artdcevm/internal/CalibrationFixture.java'])
classes=out/'classes';classes.mkdir(exist_ok=True)
result=subprocess.run([str(java/'bin/javac'),'-classpath',str(android),'-d',str(classes),*[str(s) for s in sources]],text=True,capture_output=True);(out/'java-compile.log').write_text(result.stdout+result.stderr);result.check_returncode()
ns='{http://schemas.android.com/apk/res/android}'
for name in ('main','androidTest'):
 app=ET.parse(root/f'frameproof/src/{name}/AndroidManifest.xml').getroot().find('application');assert app.get(ns+'debuggable') in (None,'false')
source=(root/'frameproof/src/main/cpp/frame_bootstrap.cc').read_text()
for forbidden in ('jvmti.h','JVMTI_VERSION','ForceGarbageCollection','AttachAgent','SetJavaDebuggable','SetCompilerOptions'):assert forbidden not in source
assert 'public static synchronized JSONObject run(' in (root/'frameproof/src/main/java/com/github/ytlog/artdcevm/testing/FrameProofBootstrap.java').read_text()
build=(root/'frameproof/build.gradle').read_text();assert "testBuildType 'probe'" in build and 'signingConfig signingConfigs.debug' in build and 'debuggable true' not in build and "implementation project(':artdcevm')" not in build
cmake=(root/'frameproof/src/main/cpp/CMakeLists.txt').read_text();assert 'add_subdirectory' in cmake and 'frame_bootstrap.cc' in cmake
print('Actual Java public-API/fixture/oracle/instrumentation compile and isolated same-signature nondebuggable configuration passed; no APK/device or actual frame pass claimed')

result=subprocess.run([str(root/'gradlew'),':frameproof:verifyFrameProofPolicy',':frameproof:processProbeMainManifest',':frameproof:processProbeAndroidTestManifest'],cwd=root,text=True,capture_output=True)
(out/'gradle-policy-manifest.log').write_text(result.stdout+result.stderr);result.check_returncode()
main=ET.parse(root/'frameproof/build/intermediates/merged_manifest/probe/processProbeMainManifest/AndroidManifest.xml').getroot()
test=ET.parse(root/'frameproof/build/intermediates/packaged_manifests/probeAndroidTest/processProbeAndroidTestManifest/AndroidManifest.xml').getroot()
assert main.get('package')=='dev.artdcevm.frameproof' and test.get('package')=='dev.artdcevm.frameproof.test'
assert main.find('application').get(ns+'debuggable','false')=='false' and test.find('application').get(ns+'debuggable','false')=='false'
runner=test.find('instrumentation')
assert runner.get(ns+'name')=='com.github.ytlog.artdcevm.frameproof.FrameProofInstrumentation' and runner.get(ns+'targetPackage')=='dev.artdcevm.frameproof'
print('Actual AGP public variant policy and merged target/test manifests: nondebuggable target, nondebuggable instrumentation and exact test target verified without APK/native build')
