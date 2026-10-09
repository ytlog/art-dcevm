#include "platform/common/whole_process_frames.h"
namespace artdcevm::whole_process_frames::detail {
// Keep one strong typed definition. On Mach-O, sanitizer metadata before a
// header weak definition can otherwise be coalesced with its preceding atom.
bool VisitThread(void* thread,void* raw) {
 auto& r=*static_cast<Service::Request*>(raw);return r.service.Thread(r,thread);
}
}
