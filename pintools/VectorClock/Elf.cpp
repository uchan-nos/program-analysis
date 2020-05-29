#include "Elf.hpp"

#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

using namespace std;

size_t GetFileSize(const char* file_path) {
  ifstream f{file_path};
  f.seekg(0, ios_base::end);
  return f.tellg();
}

struct ElfDescriptor {
  const string& path;
  int fd;
  Elf64_Ehdr* ehdr;
  size_t length;
};

ElfDescriptor OpenElfFile(const char* file_path) {
  ElfDescriptor desc{file_path};

  desc.fd = open(file_path, O_RDONLY);
  if (desc.fd < 0) {
    auto err = strerror(errno);
    cerr << "Failed to open file '" << file_path << "': " << err << endl;
    return desc;
  }

  desc.length = GetFileSize(file_path);

  void* m = mmap(nullptr, desc.length, PROT_READ, MAP_PRIVATE, desc.fd, 0);
  if (m == MAP_FAILED) {
    auto err = strerror(errno);
    cerr << "Failed to map file '" << file_path << "': " << err << endl;
    close(desc.fd);
    desc.fd = -1;
    return desc;
  }

  desc.ehdr = reinterpret_cast<Elf64_Ehdr*>(m);
  return desc;
}

bool CloseElfFile(const ElfDescriptor& desc) {
  if (munmap(desc.ehdr, desc.length) < 0) {
    auto err = strerror(errno);
    cerr << "Failed to unmap file '" << desc.path << "': " << err << endl;
    return true;
  }

  close(desc.fd);
  return false;
}

Elf64_Shdr* GetSectionHeader(Elf64_Ehdr* ehdr, unsigned int ndx) {
  auto sections = reinterpret_cast<Elf64_Shdr*>(
      reinterpret_cast<uintptr_t>(ehdr) + ehdr->e_shoff);
  return &sections[ndx];
}

template <class T>
T GetSectionContent(Elf64_Ehdr* ehdr, Elf64_Shdr* sec) {
  if (sec == nullptr) {
    return nullptr;
  }

  return reinterpret_cast<T>(
      reinterpret_cast<uintptr_t>(ehdr) + sec->sh_offset);
}

const char* GetSectionName(Elf64_Ehdr* ehdr, Elf64_Shdr* sec) {
  if (ehdr->e_shstrndx == SHN_UNDEF) {
    return nullptr;
  }

  auto shstrtab_shdr = GetSectionHeader(ehdr, ehdr->e_shstrndx);
  return GetSectionContent<const char*>(ehdr, shstrtab_shdr) + sec->sh_name;
}

Elf64_Shdr* GetSectionHeader(Elf64_Ehdr* ehdr, const string& name) {
  for (int i = 0; i < ehdr->e_shnum; ++i) {
    auto sec = GetSectionHeader(ehdr, i);
    if (name == GetSectionName(ehdr, sec)) {
      return sec;
    }
  }
  return nullptr;
}

bool GetSymbols(const char* file_path,
                std::map<std::string, Elf64_Sym>& syms) {
  auto desc = OpenElfFile(file_path);
  if (desc.fd < 0) {
    fprintf(stderr, "Failed to open ELF file\n");
    return true;
  }

  if (desc.ehdr->e_shstrndx == SHN_UNDEF) {
    fprintf(stderr, "The ELF file has no .shstrtab\n");
    return true;
  }

  auto strtab = GetSectionContent<const char*>(
      desc.ehdr, GetSectionHeader(desc.ehdr, ".strtab"));
  if (strtab == nullptr) {
    fprintf(stderr, "Failed to find '.strtab'\n");
    return true;
  }

  auto symtab_shdr = GetSectionHeader(desc.ehdr, ".symtab");
  if (symtab_shdr == nullptr) {
    fprintf(stderr, "Failed to find '.symtab'\n");
    return true;
  }
  auto symtab = GetSectionContent<Elf64_Sym*>(desc.ehdr, symtab_shdr);

  for (size_t i = 0; i < symtab_shdr->sh_size / sizeof(Elf64_Sym); ++i) {
    auto sym_name = &strtab[symtab[i].st_name];
    syms[sym_name] = symtab[i];
  }

  return false;
}
