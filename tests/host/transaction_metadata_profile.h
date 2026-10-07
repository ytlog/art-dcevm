#pragma once
#include "platform/android13/transaction_metadata.h"
#include <cassert>
#include <cstring>
#include "device_profile_fixture.h"
inline void ConfigureTransactionReaderForTest(artdcevm::HookEngine& hooks) {
 artdcevm::android13::DeviceProfile profile;std::string error;
 assert(artdcevm::android13::MatchDeviceProfile(&profile,&error));
 assert(artdcevm::android13::ConfigureTransactionMetadataReader(hooks,profile,&error));
}
