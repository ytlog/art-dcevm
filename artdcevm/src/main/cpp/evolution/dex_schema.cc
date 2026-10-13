#include "evolution/class_schema.h"
#include <cstring>
#include <set>
#include <stdexcept>
#include <string_view>

namespace artdcevm::evolution {
namespace {
[[noreturn]] void Fail(const char* text) { throw std::runtime_error(text); }

bool ValidType(std::string_view s, size_t* at, bool allow_void) {
  size_t arrays = 0;
  while (*at < s.size() && s[*at] == '[') { ++*at; ++arrays; }
  if (*at >= s.size() || arrays > 255) return false;
  const char c = s[(*at)++];
  if (c == 'V') return allow_void && arrays == 0;
  if (c != 0 && std::strchr("ZBSCIJFD", c)) return true;
  if (c != 'L') return false;
  const size_t start = *at;
  while (*at < s.size() && s[*at] != ';') {
    const char ch = s[*at];
    if (ch == '.' || ch == '[' || ch == '(' || ch == ')') return false;
    ++*at;
  }
  if (*at == start || *at == s.size()) return false;
  ++*at;
  return true;
}

class Reader {
 public:
  explicit Reader(const DexView& dex) : dex_(dex) {}

  ClassSchema Read() {
    Header(false);
    if (classes_.count != 1) Fail("Redefinition DEX must contain exactly one class");
    const size_t def = Item(classes_, 0);
    class_type_ = U32(def);
    ClassSchema result;
    result.descriptor = Type(class_type_);
    if (result.descriptor.front() != 'L') Fail("ClassDef does not name a class");
    result.access = U32(def + 4);
    result.reflection_access = InnerClassAccess(U32(def + 20));
    if (const uint32_t super = U32(def + 8); super != UINT32_MAX) {
      result.superclass = Type(super);
      if (result.superclass.front() != 'L') Fail("Invalid superclass type");
    }
    result.interfaces = TypeList(U32(def + 12));
    std::set<std::string> unique(result.interfaces.begin(), result.interfaces.end());
    if (unique.size() != result.interfaces.size()) Fail("Duplicate interface");
    for (const auto& name : result.interfaces)
      if (name.front() != 'L') Fail("Invalid interface type");
    if (const uint32_t data = U32(def + 24)) {
      size_t cursor = data;
      const uint32_t static_fields = Leb(&cursor), instance_fields = Leb(&cursor);
      const uint32_t direct_methods = Leb(&cursor), virtual_methods = Leb(&cursor);
      Fields(&cursor, static_fields, true, &result.fields);
      Fields(&cursor, instance_fields, false, &result.fields);
      Methods(&cursor, direct_methods, true, &result.methods);
      Methods(&cursor, virtual_methods, false, &result.methods);
    }
    Unique(result.fields);
    Unique(result.methods);
    return result;
  }

  Member FieldIdentity(uint32_t index) {
    Header(true);
    const size_t item = Item(fields_, index);
    std::string type = Type(U16(item + 2));
    if (type == "V") Fail("Invalid field type");
    return {String(U32(item + 4)), std::move(type), 0};
  }

  uint32_t ClassAccess(std::string_view descriptor) {
    Header(true);
    for (uint32_t i = 0; i < classes_.count; ++i) {
      const size_t def = Item(classes_, i);
      if (Type(U32(def)) == descriptor) return U32(def + 4);
    }
    Fail("Class not defined in DEX");
  }

  Member MethodIdentity(uint32_t index) {
    Header(true);
    const size_t item = Item(methods_, index);
    std::string name = String(U32(item + 4));
    if (name.empty()) Fail("Invalid method name");
    return {std::move(name), Prototype(U16(item + 2)), 0};
  }

 private:
  struct Table { uint32_t offset = 0, count = 0, width = 0; };

