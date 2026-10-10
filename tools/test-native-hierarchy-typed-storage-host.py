#!/usr/bin/env python3
"""Exercise calibrated native commit adapter and dependencies as one transaction."""
import argparse
import os
import hashlib
import struct
import zlib
from pathlib import Path
import subprocess
from concurrent.futures import ThreadPoolExecutor
parser=argparse.ArgumentParser();parser.add_argument('--syntax-only',action='store_true');parser.add_argument('--compile-only',action='store_true');parser.add_argument('--method-policy-root',type=Path);args=parser.parse_args()
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
jdk = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/native-hierarchy-typed-storage'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'native_hierarchy_typed_storage_test'
fixture = (root / 'tests/host/native_source_commit_test.cc').read_text()
assert fixture.count('int main(){') == 1
(out / 'native_commit_fixture.inc').write_text(fixture.split('int main(){')[0].replace('std::array<unsigned char,200>','std::array<unsigned char,512>'))
provider=(root/'tests/host/native_member_provider_test.cc').read_text()
prefix,body=provider.split('int main(int argc,char** argv){',1)
(out/'provider_fixture.inc').write_text(prefix)
(out/'provider_setup.inc').write_text(body.split(' Stop(nullptr,nullptr,false);',1)[0].replace('ProviderHooks hooks;','TypedHooks hooks;'))
roots=(root/'tests/host/source_handle_roots_test.cc').read_text().split('int main(){')[0]
roots=roots.replace('#include "native_commit_fixture.inc"','').replace('class RootHooks final:public Hooks','class RootHooks:public ProviderHooks').replace('return Hooks::FindSymbol(lib,name);','return ProviderHooks::FindSymbol(lib,name);')
(out/'roots_fixture.inc').write_text(roots)
cleanup=(root/'tests/host/native_required_cleanup_test.cc').read_text()
prefix,main=cleanup.split('int main(int argc,char** argv){',1)
prefix=prefix.replace('#include "provider_fixture.inc"','#include "provider_fixture.inc"\n#include "roots_fixture.inc"').replace('class OwnerHooks:public ProviderHooks','class OwnerHooks:public RootHooks').replace('return ProviderHooks::FindSymbol(lib,name);','return RootHooks::FindSymbol(lib,name);')
(out/'cleanup_fixture.inc').write_text(prefix)
main=main.replace(' dependencies.is_retired=OwnerRetired;', ''' source_handle_roots::Service source_roots;native_typed_heap_walk::Service walk;
 auto typed=PrepareTyped(hooks,metadata,fields,source_roots,walk,error);assert(typed&&typed->Staged());TestNarrowingOrdinary(*typed);MoveTypedRootsAfterStage();
 dependencies.is_retired=OwnerRetired;''')
main=main.replace('assert(registry.Freeze()&&registry.Resolve());','typed_wrong_plan=plan;assert(typed->Capture(plan,registry.Collect(),&error));assert(registry.Freeze()&&registry.Resolve());typed->Refresh(registry.Final());')


anchor=' assert(!cleanup.CommitObserver(foreign_plan));'
assert main.count(anchor)==1
main=main.replace(anchor,' TestFinalTyped(*typed,plan,cleanup,&driver,&holder,*concrete);\n'+anchor)
main=main.replace(' provider.ReleasePreparedSource(*concrete);',' typed->Clear(&env);Publish(nullptr);provider.ReleasePreparedSource(*concrete);')
# Clear JNI roots only after the original final source window has ended.
main=main.replace(' typed->Clear(&env);Publish(nullptr);provider.ReleasePreparedSource(*concrete);',' provider.ReleasePreparedSource(*concrete);')
main=main.replace('assert(session.Completed());registry.Clear();','assert(session.Completed());registry.Clear();typed->Clear(&env);Publish(nullptr);')
(out/'cleanup_main.inc').write_text(main)

