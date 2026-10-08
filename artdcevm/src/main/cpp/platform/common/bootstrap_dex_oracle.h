#pragma once
#include "evolution/class_schema.h"
namespace artdcevm::production_bootstrap {
// The input is copied from installed APK ZIP entries, not a candidate ArtMethod
// or DexCache field. Complete class_data is required; ambiguous classes reject.
bool SelectInstalledClass(const std::vector<std::vector<unsigned char>>&,const char*,
    evolution::ClassSchema*,evolution::MappedDexMemberCounts*,std::string*);
}
