#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace artdcevm::evolution {
inline constexpr uint32_t kAccPublic = 0x0001;
inline constexpr uint32_t kAccPrivate = 0x0002;
inline constexpr uint32_t kAccProtected = 0x0004;
inline constexpr uint32_t kAccStatic = 0x0008;
inline constexpr uint32_t kAccFinal = 0x0010;
inline constexpr uint32_t kAccSynchronized = 0x0020;
inline constexpr uint32_t kAccVolatile = 0x0040;
inline constexpr uint32_t kAccNative = 0x0100;
inline constexpr uint32_t kAccInterface = 0x0200;
inline constexpr uint32_t kAccAbstract = 0x0400;
inline constexpr uint32_t kAccConstructor = 0x10000;
inline constexpr uint32_t kAccDeclaredSynchronized = 0x20000;

// Field type or complete method descriptor; names alone never identify members.
struct Member {
  std::string name;
  std::string descriptor;
  uint32_t access = 0;
  bool IsStatic() const { return (access & kAccStatic) != 0; }
};

// One class declaration. Access flags are either raw DEX flags or the public
// JVMTI view, as stated by the producer.
struct ClassSchema {
  std::string descriptor;
  std::string superclass;  // Empty for java.lang.Object.
  std::vector<std::string> interfaces;
  uint32_t access = 0;
  std::vector<Member> fields;
  std::vector<Member> methods;
  // DEX InnerClass.accessFlags, when present. JVMTI reports these rather than
  // ClassDef flags for nested classes; `access` still retains the raw flags.
  std::optional<uint32_t> reflection_access;
};

// A standard DEX (035--040) in memory. For an ART DexFile, begin is the
// mapping, size the header file_size_ and data the DexFile data_ range.
struct DexView {
  const uint8_t* begin = nullptr;
  size_t size = 0;
  const uint8_t* data = nullptr;
  size_t data_size = 0;
};

// Reads ClassDef 0 with raw DEX access flags. Every read is bounds-checked;
// malformed input fails with a message and leaves the output unchanged.
bool ReadClassDeclarations(const DexView& dex, ClassSchema* output, std::string* error);

// Read field_ids[index] (name, type) or method_ids[index] (name, descriptor)
// from a standard or compact DEX with any number of classes, with the same
// bounds checks. The output carries no access flags.
bool ReadFieldIdentity(const DexView& dex, uint32_t index, Member* output, std::string* error);
bool ReadMethodIdentity(const DexView& dex, uint32_t index, Member* output, std::string* error);
// Raw access flags of the ClassDef named `descriptor`, from any standard or
// compact DEX, with the same bounds checks.
bool ReadClassAccess(const DexView& dex, std::string_view descriptor, uint32_t* output,
                     std::string* error);
// Every class type_ids names, arrays reduced to their element class, sorted
// and unique, from a standard or compact DEX with the same bounds checks.
bool ReadReferencedClasses(const DexView& dex, std::vector<std::string>* output,
                           std::string* error);
// The exact type_ids entry, including array descriptors.
bool ReadTypeDescriptor(const DexView& dex, uint32_t index, std::string* output,
                        std::string* error);
}  // namespace artdcevm::evolution
