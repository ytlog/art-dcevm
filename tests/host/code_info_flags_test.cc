#include "platform/common/code_info_flags.h"
#include "platform/android13/code_info_flags_abi.h"
#include <cassert>
#include <cstdio>
using namespace artdcevm;
int main() {
  for (int api = 30; api <= 37; ++api) {
    for (uint32_t word = 0; word < 256; ++word) {
      code_info_flags::State state{};
      assert(code_info_flags::ReadSource(api, word, &state));
      assert(state.inline_info == bool(word & 1));
      assert(state.baseline == bool(word & (api >= 35 ? 4 : 2)));
      if (api < 35) assert(!state.should_deoptimize.has_value());
      else assert(state.should_deoptimize == bool(word & 2));
      if (api < 34) assert(!state.debuggable.has_value());
      else assert(state.debuggable == bool(word & (api == 34 ? 4 : 8)));
      if (api != 37) assert(!state.fast.has_value());
      else assert(state.fast == bool(word & 16));
      uint32_t known = api < 34 ? 3 : api == 34 ? 7 : api < 37 ? 15 : 31;
      assert(state.unknown_bits == (word & ~known));
    }
  }
  auto optimized_inline = android13::code_info_flags_abi::DecodeMatchedCas(3);
  assert(optimized_inline.inline_info && !optimized_inline.baseline);
  assert(optimized_inline.should_deoptimize == true);
  auto baseline = android13::code_info_flags_abi::DecodeMatchedCas(6);
  assert(!baseline.inline_info && baseline.baseline);
  assert(baseline.should_deoptimize == true);
  assert(!baseline.debuggable && !baseline.fast);
  auto vendor_unknown = android13::code_info_flags_abi::DecodeMatchedCas(0xFFFFFFF8);
  assert(!vendor_unknown.inline_info && !vendor_unknown.baseline);
  assert(vendor_unknown.should_deoptimize == false);
  assert(vendor_unknown.unknown_bits == 0xFFFFFFF8);
  // The same word has distinct meanings for calibrated CAS and upstream 13.
  code_info_flags::State source{};
  assert(code_info_flags::ReadSource(33, 2, &source));
  assert(source.baseline && !source.should_deoptimize.has_value());
  assert(android13::code_info_flags_abi::DecodeMatchedCas(2).should_deoptimize == true);
  assert(!android13::code_info_flags_abi::DecodeMatchedCas(2).baseline);
  source.unknown_bits = 0x12345678;
  assert(!code_info_flags::ReadSource(29, 2, &source));
  assert(source.unknown_bits == 0x12345678);
  assert(!code_info_flags::ReadSource(38, 2, &source));
  assert(source.unknown_bits == 0x12345678);
  assert(!code_info_flags::ReadSource(33, 2, nullptr));
  puts("CodeInfo scalar flags: eight source APIs, distinct CAS calibration, unknown masks passed");
}
