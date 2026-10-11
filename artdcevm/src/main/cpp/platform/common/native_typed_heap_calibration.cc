#include "platform/common/native_typed_heap_calibration.h"
#include <array>
namespace artdcevm::native_typed_heap_calibration {
namespace {bool Fail(std::string*e,const char*s){if(e)*e=s;return false;}
struct Locals{JNIEnv*env;std::array<jobject,6> values{};~Locals(){for(auto r:values)if(r)env->DeleteLocalRef(r);}};
struct Globals{JNIEnv*env;std::array<jobject,7> values{};~Globals(){for(auto r:values)if(r)env->DeleteGlobalRef(r);}};
}
bool Prepare(HookEngine& hooks,JNIEnv* env,jobject fixture,void* heap,void*(*current)(),const modern_field_metadata::FieldMetadata& metadata,const exclusive_owned_roots::Service& exclusive,native_typed_heap_walk::Service* service,std::string*error){
 if(!env||!fixture||!heap||!current||!service||!metadata.Calibrated()||env->ExceptionCheck())return Fail(error,"Typed heap calibration lacks independently owned runtime/fixture");
 if(service->Calibrated())return service->BoundTo(heap,current)||Fail(error,"Typed heap calibrated owner differs");
 Locals local{env};Globals roots{env};
 local.values[0]=env->GetObjectClass(fixture);if(!local.values[0]||env->ExceptionCheck())return false;
 auto klass=static_cast<jclass>(local.values[0]);
 auto instance=env->GetFieldID(klass,"referenceValue","Ljava/lang/Object;");if(!instance||env->ExceptionCheck())return false;
 auto statik=env->GetStaticFieldID(klass,"staticReferenceValue","Ljava/lang/Object;");if(!statik||env->ExceptionCheck())return false;
 local.values[1]=env->ToReflectedField(klass,instance,false);if(!local.values[1]||env->ExceptionCheck())return false;
 local.values[2]=env->ToReflectedField(klass,statik,true);if(!local.values[2]||env->ExceptionCheck())return false;
 local.values[3]=env->GetObjectField(fixture,instance);if(!local.values[3]||env->ExceptionCheck())return false;
 local.values[4]=env->GetStaticObjectField(klass,statik);if(!local.values[4]||env->ExceptionCheck())return false;
 const bool same=env->IsSameObject(local.values[3],local.values[4]);if(env->ExceptionCheck())return false;
 if(same)return Fail(error,"Typed heap existing fixture references are not distinct");
 local.values[5]=env->FindClass("java/lang/Object");if(!local.values[5]||env->ExceptionCheck())return false;
 roots.values[0]=env->NewGlobalRef(fixture);if(!roots.values[0]||env->ExceptionCheck())return false;
 for(size_t i=0;i<5;++i){roots.values[i+1]=env->NewGlobalRef(local.values[i]);if(!roots.values[i+1]||env->ExceptionCheck())return false;}
 auto array=env->NewObjectArray(2,static_cast<jclass>(local.values[5]),nullptr);
 // Own a non-null JNI result before checking its pending exception. Both
 // Object-class and temporary array locals must be released on this path.
 if(array){env->DeleteLocalRef(local.values[5]);local.values[5]=array;}
 if(!array||env->ExceptionCheck())return false;
 env->SetObjectArrayElement(array,0,roots.values[4]);if(env->ExceptionCheck())return false;
 env->SetObjectArrayElement(array,1,roots.values[5]);if(env->ExceptionCheck())return false;
 roots.values[6]=env->NewGlobalRef(array);if(!roots.values[6]||env->ExceptionCheck())return false;
 if(!service->BoundTo(heap,current)){
  struct Binding {HookEngine&hooks;void*heap;void*(*current)();native_typed_heap_walk::Service*service;std::string*error;};
  Binding binding{hooks,heap,current,service,error};
  const auto install=[](const exclusive_owned_roots::Context&context,void*raw){
   auto&request=*static_cast<Binding*>(raw);
   return context.count==1&&context.roots[0]&&request.current()==context.self&&
    request.service->Bind(request.hooks,request.heap,request.current,request.error);
  };
  // Public oracles and strong roots are complete before installing native
  // observers. The sole original exclusive service owns the actual pause;
  // the callback performs no JNI and retains no borrowed root or Thread.
  const jobject binding_roots[]{roots.values[0]};
  if(!exclusive.With(env,binding_roots,1,install,&binding,error))return false;
 }
 if(!service->ArrayCalibrated()&&!service->Calibrate(env,static_cast<jobjectArray>(roots.values[6]),roots.values[4],roots.values[5],exclusive,error))return false;
 if(!service->FieldsCalibrated()&&!service->CalibrateFields(hooks,env,roots.values[0],static_cast<jclass>(roots.values[1]),roots.values[2],roots.values[3],roots.values[4],roots.values[5],metadata,exclusive,error))return false;
 return service->Calibrated();
}
}
