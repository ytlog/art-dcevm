"""Immutable bootstrap marker/readonly-error namespaces for real Domain owners."""
from pathlib import Path
import struct,hashlib,zlib
from managed_upcall_inline_dex_fixture import _build,write as write_inline

def empty(owner,flags):
    data,_=_build(owner,flags,(),{},[],lambda _: {})
    b=bytearray(data)
    for at in (72,88):
        assert struct.unpack_from('<I',b,at)[0]==0
        struct.pack_into('<I',b,at+4,0)
    mapoff=struct.unpack_from('<I',b,52)[0]
    count=struct.unpack_from('<I',b,mapoff)[0]
    entries=[struct.unpack_from('<HHII',b,mapoff+4+i*12) for i in range(count)]
    entries=[e for e in entries if e[2]]
    b[mapoff:]=struct.pack('<I',len(entries))+b''.join(struct.pack('<HHII',*e) for e in entries)
    struct.pack_into('<I',b,32,len(b));struct.pack_into('<I',b,104,len(b)-struct.unpack_from('<I',b,108)[0])
    b[12:32]=hashlib.sha1(b[32:]).digest();struct.pack_into('<I',b,8,zlib.adler32(b[12:]))
    return bytes(b)

def write(out):
    out=Path(out);out.mkdir(parents=True,exist_ok=True);write_inline(out)
    for name,owner,flags in [('cloneable','Ljava/lang/Cloneable;',0x601),('serializable','Ljava/io/Serializable;',0x601),('error','Lcom/example/DomainError;',1)]:
        (out/(name+'.dex')).write_bytes(empty(owner,flags))
