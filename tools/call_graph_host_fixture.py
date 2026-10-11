from pathlib import Path
import os

def prepare(r,out):
 cpp=r/'artdcevm/src/main/cpp';out.mkdir(parents=True,exist_ok=True)
 # Reuse only the independent field/method/JNI leaf protocol, not the old walker.
 pre=(r/'tools/test-source-cache-public-roles-host.py').read_text().split('# Independent complete StandardDEX',1)[0]
 ns={'__file__':str(r/'tools/test-source-cache-public-roles-host.py')};exec(compile(pre,'provider-prelude','exec'),ns)
 for name in ['native_commit_fixture.inc','provider_fixture.inc','roots_fixture.inc']:
  s=(ns['out']/name).read_text()
  if name=='native_commit_fixture.inc':s=s.replace('int thread_list,mutex,linker,runtime,instrumentation,jit,jit_cache;','int thread_list,mutex,linker,instrumentation,jit,jit_cache;JavaVM runtime{};')
  (out/name).write_text(s)
 # Preserve the production observer and public JVMTI protocol; its core receiver
 # leaf remains explicitly a host model. Never stub CalibrateOwner/ValidateRetained.
 s=(r/'tests/host/runtime_service_calibration_test.cc').read_text();body=s.split('namespace {',1)[1].split('namespace artdcevm::runnable_executor',1)[0]
 body=body.replace('JavaVM vm{};','JavaVM& vm=::runtime;').replace('mode!="attach-failure"','mode!="attach-failure"')
 (out/'early_owner_fixture.inc').write_text('namespace early_owner_fixture {\n'+body+'\nnamespace artdcevm::runtime_receiver {\nbool Calibrate(HookEngine&,JNIEnv*,Receivers*out,std::string*){*out={early_owner_fixture::class_linker,early_owner_fixture::jni_manager,&::runtime,early_owner_fixture::thread_list};return true;}\n}\n')
 java=ns['jdk'];platform=ns['platform']
 return java,platform
