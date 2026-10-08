#include "platform/common/modern_field_storage.h"
#include "platform/common/field_storage.h"
#include "platform/common/method_identity_calibration.h"
#include <vector>
#include <cstring>
namespace artdcevm::modern_field_storage {
namespace {
thread_local const Service* selected=nullptr;
// Fixed AOSP Thread declarations: 11 has 23 tls32 words, 12 has 22,
// 13--17 have 20. Align tls64 (trace + seven RuntimeStats words) to 8.
// Only the independent public-JNI non-dirty-to-dirty oracle selects a candidate.
template<size_t Words>struct ThreadCardPrefix {uint32_t tls32[Words];uint64_t tls64[8];uintptr_t card_table;};
static_assert(offsetof(ThreadCardPrefix<23>,card_table)==160);
static_assert(offsetof(ThreadCardPrefix<22>,card_table)==152);
static_assert(offsetof(ThreadCardPrefix<20>,card_table)==144);
constexpr size_t kCardDeclarations[]={offsetof(ThreadCardPrefix<20>,card_table),
  offsetof(ThreadCardPrefix<22>,card_table),offsetof(ThreadCardPrefix<23>,card_table)};
constexpr size_t kCardShift=10;
constexpr uint8_t kDirty=0x70,kAged=kDirty-1;
bool NonDirty(uint8_t value){return value==0||value==kAged;}
bool Mapped(void* p,size_t n){size_t extent=0;return method_identity::ReadableMappingExtent(p,&extent)&&extent>=n;}
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
struct Roots {
 JNIEnv* env;std::vector<jobject> values;
 ~Roots(){for(auto p:values)env->DeleteGlobalRef(p);}
 size_t Own(jobject p){auto g=env->NewGlobalRef(p);values.push_back(g);return values.size()-1;}
};
struct Value {uint64_t bits=0;size_t reference=SIZE_MAX;};
Value Get(JNIEnv* e,jclass c,jobject o,jfieldID id,char t,bool s,Roots* roots){
 Value v;
#define GET(T,N) case T:{auto x=s?e->GetStatic##N##Field(c,id):e->Get##N##Field(o,id);std::memcpy(&v.bits,&x,sizeof(x));break;}
 switch(t){GET('Z',Boolean) GET('B',Byte) GET('C',Char) GET('S',Short) GET('I',Int) GET('J',Long) GET('F',Float) GET('D',Double)
 case 'L':case '[':{jobject x=s?e->GetStaticObjectField(c,id):e->GetObjectField(o,id);if(x){v.reference=roots->Own(x);e->DeleteLocalRef(x);}break;}}
#undef GET
 return v;
}
void Set(JNIEnv* e,jclass c,jobject o,jfieldID id,char t,bool s,uint64_t bits,jobject ref){
#define SET(T,N,J) case T:{J x;std::memcpy(&x,&bits,sizeof(x));if(s)e->SetStatic##N##Field(c,id,x);else e->Set##N##Field(o,id,x);break;}
 switch(t){SET('Z',Boolean,jboolean) SET('B',Byte,jbyte) SET('C',Char,jchar) SET('S',Short,jshort) SET('I',Int,jint) SET('J',Long,jlong) SET('F',Float,jfloat) SET('D',Double,jdouble)
 case 'L':case '[':if(s)e->SetStaticObjectField(c,id,ref);else e->SetObjectField(o,id,ref);break;}
#undef SET
}
size_t Width(char t){switch(t){case 'Z':case 'B':return 1;case 'C':case 'S':return 2;case 'J':case 'D':return 8;default:return 4;}}
}
struct Adapter {
 static field_copy::CopyClassView Class(void* c,bool s){return selected->metadata_.Class(c,s);}
 static field_copy::CopyFieldView Field(void* c,bool s,size_t i){return selected->metadata_.Field(c,s,i);}
 static void* Reference(void* o,size_t offset,bool v){return selected->metadata_.Reference(o,offset,v);}
 static uint64_t Read(void* o,void* f,char t){return field_storage::Read(o,*static_cast<abi::arm64::Field*>(f),t,Reference);}
 static void Barrier(void* o){field_storage::MarkCard(o,selected->current_,selected->card_offset_,kCardShift,kDirty);}
 static void Write(void* o,void* f,char t,uint64_t bits){field_storage::Write(o,*static_cast<abi::arm64::Field*>(f),t,bits,Barrier);}
 static void Monitor(void* t,void* s){field_storage::TransferMonitor(t,s,4,0);}
};
struct Calibrator {
 Service* service;const modern_field_metadata::FieldOracle* oracle;size_t count;
 std::vector<Value> values;std::vector<abi::arm64::Field*> fields;
 size_t parent_root=SIZE_MAX,loader_root=SIZE_MAX;
 size_t active=0;uint64_t replacement=0;size_t replacement_root=SIZE_MAX;
 enum Mode{Read,Write,CardRead,CardMark,Monitor}mode=Read;
 uintptr_t owner_address=0,card_address=0;uint8_t card_value=255;
 static bool Run(const exclusive_owned_roots::Context& scope,void* opaque){
  auto& a=*static_cast<Calibrator*>(opaque);auto* previous=selected;selected=a.service;
  struct Restore{const Service* p;~Restore(){selected=p;}}restore{previous};
  if(scope.count<2||a.service->current_()!=scope.self)return false;
  if(a.mode==Monitor){
   if(scope.count!=2||!Mapped(scope.roots[0],8)||!Mapped(scope.roots[1],8))return false;
   uint32_t source=0,target=0,moved=0,cleared=1;
   std::memcpy(&source,static_cast<char*>(scope.roots[0])+4,4);
   std::memcpy(&target,static_cast<char*>(scope.roots[1])+4,4);
   Adapter::Monitor(scope.roots[1],scope.roots[0]);
   std::memcpy(&moved,static_cast<char*>(scope.roots[1])+4,4);
   std::memcpy(&cleared,static_cast<char*>(scope.roots[0])+4,4);
   constexpr uint32_t gc=0x30000000;
   uint32_t hash=(source&~gc)|(target&gc);
   std::memcpy(static_cast<char*>(scope.roots[0])+4,&source,4);
   std::memcpy(static_cast<char*>(scope.roots[1])+4,&hash,4);
   return moved==source&&cleared==0;
  }
  if(a.mode==CardRead||a.mode==CardMark){
   void* owner=scope.roots[1];if(!Mapped(scope.self,a.service->card_offset_+8))return false;
   uintptr_t biased=0;std::memcpy(&biased,static_cast<char*>(scope.self)+a.service->card_offset_,8);
   auto* card=reinterpret_cast<uint8_t*>(biased+(reinterpret_cast<uintptr_t>(owner)>>kCardShift));
   if(!Mapped(card,1))return false;
   a.owner_address=reinterpret_cast<uintptr_t>(owner);a.card_address=reinterpret_cast<uintptr_t>(card);
   if(a.mode==CardMark)Adapter::Barrier(owner);
   a.card_value=__atomic_load_n(card,__ATOMIC_RELAXED);return true;
  }
  if(a.mode==Write){auto* f=a.fields[a.active];bool stat=(a.oracle[a.active].modifiers&8)!=0;
   void* owner=scope.roots[stat?0:1];char type=a.oracle[a.active].descriptor[0];
   if(!Mapped(owner,f->offset+Width(type)))return false;
   uint64_t bits=a.replacement_root==SIZE_MAX?a.replacement:reinterpret_cast<uintptr_t>(scope.roots[a.replacement_root]);
   // This source candidate encodes release heap refs as unpoisoned uint32.
   if((type=='L'||type=='[')&&bits>UINT32_MAX)return false;
   Adapter::Write(owner,f,type,bits);return true;
  }
  if(Adapter::Reference(scope.roots[1],0,false)!=scope.roots[0])return false;
  auto identity=a.service->metadata_.Class(scope.roots[0],false);
  if(identity.parent!=(a.parent_root==SIZE_MAX?nullptr:scope.roots[a.parent_root])||
     identity.loader!=(a.loader_root==SIZE_MAX?nullptr:scope.roots[a.loader_root]))return false;
  for(size_t i=0;i<a.count;++i){bool stat=(a.oracle[i].modifiers&8)!=0;void* owner=scope.roots[stat?0:1];
   auto view=a.service->metadata_.Class(scope.roots[0],stat);void* native=nullptr;
   for(size_t j=0;j<view.field_count;++j){auto f=a.service->metadata_.Field(scope.roots[0],stat,j);
    if(f.name&&f.descriptor&&!std::strcmp(f.name,a.oracle[i].name)&&!std::strcmp(f.descriptor,a.oracle[i].descriptor))native=f.field;}
   if(!native||!Mapped(native,16))return false;a.fields[i]=static_cast<abi::arm64::Field*>(native);
   char type=a.oracle[i].descriptor[0];if(!Mapped(owner,a.fields[i]->offset+Width(type)))return false;
   auto actual=Adapter::Read(owner,native,type);
   auto expected=(type=='L'||type=='[')?(a.values[i].reference==SIZE_MAX?0:reinterpret_cast<uintptr_t>(scope.roots[a.values[i].reference])):a.values[i].bits;
   if(actual!=expected)return false;
  }return true;
 }
};
ScopedAccess::ScopedAccess(const Service& s):previous_(selected){selected=s.Calibrated()?&s:nullptr;}
ScopedAccess::~ScopedAccess(){selected=previous_;}
field_copy::FieldCopyAccess ScopedAccess::Access()const {
 if(!selected)return {};
 return {Adapter::Class,Adapter::Field,Adapter::Read,Adapter::Write,Adapter::Monitor,Adapter::Barrier};
}
bool Service::Calibrate(HookEngine& hooks,JNIEnv* env,jobject owner,jobject fixture,
 const modern_field_metadata::FieldMetadata& metadata,const modern_field_metadata::FieldOracle* oracle,size_t count,std::string* error){
 if(!env||env->ExceptionCheck()||!owner||!fixture||!metadata.Calibrated()||!oracle||count<2||count>100)
  return Fail(error,"invalid owned storage oracle");
 if(env->GetObjectRefType(owner)!=JNIGlobalRefType||env->GetObjectRefType(fixture)!=JNIGlobalRefType)
  return Fail(error,"storage requires owned strong globals");
 jclass actual=env->GetObjectClass(fixture);bool same=actual&&env->IsSameObject(actual,owner);if(actual)env->DeleteLocalRef(actual);
 if(!same||env->ExceptionCheck())return Fail(error,"fixture class identity mismatch");
 Service candidate;candidate.metadata_=metadata;
 if(!exclusive_owned_roots::Service::Bind(hooks,&candidate.exclusive_,error))return false;
 candidate.current_=reinterpret_cast<decltype(current_)>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
 if(!candidate.current_)return Fail(error,"missing named current thread");
 Roots roots{env,{}};roots.Own(owner);roots.Own(fixture);
 Calibrator a{&candidate,oracle,count,{},std::vector<abi::arm64::Field*>(count)};
 jclass class_type=env->FindClass("java/lang/Class");
 jmethodID superclass=class_type?env->GetMethodID(class_type,"getSuperclass","()Ljava/lang/Class;"):nullptr;
 jmethodID loader=class_type?env->GetMethodID(class_type,"getClassLoader","()Ljava/lang/ClassLoader;"):nullptr;
 if(!superclass||!loader||env->ExceptionCheck())return Fail(error,"public class identity oracle unavailable");
 jobject parent=env->CallObjectMethod(owner,superclass),class_loader=env->CallObjectMethod(owner,loader);
 if(parent){a.parent_root=roots.Own(parent);env->DeleteLocalRef(parent);}
 if(class_loader){a.loader_root=roots.Own(class_loader);env->DeleteLocalRef(class_loader);}
 env->DeleteLocalRef(class_type);if(env->ExceptionCheck())return false;
 std::vector<jfieldID> ids(count);size_t reference_field=SIZE_MAX;
 for(size_t i=0;i<count;++i){ids[i]=env->FromReflectedField(oracle[i].strong_global_field);if(!ids[i]||env->ExceptionCheck())return Fail(error,"public field ID unavailable");
  a.values.push_back(Get(env,static_cast<jclass>(owner),fixture,ids[i],oracle[i].descriptor[0],(oracle[i].modifiers&8)!=0,&roots));
  if(oracle[i].descriptor[0]=='L'&&!(oracle[i].modifiers&8))reference_field=i;
  if(env->ExceptionCheck())return Fail(error,"public storage read failed");}
 if(roots.values.size()>128||!candidate.exclusive_.With(env,roots.values.data(),roots.values.size(),Calibrator::Run,&a,error))return Fail(error,"native/JNI storage bits differ");
 if(reference_field==SIZE_MAX||a.values[reference_field].reference==SIZE_MAX)return Fail(error,"missing instance reference card oracle");
 // JNI's own object store supplies an independent card-table oracle. Require a
 // source-defined non-dirty card; never manufacture one by clearing collector state.
 // Keep the oracle object rooted while asking the real collector to process
 // cards that allocation/JNI bootstrap already dirtied. A GC request itself is
 // not proof: require a fresh native read to observe an actually non-dirty card.
 jclass gc_type=env->FindClass("java/lang/System");
 jmethodID gc_method=gc_type?env->GetStaticMethodID(gc_type,"gc","()V"):nullptr;
 if(!gc_method||env->ExceptionCheck())return Fail(error,"public collector oracle unavailable");
 size_t gc_root=roots.Own(gc_type);env->DeleteLocalRef(gc_type);
 auto fresh_card=[&](){
  a.mode=Calibrator::CardRead;
  bool ok=candidate.exclusive_.With(env,roots.values.data(),roots.values.size(),Calibrator::Run,&a,error);
  if(ok&&a.card_value==kDirty){
   env->CallStaticVoidMethod(static_cast<jclass>(roots.values[gc_root]),gc_method);
   if(env->ExceptionCheck())return false;
   ok=candidate.exclusive_.With(env,roots.values.data(),roots.values.size(),Calibrator::Run,&a,error);
  }
  return ok;
 };
 bool card_proven=false;std::string card_diagnostics;
 for(size_t declaration:kCardDeclarations){
  candidate.card_offset_=declaration;size_t readable=0,clean=0,stores=0;uint8_t last=255;
  for(size_t attempt=0;attempt<32&&!card_proven;++attempt){
  jobject fresh=env->AllocObject(static_cast<jclass>(owner));if(!fresh||env->ExceptionCheck())return Fail(error,"cannot allocate card oracle");
  jobject global=env->NewGlobalRef(fresh);env->DeleteLocalRef(fresh);if(!global)return false;
  jobject saved=roots.values[1];roots.values[1]=global;a.mode=Calibrator::CardRead;
  bool ok=fresh_card();uintptr_t address=a.owner_address;
  if(ok){++readable;last=a.card_value;}
  if(ok&&NonDirty(a.card_value)){
   ++clean;
   jobject value=roots.values[a.values[reference_field].reference];env->SetObjectField(global,ids[reference_field],value);
   ok=!env->ExceptionCheck()&&candidate.exclusive_.With(env,roots.values.data(),roots.values.size(),Calibrator::Run,&a,error);
   if(ok){++stores;last=a.card_value;}
   card_proven=ok&&a.owner_address==address&&a.card_value==kDirty;
   jobject got=env->GetObjectField(global,ids[reference_field]);card_proven=card_proven&&env->IsSameObject(got,value);if(got)env->DeleteLocalRef(got);
  }
  roots.values[1]=saved;env->DeleteGlobalRef(global);if(env->ExceptionCheck())return false;
 }
  card_diagnostics+=" offset="+std::to_string(declaration)+" reads="+std::to_string(readable)+" clean="+std::to_string(clean)+" stores="+std::to_string(stores)+" byte="+std::to_string(last);
  if(card_proven)break;
 }
 if(!card_proven){if(error)*error="no source card declaration matches independent non-dirty-to-dirty JNI oracle:"+card_diagnostics;return false;}
 // A second non-dirty owned object proves the source write barrier itself.
 bool mark_proven=false;
 for(size_t attempt=0;attempt<128&&!mark_proven;++attempt){
  jobject local=env->AllocObject(static_cast<jclass>(owner));
  if(!local||env->ExceptionCheck())return false;
  jobject global=env->NewGlobalRef(local);env->DeleteLocalRef(local);if(!global)return false;
  jobject saved=roots.values[1];roots.values[1]=global;a.mode=Calibrator::CardRead;
  bool ok=fresh_card();
  if(ok&&NonDirty(a.card_value)){uintptr_t address=a.owner_address;a.mode=Calibrator::CardMark;ok=candidate.exclusive_.With(env,roots.values.data(),roots.values.size(),Calibrator::Run,&a,error);mark_proven=ok&&a.owner_address==address&&a.card_value==kDirty;}
  roots.values[1]=saved;env->DeleteGlobalRef(global);
 }
 if(!mark_proven)return Fail(error,"no source-defined non-dirty barrier oracle");
 // Restore every field immediately after each candidate write, including failures.
 for(size_t i=0;i<count;++i){a.active=i;a.mode=Calibrator::Write;char type=oracle[i].descriptor[0];bool stat=(oracle[i].modifiers&8)!=0;
  for(int pass=0;pass<2;++pass){a.replacement=pass?0:0x87654321abcdef01ULL;a.replacement_root=SIZE_MAX;
   if(type=='Z')a.replacement=pass?0:1;
   if((type=='L'||type=='[')&&!pass){
    for(size_t j=0;j<count;++j){
     if(j!=i&&a.values[j].reference!=SIZE_MAX&&!std::strcmp(oracle[i].descriptor,oracle[j].descriptor)&&
        !env->IsSameObject(roots.values[a.values[i].reference],roots.values[a.values[j].reference])){
      a.replacement_root=a.values[j].reference;break;
     }
    }
    if(a.replacement_root==SIZE_MAX)return Fail(error,"missing distinct same-type reference oracle");
   }
   bool ok=candidate.exclusive_.With(env,roots.values.data(),roots.values.size(),Calibrator::Run,&a,error);
   Roots got_roots{env,{}};Value got;if(ok)got=Get(env,static_cast<jclass>(owner),fixture,ids[i],type,stat,&got_roots);
   uint64_t mask=Width(type)==8?UINT64_MAX:((uint64_t{1}<<(Width(type)*8))-1);
   if(type=='L'||type=='['){jobject expected=a.replacement_root==SIZE_MAX?nullptr:roots.values[a.replacement_root];jobject found=got.reference==SIZE_MAX?nullptr:got_roots.values[got.reference];ok=ok&&env->IsSameObject(found,expected);}
   else ok=ok&&got.bits==(a.replacement&mask);
   jobject original=a.values[i].reference==SIZE_MAX?nullptr:roots.values[a.values[i].reference];
   // Preserve an exception raised by the candidate getter while restoring the
   // owned static storage; an input exception was rejected before any mutation.
   jthrowable pending=env->ExceptionOccurred();if(pending)env->ExceptionClear();
   Set(env,static_cast<jclass>(owner),fixture,ids[i],type,stat,a.values[i].bits,original);
   if(pending){if(!env->ExceptionCheck())env->Throw(pending);env->DeleteLocalRef(pending);}
   if(!ok||env->ExceptionCheck())return Fail(error,"native write/public JNI oracle failed");
  }
 }
 jclass system=env->FindClass("java/lang/System");
 jmethodID hash=system?env->GetStaticMethodID(system,"identityHashCode","(Ljava/lang/Object;)I"):nullptr;
 if(!hash||env->ExceptionCheck())return false;
 jobject left=env->AllocObject(static_cast<jclass>(owner)),right=env->AllocObject(static_cast<jclass>(owner));
 if(!left||!right||env->ExceptionCheck())return false;
 Roots monitor{env,{}};monitor.Own(left);monitor.Own(right);
 jint expected=env->CallStaticIntMethod(system,hash,left);a.mode=Calibrator::Monitor;
 bool monitor_ok=!env->ExceptionCheck()&&candidate.exclusive_.With(env,monitor.values.data(),monitor.values.size(),Calibrator::Run,&a,error);
 monitor_ok=monitor_ok&&env->CallStaticIntMethod(system,hash,left)==expected&&env->CallStaticIntMethod(system,hash,right)==expected;
 if(monitor_ok){monitor_ok=env->MonitorEnter(right)==JNI_OK;if(monitor_ok)monitor_ok=env->MonitorExit(right)==JNI_OK;}
 env->DeleteLocalRef(left);env->DeleteLocalRef(right);env->DeleteLocalRef(system);
 if(!monitor_ok||env->ExceptionCheck())return Fail(error,"public monitor migration oracle failed");
 candidate.calibrated_=true;*this=candidate;return true;
}
}