  // CompactDex (libdexfile/dex/compact_dex_file.h, Android 11--15) extends the
  // standard header and keeps the ID tables; strings and type lists live in
  // its separate data section, which every reader here already addresses.
  void Header(bool allow_compact) {
    if (!dex_.begin || !dex_.data || dex_.size < 0x70) Fail("Missing DEX mapping");
    const bool compact = allow_compact && std::memcmp(dex_.begin, "cdex001", 8) == 0;
    if (!compact) {
      if (std::memcmp(dex_.begin, "dex\n", 4) != 0 || dex_.begin[7] != 0) Fail("Not a standard DEX");
      const std::string_view version(reinterpret_cast<const char*>(dex_.begin + 4), 3);
      if (version < "035" || version > "040") Fail("Unsupported DEX version");
    }
    if (U32(0x20) != dex_.size || (compact ? U32(0x24) < 0x70 : U32(0x24) != 0x70) ||
        U32(0x28) != 0x12345678)
      Fail("Invalid DEX size, header size or endianness");
    strings_ = Ids(0x38, 4);
    types_ = Ids(0x40, 4);
    protos_ = Ids(0x48, 12);
    fields_ = Ids(0x50, 8);
    methods_ = Ids(0x58, 8);
    classes_ = Ids(0x60, 32);
  }
  Table Ids(size_t header_offset, uint32_t width) const {
    const uint32_t count = U32(header_offset), offset = U32(header_offset + 4);
    if ((count == 0) != (offset == 0) || (count && (offset < 0x70 || offset % 4)))
      Fail("Invalid ID table");
    Range(dex_.size, offset, size_t{count} * width);
    return {offset, count, width};
  }
  static void Range(size_t limit, size_t offset, size_t size) {
    if (offset > limit || size > limit - offset) Fail("DEX range outside mapping");
  }
  size_t Item(const Table& table, uint32_t index) const {
    if (index >= table.count) Fail("Invalid DEX table index");
    return table.offset + size_t{index} * table.width;
  }
  uint16_t U16(size_t at) const {
    Range(dex_.size, at, 2);
    return static_cast<uint16_t>(dex_.begin[at] | (dex_.begin[at + 1] << 8));
  }
  uint32_t U32(size_t at) const {
    Range(dex_.size, at, 4);
    return dex_.begin[at] | (uint32_t{dex_.begin[at + 1]} << 8) |
        (uint32_t{dex_.begin[at + 2]} << 16) | (uint32_t{dex_.begin[at + 3]} << 24);
  }
  uint8_t DataByte(size_t at) const {
    Range(dex_.data_size, at, 1);
    return dex_.data[at];
  }
  uint16_t DataU16(size_t at) const { return static_cast<uint16_t>(DataByte(at) | (DataByte(at + 1) << 8)); }
  uint32_t DataU32(size_t at) const { return DataU16(at) | (uint32_t{DataU16(at + 2)} << 16); }
  uint32_t Leb(size_t* cursor) const {
    uint32_t result = 0;
    for (unsigned i = 0; i < 5; ++i) {
      const uint8_t byte = DataByte((*cursor)++);
      if (i == 4 && (byte & 0xf0)) Fail("Overflowing ULEB128");
      result |= uint32_t{byte & 0x7fu} << (7 * i);
      if (!(byte & 0x80)) return result;
    }
    Fail("Unterminated ULEB128");
  }
  std::string String(uint32_t index) const {
    size_t cursor = U32(Item(strings_, index));
    const uint32_t units = Leb(&cursor);
    const size_t start = cursor;
    uint32_t read = 0;
    for (uint8_t byte; (byte = DataByte(cursor++)) != 0; ++read) {
      if (byte < 0x80) continue;
      const unsigned more = (byte & 0xe0) == 0xc0 ? 1 : ((byte & 0xf0) == 0xe0 ? 2 : 0);
      if (!more) Fail("Invalid modified UTF-8");
      for (unsigned n = 0; n < more; ++n)
        if ((DataByte(cursor++) & 0xc0) != 0x80) Fail("Invalid modified UTF-8");
    }
    if (read != units) Fail("String length mismatch");
    return std::string(reinterpret_cast<const char*>(dex_.data + start), cursor - start - 1);
  }
  std::string Type(uint32_t index) const {
    std::string value = String(U32(Item(types_, index)));
    size_t at = 0;
    if (!ValidType(value, &at, true) || at != value.size()) Fail("Invalid type descriptor");
    return value;
  }
  std::vector<std::string> TypeList(uint32_t offset) const {
    std::vector<std::string> result;
    if (!offset) return result;
    if (offset % 4) Fail("Unaligned type list");
    const uint32_t count = DataU32(offset);
    for (uint32_t i = 0; i < count; ++i) {
      result.push_back(Type(DataU16(size_t{offset} + 4 + size_t{i} * 2)));
      if (result.back() == "V") Fail("Invalid void in type list");
    }
    return result;
  }
  std::string Prototype(uint32_t index) const {
    const size_t item = Item(protos_, index);
    std::string result = "(";
    for (const auto& type : TypeList(U32(item + 8))) result += type;
    return result + ")" + Type(U32(item + 4));
  }
  std::optional<uint32_t> InnerClassAccess(uint32_t directory) const {
    if (!directory) return std::nullopt;
    if (directory % 4) Fail("Unaligned annotations directory");
    Range(dex_.data_size, directory, 16);
    const uint32_t set = DataU32(directory);
    if (!set) return std::nullopt;
    if (set % 4) Fail("Unaligned annotation set");
    const uint32_t count = DataU32(set);
    Range(dex_.data_size, size_t{set} + 4, size_t{count} * 4);
    for (uint32_t i = 0; i < count; ++i) {
      size_t cursor = DataU32(size_t{set} + 4 + size_t{i} * 4);
      const uint8_t visibility = DataByte(cursor++);
      const std::string type = Type(Leb(&cursor));
      if (visibility != 2 || type != "Ldalvik/annotation/InnerClass;") continue;
      const uint32_t elements = Leb(&cursor);
      // Every element consumes at least its name index and value header.
      Range(dex_.data_size, cursor, size_t{elements} * 2);
      std::optional<uint32_t> access;
      for (uint32_t n = 0; n < elements; ++n) {
        const std::string name = String(Leb(&cursor));
        const uint8_t header = DataByte(cursor++);
        const uint8_t value_type = header & 0x1f, value_arg = header >> 5;
        if (name == "accessFlags") {
          if (value_type != 0x04 || value_arg > 3 || access)
            Fail("Invalid InnerClass accessFlags");
          uint32_t value = 0;
          for (unsigned byte = 0; byte <= value_arg; ++byte)
            value |= uint32_t{DataByte(cursor++)} << (byte * 8);
          const unsigned bits = (value_arg + 1u) * 8;
          if (bits < 32 && (value & (uint32_t{1} << (bits - 1)))) value |= UINT32_MAX << bits;
          access = value;
        } else if (name == "name") {
          if (value_type == 0x1e && value_arg == 0) continue;  // anonymous class
          if (value_type != 0x17 || value_arg > 3) Fail("Invalid InnerClass name");
          uint32_t index = 0;
          for (unsigned byte = 0; byte <= value_arg; ++byte)
            index |= uint32_t{DataByte(cursor++)} << (byte * 8);
          String(index);  // Validate the string index without retaining it.
        } else {
          Fail("Unexpected InnerClass annotation element");
        }
      }
      return access;
    }
    return std::nullopt;
  }
  void Fields(size_t* cursor, uint32_t count, bool is_static, std::vector<Member>* out) const {
    uint32_t index = 0;
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t delta = Leb(cursor), access = Leb(cursor);
      if ((i && !delta) || delta > UINT32_MAX - index) Fail("Invalid field index delta");
      index += delta;
      const size_t item = Item(fields_, index);
      if (U16(item) != class_type_ || ((access & kAccStatic) != 0) != is_static)
        Fail("Field owner or storage mismatch");
      std::string type = Type(U16(item + 2));
      if (type == "V") Fail("Invalid field type");
      out->push_back({String(U32(item + 4)), std::move(type), access});
    }
  }
  void Methods(size_t* cursor, uint32_t count, bool direct, std::vector<Member>* out) const {
    uint32_t index = 0;
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t delta = Leb(cursor), access = Leb(cursor);
      Leb(cursor);  // code_off; ART verifies bodies.
      if ((i && !delta) || delta > UINT32_MAX - index) Fail("Invalid method index delta");
      index += delta;
      const size_t item = Item(methods_, index);
      if (U16(item) != class_type_) Fail("Method owner mismatch");
      std::string name = String(U32(item + 4));
      const bool is_direct = (access & (kAccStatic | kAccPrivate)) || name == "<init>" || name == "<clinit>";
      if (name.empty() || is_direct != direct) Fail("Method dispatch category mismatch");
      out->push_back({std::move(name), Prototype(U16(item + 2)), access});
    }
  }
  static void Unique(const std::vector<Member>& members) {
    std::set<std::pair<std::string, std::string>> keys;
    for (const auto& m : members)
      if (!keys.emplace(m.name, m.descriptor).second) Fail("Duplicate member declaration");
  }

