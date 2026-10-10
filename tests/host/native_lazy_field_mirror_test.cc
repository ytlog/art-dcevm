#include "platform/common/native_lazy_field_mirror.h"
#include <cassert>
#include <cstring>
#include <memory>
#include <iostream>
using namespace artdcevm;
namespace lazy=native_lazy_field_mirror;
struct Class{};struct Field {Class* owner;bool statik;bool missing_type=false;};struct Mirror {Field* field;};
static Class a,b,mirror_class;static int self,owner;static bool pending=false,oom=false,wrong_factory=false,foreign_context=false;
static bool moving_root=false,moved=false,factory_null=false;
static bool skip_scope=false,local_null=false,global_null=false,mirror_leaf_wrong=false;
static int oracle_calls=0,oracle_fail=0,oracle_pending=0,decode_calls=0,decode_fail=0,decode_mismatch=0,declaring_calls=0,declaring_wrong=0;
static size_t globals=0,calls=0,local_calls=0,public_calls=0;static std::vector<std::unique_ptr<Mirror>> mirrors;
static Field first{&a,false},second{&a,true},unresolved{&b,false,true};static Mirror m1{&first},m2{&second};
static native_field_factory_observer::Return Factory(void* thread,void* raw,bool force){assert(thread==&self&&!force);++calls;
 if(oom){pending=true;return {0};}if(factory_null)return {0};auto* f=static_cast<Field*>(raw);
 // Original source force=false tolerates its own missing-type resolution and
 // clears that resolution exception internally. The service never clears it.
 if(f->missing_type){pending=true;pending=false;}
 if(moving_root){f->owner=&a;moved=true;}
 auto m=std::make_unique<Mirror>();m->field=wrong_factory?&second:f;auto p=m.get();mirrors.push_back(std::move(m));return {uintptr_t(p)};
}
static jobject Local(JNIEnv*,void* m){assert(m);++local_calls;if(local_null)return nullptr;return reinterpret_cast<jobject>(m);}
static jobject JNICALL Global(JNIEnv*,jobject p){assert(!pending);if(global_null&&p!=reinterpret_cast<jobject>(&b))return nullptr;if(p)++globals;return p;}
static void JNICALL DeleteGlobal(JNIEnv*,jobject p){if(p){assert(globals);--globals;}}
static void JNICALL DeleteLocal(JNIEnv*,jobject){}
static jboolean JNICALL Exception(JNIEnv*){return pending;}
static jfieldID JNICALL From(JNIEnv*,jobject p){assert(!pending);++public_calls;return reinterpret_cast<jfieldID>(reinterpret_cast<Mirror*>(p)->field);}
static jclass JNICALL GetClass(JNIEnv*,jobject){assert(!pending);++public_calls;return reinterpret_cast<jclass>(&mirror_class);}
static jmethodID JNICALL Method(JNIEnv*,jclass c,const char* n,const char*){assert(!pending&&c==reinterpret_cast<jclass>(&mirror_class));++public_calls;return reinterpret_cast<jmethodID>(!std::strcmp(n,"getDeclaringClass")?1:2);}
static jobject JNICALL Call(JNIEnv*,jobject p,jmethodID id,va_list){assert(!pending&&id==reinterpret_cast<jmethodID>(1));++public_calls;return reinterpret_cast<jobject>(reinterpret_cast<Mirror*>(p)->field->owner);}
static jint JNICALL Int(JNIEnv*,jobject p,jmethodID id,va_list){assert(!pending&&id==reinterpret_cast<jmethodID>(2));++public_calls;return reinterpret_cast<Mirror*>(p)->field->statik?8:0;}
static jboolean JNICALL Same(JNIEnv*,jobject x,jobject y){assert(!pending);++public_calls;return x==y||(moving_root&&moved&&((x==reinterpret_cast<jobject>(&a)&&y==reinterpret_cast<jobject>(&b))||(y==reinterpret_cast<jobject>(&a)&&x==reinterpret_cast<jobject>(&b))));}
class Hooks:public HookEngine {public:bool legacy=false,absent=false,four=false;bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*l,const char*n)override{assert(!std::strcmp(l,"libart.so"));
  if(!std::strcmp(n,"_ZN3art9JNIEnvExt11NewLocalRefEPNS_6mirror6ObjectE"))return reinterpret_cast<void*>(Local);
  if(!std::strcmp(n,"_ZN3art6mirror5Field18CreateFromArtFieldEPNS_6ThreadEPNS_8ArtFieldEb"))return !absent&&!legacy&&!four?reinterpret_cast<void*>(Factory):nullptr;
  if(!std::strcmp(n,"_ZN3art6mirror5Field18CreateFromArtFieldILNS_11PointerSizeE8ELb0EEENS_6ObjPtrIS1_EEPNS_6ThreadEPNS_8ArtFieldEb"))return !absent&&legacy&&!four?reinterpret_cast<void*>(Factory):nullptr;
  if(!std::strcmp(n,"_ZN3art6mirror5Field18CreateFromArtFieldEPNS_6ThreadENS_6HandleINS0_5ClassEEEPNS_8ArtFieldEb"))return four?reinterpret_cast<void*>(Factory):nullptr;
  assert(false);return nullptr;
 }
 void* Install(void*,void*,void**,std::string*)override{assert(false);return nullptr;}bool Remove(void*,std::string*)override{assert(false);return false;}};
