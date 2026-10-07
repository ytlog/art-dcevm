#define ARTDCEVM_SOURCE_CALIBRATION
#include "platform/android13/instrumentation_listener_abi.h"
#include "art_method-inl.h"
#include "class_linker-inl.h"
#include "instrumentation.h"
#include "mirror/class-inl.h"
#include "mirror/dex_cache-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "interpreter/shadow_frame-inl.h"
#include "dex/compact_dex_file.h"
#include "dex/standard_dex_file.h"
#include <optional>
#include <type_traits>
namespace art {
struct InstrumentationListenerLayoutProbe {
  static void Verify() {
    namespace f=artdcevm::android13::listener_abi;
    static_assert(sizeof(instrumentation::InstrumentationListener)==sizeof(f::Listener));
    static_assert(sizeof(instrumentation::OptionalFrame)==sizeof(f::OptionalFrame));
    static_assert(alignof(instrumentation::OptionalFrame)==alignof(f::OptionalFrame));
    static_assert(std::is_trivially_copyable_v<instrumentation::OptionalFrame>);
    static_assert(sizeof(Handle<mirror::Object>)==sizeof(f::Handle));
    static_assert(sizeof(ManagedStack)==sizeof(f::ManagedStack));
    static_assert(alignof(ManagedStack)==alignof(f::ManagedStack));
    static_assert(Thread::TopOfManagedStackOffset<PointerSize::k64>().Int32Value()==f::kManagedStackOffset);
    static_assert(offsetof(mirror::DexCache,resolved_types_)==offsetof(f::DexCache,types));
    static_assert(offsetof(mirror::DexCache,num_resolved_types_)==offsetof(f::DexCache,num_types));
    static_assert(offsetof(DexFile,is_compact_dex_)==offsetof(f::DexFile,compact));
    static_assert(sizeof(mirror::TypeDexCachePair)==sizeof(f::TypePair));
    static_assert(alignof(mirror::TypeDexCachePair)==alignof(f::TypePair));
    static_assert(offsetof(mirror::TypeDexCachePair,index)==offsetof(f::TypePair,index));
    static_assert(offsetof(mirror::TypeDexCachePair,object)==offsetof(f::TypePair,object));
    static_assert(mirror::DexCache::kDexCacheTypeCacheSize==f::kTypeSlots);
    static_assert(mirror::Class::kPrimitiveTypeMask==f::kPrimitiveTypeMask);
    static_assert((static_cast<uint32_t>(Primitive::kPrimNot)|(Primitive::ComponentSizeShift(Primitive::kPrimNot)<<mirror::Class::kPrimitiveTypeSizeShiftShift))==f::kNonPrimitiveEncoding);
    static_assert((instrumentation::Instrumentation::kFieldRead|instrumentation::Instrumentation::kFieldWritten|instrumentation::Instrumentation::kDexPcMoved)==f::kEvents);
    static_assert(CompactDexFile::CodeItem::kInsnsSizeShift==f::kCompactCountShift);
    static_assert(CompactDexFile::CodeItem::kFlagPreHeaderInsnsSize==f::kCompactCountPreheader);
    static_assert(offsetof(StandardDexFile::CodeItem,insns_size_in_code_units_)==12);
    static_assert(StandardDexFile::CodeItem::InsnsOffset()==16);
    static_assert(CompactDexFile::CodeItem::InsnsOffset()==4);
  }
};
}
extern "C" int ArtDcevmSourceProbeInstrumentationListener(JNIEnv* env,jobject sample,
    artdcevm::android13::listener_abi::Listener* owned,
    artdcevm::android13::listener_abi::InstructionView (*instructions)(void*)) {
  namespace f=artdcevm::android13::listener_abi;
  art::InstrumentationListenerLayoutProbe::Verify();
  art::ScopedObjectAccess soa(env);
  if (!sample || !owned || !instructions) return 1;
  if (art::ManagedStack::TopShadowFrameOffset()!=offsetof(f::ManagedStack,shadow)) return 2;
  auto* listener=reinterpret_cast<art::instrumentation::InstrumentationListener*>(owned);
  art::Handle<art::mirror::Object> null_handle;
  art::MutableHandle<art::mirror::Object> mutable_handle;
  art::JValue result; result.SetJ(0x1984);
  art::instrumentation::OptionalFrame absent;
  // Source virtual calls validate every slot including both default overloads.
  listener->MethodEntered(nullptr,nullptr);
  listener->MethodExited(nullptr,nullptr,absent,mutable_handle);
  listener->MethodExited(nullptr,nullptr,absent,result);
  listener->MethodUnwind(nullptr,null_handle,nullptr,0);
  listener->FieldRead(nullptr,null_handle,nullptr,0,nullptr);
  listener->FieldWritten(nullptr,null_handle,nullptr,0,nullptr,null_handle);
  listener->FieldWritten(nullptr,null_handle,nullptr,0,nullptr,result);
  listener->ExceptionThrown(nullptr,art::Handle<art::mirror::Throwable>());
  listener->ExceptionHandled(nullptr,art::Handle<art::mirror::Throwable>());
  listener->Branch(nullptr,nullptr,0,0);
  art::ShadowFrame* frame=art::ShadowFrame::CreateDeoptimizedFrame(0,nullptr,nullptr,0);
  listener->WatchedFramePop(nullptr,*frame);
  art::ShadowFrame::DeleteDeoptimizedFrame(frame);
  if (result.GetJ()!=0x1984) return 3;
  auto klass=soa.Decode<art::mirror::Object>(sample)->GetClass();
  for (auto& method:klass->GetMethods(art::PointerSize::k64)) {
    const auto actual=instructions(&method);
    auto* code=method.GetCodeItem();
    if (!code) { if (actual.dex || actual.instructions || actual.count) return 4; }
    else {
      const auto source=method.DexInstructionData();
      if (actual.dex!=method.GetDexFile() || actual.instructions!=source.Insns() || actual.count!=source.InsnsSizeInCodeUnits()) return 5;
    }
    listener->DexPcMoved(soa.Self(),null_handle,&method,UINT32_MAX);
  }
  return 0;
}
