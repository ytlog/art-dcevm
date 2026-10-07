#include "platform/android13/device_diagnostics_abi.h"
#include "oat_quick_method_header.h"
#include "instrumentation.h"
#include "runtime.h"
#include <functional>
#include <cstring>
extern "C" bool ArtDcevmSourceProbeDeviceDiagnostics(int (*classify)(const void*),const artdcevm::android13::diagnostics_abi::BorrowedFunction* function,void* instrumentation){
 using Function=std::function<void(const void*,art::ArtMethod*)>;
 static_assert(sizeof(Function)==sizeof(*function)&&alignof(Function)==alignof(artdcevm::android13::diagnostics_abi::BorrowedFunction));
 auto& callback=*reinterpret_cast<const Function*>(function);callback(nullptr,nullptr); // Non-target callback exercises the actual platform virtual Invoke slot.
 auto* source=art::Runtime::Current()->GetInstrumentation();if(source!=instrumentation)return false;
 uint32_t level;std::memcpy(&level,static_cast<unsigned char*>(instrumentation)+offsetof(artdcevm::android13::diagnostics_abi::InstrumentationPrefix,level),4);
 if((level==2)!=source->AreAllMethodsDeoptimized())return false;
 alignas(8) unsigned char bytes[32]{};auto* entry=bytes+16;uint32_t data=8;std::memcpy(entry-4,&data,4);
 auto* header=art::OatQuickMethodHeader::FromEntryPoint(entry);if(header->IsOptimized()||classify(entry)!=1)return false;
 data=0x40000010;std::memcpy(entry-4,&data,4);
 for(unsigned char flags:{0,1,2,3}){bytes[0]=flags;int expected=header->IsOptimized()?(art::CodeInfo::HasInlineInfo(header->GetOptimizedCodeInfoPtr())?3:2):1;if(classify(entry)!=expected)return false;callback(entry,reinterpret_cast<art::ArtMethod*>(1));}
 return true;
}
