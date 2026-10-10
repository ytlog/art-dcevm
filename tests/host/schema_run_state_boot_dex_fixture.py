"""Independent scalar Boot class declaration for the concrete AOT owner oracle."""
from pathlib import Path
import hashlib,struct,zlib

def write_boot_fixture(directory: Path) -> Path:
    strings=sorted(['I','Lcom/example/BootScalar;','Ljava/lang/Object;','staticKeep'])
    types=sorted(['I','Lcom/example/BootScalar;','Ljava/lang/Object;'])
    sid={s:i for i,s in enumerate(strings)};tid={s:i for i,s in enumerate(types)}
    data=bytearray(112)
    def put(offset,value):struct.pack_into('<I',data,offset,value)
    def reserve(n):offset=len(data);data.extend(bytes(n));return offset
    def uleb(n):
        result=bytearray()
        while n>=128:result.append((n&127)|128);n>>=7
        result.append(n);return result
    strings_at=reserve(len(strings)*4);types_at=reserve(len(types)*4);proto_at=reserve(12);methods_at=reserve(8);classes_at=reserve(32);start=len(data)
    for i,s in enumerate(strings):put(strings_at+4*i,len(data));data.extend(uleb(len(s))+s.encode()+b'\0')
    for i,s in enumerate(types):put(types_at+4*i,sid[s])
    struct.pack_into('<III',data,proto_at,sid['I'],tid['I'],0)
    struct.pack_into('<HHI',data,methods_at,tid['Lcom/example/BootScalar;'],0,sid['staticKeep'])
    data.extend(bytes((-len(data))%4));code_at=len(data)
    data.extend(struct.pack('<HHHHIIHH',1,0,0,0,0,2,0x0012,0x000f))
    class_data_at=len(data);data.extend(bytes([0,0,1,0,0,9])+uleb(code_at))
    struct.pack_into('<IIIIIIII',data,classes_at,tid['Lcom/example/BootScalar;'],1,tid['Ljava/lang/Object;'],0,0xffffffff,0,class_data_at,0)
    data.extend(bytes((-len(data))%4));map_at=len(data)
    entries=[(0,1,0),(1,len(strings),strings_at),(2,len(types),types_at),(3,1,proto_at),(5,1,methods_at),(6,1,classes_at),(0x2002,len(strings),start),(0x2001,1,code_at),(0x2000,1,class_data_at),(0x1000,1,map_at)]
    data.extend(struct.pack('<I',len(entries)))
    for kind,count,offset in entries:data.extend(struct.pack('<HHII',kind,0,count,offset))
    data[:8]=b'dex\n040\0'
    for offset,value in [(32,len(data)),(36,112),(40,0x12345678),(52,map_at),(56,len(strings)),(60,strings_at),(64,len(types)),(68,types_at),(72,1),(76,proto_at),(88,1),(92,methods_at),(96,1),(100,classes_at),(104,len(data)-start),(108,start)]:put(offset,value)
    data[12:32]=hashlib.sha1(data[32:]).digest();put(8,zlib.adler32(data[12:]));directory.mkdir(parents=True,exist_ok=True);path=directory/'boot.dex';path.write_bytes(data);return path
