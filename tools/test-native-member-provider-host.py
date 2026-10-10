#!/usr/bin/env python3
"""Exercise calibrated native commit adapter and dependencies as one transaction."""
import argparse
import os
import hashlib
import struct
import zlib
from pathlib import Path
import subprocess
parser=argparse.ArgumentParser();parser.add_argument('--syntax-only',action='store_true');parser.add_argument('--compile-only',action='store_true');parser.add_argument('--method-policy-root',type=Path);args=parser.parse_args()
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
jdk = Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'], text=True).strip())
platform = 'darwin' if os.uname().sysname == 'Darwin' else 'linux'
out = root / 'build/evidence/host/native-member-provider'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'native_member_provider_test'
fixture = (root / 'tests/host/native_source_commit_test.cc').read_text()
assert fixture.count('int main(){') == 1
(out / 'native_commit_fixture.inc').write_text(fixture.split('int main(){')[0])
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
sources = ['native_member_provider', 'native_method_table', 'native_structural_reflection', 'resolution_invalidation_plan', 'native_source_commit', 'native_initialized_transfer', 'modern_source_commit', 'source_commit',
           'modern_field_metadata', 'native_interface_graph', 'modern_field_storage', 'field_copy', 'prepared_field_copy',
           'native_schema_policy','native_schema_binding','native_method_evolution_policy','source_handle_roots',
           'exclusive_owned_roots', 'native_cache_service', 'modern_method_services',
           'structural_methods', 'method_identity_calibration', 'object_replacement_map']
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(cpp), '-I', str(out),
           '-I', str(jdk / 'include'), '-I', str(jdk / 'include' / platform),
           str(root / 'tests/host/native_member_provider_test.cc')]
command += ['-I',str(args.method_policy_root)] if args.method_policy_root else []
command += [str((args.method_policy_root if args.method_policy_root and s=='native_method_evolution_policy' else cpp) / 'platform/common' / (s + '.cc')) for s in sources]
command += [str(cpp / s) for s in ['evolution/dex_schema.cc','evolution/migration_plan.cc']]
# Compressed-root fixtures need a real heap below 4 GiB. Mach-O otherwise
# reserves that entire address range as __PAGEZERO. Keep the null guard intact.
if platform == 'darwin' and not args.syntax_only:
    command += ['-Wl,-pagezero_size,0x10000']
command += ['-fsyntax-only'] if args.syntax_only else ['-o', str(binary)]
subprocess.run(command, check=True)
if args.syntax_only or args.compile_only:
    print('Native member production TU '+('syntax PASS' if args.syntax_only else 'link PASS; execution pending'));raise SystemExit(0)
result = subprocess.run([str(binary)] + [dex_fixture(m, f) for f in (False, True) for m in (False, True)]+[dex_fixture(False,False,True),dex_fixture(False,False,True,True),dex_fixture(False,False,True,False,True)], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / 'result.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('Shared native member provider: ASan/UBSan passed')