def dex_fixture(method_deleted, field_deleted, evolved=False, final_guard=False, hierarchy=False):
    # Independent StandardDex oracle: complete IDs, explicit class_data flags,
    # separate code items and checksums. No values come from native candidates.
    strings = sorted((['LAddedParent;'] if hierarchy else [])+(['J'] if evolved else [])+['LFixture;', 'Ljava/lang/Object;', 'I', 'gone', 'keep', 'staticKeep',
                      'number', 'value', 'left', 'right'])
    ids = {value: i for i, value in enumerate(strings)}
    types = sorted((['LAddedParent;'] if hierarchy else [])+(['I','J','LFixture;','Ljava/lang/Object;'] if evolved else ['I', 'LFixture;', 'Ljava/lang/Object;']))
    tids = {value: i for i, value in enumerate(types)}
    data = bytearray(0x70)
    def put(at, value): struct.pack_into('<I', data, at, value)
    tables = {}
    for at, count, width in [(56,len(strings),4),(64,len(types),4),(72,1,12),(80,4,8),(88,3,8),(96,1,32)]:
        tables[at]=len(data); put(at,count);put(at+4,len(data));data.extend(bytes(count*width))
    start=len(data)
    def leb(value):
        output=bytearray()
        while value>=128: output.append((value&127)|128);value>>=7
        return output+bytes([value])
    for i,value in enumerate(strings):
        put(tables[56]+4*i,len(data));data.extend(leb(len(value))+value.encode()+b'\0')
    for i,value in enumerate(types):put(tables[64]+4*i,ids[value])
    struct.pack_into('<III',data,tables[72],ids['I'],0,0)
    field_names=['left','number','right','value']
    for i,name in enumerate(field_names):struct.pack_into('<HHI',data,tables[80]+8*i,tids['LFixture;'],tids['J'] if evolved and name=='number' else tids['I'] if name in ('number','value') else tids['Ljava/lang/Object;'],ids[name])
    for i,name in enumerate(['gone','keep','staticKeep']):struct.pack_into('<HHI',data,tables[88]+8*i,tids['LFixture;'],0,ids[name])
    data.extend(bytes((-len(data))%4));code=len(data)
    data.extend(struct.pack('<HHHHIIHH',1,1,0,0,0,2,0x0012,0x000f))
    code_static=len(data);data.extend(struct.pack('<HHHHIIHH',1,0,0,0,0,2,0x0012,0x000f))
    class_data=len(data);data.extend(bytes([2 if evolved else 1,2 if evolved or field_deleted else 3,1,1 if method_deleted else 2]))
    data.extend(bytes([1,8]));
    if evolved:data.extend(bytes([2,9]))
    previous=0
    for i in ([0,2] if evolved else [0,3] if field_deleted else [0,2,3]):
        data.extend(leb(i-previous)+bytes([65 if evolved and i==0 else 17 if final_guard and i==2 else 1]));previous=i
    data.extend(bytes([2,9])+leb(code_static))
    if not method_deleted:data.extend(bytes([0])+leb(0x101)+bytes([0]))
    data.extend(bytes([1,1])+leb(code))
    encoded=len(data) if evolved else 0
    if evolved:data.extend(bytes([2,0x26,0x3a,1,4,77]))
    struct.pack_into('<IIIIIIII',data,tables[96],tids['LFixture;'],1,tids['LAddedParent;' if hierarchy else 'Ljava/lang/Object;'],0,0xffffffff,0,class_data,encoded)
    data.extend(bytes((-len(data))%4));map_offset=len(data)
    entries=[(0,1,0),(1,len(strings),tables[56]),(2,len(types),tables[64]),(3,1,tables[72]),
             (4,4,tables[80]),(5,3,tables[88]),(6,1,tables[96]),(0x2002,len(strings),start),
             (0x2001,2,code),(0x2000,1,class_data),(0x1000,1,map_offset)]
    if evolved:entries.insert(-1,(0x2005,1,encoded))
    data.extend(struct.pack('<I',len(entries)))
    for kind,count,offset in entries:data.extend(struct.pack('<HHII',kind,0,count,offset))
    data[:8]=b'dex\n040\0';put(32,len(data));put(36,0x70);put(40,0x12345678);put(52,map_offset)
    put(104,len(data)-start);put(108,start);data[12:32]=hashlib.sha1(data[32:]).digest();put(8,zlib.adler32(data[12:]))
    path=out/('shape-%d-%d-%d-%d-%d.dex'%(method_deleted,field_deleted,evolved,final_guard,hierarchy));path.write_bytes(data);return str(path)
