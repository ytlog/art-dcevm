#include "platform/android13/owned_calibration.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/thread_suspend_scope.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/field_copy_metadata_abi.h"
#include "platform/android13/validation_metadata_abi.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/handle_root_scope.h"
#include "platform/android13/field_copy_storage_abi.h"
#include "platform/symbols/android13.h"
#include <cstring>
#include <string>
#include <vector>
namespace artdcevm::android13 {
namespace {
bool Fail(std::string* error,const char* message){if(error)*error=message;return false;}
struct Local {JNIEnv* env;jobject value;~Local(){if(value)env->DeleteLocalRef(value);}};
struct Text {jvmtiEnv* env;char* value=nullptr;~Text(){if(value)env->Deallocate(reinterpret_cast<unsigned char*>(value));}};
template<class T>T Read(const void* p,size_t at){T value;std::memcpy(&value,static_cast<const unsigned char*>(p)+at,sizeof(value));return value;}
bool Begin(const DeviceProfile& profile,JNIEnv* env,jobject fixture,std::string* error){
 if(error)error->clear();return profile.Matched()&&env&&fixture?true:Fail(error,"Owned calibration requires matched identity, JNI and a rooted fixture");}
struct Field {const char* name;const char* signature;bool is_static;};
struct Value {uint64_t bits=0;jobject reference=nullptr;};
template<class T>uint64_t Bits(T value){uint64_t bits=0;std::memcpy(&bits,&value,sizeof(value));return bits;}
template<class T>T Unpack(uint64_t bits){T value;std::memcpy(&value,&bits,sizeof(value));return value;}
Value Get(JNIEnv* env,jobject object,jclass klass,jfieldID id,const Field& field){Value value;
#define GET(code,Type,type) case code:value.bits=Bits(field.is_static?env->GetStatic##Type##Field(klass,id):env->Get##Type##Field(object,id));break;
 switch(field.signature[0]){GET('Z',Boolean,jboolean) GET('B',Byte,jbyte) GET('C',Char,jchar) GET('S',Short,jshort) GET('I',Int,jint) GET('J',Long,jlong) GET('F',Float,jfloat) GET('D',Double,jdouble)
 case '[':case 'L':value.reference=field.is_static?env->GetStaticObjectField(klass,id):env->GetObjectField(object,id);break;}
#undef GET
 return value;}
void Set(JNIEnv* env,jobject object,jclass klass,jfieldID id,const Field& field,const Value& value){
#define SET(code,Type,type) case code:if(field.is_static)env->SetStatic##Type##Field(klass,id,Unpack<type>(value.bits));else env->Set##Type##Field(object,id,Unpack<type>(value.bits));break;
 switch(field.signature[0]){SET('Z',Boolean,jboolean) SET('B',Byte,jbyte) SET('C',Char,jchar) SET('S',Short,jshort) SET('I',Int,jint) SET('J',Long,jlong) SET('F',Float,jfloat) SET('D',Double,jdouble)
 case '[':case 'L':if(field.is_static)env->SetStaticObjectField(klass,id,value.reference);else env->SetObjectField(object,id,value.reference);break;}
#undef SET
}
constexpr Field kStorageFields[]={
 // Explicit Java names avoid a second schema decoder as oracle.
 {"booleanValue","Z",false},{"byteValue","B",false},{"charValue","C",false},{"shortValue","S",false},
 {"intValue","I",false},{"longValue","J",false},{"floatValue","F",false},{"doubleValue","D",false},
 {"referenceValue","Ljava/lang/Object;",false},{"volatileIntValue","I",false},{"volatileLongValue","J",false},{"volatileReferenceValue","Ljava/lang/Object;",false},
 {"staticBooleanValue","Z",true},{"staticByteValue","B",true},{"staticCharValue","C",true},{"staticShortValue","S",true},
 {"staticIntValue","I",true},{"staticLongValue","J",true},{"staticFloatValue","F",true},{"staticDoubleValue","D",true},
 {"staticReferenceValue","Ljava/lang/Object;",true},{"staticVolatileIntValue","I",true},{"staticVolatileLongValue","J",true},{"staticVolatileReferenceValue","Ljava/lang/Object;",true},
 {"arrayValue","[Ljava/lang/Object;",false},{"volatileArrayValue","[Ljava/lang/Object;",false},{"staticArrayValue","[Ljava/lang/Object;",true},{"staticVolatileArrayValue","[Ljava/lang/Object;",true}
};
bool MatchValue(JNIEnv* env,jobject owner,jfieldID id,const Field& field,const Value& expected,const OwnedCalibrationReaders& readers){
 NativeObjectAccess access(env);if(!access.Supported())return false;
 void* object=access.Decode(owner);void* native_field=readers.field_id(id);if(!object||!native_field)return false;
 uint64_t observed=readers.fields.read(object,native_field,field.signature[0]);
 return (field.signature[0]=='L'||field.signature[0]=='[')?observed==reinterpret_cast<uintptr_t>(access.Decode(expected.reference)):observed==expected.bits;
}
void OwnedSet(JNIEnv* env,jobject owner,jfieldID id,const Field& field,const Value& value,const OwnedCalibrationReaders& readers){
 NativeObjectAccess access(env);uint64_t bits=(field.signature[0]=='L'||field.signature[0]=='[')?reinterpret_cast<uintptr_t>(access.Decode(value.reference)):value.bits;
 readers.fields.write(access.Decode(owner),readers.field_id(id),field.signature[0],bits);
}
// Capture public metadata before borrowing managed pointers. JNI locals root
// classes across every public call; the later ART scope reloads those references.
bool CalibrateCopyMetadata(JNIEnv* env,jvmtiEnv* ti,jclass klass,const char* signature,
                           const OwnedCalibrationReaders& r,std::string* error){
 Local parent{env,env->GetSuperclass(klass)};
 if(env->ExceptionCheck())return Fail(error,"JNI calibration superclass unavailable");
 jobject loader_object=nullptr;
 if(ti->GetClassLoader(klass,&loader_object)!=JVMTI_ERROR_NONE)return Fail(error,"JVMTI calibration class loader unavailable");
 Local loader{env,loader_object};
 jint count=0;jfieldID* ids=nullptr;
 struct Fields {jvmtiEnv* ti;jfieldID*& ids;~Fields(){if(ids)ti->Deallocate(reinterpret_cast<unsigned char*>(ids));}} allocated{ti,ids};
 if(ti->GetClassFields(klass,&count,&ids)!=JVMTI_ERROR_NONE||count<0||(count&&!ids))return Fail(error,"JVMTI calibration field enumeration unavailable");
 struct PublicField {jfieldID id;std::string name,descriptor;jint modifiers;};
 std::vector<PublicField> public_fields;public_fields.reserve(static_cast<size_t>(count));
 for(jint i=0;i<count;++i){
  Text name{ti},descriptor{ti};jint modifiers=0;jclass owner=nullptr;
  if(ti->GetFieldName(klass,ids[i],&name.value,&descriptor.value,nullptr)!=JVMTI_ERROR_NONE||
     ti->GetFieldModifiers(klass,ids[i],&modifiers)!=JVMTI_ERROR_NONE||
     ti->GetFieldDeclaringClass(klass,ids[i],&owner)!=JVMTI_ERROR_NONE)return Fail(error,"JVMTI copied-field metadata unavailable");
  Local declaring{env,owner};
  if(!name.value||!descriptor.value||!owner||!env->IsSameObject(owner,klass))return Fail(error,"JVMTI copied-field owner mismatch");
  public_fields.push_back({ids[i],name.value,descriptor.value,modifiers});
 }
 {NativeObjectAccess access(env);if(!access.Supported())return Fail(error,"Copy metadata native access unsupported");
 HandleRootScope roots(access.Self(),3);roots.Set(0,access.Decode(klass));roots.Set(1,access.Decode(parent.value));roots.Set(2,access.Decode(loader.value));
 for(bool is_static:{false,true}){
  size_t expected_count=0;for(const auto& field:public_fields)if(((field.modifiers&8)!=0)==is_static)++expected_count;
  const auto view=r.fields.klass(roots.Get(0),is_static);
  if(view.parent!=roots.Get(1)||view.loader!=roots.Get(2)||!view.descriptor||std::strcmp(view.descriptor,signature)||
     view.is_object!=(parent.value==nullptr)||view.field_count!=expected_count)return Fail(error,"Owned copy class disagrees with JNI/JVMTI metadata");
  std::vector<bool> seen(public_fields.size(),false);
  for(size_t index=0;index<expected_count;++index){
   const auto observed=r.fields.field(roots.Get(0),is_static,index);
   if(!observed.field||!observed.name||!observed.descriptor)return Fail(error,"Owned copied-field metadata missing");
   bool found=false;
   for(size_t i=0;i<public_fields.size();++i){const auto& expected=public_fields[i];
    if(((expected.modifiers&8)!=0)!=is_static||std::strcmp(observed.name,expected.name.c_str())||std::strcmp(observed.descriptor,expected.descriptor.c_str()))continue;
    if(seen[i]||observed.field!=r.field_id(expected.id)||
       (Read<uint32_t>(observed.field,offsetof(field_copy_abi::ArtField,access_flags))&0xffffu)!=static_cast<uint32_t>(expected.modifiers))return Fail(error,"Owned copied-field identity or modifiers mismatch");
    seen[i]=true;found=true;break;
   }
   if(!found)return Fail(error,"Owned copied-field name or type disagrees with JVMTI");
  }
 }
 }
 if(parent.value){
  Text parent_signature{ti};auto parent_type=static_cast<jclass>(parent.value);
  if(ti->GetClassSignature(parent_type,&parent_signature.value,nullptr)!=JVMTI_ERROR_NONE||!parent_signature.value)return Fail(error,"JVMTI superclass signature unavailable");
  return CalibrateCopyMetadata(env,ti,parent_type,parent_signature.value,r,error);
 }
 return true;
}

}
bool CalibrateOwnedMembers(const DeviceProfile& profile,HookEngine& hooks,JNIEnv* env,jvmtiEnv* ti,jobject fixture,
 const runtime_access_abi::Services& services,const OwnedCalibrationReaders& r,std::string* error){
 if(!Begin(profile,env,fixture,error))return false;
 if(!ti||!services.class_linker||!services.jni_id_manager||!r.method_id||!r.field_id||!r.reflected_field||!r.reflected_method||!r.canonical_method||!r.fields.klass||!r.fields.field)return Fail(error,"Incomplete owned member calibration bindings");
 using Lookup=void*(*)(void*,void*,const char*,void*);using DecodeMethod=void*(*)(void*,jmethodID);using DecodeField=void*(*)(void*,jfieldID);using Canonical=void*(*)(void*,size_t);
 auto lookup=reinterpret_cast<Lookup>(hooks.FindSymbol("libart.so",cas::kLookupClass));
 auto method=reinterpret_cast<DecodeMethod>(hooks.FindSymbol("libart.so",runtime_access_abi::kDecodeMethodId));
 auto field=reinterpret_cast<DecodeField>(hooks.FindSymbol("libart.so",runtime_access_abi::kDecodeFieldId));
 auto canonical=reinterpret_cast<Canonical>(hooks.FindSymbol("libart.so",cas::kGetCanonicalMethod));
 if(!lookup||!method||!field||!canonical)return Fail(error,"Missing named owned calibration ART functions");
 Local type{env,env->GetObjectClass(fixture)};if(!type.value||env->ExceptionCheck())return Fail(error,"Calibration class unavailable");auto klass=static_cast<jclass>(type.value);
 Text signature{ti};if(ti->GetClassSignature(klass,&signature.value,nullptr)!=JVMTI_ERROR_NONE||!signature.value)return Fail(error,"JVMTI calibration class signature failed");
 if(!CalibrateCopyMetadata(env,ti,klass,signature.value,r,error))return false;
 {NativeObjectAccess access(env);if(!access.Supported())return Fail(error,"Calibration native access unsupported");HandleRootScope roots(access.Self(),2);roots.Set(0,access.Decode(klass));
  roots.Set(1,ReadHeapReference(roots.Get(0),offsetof(validation_abi::ClassPrefix,class_loader)));
  void* found=lookup(services.class_linker,access.Self(),signature.value,roots.Get(1));
  if(found!=roots.Get(0))return Fail(error,"Named ClassLinker disagrees with JNI calibration class");}
 for(const auto& spec: {Field{"value","I",false},Field{"wide","J",false},Field{"counter","I",true}}){
  jfieldID id=spec.is_static?env->GetStaticFieldID(klass,spec.name,spec.signature):env->GetFieldID(klass,spec.name,spec.signature);
  if(!id||env->ExceptionCheck())return Fail(error,"Calibration JNI field lookup failed");
  Text name{ti},descriptor{ti};jint modifiers=0;jclass owner=nullptr;
  if(ti->GetFieldName(klass,id,&name.value,&descriptor.value,nullptr)!=JVMTI_ERROR_NONE||ti->GetFieldModifiers(klass,id,&modifiers)!=JVMTI_ERROR_NONE||ti->GetFieldDeclaringClass(klass,id,&owner)!=JVMTI_ERROR_NONE)return Fail(error,"JVMTI field identity query failed");
  Local declaring{env,owner};if(!name.value||!descriptor.value||std::strcmp(name.value,spec.name)||std::strcmp(descriptor.value,spec.signature)||!env->IsSameObject(owner,klass))return Fail(error,"JVMTI field identity mismatch");
  Local reflected{env,env->ToReflectedField(klass,id,spec.is_static)};if(!reflected.value||env->ExceptionCheck()||env->FromReflectedField(reflected.value)!=id)return Fail(error,"JNI reflected field roundtrip failed");
  NativeObjectAccess access(env);void* decoded=r.field_id(id);void* named=field(services.jni_id_manager,id);
  if(!decoded||decoded!=named||r.reflected_field(access.Decode(reflected.value))!=decoded||r.field_id(reinterpret_cast<jfieldID>(decoded))!=decoded||
     (Read<uint32_t>(decoded,offsetof(field_copy_abi::ArtField,access_flags))&0xffffu)!=static_cast<uint32_t>(modifiers))return Fail(error,"Owned field decode disagrees with JNI/JVMTI/named ART");
 }
 jmethodID id=env->GetMethodID(klass,"compute","()I");if(!id||env->ExceptionCheck())return Fail(error,"Calibration JNI method lookup failed");
 Text name{ti},descriptor{ti};jint modifiers=0;jclass owner=nullptr;
 if(ti->GetMethodName(id,&name.value,&descriptor.value,nullptr)!=JVMTI_ERROR_NONE||ti->GetMethodModifiers(id,&modifiers)!=JVMTI_ERROR_NONE||ti->GetMethodDeclaringClass(id,&owner)!=JVMTI_ERROR_NONE)return Fail(error,"JVMTI method identity query failed");
 Local declaring{env,owner};Local reflected{env,env->ToReflectedMethod(klass,id,JNI_FALSE)};
 if(!name.value||!descriptor.value||std::strcmp(name.value,"compute")||std::strcmp(descriptor.value,"()I")||!env->IsSameObject(owner,klass)||!reflected.value||env->ExceptionCheck()||env->FromReflectedMethod(reflected.value)!=id)return Fail(error,"JNI/JVMTI method identity mismatch");
 {NativeObjectAccess access(env);void* decoded=r.method_id(id);
  if(!decoded||decoded!=method(services.jni_id_manager,id)||r.reflected_method(env,reflected.value)!=decoded||r.canonical_method(decoded)!=canonical(decoded,8)||
     r.method_id(reinterpret_cast<jmethodID>(decoded))!=decoded||(Read<uint32_t>(decoded,offsetof(art_method_abi::ArtMethod,access_flags))&0xffffu)!=static_cast<uint32_t>(modifiers))return Fail(error,"Owned method decode disagrees with JNI/JVMTI/named ART");}
 return r.method_id(nullptr)==nullptr&&r.field_id(nullptr)==nullptr?true:Fail(error,"Owned JNI null ID decode mismatch");
}
bool CalibrateOwnedFieldStorage(const DeviceProfile& profile,JNIEnv* env,jvmtiEnv* ti,jobject fixture,const OwnedCalibrationReaders& r,std::string* error){
 if(!Begin(profile,env,fixture,error))return false;
 if(!ti||!r.field_id||!r.fields.read||!r.fields.write)return Fail(error,"Incomplete owned storage bindings");
 Local type{env,env->GetObjectClass(fixture)};if(!type.value||env->ExceptionCheck())return Fail(error,"Storage fixture class unavailable");auto klass=static_cast<jclass>(type.value);
 Local temporary{env,env->AllocObject(klass)};Local reference{env,env->NewStringUTF("owned-storage-reference")};Local object_type{env,env->FindClass("java/lang/Object")};Local array{env,object_type.value?env->NewObjectArray(1,static_cast<jclass>(object_type.value),nullptr):nullptr};if(!temporary.value||!reference.value||!array.value||env->ExceptionCheck())return Fail(error,"Storage temporary allocation failed");
 for(const auto& spec:kStorageFields){
  jfieldID id=spec.is_static?env->GetStaticFieldID(klass,spec.name,spec.signature):env->GetFieldID(klass,spec.name,spec.signature);if(!id||env->ExceptionCheck())return Fail(error,"Storage field lookup failed");
  jint modifiers=0;if(ti->GetFieldModifiers(klass,id,&modifiers)!=JVMTI_ERROR_NONE)return Fail(error,"JVMTI storage field modifiers unavailable");
  {NativeObjectAccess access(env);void* field=r.field_id(id);if(!field||(Read<uint32_t>(field,offsetof(field_copy_abi::ArtField,access_flags))&0xffffu)!=static_cast<uint32_t>(modifiers))return Fail(error,"Owned storage static/volatile flags disagree with JVMTI");}
  Value original=Get(env,fixture,klass,id,spec);Local original_reference{env,original.reference};
  if(env->ExceptionCheck()||!MatchValue(env,spec.is_static?type.value:fixture,id,spec,original,r))return Fail(error,"Owned field read disagrees with public JNI");
  // Static restoration is unconditional, including failures during verification.
  struct Restore {JNIEnv* env;jobject object;jclass klass;jfieldID id;const Field& field;const Value& value;~Restore(){if(field.is_static){jthrowable pending=env->ExceptionCheck()?env->ExceptionOccurred():nullptr;if(pending)env->ExceptionClear();Set(env,object,klass,id,field,value);if(pending){if(env->ExceptionCheck())env->ExceptionClear();env->Throw(pending);env->DeleteLocalRef(pending);}}}} restore{env,fixture,klass,id,spec,original};
  Value test=original;test.reference=spec.signature[0]=='['?array.value:reference.value;test.bits=spec.signature[0]=='Z'?1:0xfedcba9876543210ULL;
  jobject owner=spec.is_static?type.value:temporary.value;
  for(unsigned phase=0;phase<2;++phase){
   // JNI initializes an independent value; the production getter must read it.
   Set(env,temporary.value,klass,id,spec,test);Value expected=Get(env,temporary.value,klass,id,spec);Local expected_ref{env,expected.reference};
   if(env->ExceptionCheck()||!MatchValue(env,owner,id,spec,expected,r))return Fail(error,"Owned storage getter disagrees with JNI setter");
   test.bits=phase?0:0x87654321abcdef01ULL;test.reference=phase?nullptr:(spec.signature[0]=='['?array.value:reference.value);
   if(spec.signature[0]=='Z')test.bits=phase?0:1;
   OwnedSet(env,owner,id,spec,test,r);Value observed=Get(env,temporary.value,klass,id,spec);Local observed_ref{env,observed.reference};
   const auto width=spec.signature[0]=='Z'||spec.signature[0]=='B'?1:spec.signature[0]=='C'||spec.signature[0]=='S'?2:spec.signature[0]=='I'||spec.signature[0]=='F'?4:8;
   uint64_t mask=width==8?UINT64_MAX:(uint64_t{1}<<(width*8))-1;
   if(env->ExceptionCheck()||((spec.signature[0]=='L'||spec.signature[0]=='[')?!env->IsSameObject(observed.reference,test.reference):observed.bits!=(test.bits&mask)))return Fail(error,"Owned storage setter disagrees with JNI getter");
  }
 }
 return true;
}
bool CalibrateOwnedCardTable(const DeviceProfile& profile,JNIEnv* env,jobject fixture,const OwnedCalibrationReaders& r,std::string* error){
 if(!Begin(profile,env,fixture,error))return false;
 if(!r.fields.barrier)return Fail(error,"Missing owned card barrier");
 Local type{env,env->GetObjectClass(fixture)};if(!type.value||env->ExceptionCheck())return Fail(error,"Card fixture class unavailable");
 auto klass=static_cast<jclass>(type.value);jfieldID field=env->GetFieldID(klass,"referenceValue","Ljava/lang/Object;");
 Local value{env,env->NewStringUTF("owned-card-reference")};
 if(!field||!value.value||env->ExceptionCheck())return Fail(error,"Card fixture reference unavailable");
 for(bool owned:{false,true}){
  bool verified=false;
  for(unsigned attempt=0;attempt<64&&!verified;++attempt){
   Local owner{env,env->AllocObject(klass)};if(!owner.value||env->ExceptionCheck())return Fail(error,"Card temporary allocation failed");
   uintptr_t before=0;
   {NativeObjectAccess access(env);if(!access.Supported())return Fail(error,"Card native access unsupported");
    void* raw_owner=access.Decode(owner.value);before=reinterpret_cast<uintptr_t>(raw_owner);
    using namespace field_copy_storage_abi;
    const uintptr_t biased=Read<uintptr_t>(access.Self(),kThreadCardTableOffset);
    auto* card=reinterpret_cast<uint8_t*>(biased+(before>>kCardShift));
    if(__atomic_load_n(card,__ATOMIC_RELAXED)!=0)continue;
    if(owned){r.fields.barrier(raw_owner);if(__atomic_load_n(card,__ATOMIC_RELAXED)!=kCardDirty)return Fail(error,"Owned card barrier did not dirty a clean temporary owner card");verified=true;continue;}
   }
   // This is the independent device ART store, entered through public JNI in
   // its normal thread state. It may suspend; do not retain borrowed pointers.
   env->SetObjectField(owner.value,field,value.value);
   if(env->ExceptionCheck())return Fail(error,"JNI card oracle store threw");
   {NativeObjectAccess access(env);void* raw_owner=access.Decode(owner.value);
    if(reinterpret_cast<uintptr_t>(raw_owner)!=before)continue;
    using namespace field_copy_storage_abi;
    const uintptr_t biased=Read<uintptr_t>(access.Self(),kThreadCardTableOffset);
    auto* card=reinterpret_cast<uint8_t*>(biased+(before>>kCardShift));
    if(__atomic_load_n(card,__ATOMIC_RELAXED)!=kCardDirty)return Fail(error,"Thread card pointer/shift/dirty value disagrees with public JNI store");}
   Local actual{env,env->GetObjectField(owner.value,field)};
   if(env->ExceptionCheck()||!env->IsSameObject(actual.value,value.value))return Fail(error,"JNI card oracle reference roundtrip failed");
   verified=true;
  }
  if(!verified)return Fail(error,"Card calibration inconclusive: no stable clean temporary owner card observed");
 }
 return true;
}
bool CalibrateOwnedMonitorTransfer(const DeviceProfile& profile,JNIEnv* env,jobject fixture,const FieldCopyAccess& fields,std::string* error){
 if(!Begin(profile,env,fixture,error))return false;if(!fields.transfer_monitor)return Fail(error,"Missing owned monitor transfer");
 Local type{env,env->GetObjectClass(fixture)};Local system{env,env->FindClass("java/lang/System")};if(!type.value||!system.value||env->ExceptionCheck())return Fail(error,"Monitor fixture classes unavailable");
 Local source{env,env->AllocObject(static_cast<jclass>(type.value))},target{env,env->AllocObject(static_cast<jclass>(type.value))};
 jmethodID hash=env->GetStaticMethodID(static_cast<jclass>(system.value),"identityHashCode","(Ljava/lang/Object;)I");if(!source.value||!target.value||!hash||env->ExceptionCheck())return Fail(error,"Monitor fixture allocation failed");
 jint expected=env->CallStaticIntMethod(static_cast<jclass>(system.value),hash,source.value);if(env->ExceptionCheck())return Fail(error,"Monitor hash initialization failed");
 {NativeObjectAccess access(env);if(!access.Supported())return Fail(error,"Monitor native access unsupported");HandleRootScope roots(access.Self(),2);roots.Set(0,access.Decode(source.value));roots.Set(1,access.Decode(target.value));NativeSuspendScope native(access.Self());if(!native.Supported())return Fail(error,"Monitor native suspension unsupported");
  OwnedSuspendAll suspended("ART DCEVM owned monitor calibration");
  // Scope slots were published while Runnable and updated by any entry GC.
  // Reading them under exclusive ownership needs no JNI/Thread decoder.
  void* raw_source=roots.Get(0);void* raw_target=roots.Get(1);
  constexpr size_t offset=field_copy_storage_abi::kMonitorOffset;
  // Locked lock_word.h: one Baker bit at 28 and one mark bit at 29. Keep
  // each live calibration object's collector state when the experiment ends.
  constexpr uint32_t gc_mask=0x30000000u;
  const uint32_t source_bits=Read<uint32_t>(raw_source,offset),target_bits=Read<uint32_t>(raw_target,offset);
  fields.transfer_monitor(raw_target,raw_source);
  const bool transferred=Read<uint32_t>(raw_target,offset)==source_bits&&Read<uint32_t>(raw_source,offset)==field_copy_storage_abi::kDefaultLockWord;
  const uint32_t target_with_own_gc=(source_bits&~gc_mask)|(target_bits&gc_mask);
  std::memcpy(static_cast<unsigned char*>(raw_source)+offset,&source_bits,4);
  std::memcpy(static_cast<unsigned char*>(raw_target)+offset,&target_with_own_gc,4);
  if(!transferred)return Fail(error,"Owned monitor transfer disagrees with managed lock words");}
 if(env->CallStaticIntMethod(static_cast<jclass>(system.value),hash,target.value)!=expected||env->ExceptionCheck())return Fail(error,"Owned monitor transfer lost public identity hash");
 for(jobject object:{source.value,target.value})if(env->MonitorEnter(object)!=JNI_OK||env->MonitorExit(object)!=JNI_OK||env->ExceptionCheck())return Fail(error,"Owned monitor transfer broke managed synchronization");
 return true;
}
}
