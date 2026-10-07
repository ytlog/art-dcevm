#include "evolution/class_schema.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
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
  explicit Reader(const std::vector<unsigned char>& bytes, bool declaration_flags = false)
      : begin_(bytes.data()), size_(bytes.size()), data_(bytes.data()), data_length_(bytes.size()),
        declaration_flags_(declaration_flags) {}
  explicit Reader(const MappedDexMetadata& view)
      : begin_(view.begin), size_(view.size), data_(view.data_begin), data_length_(view.data_size),
        declaration_flags_(true), mapped_(true), class_def_index_(view.class_def_index) {}
  ClassSchema Read() {
    Initialize();
    auto classes = Table(96, 32);
    if (!mapped_ && classes.count != 1) Fail("Redefinition requires exactly one class definition");
    size_t def = Item(classes, class_def_index_);
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
  MappedDexMethodReference ReadMethodReference(uint32_t index) {
    Initialize();
    const size_t item = Item(methods_, index);
    MappedDexMethodReference result;
    result.class_index = U16(item);
    result.proto_index = U16(item + 2);
    result.declaring_class = Type(result.class_index);
    if (result.declaring_class.front() != 'L' && result.declaring_class.front() != '[')
      Fail("Invalid method declaring type");
    result.name = String(U32(item + 4));
    if (result.name.empty()) Fail("Empty method reference name");
    result.descriptor = Prototype(result.proto_index);
    return result;
  }
  bool SupportsDefaultMethods() {
    Initialize();
    if(std::memcmp(begin_,"cdex001\0",8)==0)return (U32(0x70)&1)!=0;
    return std::memcmp(begin_+4,"037",3)>=0;
  }
 private:
  void Initialize() {
    if (!begin_ || !data_) Fail("Missing DEX mapping");
    Range(0, 0x70);
    const bool compact = mapped_ && std::memcmp(begin_, "cdex001\0", 8) == 0;
    if (!compact && (std::memcmp(begin_, "dex\n", 4) != 0 || begin_[7] != 0))
      Fail("Not a supported DEX file");
    const size_t header_size = compact ? 0x88 : 0x70;
    Range(0, header_size);
    if (!compact) {
      std::string version(reinterpret_cast<const char*>(begin_ + 4), 3);
      if (version != "035" && version != "037" && version != "038" && version != "039" && version != "040")
        Fail("Unsupported DEX version: " + version);
    }
    if (U32(36) != header_size || U32(40) != 0x12345678 || (!mapped_ && U32(32) != size_))
      Fail("Invalid DEX size, header size or endianness");
    data_size_ = U32(104); data_off_ = compact ? 0 : U32(108);
    if (!mapped_) {
      uint32_t a = 1, b = 0;
      for (size_t i = 12; i < size_; ++i) { a = (a + begin_[i]) % 65521; b = (b + a) % 65521; }
      if (U32(8) != ((b << 16) | a)) Fail("DEX checksum mismatch");
      Range(data_off_, data_size_);
      if (data_size_ == 0 || data_off_ < 0x70 || data_off_ % 4 != 0 ||
          static_cast<size_t>(data_off_) + data_size_ != size_) Fail("Invalid DEX data section");
    } else if (data_off_ > data_length_) {
      Fail("Invalid mapped DEX data range");
    }
    table_start_ = header_size;
    strings_ = Table(56, 4); types_ = Table(64, 4); protos_ = Table(72, 12);
    fields_ = Table(80, 8); methods_ = Table(88, 8);
  }
  struct IdTable { uint32_t offset, count, width; };
  const unsigned char* begin_;
  const size_t size_;
  const unsigned char* data_;
  const size_t data_length_;
  const bool declaration_flags_;
  bool mapped_ = false;
  uint32_t class_def_index_ = 0;
  size_t table_start_ = 0x70;
  uint32_t data_off_ = 0, data_size_ = 0, class_type_ = 0;
  IdTable strings_{}, types_{}, protos_{}, fields_{}, methods_{};
  [[noreturn]] static void Fail(const std::string& text) { throw std::runtime_error(text); }
  void Range(size_t offset, size_t size) const {
    if (offset > size_ || size > size_ - offset) Fail("DEX range outside file");
  }
  void DataRange(size_t offset, size_t size) const {
    if (offset > data_length_ || size > data_length_ - offset || offset < data_off_ ||
        (!mapped_ && (offset - data_off_ > data_size_ || size > data_size_ - (offset - data_off_))))
      Fail("DEX item outside data section");
  }
  uint16_t U16(size_t off) const { Range(off, 2); return begin_[off] | (uint16_t{begin_[off + 1]} << 8); }
  uint32_t U32(size_t off) const {
    Range(off, 4);
    return begin_[off] | (uint32_t{begin_[off + 1]} << 8) | (uint32_t{begin_[off + 2]} << 16) | (uint32_t{begin_[off + 3]} << 24);
  }
  uint16_t DataU16(size_t off) const {
    DataRange(off, 2); return data_[off] | (uint16_t{data_[off + 1]} << 8);
  }
  uint32_t DataU32(size_t off) const {
    DataRange(off, 4);
    return data_[off] | (uint32_t{data_[off + 1]} << 8) | (uint32_t{data_[off + 2]} << 16) | (uint32_t{data_[off + 3]} << 24);
  }
  uint32_t Leb(size_t* cursor) const {
    uint32_t result = 0;
    for (unsigned i = 0; i < 5; ++i) {
      DataRange(*cursor, 1); uint8_t byte = data_[(*cursor)++];
      if (i == 4 && (byte & 0xf0)) Fail("Overflowing ULEB128");
      result |= uint32_t{byte & 0x7fu} << (7 * i);
      if (!(byte & 0x80)) return result;
    }
    Fail("Unterminated ULEB128");
  }
  IdTable Table(size_t header_offset, uint32_t width) const {
    uint32_t count = U32(header_offset), offset = U32(header_offset + 4);
    if ((count == 0) != (offset == 0)) Fail("Inconsistent ID table size/offset");
    if (count && (offset < table_start_ || offset % 4 ||
        (!mapped_ && (offset > data_off_ || count > (data_off_ - offset) / width))))
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
      DataRange(cursor, 1); uint8_t byte = data_[cursor++];
      if (byte == 0) break;
      ++read;
      if (byte < 0x80) continue;
      unsigned more = (byte & 0xe0) == 0xc0 ? 1 : ((byte & 0xf0) == 0xe0 ? 2 : 0);
      if (!more) Fail("Invalid modified UTF-8 lead byte");
      uint32_t scalar = byte & (more == 1 ? 0x1f : 0x0f);
      for (unsigned n = 0; n < more; ++n) {
        DataRange(cursor, 1); uint8_t next = data_[cursor++];
        if ((next & 0xc0) != 0x80) Fail("Invalid modified UTF-8 continuation");
        scalar = (scalar << 6) | (next & 0x3f);
      }
      if ((more == 1 && scalar != 0 && scalar < 0x80) || (more == 2 && scalar < 0x800))
        Fail("Overlong modified UTF-8 string");
    }
    if (read != units) Fail("String UTF-16 length mismatch");
    return std::string(reinterpret_cast<const char*>(data_ + start), cursor - start - 1);
  }
  std::string Type(uint32_t index) const {
    std::string value = String(U32(Item(types_, index)));
    if (!IsTypeDescriptor(value, true)) Fail("Invalid type descriptor");
    return value;
  }
  std::vector<std::string> TypeList(uint32_t offset, bool interfaces) const {
    if (!offset) return {};
    if (offset % 4) Fail("Unaligned type list");
    DataRange(offset, 4); uint32_t count = DataU32(offset);
    DataRange(size_t{offset} + 4, size_t{count} * 2);
    std::vector<std::string> result;
    std::set<std::string> seen;
    for (uint32_t i = 0; i < count; ++i) {
      auto value = Type(DataU16(size_t{offset} + 4 + size_t{i} * 2));
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
      if (!mapped_ && code) {
        if (code % 4 || (access & (kNative | kAbstract))) Fail("Invalid method code offset/flags");
        DataRange(code, 16);
        DataRange(size_t{code} + 16, size_t{DataU32(size_t{code} + 12)} * 2);
      } else if (!mapped_ && !(access & (kNative | kAbstract))) Fail("Concrete method has no code");
      // Match Android 13 JVMTI GetMethodModifiers / Executable.fixMethodFlags.
      uint32_t java_access = access & ~uint32_t{0x0020};
      if (access & 0x20000) java_access |= 0x0020;
      if (access & kAbstract) java_access &= ~kNative;
      java_access &= 0xffff;
      methods->push_back({std::move(name), std::move(descriptor), declaration_flags_ ? access : java_access, index});
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
static bool ReadDex(const std::vector<unsigned char>& dex, ClassSchema* output, std::string* error,
                    bool declaration_flags) {
  try {
    auto schema = Reader(dex, declaration_flags).Read();
    *output = std::move(schema);
    error->clear();
    return true;
  } catch (const std::bad_alloc&) {
    // The ART/JVMTI boundary reports allocation failures as OUT_OF_MEMORY.
    throw;
  } catch (const std::exception& failure) {
    *error = failure.what();
    return false;
  }
}
bool ReadDexSchema(const std::vector<unsigned char>& dex, ClassSchema* output, std::string* error) {
  return ReadDex(dex, output, error, false);
}

bool ReadDexDeclarations(const std::vector<unsigned char>& dex, ClassSchema* output, std::string* error) {
  return ReadDex(dex, output, error, true);
}

bool ReadMappedDexSupportsDefaultMethods(const MappedDexMetadata& view,bool* output,std::string* error) {
  try {
    const bool supported=Reader(view).SupportsDefaultMethods();
    *output=supported;error->clear();return true;
  } catch(const std::bad_alloc&) {throw;}
  catch(const std::exception& failure) {*error=failure.what();return false;}
}

bool ReadMappedDexMethodReference(const MappedDexMetadata& view, uint32_t index,
                                  MappedDexMethodReference* output, std::string* error) {
  try {
    auto reference = Reader(view).ReadMethodReference(index);
    *output = std::move(reference);
    error->clear();
    return true;
  } catch (const std::bad_alloc&) {
    throw;
  } catch (const std::exception& failure) {
    *error = failure.what();
    return false;
  }
}

bool ReadMappedDexDeclarations(const MappedDexMetadata& view, ClassSchema* output, std::string* error) {
  try {
    auto schema = Reader(view).Read();
    *output = std::move(schema);
    error->clear();
    return true;
  } catch (const std::bad_alloc&) {
    throw;
  } catch (const std::exception& failure) {
    *error = failure.what();
    return false;
  }
}

}
