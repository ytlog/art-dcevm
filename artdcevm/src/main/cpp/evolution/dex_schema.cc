#include "evolution/class_schema.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>

namespace artdcevm::evolution {
namespace {
bool ReadType(const std::string& s, size_t* at, bool allow_void) {
  if (*at >= s.size()) return false;
  size_t arrays = 0;
  while (*at < s.size() && s[*at] == '[') { ++*at; ++arrays; }
  if (*at >= s.size() || arrays > 255) return false;
  char c = s[(*at)++];
  if (c == 'V') return allow_void && arrays == 0;
  if (c != 0 && std::strchr("ZBSCIJFD", c)) return true;
  if (c != 'L') return false;
  size_t start = *at;
  while (*at < s.size() && s[*at] != ';') {
    const unsigned char ch = s[*at];
    if (ch == '.' || ch == '[' || ch == '(' || ch == ')' || ch == 0) return false;
    ++*at;
  }
  if (*at == start || *at == s.size()) return false;
  ++*at;
  return true;
}
class Reader {
 public:
  explicit Reader(const std::vector<unsigned char>& bytes) : bytes_(bytes) {}
  ClassSchema Read() {
    Range(0, 0x70);
    if (std::memcmp(bytes_.data(), "dex\n", 4) != 0 || bytes_[7] != 0)
      Fail("Not a standard DEX file");
    std::string version(reinterpret_cast<const char*>(bytes_.data() + 4), 3);
    if (version != "035" && version != "037" && version != "038" && version != "039" && version != "040")
      Fail("Unsupported DEX version: " + version);
    if (U32(32) != bytes_.size() || U32(36) != 0x70 || U32(40) != 0x12345678)
      Fail("Invalid DEX size, header size or endianness");
    uint32_t a = 1, b = 0;
    for (size_t i = 12; i < bytes_.size(); ++i) { a = (a + bytes_[i]) % 65521; b = (b + a) % 65521; }
    if (U32(8) != ((b << 16) | a)) Fail("DEX checksum mismatch");
    data_size_ = U32(104); data_off_ = U32(108);
    Range(data_off_, data_size_);
    if (data_size_ == 0 || data_off_ < 0x70 || data_off_ % 4 != 0 ||
        static_cast<size_t>(data_off_) + data_size_ != bytes_.size()) Fail("Invalid DEX data section");
    strings_ = Table(56, 4); types_ = Table(64, 4); protos_ = Table(72, 12);
    fields_ = Table(80, 8); methods_ = Table(88, 8);
    auto classes = Table(96, 32);
    if (classes.count != 1) Fail("Redefinition requires exactly one class definition");
    size_t def = classes.offset;
    class_type_ = U32(def);
    ClassSchema result;
    result.descriptor = Type(class_type_);
    if (result.descriptor.front() != 'L') Fail("A class definition must use a class descriptor");
    result.access = U32(def + 4);
    uint32_t super = U32(def + 8);
    if (super != UINT32_MAX) {
      result.superclass = Type(super);
      if (result.superclass.front() != 'L') Fail("Invalid superclass type");
    }
    result.interfaces = TypeList(U32(def + 12), true);
    uint32_t class_data = U32(def + 24);
    if (!class_data) return result;
    size_t cursor = class_data;
    DataRange(cursor, 1);
    uint32_t static_fields = Leb(&cursor), instance_fields = Leb(&cursor);
    uint32_t direct_methods = Leb(&cursor), virtual_methods = Leb(&cursor);
    if (static_fields > fields_.count || instance_fields > fields_.count - static_fields ||
        direct_methods > methods_.count || virtual_methods > methods_.count - direct_methods)
      Fail("Class member counts exceed their ID tables");
    ReadFields(&cursor, static_fields, true, &result.fields);
    ReadFields(&cursor, instance_fields, false, &result.fields);
    ReadMethods(&cursor, direct_methods, true, &result.methods);
    ReadMethods(&cursor, virtual_methods, false, &result.methods);
    Unique(result.fields); Unique(result.methods);
    return result;
  }
 private:
  struct IdTable { uint32_t offset, count, width; };
  const std::vector<unsigned char>& bytes_;
  uint32_t data_off_ = 0, data_size_ = 0, class_type_ = 0;
  IdTable strings_{}, types_{}, protos_{}, fields_{}, methods_{};
  [[noreturn]] static void Fail(const std::string& text) { throw std::runtime_error(text); }
  void Range(size_t offset, size_t size) const {
    if (offset > bytes_.size() || size > bytes_.size() - offset) Fail("DEX range outside file");
  }
  void DataRange(size_t offset, size_t size) const {
    Range(offset, size);
    if (offset < data_off_ || offset - data_off_ > data_size_ || size > data_size_ - (offset - data_off_))
      Fail("DEX item outside data section");
  }
  uint16_t U16(size_t off) const { Range(off, 2); return bytes_[off] | (uint16_t{bytes_[off + 1]} << 8); }
  uint32_t U32(size_t off) const {
    Range(off, 4);
    return bytes_[off] | (uint32_t{bytes_[off + 1]} << 8) | (uint32_t{bytes_[off + 2]} << 16) | (uint32_t{bytes_[off + 3]} << 24);
  }
  uint32_t Leb(size_t* cursor) const {
    uint32_t result = 0;
    for (unsigned i = 0; i < 5; ++i) {
      DataRange(*cursor, 1); uint8_t byte = bytes_[(*cursor)++];
      if (i == 4 && (byte & 0xf0)) Fail("Overflowing ULEB128");
      result |= uint32_t{byte & 0x7fu} << (7 * i);
      if (!(byte & 0x80)) return result;
    }
    Fail("Unterminated ULEB128");
  }
  IdTable Table(size_t header_offset, uint32_t width) const {
    uint32_t count = U32(header_offset), offset = U32(header_offset + 4);
    if ((count == 0) != (offset == 0)) Fail("Inconsistent ID table size/offset");
    if (count && (offset < 0x70 || offset % 4 || offset > data_off_ || count > (data_off_ - offset) / width))
      Fail("Invalid ID table range");
    Range(offset, size_t{count} * width);
    return {offset, count, width};
  }
  size_t Item(const IdTable& table, uint32_t index) const {
    if (index >= table.count) Fail("Invalid DEX table index");
    return table.offset + size_t{index} * table.width;
  }
  std::string String(uint32_t index) const {
    size_t cursor = U32(Item(strings_, index));
    uint32_t units = Leb(&cursor), read = 0;
    size_t start = cursor;
    for (;;) {
      DataRange(cursor, 1); uint8_t byte = bytes_[cursor++];
      if (byte == 0) break;
      ++read;
      if (byte < 0x80) continue;
      unsigned more = (byte & 0xe0) == 0xc0 ? 1 : ((byte & 0xf0) == 0xe0 ? 2 : 0);
      if (!more) Fail("Invalid modified UTF-8 lead byte");
      uint32_t scalar = byte & (more == 1 ? 0x1f : 0x0f);
      for (unsigned n = 0; n < more; ++n) {
        DataRange(cursor, 1); uint8_t next = bytes_[cursor++];
        if ((next & 0xc0) != 0x80) Fail("Invalid modified UTF-8 continuation");
        scalar = (scalar << 6) | (next & 0x3f);
      }
      if ((more == 1 && scalar != 0 && scalar < 0x80) || (more == 2 && scalar < 0x800))
        Fail("Overlong modified UTF-8 string");
    }
    if (read != units) Fail("String UTF-16 length mismatch");
    return std::string(reinterpret_cast<const char*>(bytes_.data() + start), cursor - start - 1);
  }
  std::string Type(uint32_t index) const {
    std::string value = String(U32(Item(types_, index)));
    if (!IsTypeDescriptor(value, true)) Fail("Invalid type descriptor");
    return value;
  }
  std::vector<std::string> TypeList(uint32_t offset, bool interfaces) const {
    if (!offset) return {};
    if (offset % 4) Fail("Unaligned type list");
    DataRange(offset, 4); uint32_t count = U32(offset);
    DataRange(size_t{offset} + 4, size_t{count} * 2);
    std::vector<std::string> result;
    std::set<std::string> seen;
    for (uint32_t i = 0; i < count; ++i) {
      auto value = Type(U16(size_t{offset} + 4 + size_t{i} * 2));
      if (!IsTypeDescriptor(value, false) || (interfaces && (value.front() != 'L' || !seen.insert(value).second)))
        Fail("Invalid parameter/interface type");
      result.push_back(std::move(value));
    }
    return result;
  }
  std::string Prototype(uint32_t index) const {
    size_t item = Item(protos_, index);
    std::string result = "(";
    for (const auto& type : TypeList(U32(item + 8), false)) result += type;
    result += ")" + Type(U32(item + 4));
    if (!IsMethodDescriptor(result)) Fail("Invalid method descriptor");
    return result;
  }
  void ReadFields(size_t* cursor, uint32_t count, bool is_static, std::vector<Member>* fields) const {
    uint32_t index = 0;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t delta = Leb(cursor), access = Leb(cursor);
      if ((i && delta == 0) || delta > UINT32_MAX - index) Fail("Invalid field index delta");
      index += delta; size_t item = Item(fields_, index);
      if (U16(item) != class_type_ || ((access & kStatic) != 0) != is_static) Fail("Field owner/storage mismatch");
      std::string type = Type(U16(item + 2)), name = String(U32(item + 4));
      if (!IsTypeDescriptor(type, false) || name.empty()) Fail("Invalid field name or type");
      fields->push_back({std::move(name), std::move(type), access, index});
    }
  }
  void ReadMethods(size_t* cursor, uint32_t count, bool direct, std::vector<Member>* methods) const {
    uint32_t index = 0;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t delta = Leb(cursor), access = Leb(cursor), code = Leb(cursor);
      if ((i && delta == 0) || delta > UINT32_MAX - index) Fail("Invalid method index delta");
      index += delta; size_t item = Item(methods_, index);
      if (U16(item) != class_type_) Fail("Method owner mismatch");
      std::string name = String(U32(item + 4)), descriptor = Prototype(U16(item + 2));
      bool is_direct = (access & (kStatic | 0x0002)) || name == "<init>" || name == "<clinit>";
      if (name.empty() || direct != is_direct) Fail("Method dispatch category mismatch");
      if (code) {
        if (code % 4 || (access & (kNative | kAbstract))) Fail("Invalid method code offset/flags");
        DataRange(code, 16);
        DataRange(size_t{code} + 16, size_t{U32(size_t{code} + 12)} * 2);
      } else if (!(access & (kNative | kAbstract))) Fail("Concrete method has no code");
      // Match Android 13 JVMTI GetMethodModifiers / Executable.fixMethodFlags.
      uint32_t java_access = access & ~uint32_t{0x0020};
      if (access & 0x20000) java_access |= 0x0020;
      if (access & kAbstract) java_access &= ~kNative;
      java_access &= 0xffff;
      methods->push_back({std::move(name), std::move(descriptor), java_access, index});
    }
  }
  static void Unique(const std::vector<Member>& members) {
    std::set<std::pair<std::string, std::string>> keys;
    for (const auto& member : members)
      if (!keys.emplace(member.name, member.descriptor).second) Fail("Duplicate declared member");
  }
};
}
bool IsTypeDescriptor(const std::string& value, bool allow_void) {
  size_t at = 0;
  return ReadType(value, &at, allow_void) && at == value.size();
}
bool IsMethodDescriptor(const std::string& value) {
  if (value.empty() || value.front() != '(') return false;
  size_t at = 1;
  while (at < value.size() && value[at] != ')') if (!ReadType(value, &at, false)) return false;
  if (at >= value.size() || value[at++] != ')') return false;
  return ReadType(value, &at, true) && at == value.size();
}
bool ReadDexSchema(const std::vector<unsigned char>& dex, ClassSchema* output, std::string* error) {
  try {
    auto schema = Reader(dex).Read();
    *output = std::move(schema);
    error->clear();
    return true;
  } catch (const std::exception& failure) {
    *error = failure.what();
    return false;
  }
}
}
