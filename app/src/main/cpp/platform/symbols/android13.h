#pragma once
#include "platform/symbols/art_symbol_profile.h"
#include "platform/symbols/android13/xiaomi/cas_tkq1_221114_001.h"

// Android 13 currently has one verified private ART profile. Keep the callers
// independent of its vendor path so additional profiles can be selected here.
namespace artdcevm::android13 {
namespace cas = ::artdcevm::symbols::android13::xiaomi::cas;
}
