"""Independent valid DEX namespaces for interface dispatch integration tests."""
import struct,hashlib,zlib

def leb(value):
 out=bytearray()
 while value>=128:out.append((value&127)|128);value>>=7
 return out+bytes([value])

def make(owner,kind):
 caller=kind=='caller';receiver=kind=='receiver'
 types=sorted({'I',owner,'LFixture;','Ljava/lang/Object;'})
 names=['call','keep','staticKeep'] if caller else ['keep','staticKeep']
 strings=sorted(set(types+names+(['IL'] if caller else [])))
 sid={s:i for i,s in enumerate(strings)};tid={s:i for i,s in enumerate(types)}
 protos=[()] + ([('LFixture;',)] if caller else [])
 methods=sorted(([(owner,'call',1),('LFixture;','keep',0),('LFixture;','staticKeep',0)] if caller else [(owner,n,0) for n in names]),key=lambda m:(tid[m[0]],sid[m[1]],m[2]))
 b=bytearray(112);tables={};maps=[(0,1,0)]
 def put(at,v):struct.pack_into('<I',b,at,v)
 def align():b.extend(bytes((-len(b))%4))
 for header,count,width,tag in [(56,len(strings),4,1),(64,len(types),4,2),(72,len(protos),12,3),(88,len(methods),8,5),(96,1,32,6)]:
  tables[header]=len(b);put(header,count);put(header+4,len(b));maps.append((tag,count,len(b)));b.extend(bytes(count*width))
 start=len(b);maps.append((0x2002,len(strings),start))
 for i,s in enumerate(strings):put(tables[56]+i*4,len(b));b.extend(leb(len(s))+s.encode()+b'\0')
 for i,t in enumerate(types):put(tables[64]+i*4,sid[t])
 list_offsets={};lists=[]
 for params in ([('LFixture;',)] if caller or receiver else []):
  align();list_offsets[params]=len(b);lists.append(len(b));b.extend(struct.pack('<I',len(params))+b''.join(struct.pack('<H',tid[t]) for t in params))
 if lists:maps.append((0x1001,len(lists),lists[0]))
 for i,p in enumerate(protos):struct.pack_into('<III',b,tables[72]+i*12,sid['IL' if p else 'I'],tid['I'],list_offsets.get(p,0))
 for i,(o,n,p) in enumerate(methods):struct.pack_into('<HHI',b,tables[88]+i*8,tid[o],p,sid[n])
 codes={};code_offsets=[]
 if kind!='interface':
  for i,(o,n,p) in enumerate(methods):
   if o!=owner:continue
   align();codes[i]=len(b);code_offsets.append(len(b))
   if caller:units=[0x1072,next(j for j,m in enumerate(methods) if m[:2]==('LFixture;','keep')),0,0x000a,0x000f]
   else:units=[0x0012,0x000f]
   b.extend(struct.pack('<HHHHII',1,1,1 if caller else 0,0,0,len(units))+struct.pack('<'+'H'*len(units),*units))
 if code_offsets:maps.append((0x2001,len(code_offsets),code_offsets[0]))
 data=len(b);direct=[(i,9,codes[i]) for i,m in enumerate(methods) if m[0]==owner] if caller else []
 virtual=[] if caller else [(i,0x401 if kind=='interface' else 1,codes.get(i,0)) for i,m in enumerate(methods) if m[0]==owner]
 b.extend(leb(0)+leb(0)+leb(len(direct))+leb(len(virtual)))
 for entries in (direct,virtual):
  prev=0
  for i,access,code in entries:b.extend(leb(i-prev)+leb(access)+leb(code));prev=i
 maps.append((0x2000,1,data))
 struct.pack_into('<IIIIIIII',b,tables[96],tid[owner],0x601 if kind=='interface' else 1,tid['Ljava/lang/Object;'],list_offsets.get(('LFixture;',),0) if receiver else 0,0xffffffff,0,data,0)
 align();mapoff=len(b);maps.append((0x1000,1,mapoff));maps.sort(key=lambda x:x[2]);b.extend(struct.pack('<I',len(maps)))
 for tag,count,off in maps:b.extend(struct.pack('<HHII',tag,0,count,off))
 b[:8]=b'dex\n040\0';put(32,len(b));put(36,112);put(40,0x12345678);put(52,mapoff);put(104,len(b)-start);put(108,start);b[12:32]=hashlib.sha1(b[32:]).digest();put(8,zlib.adler32(b[12:]))
 return b,next(iter(codes.values()),0)

def write(out):
 for name,owner,kind in [('interface','LFixture;','interface'),('receiver','LChild;','receiver'),('caller','LCaller;','caller')]:
  data,code=make(owner,kind);(out/(name+'.dex')).write_bytes(data)
  if kind=='caller':(out/'dispatch_code.inc').write_text('constexpr size_t kCallerCodeOffset='+str(code)+';\n')
