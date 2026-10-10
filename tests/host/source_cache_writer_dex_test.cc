#include "platform/common/source_cache_writer_dex.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>
#include <iostream>
int main(int argc,char**argv){
 assert(argc==2);std::ifstream in(argv[1],std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)),{});
 using artdcevm::source_cache_writer_dex::Read;
 // Independent actual D8 output indexes; production obtains them from its
 // immutable declaration parser and verifies these exact opcode operands.
 assert(Read(bytes.data(),bytes.size(),1026,1025,1,2052,1028,2055));
 assert(!Read(bytes.data(),bytes.size(),1026,1025,1,2052,1028,2052)); // same-class caller
 assert(!Read(bytes.data(),bytes.size(),1026,1025,2,2052,1028,2055)); // wrong collision slot
 assert(!Read(bytes.data(),bytes.size(),1026,1025,1,2052,1029,2055));
 assert(!Read(bytes.data(),bytes.size(),1027,1025,1,2052,1028,2055));
 assert(!Read(bytes.data(),112,1026,1025,1,2052,1028,2055));
 auto wrong=bytes;wrong[80]=1;wrong[81]=0;wrong[82]=wrong[83]=0;
 assert(!Read(wrong.data(),wrong.size(),1026,1025,1,2052,1028,2055));
 // Exact generated writer instructions changed to the wrong invocation/storage
 // category must refuse even though all names and IDs remain unchanged.
 for(unsigned op:{0x60u,0x71u}){
  wrong=bytes;bool changed=false;
  for(size_t i=112;i+4<wrong.size();i+=2){
   if(op==0x60&&wrong[i]==0x60&&wrong[i+2]==1&&wrong[i+3]==4){wrong[i]=0x52;changed=true;}
   if(op==0x71&&wrong[i]==0x71&&wrong[i+2]==4&&wrong[i+3]==8){wrong[i]=0x6e;changed=true;}
  }
  assert(changed&&!Read(wrong.data(),wrong.size(),1026,1025,1,2052,1028,2055));
 }
 std::cout<<"Actual immutable DEX cross-class static writer proof PASS; same-class/wrong-type/collision/missing-table/truncation rejected\n";
}
