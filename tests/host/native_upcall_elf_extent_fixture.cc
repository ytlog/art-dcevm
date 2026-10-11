/* Compile this .cc with -x c: official xDL's overlay is C, unchanged below. */
#include "native_upcall_elf_extent_fixture.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
typedef struct {unsigned char e_ident[16];uint16_t e_type,e_machine;uint32_t e_version;uint64_t e_entry,e_phoff,e_shoff;uint32_t e_flags;uint16_t e_ehsize,e_phentsize,e_phnum,e_shentsize,e_shnum,e_shstrndx;} Elf64_Ehdr;
typedef struct {uint32_t sh_name,sh_type;uint64_t sh_flags,sh_addr,sh_offset,sh_size;uint32_t sh_link,sh_info;uint64_t sh_addralign,sh_entsize;} Elf64_Shdr;
typedef struct {uint32_t st_name;uint8_t st_info,st_other;uint16_t st_shndx;uint64_t st_value,st_size;} Elf64_Sym;
typedef struct {uint32_t p_type,p_flags;uint64_t p_offset,p_vaddr,p_paddr,p_filesz,p_memsz,p_align;} Elf64_Phdr;
#define ElfW(x) Elf64_##x
#define STT_FUNC 2
#define ELF64_ST_TYPE(i) ((i)&15)
#define PT_LOAD 1
#define PF_X 1
#define XDL_SYMTAB_IS_EXPORT_SYM(n) ((n)!=0 && (n)<0xff00)
typedef struct {uintptr_t load_bias;Elf64_Phdr* dlpi_phdr;size_t dlpi_phnum;Elf64_Sym* dynsym;const char* dynstr;size_t dynstr_sz;struct {size_t buckets_cnt;} gnu_hash,sysv_hash;Elf64_Sym* symtab;size_t symtab_cnt;char* strtab;size_t strtab_sz;size_t dynsym_cnt;} xdl_t;
static xdl_t module;
static unsigned char* file;
static size_t file_size,mapping_size;
static void* mapping;
static bool range(uint64_t off,uint64_t n){return off<=file_size&&n<=file_size-off;}
static Elf64_Sym* lookup(Elf64_Sym* syms,size_t n,const char* strings,size_t size,const char* name){for(size_t i=0;i<n;i++){uint32_t j=syms[i].st_name;if(j<size&&memchr(strings+j,0,size-j)&&strcmp(strings+j,name)==0)return syms+i;}return NULL;}
static void xdl_ensure_dynsym_loaded(xdl_t* p){(void)p;}
static void xdl_ensure_symtab_loaded(xdl_t* p){(void)p;}
static Elf64_Sym* xdl_dynsym_find_symbol_use_gnu_hash(xdl_t* p,const char* n){return lookup(p->dynsym,p->dynsym_cnt,p->dynstr,p->dynstr_sz,n);}
static Elf64_Sym* xdl_dynsym_find_symbol_use_sysv_hash(xdl_t* p,const char* n){return xdl_dynsym_find_symbol_use_gnu_hash(p,n);}
#include "support/shadowhook_metadata/exact_function_extent.inc"
void native_upcall_elf_unload(void){if(mapping)munmap(mapping,mapping_size);free(file);file=NULL;mapping=NULL;file_size=mapping_size=0;memset(&module,0,sizeof(module));}
int native_upcall_elf_load(const char* path){
 native_upcall_elf_unload();FILE* f=fopen(path,"rb");if(!f)return -1;
 if(fseek(f,0,SEEK_END)||ftell(f)<0){fclose(f);return -1;}file_size=(size_t)ftell(f);rewind(f);file=malloc(file_size);if(!file||fread(file,1,file_size,f)!=file_size){fclose(f);goto fail;}fclose(f);
 if(file_size<sizeof(Elf64_Ehdr))goto fail;Elf64_Ehdr* h=(void*)file;
 if(memcmp(h->e_ident,"\177ELF",4)||h->e_ident[4]!=2||h->e_ident[5]!=1||h->e_type!=3||h->e_machine!=183||h->e_phentsize!=sizeof(Elf64_Phdr)||h->e_shentsize!=sizeof(Elf64_Shdr)||!range(h->e_phoff,(uint64_t)h->e_phnum*sizeof(Elf64_Phdr))||!range(h->e_shoff,(uint64_t)h->e_shnum*sizeof(Elf64_Shdr)))goto fail;
 module.dlpi_phdr=(void*)(file+h->e_phoff);module.dlpi_phnum=h->e_phnum;uint64_t high=0;
 for(size_t i=0;i<h->e_phnum;i++){Elf64_Phdr* p=module.dlpi_phdr+i;if(p->p_type!=PT_LOAD)continue;if(!range(p->p_offset,p->p_filesz)||p->p_filesz>p->p_memsz||p->p_memsz>UINT64_MAX-p->p_vaddr)goto fail;if(p->p_vaddr+p->p_memsz>high)high=p->p_vaddr+p->p_memsz;}
 size_t page=(size_t)sysconf(_SC_PAGESIZE);if(!high||high>SIZE_MAX-page)goto fail;mapping_size=(high+page-1)&~(page-1);mapping=mmap(NULL,mapping_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);if(mapping==MAP_FAILED){mapping=NULL;goto fail;}module.load_bias=(uintptr_t)mapping;
 for(size_t i=0;i<h->e_phnum;i++){Elf64_Phdr* p=module.dlpi_phdr+i;if(p->p_type==PT_LOAD)memcpy((unsigned char*)mapping+p->p_vaddr,file+p->p_offset,p->p_filesz);}
 for(size_t i=0;i<h->e_phnum;i++){Elf64_Phdr* p=module.dlpi_phdr+i;if(p->p_type!=PT_LOAD)continue;size_t lo=p->p_vaddr&~(page-1),end=(p->p_vaddr+p->p_memsz+page-1)&~(page-1);int prot=((p->p_flags&4)?PROT_READ:0)|((p->p_flags&2)?PROT_WRITE:0)|((p->p_flags&1)?PROT_EXEC:0);if(end>lo&&mprotect((unsigned char*)mapping+lo,end-lo,prot))goto fail;}
 Elf64_Shdr* sh=(void*)(file+h->e_shoff);
 for(size_t i=0;i<h->e_shnum;i++){Elf64_Shdr* s=sh+i;if(s->sh_type!=2&&s->sh_type!=11)continue;if(s->sh_link>=h->e_shnum||s->sh_entsize!=sizeof(Elf64_Sym)||s->sh_size%sizeof(Elf64_Sym)||!range(s->sh_offset,s->sh_size))goto fail;Elf64_Shdr* str=sh+s->sh_link;if(str->sh_type!=3||!range(str->sh_offset,str->sh_size))goto fail;if(s->sh_type==2){module.symtab=(void*)(file+s->sh_offset);module.symtab_cnt=s->sh_size/sizeof(Elf64_Sym);module.strtab=(void*)(file+str->sh_offset);module.strtab_sz=str->sh_size;}else {module.dynsym=(void*)(file+s->sh_offset);module.dynsym_cnt=s->sh_size/sizeof(Elf64_Sym);module.dynstr=(void*)(file+str->sh_offset);module.dynstr_sz=str->sh_size;module.gnu_hash.buckets_cnt=1;}}
 if(!module.symtab||!module.dynsym)goto fail;return 0;
fail:native_upcall_elf_unload();return -1;
}
static Elf64_Sym* symbol(const char* name){return lookup(module.symtab,module.symtab_cnt,module.strtab,module.strtab_sz,name);}
void* native_upcall_elf_entry(const char* n){Elf64_Sym* s=symbol(n);return s?(void*)(module.load_bias+s->st_value):NULL;}
size_t native_upcall_elf_size(const char* n){Elf64_Sym* s=symbol(n);return s?s->st_size:0;}
uintptr_t native_upcall_elf_return_pc(const char* n){size_t z=native_upcall_elf_size(n);return z>4?(uintptr_t)native_upcall_elf_entry(n)+4:0;}
void* shadowhook_dlopen(const char* n){return file&&strcmp(n,"libart.so")==0?&module:NULL;}
void* shadowhook_dlsym(void* h,const char* n){return h==&module?native_upcall_elf_entry(n):NULL;}
void shadowhook_dlclose(void* h){(void)h;}
int native_upcall_elf_corrupt(const char* name,const char* mutation){Elf64_Sym* s=symbol(name);if(!s)return -1;if(strcmp(mutation,"pf-x")==0){for(size_t i=0;i<module.dlpi_phnum;i++)module.dlpi_phdr[i].p_flags&=~PF_X;return 0;}Elf64_Sym* d=lookup(module.dynsym,module.dynsym_cnt,module.dynstr,module.dynstr_sz,name);Elf64_Sym* both[2]={s,d};for(size_t i=0;i<2;i++){if(!both[i])continue;if(strcmp(mutation,"object")==0)both[i]->st_info=(both[i]->st_info&0xf0)|1;else if(strcmp(mutation,"zero-size")==0)both[i]->st_size=0;else if(strcmp(mutation,"name")==0)both[i]->st_name=0;else return -1;}return 0;}