sources = ['native_narrowing_authority','native_receiver_service','native_receiver_guard','native_receiver_frame','native_receiver_bytecode','native_final_write_listener','native_final_write_guard','modern_final_field_guard','native_hierarchy_typed_storage','native_typed_heap_walk','native_typed_heap_calibration','whole_process_frames', 'native_required_cleanup', 'native_source_stop', 'native_source_class_loading', 'native_member_provider', 'native_method_table', 'native_structural_reflection', 'resolution_invalidation_plan', 'native_source_commit', 'modern_source_commit', 'source_commit',
           'modern_field_metadata', 'native_interface_graph', 'modern_field_storage', 'field_copy', 'prepared_field_copy',
           'native_schema_policy','native_schema_binding','native_method_evolution_policy','source_handle_roots',
           'exclusive_owned_roots', 'native_cache_service', 'modern_method_services',
           'structural_methods', 'method_identity_calibration', 'object_replacement_map']
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(cpp), '-I', str(out),
           '-I', str(jdk / 'include'), '-I', str(jdk / 'include' / platform),
           str(root / 'tests/host/native_hierarchy_typed_storage_test.cc')]
command += ['-I',str(args.method_policy_root)] if args.method_policy_root else []
command += [str((args.method_policy_root if args.method_policy_root and s=='native_method_evolution_policy' else cpp) / 'platform/common' / (s + '.cc')) for s in sources]
command += [str(cpp / s) for s in ['evolution/dex_schema.cc','evolution/migration_plan.cc']]
# Compressed-root fixtures need a real heap below 4 GiB. Mach-O otherwise
# reserves that entire address range as __PAGEZERO. Keep the null guard intact.
if platform == 'darwin' and not args.syntax_only:
    command += ['-Wl,-pagezero_size,0x10000']
command += ['-fsyntax-only'] if args.syntax_only else ['-o', str(binary)]
if args.syntax_only:
    subprocess.run(command,check=True)
else:
    units=[Path(x) for x in command if x.endswith('.cc')]
    common=[x for x in command[:-2] if not x.endswith('.cc') and not x.startswith('-Wl,')]
    headers=b''.join(p.read_bytes() for p in sorted(cpp.rglob('*.h')))+b''.join(p.read_bytes() for p in sorted(out.glob('*.inc')))
    objects=out/'objects';objects.mkdir(exist_ok=True)
    def compile_unit(source):
        obj=objects/(source.stem+'.o');stamp=obj.with_suffix('.sha256')
        key=hashlib.sha256(source.read_bytes()+headers+' '.join(common).encode()).hexdigest()
        if not obj.exists() or not stamp.exists() or stamp.read_text()!=key:
            subprocess.run(common+['-c',str(source),'-o',str(obj)],check=True)
            stamp.write_text(key)
        return str(obj)
    with ThreadPoolExecutor(max_workers=4) as pool:compiled=list(pool.map(compile_unit,units))
    link=[common[0]]+compiled+['-fsanitize=address,undefined']
    if platform=='darwin':link+=['-Wl,-pagezero_size,0x10000']
    subprocess.run(link+['-o',str(binary)],check=True)
if args.syntax_only or args.compile_only:
    print('Native member production TU '+('syntax PASS' if args.syntax_only else 'link PASS; execution pending'));raise SystemExit(0)
result = subprocess.run([str(binary)] + [dex_fixture(m, f) for f in (False, True) for m in (False, True)]+[dex_fixture(False,False,True),dex_fixture(False,False,True,True),dex_fixture(False,False,True,False,True)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / 'result.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('Complete typed-storage source lifecycle: ASan/UBSan passed')
