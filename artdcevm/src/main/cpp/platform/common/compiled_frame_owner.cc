#include "platform/common/compiled_frame_owner.h"
#include "platform/common/compiled_frame_fixture.h"
#include "platform/common/bootstrap_dex_oracle.h"
#include <algorithm>
namespace artdcevm::compiled_frame_fixture {
namespace {
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
}
Owner::~Owner(){
 // Get the actual current attached thread's JNI environment. Never retain a
 // preparation JNIEnv across threads or attach while destroying this owner.
 JNIEnv* env=nullptr;if(!vm_||vm_->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK)return;
 jthrowable pending=env->ExceptionOccurred();if(pending)env->ExceptionClear();
 Close(env,nullptr);
 if(env->ExceptionCheck())env->ExceptionClear();
 for(auto root:reflected_)if(root)env->DeleteGlobalRef(root);
 for(auto root:references_)if(root)env->DeleteGlobalRef(root);
 if(run_)env->DeleteGlobalRef(run_);if(fixture_)env->DeleteGlobalRef(fixture_);
 if(pending){env->Throw(pending);env->DeleteLocalRef(pending);}
}
bool Owner::Resolve(const runnable_jni::Context& context,void* raw){
 auto& o=*static_cast<Owner*>(raw);method_identity::Calibration calibration;
 if(!method_identity::Calibrate(context,o.leaf_,o.public_.data(),o.public_.size(),&calibration,nullptr))return false;
 const abi::arm64::android_common::RunnableJniScope scope{context.self,context.env,context.vm};
 for(size_t i=0;i<o.native_.size();++i){auto method=o.leaf_(scope,o.reflected_[i]);if(!method)return false;o.native_[i]={method,calibration.readable_extents[i],o.public_[i].dex_method_index,o.public_[i].java_modifiers};}
 return true;
}
std::unique_ptr<Owner> Owner::Prepare(HookEngine& hooks,JNIEnv* env,jvmtiEnv* ti,jobjectArray dex,std::string* error){
 if(!env||!ti||!dex||env->ExceptionCheck()){Fail(error,"Missing ordinary compiled fixture input");return nullptr;}
 auto o=std::unique_ptr<Owner>(new Owner);
 if(env->GetJavaVM(&o->vm_)!=JNI_OK||!o->vm_){Fail(error,"Missing compiled fixture native VM");return nullptr;}
 o->leaf_=reinterpret_cast<abi::arm64::android_common::FromReflectedMethod>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kFromReflectedMethod));
 if(!Initialize(hooks)||!o->leaf_||!frame_family_calibration::Service::Bind([&](const char* name){return hooks.FindSymbol("libart.so",name);},&o->service_)||!o->service_.Prepare(&o->prepared_)){Fail(error,"Missing named compiled fixture preparation bindings");return nullptr;}
 // Optional original named Nterp candidates are bound only while ordinary.
 // Actual geometry and complete physical proof, never SDK, select a candidate.
 const stack_visitor_service::Declaration declarations[]={stack_visitor_service::Declaration::kAndroid12,stack_visitor_service::Declaration::kAndroid13To16,stack_visitor_service::Declaration::kAndroid17};
 for(size_t i=0;i<3;++i)for(size_t c=0;c<2;++c)o->nterp_bound_[i*2+c]=native_nterp_frame_proof::Service::Bind(declarations[i],c?native_nterp_frame_reader::CanonicalDeclaration::WithFullRegisterList:native_nterp_frame_reader::CanonicalDeclaration::WithoutFullRegisterList,[&](const char*name){return hooks.FindSymbol("libart.so",name);},&o->nterp_[i*2+c]);
 auto local=env->FindClass("com/github/ytlog/artdcevm/internal/CompiledFrameFixture");
 auto prepare=local?env->GetStaticMethodID(local,"prepare","()Lcom/github/ytlog/artdcevm/internal/CompiledFrameFixture$Run;"):nullptr;
 auto run=prepare?env->CallStaticObjectMethod(local,prepare):nullptr;
 o->fixture_=local?static_cast<jclass>(env->NewGlobalRef(local)):nullptr;if(local)env->DeleteLocalRef(local);
 o->run_=run?env->NewGlobalRef(run):nullptr;if(run)env->DeleteLocalRef(run);
 if(!o->run_||!o->fixture_||env->ExceptionCheck()){Fail(error,"Compiled fixture warmup failed");return nullptr;}
 auto type=env->GetObjectClass(o->run_);
 o->start_=type?env->GetMethodID(type,"start","()V"):nullptr;o->close_=type?env->GetMethodID(type,"close","()V"):nullptr;
 auto methods_id=type?env->GetFieldID(type,"methods","[Ljava/lang/reflect/Method;"):nullptr;
 auto refs_id=type?env->GetFieldID(type,"roots","[Ljava/lang/Object;"):nullptr;
 auto ints_id=type?env->GetFieldID(type,"integers","[I"):nullptr;
 auto methods=methods_id?static_cast<jobjectArray>(env->GetObjectField(o->run_,methods_id)):nullptr;
 auto refs=refs_id?static_cast<jobjectArray>(env->GetObjectField(o->run_,refs_id)):nullptr;
 auto ints=ints_id?static_cast<jintArray>(env->GetObjectField(o->run_,ints_id)):nullptr;
 if(type)env->DeleteLocalRef(type);
 if(!methods||!refs||!ints||!o->start_||!o->close_||env->ExceptionCheck()||env->GetArrayLength(methods)!=5||env->GetArrayLength(refs)!=3||env->GetArrayLength(ints)!=3){Fail(error,"Compiled fixture public inputs are incomplete");return nullptr;}
 for(size_t i=0;i<5;++i){auto m=env->GetObjectArrayElement(methods,i);o->reflected_[i]=m?env->NewGlobalRef(m):nullptr;if(m)env->DeleteLocalRef(m);}
 for(size_t i=0;i<3;++i){auto ref=env->GetObjectArrayElement(refs,i);o->references_[i]=ref?env->NewGlobalRef(ref):nullptr;if(ref)env->DeleteLocalRef(ref);}
 env->GetIntArrayRegion(ints,0,3,o->integers_.data());env->DeleteLocalRef(methods);env->DeleteLocalRef(refs);env->DeleteLocalRef(ints);
 std::vector<std::vector<unsigned char>> files;
 const auto dex_count=env->GetArrayLength(dex);
 for(jsize i=0;i<dex_count;++i){auto array=static_cast<jbyteArray>(env->GetObjectArrayElement(dex,i));if(!array){Fail(error,"Missing compiled fixture original installed DEX");return nullptr;}const auto n=env->GetArrayLength(array);if(n<112||n>64*1024*1024){env->DeleteLocalRef(array);Fail(error,"Compiled fixture DEX extent rejected");return nullptr;}files.emplace_back(n);env->GetByteArrayRegion(array,0,n,reinterpret_cast<jbyte*>(files.back().data()));env->DeleteLocalRef(array);}
 if(env->ExceptionCheck())return nullptr;
 evolution::ClassSchema schema;evolution::MappedDexMemberCounts counts;
 const char* descriptor="Lcom/github/ytlog/artdcevm/internal/CompiledFrameFixture;";
 if(!production_bootstrap::SelectInstalledClass(files,descriptor,&schema,&counts,error))return nullptr;
 const char* names[]={"carrier","middle","inner","awaitGate","marker"};
 const char* signatures[]={"(ILjava/lang/Object;ILjava/lang/Object;ILjava/lang/Object;)I","(ILjava/lang/Object;ILjava/lang/Object;ILjava/lang/Object;)I","(ILjava/lang/Object;ILjava/lang/Object;ILjava/lang/Object;)I","()V","()I"};
 std::array<uint32_t,4> indices{};
 for(size_t i=0;i<5;++i){
  if(!o->reflected_[i]){Fail(error,"Missing strong compiled fixture public Method");return nullptr;}
  auto id=env->FromReflectedMethod(o->reflected_[i]);char* name=nullptr;char* signature=nullptr;jint modifiers=0;
  if(!id||ti->GetMethodName(id,&name,&signature,nullptr)!=JVMTI_ERROR_NONE||ti->GetMethodModifiers(id,&modifiers)!=JVMTI_ERROR_NONE){if(name)ti->Deallocate(reinterpret_cast<unsigned char*>(name));if(signature)ti->Deallocate(reinterpret_cast<unsigned char*>(signature));Fail(error,"Compiled fixture public Method identity unavailable");return nullptr;}
  jclass declaring=nullptr;
  const bool same_owner=ti->GetMethodDeclaringClass(id,&declaring)==JVMTI_ERROR_NONE&&declaring&&env->IsSameObject(declaring,o->fixture_);
  if(declaring)env->DeleteLocalRef(declaring);
  const bool same=same_owner&&name&&signature&&!std::strcmp(name,names[i])&&!std::strcmp(signature,signatures[i]);
  if(name)ti->Deallocate(reinterpret_cast<unsigned char*>(name));if(signature)ti->Deallocate(reinterpret_cast<unsigned char*>(signature));
  const evolution::Member* found=nullptr;
  for(const auto& member:schema.methods)if(member.name==names[i]&&member.descriptor==signatures[i]){if(found){Fail(error,"Ambiguous original DEX compiled fixture Method");return nullptr;}found=&member;}
  if(!same||!found||(found->access&0xffffu)!=(uint32_t(modifiers)&0xffffu)){Fail(error,"Public compiled Method differs from original installed DEX");return nullptr;}
  o->public_[i]={o->reflected_[i],found->dex_index,static_cast<uint32_t>(modifiers)};if(i<4)indices[i]=found->dex_index;
 }
 bool selected=false;
 for(const auto& file:files){evolution::ClassSchema candidate;evolution::MappedDexMemberCounts unused;std::string ignored;if(!production_bootstrap::SelectInstalledClass({file},descriptor,&candidate,&unused,&ignored))continue;if(selected||!compiled_frame_dex_oracle::Read(file.data(),file.size(),indices,&o->code_)){Fail(error,"Actual installed compiled fixture CodeItems are unsupported or ambiguous");return nullptr;}selected=true;}
 if(!selected||runnable_executor::With(env,o->reflected_[0],Resolve,o.get())!=runnable_jni::Result::kSuccess){Fail(error,"Independent native compiled Method prefix calibration failed");return nullptr;}
 return o;
}
bool Owner::RegisterRoots(const runnable_jni::Context& context,source_handle_roots::Pool& roots,std::string* error){
 if(started_||slots_[0]||context.vm!=vm_||!context.self||!roots.ReadyForSourceAccess())return Fail(error,"Compiled fixture roots require the ordinary preparation scope");
 for(size_t i=0;i<3;++i){void* object=nullptr;if(!runnable_executor::DecodeStrongGlobal(context,references_[i],&object)||!object||!(slots_[i]=roots.Add(object,error)))return false;}
 root_owner_=context.self;return true;
}
bool Owner::Start(JNIEnv* env,std::string* error){
 JavaVM* observed=nullptr;
 if(!env||env->GetJavaVM(&observed)!=JNI_OK||observed!=vm_||started_||!slots_[0]||!slots_[1]||!slots_[2]||env->ExceptionCheck())return Fail(error,"Compiled fixture start requires all pre-registered roots");
 // Start may arm the Java/native gate before throwing any Throwable. Retain
 // cleanup responsibility before invoking it, so destruction clears/restores
 // the pending exception while releasing and joining the owned worker.
 started_=true;env->CallVoidMethod(run_,start_);if(env->ExceptionCheck())return Fail(error,"Compiled fixture actual worker did not enter its native gate");
 target_=OrdinaryBlockedTarget();if(!target_){Close(env,nullptr);return Fail(error,"Compiled fixture native target missing after public gate entry");}return true;
}
bool Owner::CalibrateStopped(const native_source_stop::Session& stop,void* self,void* jit,
 const source_handle_roots::StoppedReader& roots,active_frame_recovery_proof::Service::Readable readable,frame_family_calibration::Result* output)const {
 if(!started_||!target_||target_==self||self!=root_owner_||!stop.IsStopped(self)||!roots.FinalRootWindow())return false;
 active_frame_recovery_proof::Witness witness[3][2];active_frame_recovery_proof::PhysicalOracle physical;
 for(size_t i=0;i<3;++i){void* fresh=nullptr;if(!slots_[i]||!roots.ReadStoppedRoot(slots_[i],&fresh)||!fresh||uintptr_t(fresh)>UINT32_MAX)return false;
  witness[i][0]={code_[i].integer_vreg,active_frame_recovery_proof::Kind::kInt,uint32_t(integers_[i])};
  witness[i][1]={code_[i].reference_vreg,active_frame_recovery_proof::Kind::kReference,uint32_t(uintptr_t(fresh))};
  physical.callers[i]={const_cast<void*>(native_[i].method),code_[i].registers,witness[i],2,code_[i].invoke_dex_pc};
 }
 return service_.Calibrate(target_,jit,native_.data(),native_.size(),physical,scratch_.data(),scratch_.size(),readable,prepared_,output)&&stop.IsStopped(self)&&roots.FinalRootWindow();
}
bool Owner::CalibrateNterpStopped(const native_source_stop::Session& stop,void* self,
 const source_handle_roots::StoppedReader&roots,const modern_field_metadata::ReadContext&context,native_nterp_frame_proof::Report*output)const {
 if(!output)return false;*output={};
 if(!started_||!target_||target_==self||self!=root_owner_||!stop.IsStopped(self)||!roots.FinalRootWindow()||!context.owner||!context.extent)return false;
 frame_family_calibration::Result geometry;
 if(!service_.ObserveGeometry(target_,native_.data(),native_.size(),prepared_,&geometry))return false;
 using G=frame_family_calibration::Geometry;
 if(geometry.geometry==G::kLegacy368){output->failure="Nterp-unavailable-source-geometry";return false;}
 const size_t first=geometry.geometry==G::kLegacy376?0:geometry.geometry==G::kCompact344?2:4;
 native_nterp_frame_reader::Witness witnesses[3][2];std::array<native_nterp_frame_proof::Frame,3> frames;
 for(size_t i=0;i<3;++i){void*fresh=nullptr;if(!slots_[i]||!roots.ReadStoppedRoot(slots_[i],&fresh)||!fresh||uintptr_t(fresh)>UINT32_MAX)return false;
  witnesses[i][0]={code_[i].integer_vreg,native_nterp_frame_reader::Kind::kInt,uint32_t(integers_[i])};
  witnesses[i][1]={code_[i].reference_vreg,native_nterp_frame_reader::Kind::kReference,uint32_t(uintptr_t(fresh))};
  frames[i]={{const_cast<void*>(native_[i].method),code_[i].registers,code_[i].outs,code_[i].invoke_dex_pc},witnesses[i],2};
 }
 for(size_t candidate=first;candidate<first+2;++candidate)if(nterp_bound_[candidate]){
  *output=nterp_[candidate].Prove(target_,prepared_.context.get(),frames,context);
  if(output->accepted){if(stop.IsStopped(self)&&roots.FinalRootWindow())return true;output->accepted=false;output->failure="Nterp-stop-or-root-authority-expired";return false;}
 }
 return false;
}
bool Owner::Close(JNIEnv* env,std::string* error){
 if(!started_)return true;JavaVM* observed=nullptr;if(!env||env->GetJavaVM(&observed)!=JNI_OK||observed!=vm_)return Fail(error,"Compiled fixture close requires its actual VM");if(env->ExceptionCheck())return Fail(error,"Compiled fixture close requires exception-preserving ordinary caller");
 env->CallVoidMethod(run_,close_);started_=false;target_=nullptr;
 return !env->ExceptionCheck()||Fail(error,"Compiled fixture gate release or post-release values failed");
}
}
