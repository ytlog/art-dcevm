package com.github.ytlog.artdcevm.testing;
import java.io.IOException;
import java.util.Arrays;
public final class ActiveFrameDexOracleTest {
 static void u16(byte[] b,int at,int v){b[at]=(byte)v;b[at+1]=(byte)(v>>>8);}
 static void u32(byte[] b,int at,int v){u16(b,at,v);u16(b,at+2,v>>>16);}
 static int leb(byte[] b,int at,int value){do{int digit=value&127;value>>>=7;b[at++]=(byte)(digit|(value==0?0:128));}while(value!=0);return at;}
 static byte[] fixture(){byte[] b=new byte[400];u32(b,88,5);u32(b,92,112);u32(b,96,1);u32(b,100,152);u32(b,152+24,184);
 int at=184;for(int value:new int[]{0,0,4,1})at=leb(b,at,value);
 for(int i=0;i<4;++i){at=leb(b,at,i==0?0:1);at=leb(b,at,i==3?0x109:9);at=leb(b,at,i==3?0:224+i*32);}
 at=leb(b,at,4);at=leb(b,at,1);leb(b,at,320);
 for(int i=0;i<3;++i){int code=224+i*32;u16(b,code,10+i*2);u16(b,code+2,6);u32(b,code+12,6);u16(b,code+16,0x13);u16(b,code+18,0x71);u16(b,code+20,i==1?0x77:0x71);u16(b,code+22,i+1);u16(b,code+26,0xf);}
 return b;}
 static void reject(byte[] bytes,int[] ids)throws Exception{try{ActiveFrameDexOracle.decode(bytes,0,ids);throw new AssertionError("Malformed CodeItem accepted");}catch(IOException expected){}}
 public static void main(String[] args)throws Exception{int[] ids={0,1,2,3};byte[] b=fixture();
 if(!Arrays.equals(ActiveFrameDexOracle.decode(b,0,ids),new int[]{10,12,14,4,8,12,5,9,13,2,2,2}))throw new AssertionError("DEX input vregs incorrectly derived");
 reject(b,new int[]{0,0,2,3});reject(b,new int[]{0,1,5,3});
 byte[] bad=b.clone();u16(bad,224+2,5);reject(bad,ids);
 bad=b.clone();u16(bad,224,5);reject(bad,ids);
 bad=b.clone();u32(bad,224+12,1000);reject(bad,ids);
 bad=b.clone();bad[188]=0;bad[189]=(byte)0x89;bad[190]=2;reject(bad,ids);
 bad=b.clone();u16(bad,224+22,4);reject(bad,ids); // unrelated invoke
 bad=b.clone();u16(bad,224+16,0x0073);reject(bad,ids); // unused opcode
 bad=b.clone();u16(bad,224+16,0x0100);reject(bad,ids); // payload
 bad=b.clone();u32(bad,224+12,4);reject(bad,ids); // truncated invoke
 bad=b.clone();u32(bad,224+12,8);u16(bad,224+26,0x71);u16(bad,224+28,1);reject(bad,ids); // ambiguous invocation
 reject(Arrays.copyOf(b,290),ids);
 System.out.println("Active frame DEX oracle: real CodeItem input/register positions and malformed/duplicate/truncated shapes passed");
 }
}
