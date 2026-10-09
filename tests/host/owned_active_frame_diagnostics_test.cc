// Exercise the real diagnostic serializer; no ART objects or capabilities mocked.
#include "platform/common/owned_active_frame_probe.cc"
#include <cstdio>
int main() {
 using namespace artdcevm::owned_active_frame;
 using namespace artdcevm::active_frame_recovery_proof;
 Request request(nullptr,nullptr,nullptr);
 request.jit_compiled=true;request.debug_observed=true;request.java_debuggable=true;
 request.report.failure=Failure::kMissingFrames;
 request.report.detail="missing-actual-inlinees-physical-fixture-frames-observed";
 request.report.callbacks=9;request.report.matched_mask=7;request.report.physical_matches=3;
 request.error=artdcevm::owned_active_frame::Failure(request.report.failure);
 request.attempts.push_back({Family::kAndroid12,"_missing_named_leaf",{},false});
 request.attempts.push_back({Family::kAndroid17,nullptr,request.report,true});
 std::puts(Json(request).c_str());
}