  DexView dex_;
  Table strings_, types_, protos_, fields_, methods_, classes_;
  uint32_t class_type_ = 0;
};
}  // namespace

bool ReadFieldIdentity(const DexView& dex, uint32_t index, Member* output, std::string* error) {
  try {
    *output = Reader(dex).FieldIdentity(index);
    error->clear();
    return true;
  } catch (const std::runtime_error& failure) {
    *error = failure.what();
    return false;
  }
}

bool ReadClassAccess(const DexView& dex, std::string_view descriptor, uint32_t* output,
                     std::string* error) {
  try {
    *output = Reader(dex).ClassAccess(descriptor);
    error->clear();
    return true;
  } catch (const std::runtime_error& failure) {
    *error = failure.what();
    return false;
  }
}

bool ReadMethodIdentity(const DexView& dex, uint32_t index, Member* output, std::string* error) {
  try {
    *output = Reader(dex).MethodIdentity(index);
    error->clear();
    return true;
  } catch (const std::runtime_error& failure) {
    *error = failure.what();
    return false;
  }
}

bool ReadClassDeclarations(const DexView& dex, ClassSchema* output, std::string* error) {
  try {
    *output = Reader(dex).Read();
    error->clear();
    return true;
  } catch (const std::runtime_error& failure) {
    *error = failure.what();
    return false;
  }
}
}  // namespace artdcevm::evolution
