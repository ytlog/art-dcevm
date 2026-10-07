#include "support/elf_modules.h"
#include <cstring>
#include <elf.h>
#include <iomanip>
#include <link.h>
#include <sstream>
namespace artdcevm {
namespace {
struct Search { const char* basename; LoadedModule result; };
int Collect(dl_phdr_info* info, size_t, void* output) {
  auto* search = static_cast<Search*>(output);
  if (!info->dlpi_name) return 0;
  const char* slash = std::strrchr(info->dlpi_name, '/');
  if (std::strcmp(slash ? slash + 1 : info->dlpi_name, search->basename) != 0) return 0;
  search->result.path = info->dlpi_name;
  for (int i = 0; i < info->dlpi_phnum; ++i) {
    const auto& phdr = info->dlpi_phdr[i];
    if (phdr.p_type != PT_NOTE) continue;
    const auto* p = reinterpret_cast<const unsigned char*>(info->dlpi_addr + phdr.p_vaddr);
    const auto* end = p + phdr.p_memsz;
    while (static_cast<size_t>(end - p) >= sizeof(ElfW(Nhdr))) {
      const auto* n = reinterpret_cast<const ElfW(Nhdr)*>(p);
      const size_t names = (size_t{n->n_namesz} + 3u) & ~size_t{3};
      const size_t descs = (size_t{n->n_descsz} + 3u) & ~size_t{3};
      const size_t remaining = static_cast<size_t>(end - p) - sizeof(*n);
      if (names > remaining || descs > remaining - names) break;
      const auto* name = p + sizeof(*n);
      const auto* desc = name + names;
      if (n->n_type == NT_GNU_BUILD_ID && n->n_namesz == 4 && std::memcmp(name, "GNU", 4) == 0) {
        std::ostringstream id;
        for (size_t j = 0; j < n->n_descsz; ++j) id << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(desc[j]);
        search->result.build_id = id.str();
      }
      p += sizeof(*n) + names + descs;
    }
  }
  return 1;
}
}
LoadedModule FindLoadedModule(const char* basename) {
  Search search{basename, {}};
  dl_iterate_phdr(Collect, &search);
  return search.result;
}
}
