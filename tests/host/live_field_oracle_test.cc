#include "platform/common/runnable_jni_bridge.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;
static int self,vm,klass,mirror,native,foreign;static bool root_valid=true;static void* leaf=&native;static void* decoded=&native;static unsigned owner_reads=0;
static void* Current(){return &self;}
struct NativeEngine{void*vm=nullptr;void*(*graph_current)()=nullptr;};
static bool FieldOracle(void*,const runnable_jni::Context&,jobject p,void**out){*out=p;return root_valid;}
static void* FieldLeaf(void*,void*){return leaf;}
static void* FieldOwner(void*,void*p){++owner_reads;assert(p==&native);return &klass;}
#include "production_callback.inc"
int main(){NativeEngine e{&vm,Current};runnable_jni::Context c{&self,reinterpret_cast<JNIEnv*>(&vm),&vm};
 auto run=[&]{Decode proof{e,reinterpret_cast<jclass>(&klass),reinterpret_cast<jobject>(&mirror),decoded};const bool result=Decode::Run(c,&proof);return std::pair<bool,const char*>(result,proof.stage);};
 assert(run().first&&owner_reads==1);leaf=&foreign;const auto reads=owner_reads;auto bad=run();assert(!bad.first&&!std::strcmp(bad.second,"original-leaf-public-proof-equality")&&owner_reads==reads);
 leaf=&native;decoded=nullptr;bad=run();assert(!bad.first&&!std::strcmp(bad.second,"original-leaf-public-proof-equality")&&owner_reads==reads);decoded=&native;
 c.vm=&foreign;const auto owners=owner_reads;bad=run();assert(!bad.first&&!std::strcmp(bad.second,"actual-Thread-VM")&&owner_reads==owners);c.vm=&vm;
 root_valid=false;bad=run();assert(!bad.first&&!std::strcmp(bad.second,"public-Class-root")&&owner_reads==owners);root_valid=true;
 assert(run().first&&owner_reads==reads+1);std::cout<<"Actual production live Field callback: exact leaf/public-proof/Class identity, wrong VM/root/proof and foreign leaf reject before ArtField root dereference PASS\n";
}
