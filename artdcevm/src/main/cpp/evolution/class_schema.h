#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace artdcevm::evolution {
inline constexpr uint32_t kStatic = 0x0008;
inline constexpr uint32_t kNative = 0x0100;
inline constexpr uint32_t kAbstract = 0x0400;

struct Member {
  std::string name;
  // Field type or complete method descriptor. Names alone never identify members.
  std::string descriptor;
  uint32_t access = 0;
  uint32_t dex_index = UINT32_MAX;
  bool IsStatic() const { return (access & kStatic) != 0; }
};
struct ClassSchema {
  std::string descriptor;
  std::string superclass;
  std::vector<std::string> interfaces;
  uint32_t access = 0;
  std::vector<Member> fields;
  std::vector<Member> methods;
};

// Borrowed regions from an already verified ART DEX. CompactDex shares a data
// region outside its ID-table mapping. DEX 041 uses the full container as
// data_begin; begin/size select its header/file. No JNI member IDs are created.
struct MappedDexMetadata {
  const unsigned char* begin;
  size_t size;
  const unsigned char* data_begin;
  size_t data_size;
  uint32_t class_def_index;
};
// Counts from the selected ordinary class's class_data, as used by ART 17
// ClassAccessor. These are DEX declaration counts, not ArtMethod array slice
// offsets: ART 17 interleaves direct/virtual declarations by DEX method index.
struct MappedDexMemberCounts {
  uint32_t static_fields = 0, instance_fields = 0, direct_methods = 0, virtual_methods = 0;
};
// Requires a live, immutable, verified DEX mapping and an ordinary class definition.
// Reads the four bounded ULEBs only; format failure preserves output.
bool ReadMappedDexMemberCounts(const MappedDexMetadata&, MappedDexMemberCounts*, std::string*);
// Borrowed null-terminated type descriptor; the caller keeps the immutable
// mapping live. Uses full uint32 type indices and shared/container ID bases.
// Format errors preserve output; allocation failures propagate.
bool ReadMappedDexTypeDescriptor(const MappedDexMetadata&, uint32_t index,
                                 const char**, std::string*);
// Selected class_def superclass, or nullptr for the DEX NO_INDEX root.
// Same borrowed lifetime/error contract as type descriptor lookup.
bool ReadMappedDexSuperclassDescriptor(const MappedDexMetadata&, const char**, std::string*);
// A method ID may reference another class, including an array, with no local
// class definition. Explicit cache resolution must use this owner, not referrer.
struct MappedDexMethodReference {
  uint16_t class_index = 0;
  uint16_t proto_index = 0;
  // Borrowed 12-byte ProtoId; valid while the immutable DEX mapping is live.
  const unsigned char* proto_id = nullptr;
  std::string declaring_class;
  std::string name;
  std::string descriptor;
};
// Reads ID tables and shared data only; class_def_index is unused. Output is
// unchanged on format errors; bad_alloc propagates to the ART/JVMTI boundary.
bool ReadMappedDexMethodReference(const MappedDexMetadata&, uint32_t index,
                                  MappedDexMethodReference*, std::string*);
bool ReadMappedDexSupportsDefaultMethods(const MappedDexMetadata&,bool*,std::string*);
bool ReadMappedDexDeclarations(const MappedDexMetadata&, ClassSchema*, std::string*);

// Reads metadata only. ART must still verify method bodies, annotations and encoded values.
// On format error the output is unchanged; allocation failures propagate as bad_alloc.
// No ART layout or OS-specific symbol is used here.
// Validation against original DEX declarations retains constructor and declared-sync bits.
bool ReadDexDeclarations(const std::vector<unsigned char>& dex, ClassSchema* output,
                         std::string* error);
bool ReadDexSchema(const std::vector<unsigned char>& dex, ClassSchema* output, std::string* error);
bool IsTypeDescriptor(const std::string& value, bool allow_void);
bool IsMethodDescriptor(const std::string& value);
}
