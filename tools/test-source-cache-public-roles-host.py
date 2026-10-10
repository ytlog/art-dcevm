#!/usr/bin/env python3
"""Public complete cache roles against real metadata and source-owned movable roots."""
import os, subprocess, struct, hashlib, zlib
from pathlib import Path
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
out=root/'build/evidence/host/source-cache-public-roles';out.mkdir(parents=True,exist_ok=True)
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
fixture=(root/'tests/host/native_source_commit_test.cc').read_text().split('int main(){')[0].replace('std::array<unsigned char,200>','std::array<unsigned char,512>')
(out/'native_commit_fixture.inc').write_text(fixture)
provider=(root/'tests/host/native_member_provider_test.cc').read_text().split('int main(int argc,char** argv){')[0]
provider=provider.replace('#include "native_receiver_class_source_cases.h"','')
(out/'provider_fixture.inc').write_text(provider)
roots=(root/'tests/host/source_handle_roots_test.cc').read_text().split('int main(){')[0]
roots=roots.replace('#include "native_commit_fixture.inc"','').replace('class RootHooks final:public Hooks','class RootHooks final:public ProviderHooks').replace('return Hooks::FindSymbol(lib,name);','return ProviderHooks::FindSymbol(lib,name);')
(out/'roots_fixture.inc').write_text(roots)
# Independent complete StandardDEX, with primitive/array and inherited external IDs.
strings=sorted(['I','V','LFixture;','LChild;','Ljava/lang/Object;','[I','left','number','right','value','<clinit>','keep','staticKeep'])
sids={s:i for i,s in enumerate(strings)};types=sorted(['I','V','LFixture;','LChild;','Ljava/lang/Object;','[I']);tids={s:i for i,s in enumerate(types)}
fields=sorted([('LFixture;',n,t) for n,t in [('number','I'),('value','I'),('left','Ljava/lang/Object;'),('right','Ljava/lang/Object;')]]+[('LChild;','value','I')],key=lambda x:(tids[x[0]],sids[x[1]],tids[x[2]]))
methods=sorted([('LFixture;','<clinit>','V'),('LFixture;','keep','I'),('LFixture;','staticKeep','I'),('LChild;','keep','I')],key=lambda x:(tids[x[0]],sids[x[1]]))
b=bytearray(0x70)
def put(at,v):struct.pack_into('<I',b,at,v)
def leb(v):
 r=bytearray()
 while v>=128:r.append((v&127)|128);v>>=7
 return r+bytes([v])
tables={}
for off,count,width in [(56,len(strings),4),(64,len(types),4),(72,2,12),(80,len(fields),8),(88,len(methods),8),(96,1,32)]:
 tables[off]=len(b);put(off,count);put(off+4,len(b));b.extend(bytes(count*width))
start=len(b)
for i,s in enumerate(strings):put(tables[56]+i*4,len(b));b.extend(leb(len(s))+s.encode()+b'\0')
for i,s in enumerate(types):put(tables[64]+i*4,sids[s])
for i,t in enumerate(['I','V']):struct.pack_into('<III',b,tables[72]+i*12,sids[t],tids[t],0)
for i,(c,n,t) in enumerate(fields):struct.pack_into('<HHI',b,tables[80]+8*i,tids[c],tids[t],sids[n])
for i,(c,n,t) in enumerate(methods):struct.pack_into('<HHI',b,tables[88]+8*i,tids[c],['I','V'].index(t),sids[n])
b.extend(bytes((-len(b))%4));code=len(b);b.extend(struct.pack('<HHHHIIHH',1,1,0,0,0,2,0x0012,0x000f))
code_static=len(b);b.extend(struct.pack('<HHHHIIHH',1,0,0,0,0,2,0x0012,0x000f));class_data=len(b)
static=[(i,8) for i,f in enumerate(fields) if f[:2]==('LFixture;','number')]
instance=[(i,1) for i,f in enumerate(fields) if f[0]=='LFixture;' and f[1]!='number']
direct=[(i,0x10008 if m[1]=='<clinit>' else 9,code_static) for i,m in enumerate(methods) if m[0]=='LFixture;' and m[1]!='keep']
virtual=[(i,1,code) for i,m in enumerate(methods) if m[:2]==('LFixture;','keep')]
b.extend(leb(len(static))+leb(len(instance))+leb(len(direct))+leb(len(virtual)))
for entries in (static,instance,direct,virtual):
 prev=0
 for entry in entries:
  i,flags,*location=entry;b.extend(leb(i-prev)+leb(flags));prev=i
  if location:b.extend(leb(location[0]))
