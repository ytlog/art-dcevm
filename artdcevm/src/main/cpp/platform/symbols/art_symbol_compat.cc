#include "platform/symbols/art_symbol_compat.h"
#include "platform/symbols/android13/source_symbols.h"
#include <initializer_list>

namespace artdcevm::symbols {
namespace {
bool Require(HookEngine& hooks, Library library, const char* name, std::string* error) {
  if (hooks.FindSymbol(LibraryName(library), name)) return true;
  if (error) *error = std::string("Missing private symbol in ") + LibraryName(library) + ": " + name;
  return false;
}
}

bool CheckValidationSymbols(HookEngine& hooks, std::string* error) {
  const char* names[]{::artdcevm::symbols::android13::kCheckFields, ::artdcevm::symbols::android13::kCheckMethods, ::artdcevm::symbols::android13::kCheckClass,
                      ::artdcevm::symbols::android13::kCollectMappings, ::artdcevm::symbols::android13::kStructuralCommit, ::artdcevm::symbols::android13::kInPlaceCommit};
  for (const char* name : names)
    if (!Require(hooks, Library::kJvmti, name, error)) return false;
  return true;
}

bool CheckEvolutionSymbols(HookEngine& hooks, std::string* error) {
  if (!CheckValidationSymbols(hooks, error)) return false;
  const char* jvmti[]{::artdcevm::symbols::android13::kRedefineClasses, ::artdcevm::symbols::android13::kStructuralRedefineClasses,
                      ::artdcevm::symbols::android13::kCollectNewInstances,
                      ::artdcevm::symbols::android13::kAllocateSubtree, ::artdcevm::symbols::android13::kRedefineRun, ::artdcevm::symbols::android13::kAllocateClass};
  for (const char* name : jvmti)
    if (!Require(hooks, Library::kJvmti, name, error)) return false;
  const char* art[]{::artdcevm::symbols::android13::kVerifyClass, ::artdcevm::symbols::android13::kVerifierResolve, ::artdcevm::symbols::android13::kFindClass,
                    ::artdcevm::symbols::android13::kLookupClass, ::artdcevm::symbols::android13::kDeleteClassLoader,
                    ::artdcevm::symbols::android13::kIsAsyncDeoptimizeable, ::artdcevm::symbols::android13::kTestSuspendFromCode,
                    ::artdcevm::symbols::android13::kReadBarrierMark,
                    ::artdcevm::symbols::android13::kCheckedResolve,
                    ::artdcevm::symbols::android13::kInvalidateAllCompiledCode};
  for (const char* name : art)
    if (!Require(hooks, Library::kArt, name, error)) return false;
  for (const char* name : ::artdcevm::symbols::android13::kRetiredMethodHooks)
    if (!Require(hooks, Library::kArt, name, error)) return false;
  for (const char* name : ::artdcevm::symbols::android13::kRetiredFieldHooks)
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
          auto name = ::artdcevm::symbols::android13::JniFieldSymbol(indexed, is_static, setter,
                                         type.name, type.code, type.object);
          if (!Require(hooks, Library::kArt, name.c_str(), error)) return false;
        }
      }
    }
    for (bool setter : {false, true}) {
      auto name = ::artdcevm::symbols::android13::ReflectionFieldSymbol(setter, type.name, type.code, type.object);
      if (!Require(hooks, Library::kArt, name.c_str(), error)) return false;
    }
  }
  return true;
}

void* FindSymbol(HookEngine& hooks, NamedSymbol symbol) {
  return hooks.FindSymbol(LibraryName(symbol.library), symbol.name);
}

}  // namespace artdcevm::symbols
