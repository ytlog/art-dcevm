#include "platform/common/runnable_jni_bridge.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace artdcevm;
static int self,vm,klass,mirror,native,foreign;static bool root_valid=true;static void* leaf=&native;static void* decoded=&native;static unsigned owner_reads=0,id_reads=0;
static void* Current(){return &self;}
static void* Id(void*,jfieldID){++id_reads;return decoded;}
struct NativeEngine{void*vm=nullptr;void*(*graph_current)()=nullptr;void*(*lazy_decode_field)(void*,jfieldID)=nullptr;struct{struct{void*jni_id_manager;}receivers;}runtime;};
static bool FieldOracle(void*,const runnable_jni::Context&,jobject p,void**out){*out=p;return root_valid;}
static void* FieldLeaf(void*,void*){return leaf;}
static void* FieldOwner(void*,void*p){++owner_reads;assert(p==&native);return &klass;}
#include "production_callback.inc"
int main(){NativeEngine e{&vm,Current,Id,{{&self}}};runnable_jni::Context c{&self,reinterpret_cast<JNIEnv*>(&vm),&vm};
 auto run=[&]{Decode proof{e,reinterpret_cast<jclass>(&klass),reinterpret_cast<jobject>(&mirror),reinterpret_cast<jfieldID>(&native)};const bool result=Decode::Run(c,&proof);return std::pair<bool,const char*>(result,proof.stage);};
 assert(run().first&&owner_reads==1);leaf=&foreign;const auto reads=owner_reads;auto bad=run();assert(!bad.first&&!std::strcmp(bad.second,"original-leaf-ID-equality")&&owner_reads==reads);
 leaf=&native;decoded=nullptr;bad=run();assert(!bad.first&&!std::strcmp(bad.second,"original-opaque-ID")&&owner_reads==reads);decoded=&native;
 c.vm=&foreign;const auto ids=id_reads;bad=run();assert(!bad.first&&!std::strcmp(bad.second,"actual-Thread-VM")&&id_reads==ids);c.vm=&vm;
 root_valid=false;bad=run();assert(!bad.first&&!std::strcmp(bad.second,"public-Class-root")&&id_reads==ids);root_valid=true;
 assert(run().first&&owner_reads==reads+1);std::cout<<"Actual production live Field callback: exact leaf/opaque/Class identity, wrong VM/root/opaque and foreign leaf reject before ArtField root dereference PASS\n";
}
