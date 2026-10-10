#pragma once
#include "platform/common/native_method_handle_receiver_source.h"
#include "platform/common/runnable_jni_bridge.h"
#include <string>
namespace artdcevm::receiver_guard {
struct PublicHandleSample {
 jobject handle=nullptr; // Live local or strong global, independently constructed.
 jobject reflected_member=nullptr; // Original public Method or Field.
 uint32_t kind=UINT32_MAX;
};
struct PublicHandleMemberDecoder {
 void* owner=nullptr;
 // Independent named public-ID decoder, in the real JNI Runnable callback.
 // ALL decodes finish before the observation window/first managed borrow.
 // Use the independently observed named JNI ID decoder. A reflected-field
 // fallback requires contextual_field_id::Proof for this exact Field/id;
 // never cast an opaque JNI ID into ArtField.
 bool(*decode)(void*,const runnable_jni::Context&,jobject,jmethodID,jfieldID,void**)noexcept=nullptr;
};
struct PublicHandleObservationWindow {
 void* owner=nullptr;
 // After the LAST public JNI query/root allocation, before the source callback.
 // Ordinary RefreshMaps belongs here; this does not grant access permission.
 bool(*prepare)(void*)=nullptr;
 // Inside the genuine Runnable callback. Establish the actual entry invocation
 // and matching ClassSource ReadWindow for the WHOLE operation; no JNI or maps
 // refresh after its first native borrow. The operation still checks every leaf.
 bool(*with)(void*,const runnable_jni::Context&,runnable_jni::Operation,void*)=nullptr;
};
// Ordinary stage only. Caller supplies a lifecycle-safe source access and a
// strong Executable carrier for runnable_executor. The helper overrides entry
// with the process's sole calibrated MethodHandle VisitTarget owner. All public
// JNI roots are prepared before borrowing native identities. On any failure,
// output remains unchanged and all temporary JNI references are released;
// pending Java exceptions remain pending. No permanent access lifetime is
// granted by this calibration: the supplied access owner must outlive use.
bool PrepareMethodHandleReceiverSource(JNIEnv*,jobject executable_carrier,
 const std::vector<PublicHandleSample>&,MethodHandleSourceAccess,
 PublicHandleMemberDecoder,PublicHandleObservationWindow,MethodHandleReceiverSource*,std::string* error);
}
