#include "platform/common/art_runtime_profile.h"

#include <cstring>
#include <elf.h>
#include <link.h>
#include <string_view>

namespace artdcevm::method_retirement {
namespace {
struct Match {
  const BuildId& art_id;
  const BuildId& jvmti_id;
  bool art = false;
  bool jvmti = false;
};
size_t Padded(uint32_t size) { return (static_cast<size_t>(size) + 3) & ~size_t{3}; }

bool BuildIdMatches(const dl_phdr_info& library, const BuildId& expected) {
  for (ElfW(Half) i = 0; i < library.dlpi_phnum; ++i) {
    const auto& note = library.dlpi_phdr[i];
    if (note.p_type != PT_NOTE || note.p_memsz > 64 * 1024) continue;
    // Only inspect loader-mapped readable metadata. No ELF symbol value or ART
    // object is read; p_vaddr locates a public ELF note, not a private service.
    bool readable = false;
    for (ElfW(Half) j = 0; j < library.dlpi_phnum; ++j) {
      const auto& load = library.dlpi_phdr[j];
      if (load.p_type == PT_LOAD && (load.p_flags & PF_R) &&
          note.p_vaddr >= load.p_vaddr && note.p_vaddr - load.p_vaddr <= load.p_memsz &&
          note.p_memsz <= load.p_memsz - (note.p_vaddr - load.p_vaddr)) {
        readable = true;
        break;
      }
    }
    if (!readable) continue;
    auto* data = reinterpret_cast<const unsigned char*>(library.dlpi_addr + note.p_vaddr);
    size_t remaining = note.p_memsz;
    while (remaining >= sizeof(ElfW(Nhdr))) {
      ElfW(Nhdr) header;
      std::memcpy(&header, data, sizeof(header));
      data += sizeof(header);
      remaining -= sizeof(header);
      const size_t name = Padded(header.n_namesz);
      const size_t description = Padded(header.n_descsz);
      if (name > remaining || description > remaining - name) break;
      if (header.n_type == NT_GNU_BUILD_ID && header.n_namesz == 4 &&
          std::memcmp(data, "GNU", 4) == 0 && header.n_descsz == expected.size() &&
          std::memcmp(data + name, expected.data(), expected.size()) == 0) return true;
      data += name + description;
      remaining -= name + description;
    }
  }
  return false;
}
int Inspect(dl_phdr_info* library, size_t, void* state) {
  auto& match = *static_cast<Match*>(state);
  const std::string_view path(library->dlpi_name ? library->dlpi_name : "");
  const size_t slash = path.rfind('/');
  const auto filename = path.substr(slash == std::string_view::npos ? 0 : slash + 1);
  if (filename == "libart.so") match.art = BuildIdMatches(*library, match.art_id);
  if (filename == "libopenjdkjvmti.so") match.jvmti = BuildIdMatches(*library, match.jvmti_id);
  return 0;
}
}

bool LoadedBuildIdsMatch(const BuildId& art, const BuildId& jvmti) {
  Match match{art, jvmti};
  dl_iterate_phdr(Inspect, &match);
  return match.art && match.jvmti;
}
}
