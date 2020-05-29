#include <elf.h>
#include <map>
#include <string>

bool GetSymbols(const char* file_path,
                std::map<std::string, Elf64_Sym>& syms);
