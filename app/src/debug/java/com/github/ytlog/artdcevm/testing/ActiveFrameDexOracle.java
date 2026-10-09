package com.github.ytlog.artdcevm.testing;

import java.io.*;
import java.lang.reflect.Method;
import java.util.Arrays;
import java.util.zip.ZipFile;

/** Indexed input vregs from actual installed DEX CodeItems, independent of ART. */
final class ActiveFrameDexOracle {
    static int[] read(String apk,Method[] methods) throws IOException {
        if(methods.length!=5)throw new IOException("Expected carrier/two inlinees/native/instance oracles");
        MethodDexOracle.Result identity=MethodDexOracle.read(apk,methods);
        byte[] bytes;
        try(ZipFile zip=new ZipFile(apk);InputStream in=zip.getInputStream(zip.getEntry(identity.dexEntry))) {
            ByteArrayOutputStream out=new ByteArrayOutputStream();byte[] chunk=new byte[8192];int count;
            while((count=in.read(chunk))!=-1) {if(out.size()>128*1024*1024-count)throw new IOException("DEX too large");out.write(chunk,0,count);}bytes=out.toByteArray();
        }
        int[] shape=decode(bytes,identity.headerOffset,identity.indices);
        int[] result=new int[22];System.arraycopy(identity.indices,0,result,0,5);
        System.arraycopy(identity.modifiers,0,result,5,5);System.arraycopy(shape,0,result,10,12);return result;
    }
    static int[] decode(byte[] bytes,int base,int[] methods) throws IOException {
        if(methods.length<4)throw new IOException("Missing frame method ids");
        Reader dex=new Reader(bytes);dex.range(base,112);
        int methodCount=dex.u32(base+88),methodOff=dex.u32(base+92);
        dex.range(methodOff,(long)methodCount*8);
        int owner=-1;
        for(int i=0;i<3;++i) {
            if(methods[i]<0||methods[i]>=methodCount)throw new IOException("Method id out of range");
            int type=dex.u16(methodOff+methods[i]*8);
            if(owner!=-1&&owner!=type)throw new IOException("Mixed frame declaring classes");owner=type;
            for(int j=0;j<i;++j)if(methods[j]==methods[i])throw new IOException("Duplicate frame method id");
        }
        int classes=dex.u32(base+96),classOff=dex.u32(base+100),classData=0;boolean classFound=false;
        dex.range(classOff,(long)classes*32);
        for(int i=0;i<classes;++i)if(dex.u32(classOff+i*32)==owner) {
            if(classFound)throw new IOException("Duplicate frame class definition");classFound=true;classData=dex.u32(classOff+i*32+24);
        }
        if(classData==0)throw new IOException("Missing frame class_data");
        int[] cursor={classData};int staticFields=dex.uleb(cursor),instanceFields=dex.uleb(cursor),direct=dex.uleb(cursor),virtual=dex.uleb(cursor);
        if((long)staticFields+instanceFields+direct+virtual>bytes.length)throw new IOException("Unbounded class_data");
        for(long i=0;i<(long)staticFields+instanceFields;++i){dex.uleb(cursor);dex.uleb(cursor);}
        int[] result=new int[12];Arrays.fill(result,-1);
        for(int section=0;section<2;++section) {
            int index=0,count=section==0?direct:virtual;
            for(int i=0;i<count;++i) {
                int delta=dex.uleb(cursor);if(delta>Integer.MAX_VALUE-index)throw new IOException("Method delta overflow");index+=delta;
                int flags=dex.uleb(cursor),code=dex.uleb(cursor);
                for(int frame=0;frame<3;++frame)if(index==methods[frame]) {
                    if(result[frame]!=-1||section!=0||(flags&8)==0||(flags&0x500)!=0||code==0||(code&3)!=0)
                        throw new IOException("Frame must be unique static nonnative CodeItem");
                    dex.range(code,16);int registers=dex.u16(code),incoming=dex.u16(code+2);
                    int instructionCount=dex.u32(code+12);
                    dex.range(code+16,(long)instructionCount*2);
                    result[9+frame]=invokePc(dex,code+16,instructionCount,methods[frame+1]);
                    if(incoming!=6||registers<incoming)throw new IOException("Fixture signature/CodeItem input mismatch");
                    result[frame]=registers;
                    result[3+frame]=registers-incoming+frame*2;
                    result[6+frame]=result[3+frame]+1;
                }
            }
        }
        for(int value:result)if(value<0)throw new IOException("Frame CodeItem missing");return result;
    }
    // Fixed DEX instruction formats (source.android.com/docs/core/runtime/dalvik-bytecode).
    // This fixture has straight-line code; payload/quickened/unused instructions
    // are rejected rather than scanned as if their operands were instructions.
    private static int invokePc(Reader dex,int start,int units,int target)throws IOException {
        int found=-1;
        for(int pc=0;pc<units;) {
            int word=dex.u16(start+pc*2),op=word&255,width=width(op);
            if(op==0&&word!=0)throw new IOException("Fixture payload is not supported");
            if(width>units-pc)throw new IOException("Truncated DEX instruction");
            if((op==0x71||op==0x77)&&dex.u16(start+(pc+1)*2)==target) {
                if(found!=-1)throw new IOException("Ambiguous fixture child invocation");found=pc;
            }
            pc+=width;
        }
        if(found<0)throw new IOException("Missing independently indexed fixture child invoke");return found;
    }
    private static int width(int op)throws IOException {
        if(op==3||op==6||op==9||op==0x14||op==0x17||op==0x1b||op==0x24||op==0x25||op==0x26||op==0x2a||op==0x2b||op==0x2c||(op>=0x6e&&op<=0x72)||(op>=0x74&&op<=0x78)||op==0xfc||op==0xfd)return 3;
        if(op==0x18)return 5;if(op==0xfa||op==0xfb)return 4;
        if(op==2||op==5||op==8||op==0x13||op==0x15||op==0x16||op==0x19||op==0x1a||op==0x1c||op==0x1f||op==0x20||op==0x22||op==0x23||op==0x29||(op>=0x2d&&op<=0x3d)||(op>=0x44&&op<=0x6d)||(op>=0x90&&op<=0xaf)||(op>=0xd0&&op<=0xe2)||op==0xfe||op==0xff)return 2;
        if(op<=0x12||op==0x1d||op==0x1e||op==0x21||op==0x27||op==0x28||(op>=0x7b&&op<=0x8f)||(op>=0xb0&&op<=0xcf))return 1;
        throw new IOException("Unknown/unused fixture DEX opcode");
    }
    private static final class Reader {
        final byte[] bytes;Reader(byte[] bytes){this.bytes=bytes;}
        void range(int start,long count)throws IOException{if(start<0||count<0||count>bytes.length-start)throw new IOException("DEX range");}
        int u16(int at)throws IOException{range(at,2);return(bytes[at]&255)|((bytes[at+1]&255)<<8);}
        int u32(int at)throws IOException{range(at,4);long value=(bytes[at]&255L)|((bytes[at+1]&255L)<<8)|((bytes[at+2]&255L)<<16)|((bytes[at+3]&255L)<<24);if(value>Integer.MAX_VALUE)throw new IOException("DEX integer overflow");return(int)value;}
        int uleb(int[] cursor)throws IOException{long value=0;for(int i=0;i<5;++i){range(cursor[0],1);int next=bytes[cursor[0]++]&255;value|=(long)(next&127)<<(i*7);if((next&128)==0){if(value>Integer.MAX_VALUE)throw new IOException("DEX ULEB overflow");return(int)value;}}throw new IOException("DEX ULEB length");}
    }
}
