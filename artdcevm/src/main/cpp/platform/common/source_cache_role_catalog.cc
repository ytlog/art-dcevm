#include "platform/common/source_cache_role_catalog.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace artdcevm::source_cache_role_catalog {
namespace {
[[noreturn]] void Fail(const char* message) { throw std::runtime_error(message); }
struct Table { uint32_t count, offset, width; };
class Reader {
 public:
  Reader(const unsigned char* bytes, size_t size) : bytes_(bytes), size_(size) {}
  Catalog Parse() {
    if (!bytes_ || size_ < 112 || std::memcmp(bytes_, "dex\n", 4) || bytes_[7] ||
        bytes_[4] != '0' || bytes_[5] < '3' || bytes_[5] > '4' ||
        bytes_[6] < '0' || bytes_[6] > '9') Fail("Not standalone StandardDEX");
    const unsigned version = (bytes_[5]-'0')*10 + bytes_[6]-'0';
    if (version < 35 || version > 40 || U32(32) != size_ || U32(36) != 112 ||
        U32(40) != 0x12345678) Fail("Invalid StandardDEX header");
    data_size_ = U32(104); data_off_ = U32(108);
    if (!data_size_ || data_off_ < 112 || (data_off_ & 3) ||
        data_off_ > size_ || data_size_ != size_-data_off_)
      Fail("Invalid DEX data section");
    tables_ = {GetTable(56,4), GetTable(64,4), GetTable(72,12),
               GetTable(80,8), GetTable(88,8), GetTable(96,32)};
    for (size_t i=0;i<tables_.size();++i) for (size_t j=0;j<i;++j) {
      const auto& a=tables_[i]; const auto& b=tables_[j];
      if (a.count && b.count && a.offset < b.offset+size_t(b.count)*b.width &&
          b.offset < a.offset+size_t(a.count)*a.width) Fail("Overlapping DEX ID tables");
    }
    if (tables_[1].count > 65535 || tables_[2].count > 65535)
      Fail("DEX type/proto ID limit exceeded");
    std::vector<std::string> strings;
    strings.reserve(tables_[0].count);
    for (uint32_t i=0;i<tables_[0].count;++i) strings.push_back(String(U32(Item(0,i))));
    auto str=[&](uint32_t i)->const std::string& {
      if (i>=strings.size()) Fail("Invalid DEX string index"); return strings[i];
    };
    Catalog result;
    result.types.reserve(tables_[1].count);
    for (uint32_t i=0;i<tables_[1].count;++i) {
      const auto& s=str(U32(Item(1,i)));
      if (!TypeValid(s,true)) Fail("Invalid DEX type descriptor");
      result.types.push_back(s);
    }
    auto type=[&](uint32_t i)->const std::string& {
      if (i>=result.types.size()) Fail("Invalid DEX type index"); return result.types[i];
    };
    std::vector<std::string> protos;
    protos.reserve(tables_[2].count);
    for (uint32_t i=0;i<tables_[2].count;++i) {
      const size_t at=Item(2,i); const auto& returns=type(U32(at+4));
      std::string descriptor="(", shorty(1,Shorty(returns));
      const uint32_t list=U32(at+8);
      if (list) {
        if (list&3) Fail("Unaligned DEX parameter list");
        DataRange(list,4); const uint32_t count=U32(list);
        DataRange(size_t(list)+4,size_t(count)*2);
        for (uint32_t n=0;n<count;++n) {
          const auto& parameter=type(U16(size_t(list)+4+size_t(n)*2));
          if (parameter=="V") Fail("Void DEX parameter");
          descriptor+=parameter; shorty+=Shorty(parameter);
        }
      }
      descriptor+=')'; descriptor+=returns;
      if (shorty!=str(U32(at))) Fail("DEX proto shorty mismatch");
      protos.push_back(std::move(descriptor));
    }
    result.fields.reserve(tables_[3].count);
    for (uint32_t i=0;i<tables_[3].count;++i) {
      const size_t at=Item(3,i);
      Member member{type(U16(at)),str(U32(at+4)),type(U16(at+2)),false,0};
      if (member.declaring_class[0]!='L' || member.descriptor=="V" ||
          !NameValid(member.name,false)) Fail("Invalid DEX field identity");
      result.fields.push_back(std::move(member));
    }
    result.methods.reserve(tables_[4].count);
    for (uint32_t i=0;i<tables_[4].count;++i) {
      const size_t at=Item(4,i); const uint16_t proto=U16(at+2);
      if (proto>=protos.size()) Fail("Invalid DEX proto index");
      Member member{type(U16(at)),str(U32(at+4)),protos[proto],false,0};
      if ((member.declaring_class[0]!='L' && member.declaring_class[0]!='[') ||
          !NameValid(member.name,true)) Fail("Invalid DEX method identity");
      result.methods.push_back(std::move(member));
    }
    std::vector<bool> classes(result.types.size(),false);
    for (uint32_t i=0;i<tables_[5].count;++i) {
      const size_t at=Item(5,i); const uint32_t owner=U32(at);
      const auto& declaring=type(owner);
      if (declaring[0]!='L' || classes[owner]) Fail("Invalid or duplicate DEX ClassDef");
      classes[owner]=true;
      const uint32_t parent=U32(at+8);
      if (parent!=UINT32_MAX && type(parent)[0]!='L') Fail("Invalid DEX superclass");
      const uint32_t interfaces=U32(at+12);
      if (interfaces) {
        if (interfaces&3) Fail("Unaligned DEX interface list");
        DataRange(interfaces,4); const uint32_t count=U32(interfaces);
        DataRange(size_t(interfaces)+4,size_t(count)*2);
        for (uint32_t n=0;n<count;++n)
          if (type(U16(size_t(interfaces)+4+size_t(n)*2))[0]!='L')
            Fail("Invalid DEX interface type");
      }
      const uint32_t source=U32(at+16); if (source!=UINT32_MAX) (void)str(source);
      size_t cursor=U32(at+24); if (!cursor) continue;
      std::array<uint32_t,4> counts{}; for (auto& count:counts) count=Leb(&cursor);
      if (uint64_t(counts[0])+counts[1]>result.fields.size() ||
          uint64_t(counts[2])+counts[3]>result.methods.size())
        Fail("Excessive DEX declaration count");
      for (unsigned section=0;section<4;++section) {
        uint32_t index=0;
        for (uint32_t n=0;n<counts[section];++n) {
          const uint32_t delta=Leb(&cursor);
          if ((n && !delta) || delta>UINT32_MAX-index) Fail("Invalid DEX declaration delta");
          index+=delta; const uint32_t flags=Leb(&cursor);
          auto& members=section<2 ? result.fields : result.methods;
          if (index>=members.size() || members[index].declared ||
              members[index].declaring_class!=declaring)
            Fail("DEX declaration ID owner mismatch or duplicate");
          if (section<2 && bool(flags&8)!=(section==0))
            Fail("DEX field static section mismatch");
          if (section>=2) {
            const bool direct=(flags & (8u|2u|0x10000u))!=0;
            if (direct!=(section==2)) Fail("DEX method direct section mismatch");
            const uint32_t code=Leb(&cursor);
            if (code) {
              if (code&3) Fail("Unaligned DEX code item");
              DataRange(code,16); DataRange(size_t(code)+16,size_t(U32(size_t(code)+12))*2);
              if (flags&0x500) Fail("Native or abstract DEX method has code");
            } else if (!(flags&0x500)) Fail("Concrete DEX method has no code");
          }
          members[index].declared=true; members[index].access=flags;
        }
      }
    }
    return result;
  }
 private:
  const unsigned char* bytes_; size_t size_;
  uint32_t data_off_=0,data_size_=0;
  std::array<Table,6> tables_{};
  void Range(size_t at,size_t count) const {
    if (at>size_ || count>size_-at) Fail("DEX range outside file");
  }
  void DataRange(size_t at,size_t count) const {
    Range(at,count);
    if (at<data_off_ || at-data_off_>data_size_ || count>data_size_-(at-data_off_))
      Fail("DEX item outside data section");
  }
  uint16_t U16(size_t at) const { Range(at,2); return bytes_[at]|uint16_t(bytes_[at+1])<<8; }
  uint32_t U32(size_t at) const {
    Range(at,4); return bytes_[at]|uint32_t(bytes_[at+1])<<8|
        uint32_t(bytes_[at+2])<<16|uint32_t(bytes_[at+3])<<24;
  }
  Table GetTable(size_t header,uint32_t width) const {
    const uint32_t count=U32(header),offset=U32(header+4);
    if ((count==0)!=(offset==0) || (count && ((offset&3) || offset<112 ||
        offset>data_off_ || count>(data_off_-offset)/width))) Fail("Invalid DEX ID table range");
    Range(offset,size_t(count)*width); return {count,offset,width};
  }
  size_t Item(size_t table,uint32_t index) const {
    const auto& t=tables_[table]; if (index>=t.count) Fail("Invalid DEX ID index");
    return t.offset+size_t(index)*t.width;
  }
  uint32_t Leb(size_t* cursor) const {
    uint32_t value=0;
    for (unsigned n=0;n<5;++n) {
      DataRange(*cursor,1); const unsigned char byte=bytes_[(*cursor)++];
      if (n==4 && (byte&0xf0)) Fail("Overflowing DEX ULEB128");
      value|=uint32_t(byte&127)<<(7*n); if (!(byte&128)) return value;
    }
    Fail("Unterminated DEX ULEB128");
  }
  std::string String(size_t cursor) const {
    const uint32_t units=Leb(&cursor); uint32_t read=0; const size_t start=cursor;
    for (;;) {
      DataRange(cursor,1); const uint8_t byte=bytes_[cursor++]; if (!byte) break;
      if (read==UINT32_MAX) Fail("DEX string too long"); ++read;
      if (byte<128) continue;
      const unsigned more=(byte&0xe0)==0xc0 ? 1 : ((byte&0xf0)==0xe0 ? 2 : 0);
      if (!more) Fail("Invalid DEX MUTF8 lead");
      uint32_t scalar=byte & (more==1 ? 31 : 15);
      for (unsigned n=0;n<more;++n) {
        DataRange(cursor,1); const uint8_t next=bytes_[cursor++];
        if ((next&0xc0)!=0x80) Fail("Invalid DEX MUTF8 continuation");
        scalar=(scalar<<6)|(next&63);
      }
      if ((more==1 && scalar && scalar<128) || (more==2 && scalar<2048))
        Fail("Overlong DEX MUTF8 string");
    }
    if (read!=units) Fail("DEX string UTF16 length mismatch");
    return {reinterpret_cast<const char*>(bytes_+start),cursor-start-1};
  }
  static char Shorty(const std::string& descriptor) {
    return descriptor[0]=='[' ? 'L' : descriptor[0];
  }
  static bool TypeValid(std::string_view s,bool allow_void) {
    size_t at=0; while (at<s.size() && s[at]=='[') ++at;
    if (at>255 || at==s.size()) return false;
    const char tag=s[at++];
    if (tag=='V') return allow_void && at==1 && at==s.size();
    if (std::strchr("ZBSCIJFD",tag) && tag) return at==s.size();
    if (tag!='L' || at+1>=s.size() || s.back()!=';') return false;
    bool segment=false;
    for (;at+1<s.size();++at) {
      const unsigned char c=s[at];
      if (!c || c=='.' || c=='[' || c=='(' || c==')' || c==';' || c==0xc0)
        return false;
      if (c=='/') { if (!segment) return false; segment=false; }
      else segment=true;
    }
    return segment;
  }
  static bool NameValid(std::string_view s,bool method) {
    if (method && (s=="<init>" || s=="<clinit>")) return true;
    if (s.empty()) return false;
    for (unsigned char c:s) if (!c || c=='.' || c==';' || c=='[' || c=='/' ||
        c=='(' || c==')' || c=='<' || c=='>' || c==0xc0) return false;
    return true;
  }
};
}  // namespace
bool Read(const unsigned char* bytes,size_t size,Catalog* out,std::string* error) noexcept {
  if (!out) return false;
  *out=Catalog{};
  try {
    Catalog parsed=Reader(bytes,size).Parse(); *out=std::move(parsed);
    if (error) error->clear(); return true;
  } catch (const std::exception& failure) {
    if (error) try { *error=failure.what(); } catch (...) { error->clear(); }
  } catch (...) {
    if (error) try { *error="DEX catalog allocation failure"; } catch (...) { error->clear(); }
  }
  return false;
}
}  // namespace artdcevm::source_cache_role_catalog
