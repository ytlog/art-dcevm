#pragma once
#include <ostream>
namespace artdcevm::android13 {
// Both contracts describe the same already-established source engine boundary.
// Never derive either permission from named symbols alone or a platform version.
inline void WriteNativeDeletionCapabilities(std::ostream& output,bool available) {
  const char* value=available?"true":"false";
  output << ",\"nativeDelete\":" << value
         << ",\"fieldDeletion\":" << value;
}
}
