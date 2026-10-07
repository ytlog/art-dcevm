#include "platform/symbols/art_symbol_compat.h"
#include "platform/symbols/android13/xiaomi/cas_tkq1_221114_001.h"
#include <initializer_list>

namespace artdcevm::symbols {
namespace {
namespace cas = android13::xiaomi::cas;
bool Require(HookEngine& hooks, Library library, const char* name, std::string* error) {
  if (hooks.FindSymbol(LibraryName(library), name)) return true;
  if (error) *error = std::string("Missing private symbol in ") + LibraryName(library) + ": " + name;
  return false;
}
}

bool CheckValidationSymbols(HookEngine& hooks, std::string* error) {
  const char* names[]{cas::kCheckFields, cas::kCheckMethods, cas::kCheckClass,
                      cas::kCollectMappings, cas::kStructuralCommit, cas::kInPlaceCommit};
  for (const char* name : names)
    if (!Require(hooks, Library::kJvmti, name, error)) return false;
  return true;
}

bool CheckEvolutionSymbols(HookEngine& hooks, std::string* error) {
  if (!CheckValidationSymbols(hooks, error)) return false;
  const char* jvmti[]{cas::kRedefineClasses, cas::kStructuralRedefineClasses,
                      cas::kCollectNewInstances,
                      cas::kAllocateSubtree, cas::kRedefineRun, cas::kAllocateClass};
  for (const char* name : jvmti)
    if (!Require(hooks, Library::kJvmti, name, error)) return false;
  const char* art[]{cas::kVerifyClass, cas::kVerifierResolve, cas::kFindClass,
                    cas::kLookupClass, cas::kDeleteClassLoader,
                    cas::kLinearAllocContainsUnsafe,
                    cas::kIsAsyncDeoptimizeable, cas::kTestSuspendFromCode,
                    cas::kReadBarrierMark,
                    cas::kCheckedResolve, cas::kCheckedResolveHandles,
                    cas::kInvalidateAllCompiledCode};
  for (const char* name : art)
    if (!Require(hooks, Library::kArt, name, error)) return false;
  for (const char* name : cas::kRetiredMethodHooks)
    if (!Require(hooks, Library::kArt, name, error)) return false;
  for (const char* name : cas::kRetiredFieldHooks)
    if (!Require(hooks, Library::kArt, name, error)) return false;
  struct FieldType { const char* name; const char* code; bool object; };
  constexpr FieldType types[]{{"Boolean", "h", false}, {"Byte", "a", false},
                              {"Char", "t", false}, {"Short", "s", false},
                              {"Int", "i", false}, {"Long", "l", false},
                              {"Float", "f", false}, {"Double", "d", false},
                              {"Object", "", true}};
  for (const auto& type : types) {
    for (int indexed = 0; indexed != 2; ++indexed) {
      for (bool is_static : {false, true}) {
        for (bool setter : {false, true}) {
          auto name = cas::JniFieldSymbol(indexed, is_static, setter,
                                         type.name, type.code, type.object);
          if (!Require(hooks, Library::kArt, name.c_str(), error)) return false;
        }
      }
    }
    for (bool setter : {false, true}) {
      auto name = cas::ReflectionFieldSymbol(setter, type.name, type.code, type.object);
      if (!Require(hooks, Library::kArt, name.c_str(), error)) return false;
    }
  }
  return true;
}

void* FindSymbol(HookEngine& hooks, NamedSymbol symbol) {
  return hooks.FindSymbol(LibraryName(symbol.library), symbol.name);
}

}  // namespace artdcevm::symbols
