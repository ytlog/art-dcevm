#include "platform/common/modern_final_field_guard.h"

#include <cassert>
#include <thread>
#include <vector>

using artdcevm::final_field_guard::FinalFieldGuardState;

int main() {
  // Guard publication must synchronize readers on threads that already hold
  // cached field setters, and it remains enabled after repeated transactions.
  for (int iteration = 0; iteration < 128; ++iteration) {
    FinalFieldGuardState state;
    assert(!state.Enabled());
    int published = 0;
    std::thread reader([&] {
      while (!state.Enabled()) std::this_thread::yield();
      assert(published == 42);
      for (int read = 0; read < 1024; ++read) assert(state.Enabled());
    });
    std::thread writer([&] {
      published = 42;
      state.Mark();
      for (int mark = 0; mark < 1024; ++mark) state.Mark();
    });
    reader.join();
    writer.join();
    assert(state.Enabled());
  }
  FinalFieldGuardState shared;
  std::vector<std::thread> workers;
  for (int index = 0; index < 16; ++index) {
    workers.emplace_back([&] {
      for (int mark = 0; mark < 4096; ++mark) {
        shared.Mark();
        assert(shared.Enabled());
      }
    });
  }
  for (auto& worker : workers) worker.join();
  assert(shared.Enabled());
  assert(!artdcevm::final_field_guard::FinalFieldGuardEnabled());
  artdcevm::final_field_guard::MarkFinalFieldGuard();
  assert(artdcevm::final_field_guard::FinalFieldGuardEnabled());
  artdcevm::final_field_guard::MarkFinalFieldGuard();
  assert(artdcevm::final_field_guard::FinalFieldGuardEnabled());
}
