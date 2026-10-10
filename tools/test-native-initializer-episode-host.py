#!/usr/bin/env python3
"""Actual producer/hook TUs with calibrated roots and independent blocking source threads."""
import ast,os,subprocess
from host_tu_cache import compile_cached
from pathlib import Path
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
out=root/'build/evidence/host/native-initializer-episode';out.mkdir(parents=True,exist_ok=True)
fixture=(root/'tests/host/native_source_commit_test.cc').read_text().split('int main(){')[0]
fixture=fixture.replace('std::array<unsigned char,200>', 'std::array<unsigned char,512>')
fixture=fixture.replace('void* current=thread.data();','thread_local void* current=thread.data();')
fixture=fixture.replace('mutations=0,jni_calls=0;', 'mutations=0;std::atomic<int> jni_calls{0};')
fixture=fixture.replace('current==thread.data()', 'current!=nullptr').replace('Put32(thread.data(),0,', 'Put32(current,0,')
fixture=fixture.replace('&runtime','EpisodeVM()')
fixture=fixture.replace('return {uintptr_t(p)};', 'return EpisodeGlobal(p);')
(out/'native_commit_fixture.inc').write_text(fixture)
source=(root/'tests/host/source_handle_roots_test.cc').read_text()
source=source.replace('thread.data()+original_top','static_cast<unsigned char*>(current)+original_top').replace('self==thread.data()','self==current')
source=source.replace('&runtime','EpisodeVM()').replace('=jni_calls;', '=jni_calls.load();')
marker=' const auto native_queries=method_identity::MappingOsQueries();'
assert source.count(marker)==1
source=source.replace(marker, ' RunEpisodes(metadata,service);\n'+marker)
(out/'episode_root_fixture.inc').write_text(source)
tree=ast.parse((root/'tools/test-source-handle-roots-host.py').read_text())
sources=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='sources' for t in n.targets));sources+=['native_initializer_episode']
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip());platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
binary=out/'native_initializer_episode_test'
base=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-I',str(cpp),'-I',str(out),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform)]
units=[root/'tests/host/native_initializer_episode_test.cc']+[cpp/'platform/common'/(s+'.cc') for s in sources]+[cpp/'evolution/dex_schema.cc']
compiled=compile_cached(base,units,out/'objects',max_workers=2)
cmd=base+compiled
if platform=='darwin':cmd+=['-Wl,-pagezero_size,0x10000']
cmd+=['-o',str(binary)];subprocess.run(cmd,check=True)
for layout in ('legacy','sized'):
 env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
 if layout=='sized':env['SOURCE_SCOPE_SIZED']='1'
 else:env.pop('SOURCE_SCOPE_SIZED',None)
 result=subprocess.run([str(binary)],capture_output=True,text=True,env=env)
 (out/('result-'+layout+'.log')).write_text(result.stdout+result.stderr);result.check_returncode();print(result.stdout.strip())
