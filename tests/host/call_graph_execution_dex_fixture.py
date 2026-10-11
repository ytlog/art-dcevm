"""Independent StandardDEX public declarations and executable callsite body."""
from pathlib import Path
import hashlib,struct,zlib

def write(directory:Path,incoming_extra=False,abstract_keep=False,native_all=False,outgoing='static')->Path:
    method_names=['gone','keep']+(['newKeep'] if incoming_extra else [])+['staticKeep']
    strings=sorted(['I','LFixture;','Ljava/lang/Object;','number','value','left','right']+method_names)
    types=sorted(['I','LFixture;','Ljava/lang/Object;']);sid={s:i for i,s in enumerate(strings)};tid={s:i for i,s in enumerate(types)}
    b=bytearray(112)
    def put(at,n):struct.pack_into('<I',b,at,n)
    def reserve(n):at=len(b);b.extend(bytes(n));return at
    def leb(n):
        r=bytearray()
        while n>=128:r.append((n&127)|128);n>>=7
        r.append(n);return r
    sa=reserve(len(strings)*4);ta=reserve(len(types)*4);pa=reserve(12);fa=reserve(32);ma=reserve((len(method_names)+1)*8);ca=reserve(64);data=len(b)
    for i,s in enumerate(strings):put(sa+i*4,len(b));b.extend(leb(len(s))+s.encode()+b'\0')
    for i,s in enumerate(types):put(ta+i*4,sid[s])
    struct.pack_into('<III',b,pa,sid['I'],tid['I'],0)
    fields=sorted([('number','I',8),('value','I',1),('left','Ljava/lang/Object;',1),('right','Ljava/lang/Object;',1)])
    for i,(n,t,_) in enumerate(fields):struct.pack_into('<HHI',b,fa+8*i,tid['LFixture;'],tid[t],sid[n])
    for i,n in enumerate(method_names):struct.pack_into('<HHI',b,ma+8*i,tid['LFixture;'],0,sid[n])
    struct.pack_into('<HHI',b,ma+len(method_names)*8,tid['Ljava/lang/Object;'],0,sid['keep'])
    invoke=0x1070 if outgoing in ('direct','wrong-direct') else 0x106e if outgoing in ('virtual','external') else 0x0071
    target=len(method_names) if outgoing=='external' else 2
    b.extend(bytes((-len(b))%4));keep=len(b);b.extend(struct.pack('<HHHHII5H',1,1,0,0,0,5,invoke,target,0,0x000a,0x000f))
    b.extend(bytes((-len(b))%4));static=len(b);b.extend(struct.pack('<HHHHII2H',1,0,0,0,0,2,0x0012,0x000f))
    b.extend(bytes((-len(b))%4));parent_code=len(b);b.extend(struct.pack('<HHHHII2H',1,1,0,0,0,2,0x0012,0x000f))
    extra=0
    if incoming_extra:
        b.extend(bytes((-len(b))%4));extra=len(b);b.extend(struct.pack('<HHHHII5H',1,1 if outgoing=='direct' else 0,0,0,0,5,0x0071,3,0,0x000a,0x000f))
    cd=len(b);b.extend(bytes([1,3,2 if incoming_extra else 1,2]))
    for group in ([x for x in enumerate(fields) if x[1][2]&8],[x for x in enumerate(fields) if not x[1][2]&8]):
        previous=0
        for index,(_,_,flags) in group:b.extend(leb(index-previous)+leb(flags));previous=index
    if incoming_extra:b.extend(leb(2)+leb(0x109 if native_all else 2 if outgoing=='direct' else 9)+leb(0 if native_all else extra)+leb(1)+leb(0x109 if native_all or outgoing=='native' else 9)+leb(0 if native_all or outgoing=='native' else static))
    else:b.extend(leb(2)+leb(0x109 if native_all else 9)+leb(0 if native_all else static))
    b.extend(leb(0)+leb(0x101)+leb(0));b.extend(leb(1)+leb(0x101 if native_all else 0x401 if abstract_keep else 1)+leb(0 if abstract_keep or native_all else keep))
    struct.pack_into('<8I',b,ca,tid['LFixture;'],0x401 if abstract_keep else 1,tid['Ljava/lang/Object;'],0,0xffffffff,0,cd,0)
    parent_cd=len(b);b.extend(bytes([0,0,0,1])+leb(len(method_names))+leb(1)+leb(parent_code))
    struct.pack_into('<8I',b,ca+32,tid['Ljava/lang/Object;'],1,0xffffffff,0,0xffffffff,0,parent_cd,0)
    b.extend(bytes((-len(b))%4));map_at=len(b);entries=[(0,1,0),(1,len(strings),sa),(2,len(types),ta),(3,1,pa),(4,4,fa),(5,len(method_names)+1,ma),(6,2,ca),(0x2002,len(strings),data),(0x2001,4 if incoming_extra else 3,keep),(0x2000,2,cd),(0x1000,1,map_at)]
    b.extend(struct.pack('<I',len(entries)))
    for k,n,at in entries:b.extend(struct.pack('<HHII',k,0,n,at))
    b[:8]=b'dex\n040\0'
    for at,n in [(32,len(b)),(36,112),(40,0x12345678),(52,map_at),(56,len(strings)),(60,sa),(64,len(types)),(68,ta),(72,1),(76,pa),(80,4),(84,fa),(88,len(method_names)+1),(92,ma),(96,2),(100,ca),(104,len(b)-data),(108,data)]:put(at,n)
    b[12:32]=hashlib.sha1(b[32:]).digest();put(8,zlib.adler32(b[12:]));directory.mkdir(parents=True,exist_ok=True);p=directory/'execution.dex';p.write_bytes(b);return p