struct.pack_into('<IIIIIIII',b,tables[96],tids['LFixture;'],1,tids['Ljava/lang/Object;'],0,0xffffffff,0,class_data,0)
b.extend(bytes((-len(b))%4));mapoff=len(b)
entries=[(0,1,0),(1,len(strings),tables[56]),(2,len(types),tables[64]),(3,2,tables[72]),(4,len(fields),tables[80]),(5,len(methods),tables[88]),(6,1,tables[96]),(0x2002,len(strings),start),(0x2001,2,code),(0x2000,1,class_data),(0x1000,1,mapoff)]
b.extend(struct.pack('<I',len(entries)))
for kind,count,off in entries:b.extend(struct.pack('<HHII',kind,0,count,off))
b[:8]=b'dex\n040\0';put(32,len(b));put(36,0x70);put(40,0x12345678);put(52,mapoff);put(104,len(b)-start);put(108,start);b[12:32]=hashlib.sha1(b[32:]).digest();put(8,zlib.adler32(b[12:]))
dex=out/'roles.dex';dex.write_bytes(b)
sources=['source_cache_public_roles','source_cache_role_catalog','source_handle_roots','source_allocation_transaction','native_source_commit','native_initialized_transfer','modern_source_commit','source_commit','modern_field_metadata','native_interface_graph','modern_field_storage','field_copy','prepared_field_copy','exclusive_owned_roots','native_cache_service','modern_method_services','structural_methods','method_identity_calibration','object_replacement_map']
cmd=[os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(out),'-I',str(root/'tests/host'),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/source_cache_public_roles_test.cc')]+[str(cpp/'platform/common'/(s+'.cc')) for s in sources]+[str(cpp/'evolution/dex_schema.cc')]
if platform=='darwin':cmd+=['-Wl,-pagezero_size,0x10000','-Wl,-dead_strip']
else:cmd+=['-ffunction-sections','-Wl,--gc-sections']
cmd+=['-o',str(out/'test')]
from concurrent.futures import ThreadPoolExecutor
units=[Path(x) for x in cmd if x.endswith('.cc')]
common=[x for x in cmd[:-2] if not x.endswith('.cc') and not x.startswith('-Wl,')]
headers=b''.join(p.read_bytes() for p in sorted(cpp.rglob('*.h')))+b''.join(p.read_bytes() for p in sorted(out.glob('*.inc')))
objects=out/'objects';objects.mkdir(exist_ok=True)
def compile_unit(source):
 obj=objects/(source.stem+'.o');stamp=obj.with_suffix('.sha256');key=hashlib.sha256(source.read_bytes()+headers+' '.join(common).encode()).hexdigest()
 if not obj.exists() or not stamp.exists() or stamp.read_text()!=key:
  r=subprocess.run(common+['-c',str(source),'-o',str(obj)],capture_output=True,text=True);obj.with_suffix('.log').write_text(r.stdout+r.stderr);r.check_returncode();stamp.write_text(key)
 return str(obj)
with ThreadPoolExecutor(max_workers=4) as workers:compiled=list(workers.map(compile_unit,units))
link=[common[0]]+compiled+['-fsanitize=address,undefined']
link+=['-Wl,-pagezero_size,0x10000','-Wl,-dead_strip'] if platform=='darwin' else ['-Wl,--gc-sections']
c=subprocess.run(link+['-o',str(out/'test')],capture_output=True,text=True);(out/'compile.log').write_text(c.stdout+c.stderr);c.check_returncode()
for layout in ('legacy','sized'):
 env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
 if layout=='sized':env['SOURCE_SCOPE_SIZED']='1'
 else:env.pop('SOURCE_SCOPE_SIZED',None)
 r=subprocess.run([str(out/'test'),str(dex)],env=env,capture_output=True,text=True);(out/('result-'+layout+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();print(layout+': '+r.stdout.strip())
