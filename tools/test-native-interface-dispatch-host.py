#!/usr/bin/env python3
"""Real public boot catalog + source Pool + native IfTable interface-edge proof."""
from pathlib import Path
import os,subprocess,hashlib
root=Path(__file__).resolve().parents[1]
base=root/'tools/test-source-cache-public-roles-host.py'
namespace={'__file__':str(base)}
# Reuse original JNI/root/provider fixture generation and the independent DEX.
exec(compile(base.read_text().split('sources=',1)[0],str(base),'exec'),namespace)
shared=namespace['out'];out=root/'build/evidence/host/native-interface-dispatch';out.mkdir(parents=True,exist_ok=True)
for name in ('native_commit_fixture.inc','provider_fixture.inc','roots_fixture.inc','roles.dex'):(out/name).write_bytes((shared/name).read_bytes())
prefix=(root/'tests/host/source_cache_public_roles_test.cc').read_text().split('int main(',1)[0]
(out/'public_roles_fixture.inc').write_text(prefix)
body=(root/'tests/host/source_cache_public_roles_test.cc').read_text().split('int main(',1)[1].split(' auto dex=File',1)[0].split(' assert(argc==2);',1)[1]
(out/'source_setup.inc').write_text(body)
exec(compile((root/'tests/host/interface_dispatch_dex_fixture.py').read_text(),'interface_dispatch_dex_fixture.py','exec'),namespace)
namespace['write'](out)
cpp=namespace['cpp'];jdk=namespace['jdk'];platform=namespace['platform']
sources=['native_interface_dispatch','native_boot_method_catalog','native_receiver_bytecode','source_handle_roots','source_allocation_transaction','native_source_commit','native_initialized_transfer','modern_source_commit','source_commit','modern_field_metadata','native_interface_graph','modern_field_storage','field_copy','prepared_field_copy','exclusive_owned_roots','native_cache_service','modern_method_services','structural_methods','method_identity_calibration','object_replacement_map']
common=[os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(out),'-I',str(root/'tests/host'),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform)]
units=[root/'tests/host/native_interface_dispatch_test.cc']+[cpp/'platform/common'/(s+'.cc') for s in sources]+[cpp/'evolution/dex_schema.cc']
headers=b''.join(p.read_bytes() for p in sorted(cpp.rglob('*.h')))+b''.join(p.read_bytes() for p in sorted(out.glob('*.inc')))
objects=out/'objects';objects.mkdir(exist_ok=True)
def compile_unit(source):
 obj=objects/(source.stem+'.o');stamp=obj.with_suffix('.sha256');key=hashlib.sha256(source.read_bytes()+headers+' '.join(common).encode()).hexdigest()
 if not obj.exists() or not stamp.exists() or stamp.read_text()!=key:
  r=subprocess.run(common+['-c',str(source),'-o',str(obj)],capture_output=True,text=True);obj.with_suffix('.log').write_text(r.stdout+r.stderr);r.check_returncode();stamp.write_text(key)
 return str(obj)
from concurrent.futures import ThreadPoolExecutor
with ThreadPoolExecutor(max_workers=4) as workers:compiled=list(workers.map(compile_unit,units))
link=common+compiled+(['-Wl,-pagezero_size,0x10000','-Wl,-dead_strip'] if platform=='darwin' else ['-Wl,--gc-sections'])
r=subprocess.run(link+['-o',str(out/'test')],capture_output=True,text=True);(out/'link.log').write_text(r.stdout+r.stderr);r.check_returncode()
for layout in ('legacy','sized'):
 for method in ('11','12'):
  env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1');env.pop('SOURCE_SCOPE_SIZED',None);env.pop('DISPATCH_METHOD11',None)
  if layout=='sized':env['SOURCE_SCOPE_SIZED']='1'
  if method=='11':env['DISPATCH_METHOD11']='1'
  r=subprocess.run([str(out/'test'),str(out/'caller.dex'),str(out/'interface.dex'),str(out/'receiver.dex')],env=env,capture_output=True,text=True);(out/('result-'+layout+'-method'+method+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();print(layout+'/method'+method+': '+r.stdout.strip())
