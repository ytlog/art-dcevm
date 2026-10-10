"""Independent StandardDex declaration oracle for actual RunState fixtures."""
import hashlib
import struct
import zlib
from pathlib import Path

TARGET='Lcom/example/Fixture;'
BASE='Lcom/example/HierarchyBase;'
INSERTED='Lcom/example/HierarchyInserted;'
MORE='Lcom/example/HierarchyMore;'
OBJECT='Ljava/lang/Object;'

def write_fixture(directory: Path, before: bool) -> Path:
    strings=sorted([TARGET,BASE,INSERTED,MORE,OBJECT,'I','gone','keep','staticKeep','number','value','left','right'])
    types=sorted([TARGET,BASE,INSERTED,MORE,OBJECT,'I'])
    ids={s:i for i,s in enumerate(strings)};tids={s:i for i,s in enumerate(types)}
    data=bytearray(0x70)
    def put(at,n):struct.pack_into('<I',data,at,n)
    def leb(n):
        out=bytearray()
        while n>=128:out.append((n&127)|128);n>>=7
        return out+bytes([n])
    tables={}
    for at,count,width in [(56,len(strings),4),(64,len(types),4),(72,1,12),(80,4,8),(88,3,8),(96,1,32)]:
        tables[at]=len(data);put(at,count);put(at+4,len(data));data.extend(bytes(count*width))
    start=len(data)
    for i,s in enumerate(strings):put(tables[56]+4*i,len(data));data.extend(leb(len(s))+s.encode()+b'\0')
    for i,s in enumerate(types):put(tables[64]+4*i,ids[s])
    struct.pack_into('<III',data,tables[72],ids['I'],tids['I'],0)
    for i,name in enumerate(['left','number','right','value']):
        struct.pack_into('<HHI',data,tables[80]+8*i,tids[TARGET],tids['I' if name in ('number','value') else OBJECT],ids[name])
    for i,name in enumerate(['gone','keep','staticKeep']):struct.pack_into('<HHI',data,tables[88]+8*i,tids[TARGET],0,ids[name])
    data.extend(bytes((-len(data))%4));interfaces=len(data);data.extend(struct.pack('<IH',1,tids[MORE]));data.extend(bytes((-len(data))%4))
    code=len(data);data.extend(struct.pack('<HHHHIIHH',1,1,0,0,0,2,0x0012,0x000f))
    code_static=len(data);data.extend(struct.pack('<HHHHIIHH',1,0,0,0,0,2,0x0012,0x000f))
    class_data=len(data);data.extend(bytes([1,3,1,2]))
    data.extend(bytes([1,8]))
    previous=0
    for index in [0,2,3]:data.extend(leb(index-previous)+bytes([1]));previous=index
    data.extend(bytes([2,9])+leb(code_static))
    data.extend(bytes([0])+leb(0x101)+bytes([0]))
    data.extend(bytes([1,1])+leb(code))
    struct.pack_into('<IIIIIIII',data,tables[96],tids[TARGET],1,tids[BASE if before else INSERTED],interfaces,0xffffffff,0,class_data,0)
    data.extend(bytes((-len(data))%4));map_at=len(data)
    entries=[(0,1,0),(1,len(strings),tables[56]),(2,len(types),tables[64]),(3,1,tables[72]),(4,4,tables[80]),(5,3,tables[88]),(6,1,tables[96]),(0x2002,len(strings),start),(0x1001,1,interfaces),(0x2001,2,code),(0x2000,1,class_data),(0x1000,1,map_at)]
    data.extend(struct.pack('<I',len(entries)))
    for kind,count,offset in entries:data.extend(struct.pack('<HHII',kind,0,count,offset))
    data[:8]=b'dex\n040\0';put(32,len(data));put(36,0x70);put(40,0x12345678);put(52,map_at);put(104,len(data)-start);put(108,start)
    data[12:32]=hashlib.sha1(data[32:]).digest();put(8,zlib.adler32(data[12:]))
    directory.mkdir(parents=True,exist_ok=True);path=directory/('before.dex' if before else 'after.dex');path.write_bytes(data);return path

def write_graph_fixture(directory: Path, package: str = 'Lcom/example/') -> Path:
    """Independent immutable defining DEX for the public loaded boot/parent graph."""
    root=package+'HierarchyRoot;';tag=package+'HierarchyTag;'
    base=package+'HierarchyBase;';inserted=package+'HierarchyInserted;';more=package+'HierarchyMore;'
    parents={OBJECT:None,root:OBJECT,base:root,inserted:base,tag:OBJECT,more:OBJECT}
    flags={name:(0x601 if name in (tag,more) else 1) for name in parents}
    strings=sorted(parents);types=strings;ids={s:i for i,s in enumerate(strings)}
    data=bytearray(0x70)
    def put(at,n):struct.pack_into('<I',data,at,n)
    strings_at=len(data);data.extend(bytes(len(strings)*4));types_at=len(data);data.extend(bytes(len(types)*4));classes_at=len(data);data.extend(bytes(len(types)*32));start=len(data)
    for i,s in enumerate(strings):put(strings_at+i*4,len(data));assert len(s)<128;data.extend(bytes([len(s)])+s.encode()+b'\0');put(types_at+i*4,i)
    data.extend(bytes((-len(data))%4));more_interfaces=len(data);data.extend(struct.pack('<IH',1,ids[tag]));data.extend(bytes((-len(data))%4))
    for i,name in enumerate(types):struct.pack_into('<IIIIIIII',data,classes_at+32*i,i,flags[name],ids[parents[name]] if parents[name] else 0xffffffff,more_interfaces if name==more else 0,0xffffffff,0,0,0)
    map_at=len(data);entries=[(0,1,0),(1,len(strings),strings_at),(2,len(types),types_at),(6,len(types),classes_at),(0x2002,len(strings),start),(0x1001,1,more_interfaces),(0x1000,1,map_at)]
    data.extend(struct.pack('<I',len(entries)))
    for kind,count,offset in entries:data.extend(struct.pack('<HHII',kind,0,count,offset))
    data[:8]=b'dex\n040\0'
    for at,value in [(32,len(data)),(36,0x70),(40,0x12345678),(52,map_at),(56,len(strings)),(60,strings_at),(64,len(types)),(68,types_at),(96,len(types)),(100,classes_at),(104,len(data)-start),(108,start)]:put(at,value)
    data[12:32]=hashlib.sha1(data[32:]).digest();put(8,zlib.adler32(data[12:]));directory.mkdir(parents=True,exist_ok=True);path=directory/'graph.dex';path.write_bytes(data);return path
