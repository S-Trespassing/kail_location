/*
 * Simplified ELF utility implementation for Android
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <elf.h>
#include <dlfcn.h>
#include <android/log.h>

#include "elf_util.h"

#define LOG_TAG "ElfUtil"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

ElfImg::ElfImg(const char* elf_name) : elf(elf_name) {
    initModuleBase();
    if (!base) return;
    
    ALOGI("ElfImg: base=%p", base);
    
    int fd = open(elf.c_str(), O_RDONLY);
    if (fd < 0) {
        ALOGE("Failed to open %s", elf.c_str());
        return;
    }
    
    size = lseek(fd, 0, SEEK_END);
    if (size <= 0) {
        ALOGE("lseek() failed for %s", elf.c_str());
        close(fd);
        return;
    }
    
    void* mapped = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    
    if (mapped == MAP_FAILED) {
        ALOGE("mmap failed");
        return;
    }
    
    parseDynamic();
}

void ElfImg::initModuleBase() {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return;
    
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, elf.c_str())) {
            uint64_t start;
            sscanf(line, "%lx-", &start);
            base = (void*)start;
            ALOGI("Found %s at base=%p", elf.c_str(), base);
            break;
        }
    }
    fclose(fp);
}

void ElfImg::parseDynamic() {
    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)base;
    Elf64_Phdr* phdr = (Elf64_Phdr*)((char*)base + ehdr->e_phoff);
    
    ALOGI("parseDynamic: e_phnum=%d", ehdr->e_phnum);
    
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_DYNAMIC) {
            bias = (off_t)phdr[i].p_vaddr - (off_t)phdr[i].p_offset;
            ALOGI("Found PT_DYNAMIC at p_vaddr=0x%lx, bias=0x%lx", phdr[i].p_vaddr, bias);
            break;
        }
    }
    
    if (!bias) {
        ALOGE("No bias found!");
        return;
    }
    
    // Find dynamic section using program header
    Elf64_Dyn* dynamic = (Elf64_Dyn*)((char*)base + 0x1000); // approximate
    
    ALOGI("Scanning dynamic section...");
    
    for (int i = 0; i < 100; i++) {
        if (dynamic[i].d_tag == DT_NULL) break;
        
        if (dynamic[i].d_tag == DT_STRTAB) {
            strtab = (void*)(dynamic[i].d_un.d_val + bias);
            ALOGI("Found DT_STRTAB: %p", strtab);
        } else if (dynamic[i].d_tag == DT_SYMTAB) {
            dynsym = (void*)(dynamic[i].d_un.d_val + bias);
            ALOGI("Found DT_SYMTAB: %p", dynsym);
        } else if (dynamic[i].d_tag == DT_GNU_HASH) {
            gnu_hash = (uint32_t*)(dynamic[i].d_un.d_val + bias);
            ALOGI("Found DT_GNU_HASH: %p", gnu_hash);
            if (gnu_hash) {
                nbucket = gnu_hash[0];
                bucket = &gnu_hash[4 + (gnu_hash[2] / sizeof(void*))];
                chain = &bucket[nbucket];
                ALOGI("GNU_HASH: nbucket=%d", nbucket);
            }
        } else if (dynamic[i].d_tag == DT_HASH) {
            uint32_t* h = (uint32_t*)(dynamic[i].d_un.d_val + bias);
            ALOGI("Found DT_HASH: %p", h);
            nbucket = h[0];
            symtab_count = h[1];
            bucket = &h[2];
            chain = &bucket[nbucket];
            ALOGI("ELF_HASH: nbucket=%d, symtab_count=%zu", nbucket, symtab_count);
        }
    }
    
    if (!dynsym || !strtab) {
        ALOGE("Missing dynsym or strtab! dynsym=%p strtab=%p", dynsym, strtab);
    }
}

uint32_t ElfImg::elfHash(const char* name) const {
    uint32_t h = 0;
    while (*name) {
        h = (h << 4) + *name++;
        uint32_t g = h & 0xf0000000;
        h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

uint32_t ElfImg::gnuHash(const char* name) const {
    uint32_t h = 5381;
    while (*name) {
        h += (h << 5) + *name++;
    }
    return h;
}

void* ElfImg::findSymbol(const char* name) {
    if (!dynsym || !strtab) {
        ALOGE("findSymbol: dynsym or strtab null");
        return nullptr;
    }
    
    if (gnu_hash) {
        uint32_t h = gnuHash(name);
        uint32_t idx = bucket[h % nbucket];
        
        ALOGI("Searching for %s, hash=0x%x, idx=%d", name, h, idx);
        
        int attempts = 0;
        while (idx != 0 && attempts < 100) {
            char* sym_name = (char*)strtab + ((Elf64_Sym*)dynsym)[idx].st_name;
            if (strcmp(sym_name, name) == 0) {
                void* addr = (void*)(((Elf64_Sym*)dynsym)[idx].st_value + bias);
                ALOGI("Found %s at %p", name, addr);
                return addr;
            }
            idx = chain[idx];
            attempts++;
        }
    }
    
    return nullptr;
}

void* ElfImg::getSymbolAddress(const char* name) {
    return findSymbol(name);
}

void* ElfImg::getSymbolAddressByPrefix(const char* prefix) {
    if (!dynsym || !strtab) {
        ALOGE("getSymbolAddressByPrefix: dynsym or strtab null");
        return nullptr;
    }
    
    size_t prefix_len = strlen(prefix);
    
    ALOGI("Prefix searching for: %s", prefix);
    
    // Iterate through symbol table
    for (uint32_t i = 1; i < 10000; i++) {
        Elf64_Sym* sym = ((Elf64_Sym*)dynsym) + i;
        if (sym->st_name == 0) continue;
        
        char* sym_name = (char*)strtab + sym->st_name;
        if (sym_name && strncmp(sym_name, prefix, prefix_len) == 0) {
            void* addr = (void*)(sym->st_value + bias);
            uintptr_t addr_val = (uintptr_t)addr;
            uintptr_t base_val = (uintptr_t)base;
            
            if (addr_val >= base_val && addr_val <= base_val + 0x200000) {
                ALOGI("Found prefix match: %s at %p (st_value=0x%lx)", 
                      sym_name, addr, sym->st_value);
                return addr;
            }
        }
    }
    
    ALOGI("Prefix %s not found", prefix);
    return nullptr;
}