static bool Scope(void*,JNIEnv* e,jobject carrier,runnable_jni::Operation op,void*p){assert(carrier);if(skip_scope)return false;runnable_jni::Context c{&self,foreign_context?nullptr:e,&owner};return op(c,p);}
static bool Oracle(void*,const runnable_jni::Context&,jobject p,void**out){++oracle_calls;if(oracle_calls==oracle_pending)pending=true;if(oracle_calls==oracle_fail)return false;*out=moving_root&&moved&&p==reinterpret_cast<jobject>(&b)?reinterpret_cast<jobject>(&a):p;return true;}
static bool Decode(void*,jobject p,void**out){++decode_calls;if(decode_calls==decode_fail)return false;*out=decode_calls==decode_mismatch?reinterpret_cast<jobject>(&a):(moving_root&&moved&&p==reinterpret_cast<jobject>(&b)?reinterpret_cast<jobject>(&a):p);return true;}
static void* FieldId(void*,const runnable_jni::Context&,void*c,jfieldID id){auto*f=reinterpret_cast<Field*>(id);return f->owner==c?f:nullptr;}
static void* MirrorField(void*,void*m){return mirror_leaf_wrong?&first:static_cast<Mirror*>(m)->field;}
static void* Declaring(void*,void*f){++declaring_calls;return declaring_calls==declaring_wrong?&a:static_cast<Field*>(f)->owner;}
static int ti_calls=0,ti_fail=0,ti_pending=0,buffers=0;static bool foreign_decl=false,wrong_text=false,wrong_flags=false,duplicate_native=false,stale_declared=false;static int declared_calls=0;
static bool TiLeaf(){assert(!pending);++ti_calls;if(ti_calls==ti_pending)pending=true;return ti_calls!=ti_fail;}
static jvmtiError JNICALL TiOwner(jvmtiEnv*,jclass c,jfieldID,jclass*out){bool ok=TiLeaf();*out=foreign_decl?reinterpret_cast<jclass>(&a):c;return ok?JVMTI_ERROR_NONE:JVMTI_ERROR_INVALID_FIELDID;}
static char* Buffer(const char* text){++buffers;return strdup(text);}
static jvmtiError JNICALL TiName(jvmtiEnv*,jclass,jfieldID,char**n,char**d,char**g){bool ok=TiLeaf();*n=Buffer(wrong_text?"foreign":"unresolved");*d=Buffer("Lmissing/Type;");*g=Buffer("generic");return ok?JVMTI_ERROR_NONE:JVMTI_ERROR_INVALID_FIELDID;}
static jvmtiError JNICALL TiFlags(jvmtiEnv*,jclass,jfieldID,jint*out){bool ok=TiLeaf();*out=wrong_flags?1:0;return ok?JVMTI_ERROR_NONE:JVMTI_ERROR_INVALID_FIELDID;}
static jvmtiError JNICALL TiFree(jvmtiEnv*,unsigned char*p){assert(buffers>0);--buffers;free(p);return JVMTI_ERROR_NONE;}
static void* Declared(void*,const runnable_jni::Context& c,void* klass,const original_field_declaration::Declaration& d,std::string*){
 assert(c.self==&self);++declared_calls;if(stale_declared&&declared_calls>1)return nullptr;return klass==(moving_root&&moved?&a:&b)&&d.name=="unresolved"&&d.descriptor=="Lmissing/Type;"&&d.flags==0&&!duplicate_native?&unresolved:nullptr;
}
static void MatchTables(){
 // Actual fixed Field array structure, independently supplied named identities.
 struct Table{uint32_t count;abi::arm64::Field fields[3];} table{};table.count=3;
 table.fields[0].access_flags=0;table.fields[1].access_flags=8;table.fields[2].access_flags=0x10000;
 field_array::View view;assert(field_array::Read(field_array::kMerged17,&table,sizeof(table),false,&view));
 field_array::View empty{};assert(!original_field_declaration::Match(empty,&b,original_field_declaration::Declaration{},[](void*,field_copy::CopyFieldView*,void**){assert(false);return false;}));
 original_field_declaration::Declaration d{"hidden","Lmissing/Type;",0};bool duplicate=false,wrong_owner=false,bad_leaf=false;
 const char* failure=nullptr;
 auto leaf=[&](void*f,field_copy::CopyFieldView*t,void**o){*o=wrong_owner?&a:&b;*t={f,f==&table.fields[0]||duplicate?"hidden":"other","Lmissing/Type;"};return !bad_leaf;};
 assert(original_field_declaration::Match(view,&b,d,leaf,&failure)==&table.fields[0]&&!failure);
 duplicate=true;assert(!original_field_declaration::Match(view,&b,d,leaf,&failure)&&!std::strcmp(failure,"native exact declaration duplicated"));duplicate=false;
 wrong_owner=true;assert(!original_field_declaration::Match(view,&b,d,leaf,&failure)&&!std::strcmp(failure,"native field declaring Class differs"));wrong_owner=false;
 bad_leaf=true;assert(!original_field_declaration::Match(view,&b,d,leaf,&failure)&&!std::strcmp(failure,"native field text/owner leaf rejected"));bad_leaf=false;
 d.flags=1;assert(!original_field_declaration::Match(view,&b,d,leaf,&failure)&&!std::strcmp(failure,"native exact name/descriptor/modifiers declaration absent"));d.flags=0;d.descriptor="Lother;";assert(!original_field_declaration::Match(view,&b,d,leaf));
}
int main(){JNINativeInterface_ jni{};jni.NewGlobalRef=Global;jni.DeleteGlobalRef=DeleteGlobal;jni.DeleteLocalRef=DeleteLocal;jni.ExceptionCheck=Exception;jni.FromReflectedField=From;jni.GetObjectClass=GetClass;jni.GetMethodID=Method;jni.CallObjectMethodV=Call;jni.CallIntMethodV=Int;jni.IsSameObject=Same;JNIEnv env{&jni};
 MatchTables();jvmtiInterface_1 ti_table{};ti_table.GetFieldDeclaringClass=TiOwner;ti_table.GetFieldName=TiName;ti_table.GetFieldModifiers=TiFlags;ti_table.Deallocate=TiFree;jvmtiEnv ti{&ti_table};
 lazy::Bindings binding{&owner,true,Scope,Oracle,Decode,FieldId,MirrorField,Declaring};
 binding.declared_field=Declared;
 std::vector<lazy::Oracle> samples{{reinterpret_cast<jobject>(&m1),reinterpret_cast<jclass>(&a),reinterpret_cast<jfieldID>(&first),&first,false},{reinterpret_cast<jobject>(&m2),reinterpret_cast<jclass>(&a),reinterpret_cast<jfieldID>(&second),&second,true}};
 std::string error;
 for(bool legacy:{false,true}){Hooks hooks;hooks.legacy=legacy;lazy::Service service;assert(service.Bind(hooks,binding,&error));jobject result=nullptr;
  assert(!service.Create(&env,reinterpret_cast<jclass>(&a),samples[0].id,false,&result,&error));
  auto duplicate=samples;duplicate[1]=duplicate[0];const auto before=calls;assert(!service.Calibrate(&env,duplicate,&error)&&calls==before);
  assert(service.Calibrate(&env,samples,&error)&&service.Ready()&&calls==before+2&&globals==1);
  assert(service.Create(&env,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error)&&result);env.DeleteGlobalRef(result);assert(globals==1);
  ti_calls=0;assert(service.CreateDeclared(&env,&ti,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error)&&result);env.DeleteGlobalRef(result);assert(!buffers&&ti_calls==3&&globals==1);
  moving_root=true;moved=false;ti_calls=0;assert(service.CreateDeclared(&env,&ti,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error)&&result&&moved);env.DeleteGlobalRef(result);moving_root=false;moved=false;unresolved.owner=&b;assert(globals==1&&!buffers);
  ti_calls=0;assert(!service.CreateDeclared(&env,&ti,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),true,&result,&error)&&!result&&!buffers);
  const auto reset=[](){oracle_calls=decode_calls=declaring_calls=declared_calls=ti_calls=0;};
  const auto rejection=[&](const char* leaf){reset();assert(!service.CreateDeclared(&env,&ti,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error)&&!result&&globals==1&&!buffers);assert(error.find(std::string("leaf=")+leaf)!=std::string::npos);};
  skip_scope=true;rejection("callback-not-entered");skip_scope=false;
  foreign_context=true;rejection("current-Runnable-context");foreign_context=false;
  oracle_fail=1;rejection("declaring-Class-public-root");oracle_fail=0;
  oracle_pending=1;rejection("declaring-Class-root-pending-exception");oracle_pending=0;pending=false;
  decode_fail=1;rejection("declaring-Class-root-codec");decode_fail=0;
  decode_mismatch=1;rejection("declaring-Class-root-codec");decode_mismatch=0;
  duplicate_native=true;rejection("native-declaration-before-factory");duplicate_native=false;
  declaring_wrong=1;rejection("native-declaring-Class-before-factory");declaring_wrong=0;
  local_null=true;rejection("original-local-reference-root");local_null=false;
  global_null=true;rejection("mirror-strong-global-root");global_null=false;
  oracle_fail=2;rejection("mirror-public-root");oracle_fail=0;
  oracle_pending=2;rejection("mirror-public-root-pending-exception");oracle_pending=0;pending=false;
  decode_fail=2;rejection("mirror-root-codec");decode_fail=0;
  decode_mismatch=2;rejection("mirror-root-codec");decode_mismatch=0;
  decode_fail=3;rejection("fresh-declaring-Class-root-codec");decode_fail=0;
  mirror_leaf_wrong=true;rejection("named-GetArtField-round-trip");mirror_leaf_wrong=false;
  declaring_wrong=2;rejection("fresh-native-declaring-Class");declaring_wrong=0;
  stale_declared=true;rejection("native-declaration-after-factory");stale_declared=false;
  factory_null=true;rejection("original-false-factory-null-result");factory_null=false;
  oom=true;rejection("original-false-factory-pending-exception");oom=false;pending=false;
  for(int leaf=1;leaf<=3;++leaf){ti_calls=0;ti_fail=leaf;const auto count=calls;assert(!service.CreateDeclared(&env,&ti,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error)&&!result&&calls==count&&!buffers&&ti_calls==leaf);ti_fail=0;
   ti_calls=0;ti_pending=leaf;assert(!service.CreateDeclared(&env,&ti,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error)&&pending&&!result&&calls==count&&!buffers&&ti_calls==leaf);pending=false;ti_pending=0;}
  for(bool* bad:{&foreign_decl,&wrong_text,&wrong_flags,&duplicate_native}){*bad=true;ti_calls=0;const auto count=calls;assert(!service.CreateDeclared(&env,&ti,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error)&&!result&&calls==count&&!buffers);*bad=false;}
  stale_declared=true;declared_calls=0;ti_calls=0;assert(!service.CreateDeclared(&env,&ti,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error)&&!result&&!buffers&&declared_calls==2&&globals==1);stale_declared=false;
  oom=true;ti_calls=0;assert(!service.CreateDeclared(&env,&ti,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error)&&pending&&!result&&!buffers&&globals==1);oom=false;pending=false;
  wrong_factory=true;ti_calls=0;assert(!service.CreateDeclared(&env,&ti,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error)&&!result&&!buffers);wrong_factory=false;
  assert(!service.Create(&env,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),true,&result,&error)&&globals==1);
  const auto c=calls,p=public_calls;pending=true;assert(!service.Create(&env,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error)&&pending&&calls==c&&public_calls==p);pending=false;
  assert(!service.Create(&env,reinterpret_cast<jclass>(&a),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error)&&calls==c);
  oom=true;assert(!service.Create(&env,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error)&&pending);oom=false;pending=false;
  wrong_factory=true;assert(!service.Create(&env,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error));wrong_factory=false;assert(globals==1);
  foreign_context=true;assert(!service.Create(&env,reinterpret_cast<jclass>(&b),reinterpret_cast<jfieldID>(&unresolved),false,&result,&error));foreign_context=false;
  service.Clear(&env);assert(!globals);
 }
 Hooks absent;absent.absent=true;lazy::Service unsupported;assert(!unsupported.Bind(absent,binding,&error));
 Hooks four;four.four=true;lazy::Service missing;const auto before_four=calls;assert(!missing.Bind(four,binding,&error)&&calls==before_four);
 native_field_factory_observer::Service observer;source_handle_roots::Service uncalibrated;
 native_field_factory_observer::Trace primitive{},optional{};std::array<bool,4> roots{{true,true,true,true}};
 auto incomplete=binding;incomplete.observer=&observer;incomplete.roots=&uncalibrated;incomplete.primitive_trace=&primitive;incomplete.optional_trace=&optional;incomplete.public_roots=&roots;
 lazy::Service unproven;assert(!unproven.Bind(four,incomplete,&error)&&calls==before_four&&!unproven.Ready());
 Hooks hooks;lazy::Service wrong;assert(wrong.Bind(hooks,binding,&error));wrong_factory=true;assert(!wrong.Calibrate(&env,samples,&error)&&!wrong.Ready()&&!globals);wrong_factory=false;
 std::cout<<"Lazy source Field factories: two public/native oracles, opaque ID/owner/static identity, original false missing-type semantics, incoming/OOME preservation, wrong codec/namespace/context rejection and reference cleanup PASS\n";
}
