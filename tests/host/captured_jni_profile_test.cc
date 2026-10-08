#include "platform/jni_access/captured_profile.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace artdcevm;
using namespace artdcevm::jni_access;
namespace {
struct Expected {const char* build_id;const char* id;DecoderModel decoder;PeerModel peer;};
constexpr Expected kExpected[] = {
 {"6d01a26aa5bb297b773f6354dac69d75","redfin-30",DecoderModel::kLegacyAny,PeerModel::kConst},
 {"e6c658201ef1ec3760112fa1b838ab2c","oriole-31",DecoderModel::kLegacyAny,PeerModel::kConst},
 {"2b417e2566f5eb686666666b6ee952ea","bluejay-32",DecoderModel::kLegacyAny,PeerModel::kConst},
 {"289d75599f6112d5757113220599e90b","panther-33",DecoderModel::kLegacyAny,PeerModel::kConst},
 {"97fdb979efb7d2b596fa4fceabaad95b","b0q-33",DecoderModel::kLegacyAny,PeerModel::kConst},
 {"02bec5940be704b863f6514fc7d81c41","shiba-34",DecoderModel::kGlobalOnly,PeerModel::kNonconst},
 {"c35c9ebf7bb06435e4b31977d87bd5d5","husky-35",DecoderModel::kGlobalOnly,PeerModel::kNonconst},
 {"be34fbe63ff357beb403f9cb39923ea7","frankel-36",DecoderModel::kGlobalOnly,PeerModel::kNonconst},
 {"1f156fc62660d075a8d675db850b95d5","cubs-37",DecoderModel::kGlobalOnly,PeerModel::kNonconst},
 {"14fc77b24db0b351b919c97009dc86bf","xiaomi-cas-33",DecoderModel::kLegacyAny,PeerModel::kConst}
};
void FailsPreserving(const LoadedModule& module) {
 const char* sentinel="sentinel";
 CapturedProfile profile{sentinel,DecoderModel::kGlobalOnly,PeerModel::kConst};
 std::string error;
 assert(!MatchCapturedProfile(module,&profile,&error));
 assert(!error.empty());
 assert(profile.id==sentinel && profile.decoder==DecoderModel::kGlobalOnly && profile.peer==PeerModel::kConst);
 assert(!MatchCapturedProfile(module,&profile,nullptr));
 assert(profile.id==sentinel);
}
}
int main() {
 for (const auto& expected:kExpected) {
  CapturedProfile profile{};std::string error="old failure";
  assert(MatchCapturedProfile({"/apex/com.android.art/lib64/libart.so",expected.build_id},&profile,&error));
  assert(error.empty() && std::strcmp(profile.id,expected.id)==0);
  assert(profile.decoder==expected.decoder && profile.peer==expected.peer);
  assert(MatchCapturedProfile({"/system/lib64/libart.so",expected.build_id},&profile,nullptr));
  // Exact identities do not accept truncation, extensions or modified bytes.
  const std::string id=expected.build_id;
  FailsPreserving({"libart.so",id.substr(1)});
  FailsPreserving({"libart.so",id+"0"});
  std::string changed=id;changed[0]=changed[0]=='a'?'b':'a';
  FailsPreserving({"libart.so",changed});
 }
 const char* known=kExpected[0].build_id;
 FailsPreserving({"libartd.so",known});
 FailsPreserving({"libart.so.debug",known});
 FailsPreserving({"/apex/com.android.art/lib64/",known});
 FailsPreserving({"",known});
 FailsPreserving({"libart.so",""});
 FailsPreserving({"libart.so","unknown"});
 FailsPreserving({"libart.so","6D01A26AA5BB297B773F6354DAC69D75"});
 std::string error;
 assert(!MatchCapturedProfile({"libart.so",known},nullptr,&error) && !error.empty());
 assert(!MatchCapturedProfile({"libart.so",known},nullptr,nullptr));
 puts("captured JNI profiles: 10 exact identities, models and failure preservation passed");
}
