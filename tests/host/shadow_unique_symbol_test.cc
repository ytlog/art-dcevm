#include "hooks/shadow_hook_engine.h"
#include "support/elf_modules.h"
#include "shadowhook.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {
const char* canonical = "_ZN3artL12Field_getIntEP7_JNIEnvP8_jobjectS3_";
const char* unique = "_ZN3artL12Field_getIntEP7_JNIEnvP8_jobjectS3_.__uniq.201706906895676056810378034188277470683";
artdcevm::LoadedModule module;
bool exact = false;
bool open = true;
int target;
unsigned closes = 0, identities = 0;
std::vector<std::string> requests;
}
namespace artdcevm {
LoadedModule FindLoadedModule(const char* library) {
  assert(std::string(library) == "libart.so");
  ++identities;
  return module;
}
}
int shadowhook_init(int, bool) { return 0; }
const char* shadowhook_to_errmsg(int) { return "stub"; }
void* shadowhook_dlopen(const char*) { return open ? &target : nullptr; }
void* shadowhook_dlsym_dynsym(void*, const char* name) {
  requests.emplace_back(std::string("dyn:") + name);
  return exact ? &target : nullptr;
}
void* shadowhook_dlsym_symtab(void*, const char* name) {
  requests.emplace_back(std::string("sym:") + name);
  return std::string(name) == unique ? &target : nullptr;
}
void shadowhook_dlclose(void*) { ++closes; }
void* shadowhook_hook_sym_addr(void*, void*, void**) { return nullptr; }
int shadowhook_get_errno() { return 0; }
int shadowhook_unhook(void*) { return 0; }

int main(int argc, char** argv) {
  assert(argc == 2);
  module = {argv[1], "fixture-build-id"};
  artdcevm::ShadowHookEngine hooks;
  exact = true;
  assert(hooks.FindSymbol("libart.so", canonical) == &target);
  assert(requests.size() == 1 && identities == 0 && closes == 1);
  exact = false;
  requests.clear();
  assert(hooks.FindSymbol("libart.so", canonical) == &target);
  assert(requests == (std::vector<std::string>{std::string("dyn:") + canonical,
      std::string("sym:") + canonical, std::string("sym:") + unique}));
  assert(identities == 1 && closes == 2);
  module.build_id.clear();
  requests.clear();
  assert(!hooks.FindSymbol("libart.so", canonical));
  assert(requests.size() == 2 && closes == 3);
  open = false;
  requests.clear();
  assert(!hooks.FindSymbol("libart.so", canonical));
  assert(requests.empty() && closes == 3);
  std::puts("ShadowHook exact-first, real ELF unique name fallback, missing identity and balanced handle close passed");
}
