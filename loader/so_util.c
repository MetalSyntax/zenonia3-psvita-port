/* so_util.c -- utils to load and hook .so modules
 *
 * Copyright (C) 2021 Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.	See the LICENSE file for details.
 */

#include <vitasdk.h>
#include <kubridge.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "so_util.h"

extern void game_log(const char *fmt, ...);
extern void fatal_error(const char *fmt, ...);

#ifdef EMULATOR_BUILD
// Vita3K does not implement the kuKernelCpuUnrestrictedMemcpy NID either; under
// the emulator all the memory we write into here is memory we allocated
// ourselves (see the EMULATOR_BUILD path in _so_load), so a plain memcpy works.
#define ku_memcpy(dst, src, n) memcpy((dst), (src), (n))
// Nor does it implement kuKernelFlushCaches. Vita3K's CPU emulation always
// reads fresh memory (no real instruction cache to keep coherent), so this is
// a safe no-op under EMULATOR_BUILD.
#define ku_flush_caches(addr, size) ((void)0)
#else
// Real hardware: unprivileged sceKernelAllocMemBlock() cannot create
// executable memory (W^X enforced by the MMU) -- kuKernelAllocMemBlock is
// kubridge's kernel-level allocator that can, and kuKernelCpuUnrestrictedMemcpy/
// kuKernelFlushCaches are needed to write into and sync that memory. Without
// this, the text segment ends up RW-only and any attempt to execute code from
// it faults with a Prefetch Abort exactly at the first instruction fetched.
#define ku_memcpy(dst, src, n) kuKernelCpuUnrestrictedMemcpy((dst), (src), (n))
#define ku_flush_caches(addr, size) kuKernelFlushCaches((addr), (size))
#endif

typedef struct b_enc {
    union {
        struct __attribute__((__packed__)) {
            int imm24: 24;
            unsigned int l: 1; // Branch with Link flag
            unsigned int enc: 3; // 0b101
            unsigned int cond: 4; // 0b1110
        } bits;
        uint32_t raw;
    };
} b_enc;

typedef struct ldst_enc {
    union {
        struct __attribute__((__packed__)) {
            int imm12: 12;
            unsigned int rt: 4; // Source/Destination register
            unsigned int rn: 4; // Base register
            unsigned int bit20_1: 1; // 0: store to memory, 1: load from memory
            unsigned int w: 1; // 0: no write-back, 1: write address into base
            unsigned int b: 1; // 0: word, 1: byte
            unsigned int u: 1; // 0: subtract offset from base, 1: add to base
            unsigned int p: 1; // 0: post indexing, 1: pre indexing
            unsigned int enc: 3;
            unsigned int cond: 4;
        } bits;
        uint32_t raw;
    };
} ldst_enc;

#define B_RANGE ((1 << 24) - 1)
#define B_OFFSET(x) (x + 8) // branch jumps into addr - 8, so range is biased forward
#define B(PC, DEST) ((b_enc){.bits = {.cond = 0b1110, .enc = 0b101, .l = 0, .imm24 = (((intptr_t)DEST-(intptr_t)PC) / 4) - 2}})
#define LDR_OFFS(RT, RN, IMM) ((ldst_enc){.bits = {.cond = 0b1110, .enc = 0b010, .p = 1, .u = (IMM >= 0), .b = 0, .w = 0, .bit20_1 = 1, .rn = RN, .rt = RT, .imm12 = (IMM >= 0) ? IMM : -IMM}})

#define PATCH_SZ 0x10000 //64 KB-ish arenas
static so_module *head = NULL, *tail = NULL;

so_hook hook_thumb(uintptr_t addr, uintptr_t dst) {
    so_hook h;
    if (addr == 0)
        return h;
    h.thumb_addr = addr;
    addr &= ~1;
    if (addr & 2) {
        uint16_t nop = 0xbf00;
        ku_memcpy((void *)addr, &nop, sizeof(nop));
        addr += 2;
    }

    h.addr = addr;
    h.patch_instr[0] = 0xf000f8df; // LDR PC, [PC]
    h.patch_instr[1] = dst;
    ku_memcpy(&h.orig_instr, (void *)addr, sizeof(h.orig_instr));
    ku_memcpy((void *)addr, h.patch_instr, sizeof(h.patch_instr));

    return h;
}

so_hook hook_arm(uintptr_t addr, uintptr_t dst) {
    so_hook h;
    if (addr == 0)
        return h;
    h.thumb_addr = 0;
    h.addr = addr;
    h.patch_instr[0] = 0xe51ff004; // LDR PC, [PC, #-0x4]
    h.patch_instr[1] = dst;
    ku_memcpy(&h.orig_instr, (void *)addr, sizeof(h.orig_instr));
    ku_memcpy((void *)addr, h.patch_instr, sizeof(h.patch_instr));

    return h;
}

so_hook hook_addr(uintptr_t addr, uintptr_t dst) {
    if (addr == 0) {
        so_hook h;
        return h;
    }
    if (addr & 1)
        return hook_thumb(addr, dst);
    else
        return hook_arm(addr, dst);
}

void so_unhook(so_hook *hook) {
    ku_memcpy((void *)hook->addr, hook->orig_instr, sizeof(hook->orig_instr));
    ku_flush_caches((void *)hook->addr, sizeof(hook->orig_instr));
}

void so_flush_caches(so_module *mod) {
    ku_flush_caches((void *)mod->text_base, mod->text_size);
}

int _so_load(so_module *mod, SceUID so_blockid, void *so_data, uintptr_t load_addr) {
    int res = 0;
    uintptr_t data_addr = 0;

    if (memcmp(so_data, ELFMAG, SELFMAG) != 0) {
        res = -1;
        goto err_free_so;
    }

    mod->ehdr = (Elf32_Ehdr *)so_data;
    mod->phdr = (Elf32_Phdr *)((uintptr_t)so_data + mod->ehdr->e_phoff);
    mod->shdr = (Elf32_Shdr *)((uintptr_t)so_data + mod->ehdr->e_shoff);

    mod->shstr = (char *)((uintptr_t)so_data + mod->shdr[mod->ehdr->e_shstrndx].sh_offset);

#ifdef EMULATOR_BUILD
    // Vita3K does not implement kuKernelAllocMemBlock (fixed-address allocation),
    // which the code below normally relies on to place the patch/text/data blocks
    // at exact, contiguous addresses (mirroring a single mmap of the whole module
    // image, like a real ELF loader would do). Since we can't request specific
    // addresses under Vita3K, reserve ONE big block up front sized to fit the
    // whole image contiguously, and sub-allocate patch/text/data regions from it
    // via pointer arithmetic instead of separate fixed-address OS allocations.
    size_t emu_patch_size = 0;
    uintptr_t emu_cursor = 0; // relative distance from the text segment's start
    for (int i = 0; i < mod->ehdr->e_phnum; i++) {
        if (mod->phdr[i].p_type != PT_LOAD)
            continue;
        if ((mod->phdr[i].p_flags & PF_X) == PF_X) {
            emu_patch_size = ALIGN_MEM(PATCH_SZ, mod->phdr[i].p_align);
            emu_cursor = ALIGN_MEM(mod->phdr[i].p_memsz, mod->phdr[i].p_align);
        } else {
            size_t seg_size = ALIGN_MEM(mod->phdr[i].p_memsz + mod->phdr[i].p_vaddr - emu_cursor, mod->phdr[i].p_align);
            emu_cursor += seg_size;
        }
    }
    size_t emu_total_size = ALIGN_MEM(emu_patch_size + emu_cursor, 0x1000);
    SceUID emu_blockid = sceKernelAllocMemBlock("so_arena", SCE_KERNEL_MEMBLOCK_TYPE_USER_RX, emu_total_size, NULL);
    if (emu_blockid < 0) {
        res = emu_blockid;
        goto err_free_so;
    }
    void *emu_arena_base;
    sceKernelGetMemBlockBase(emu_blockid, &emu_arena_base);
    uintptr_t emu_load_addr = (uintptr_t)emu_arena_base + emu_patch_size;
#endif

    for (int i = 0; i < mod->ehdr->e_phnum; i++) {
        if (mod->phdr[i].p_type == PT_LOAD) {
            void *prog_data;
            size_t prog_size;

            if ((mod->phdr[i].p_flags & PF_X) == PF_X) {
                // Allocate arena for code patches, trampolines, etc
                // Sits exactly under the desired allocation space
                mod->patch_size = ALIGN_MEM(PATCH_SZ, mod->phdr[i].p_align);
                prog_size = ALIGN_MEM(mod->phdr[i].p_memsz, mod->phdr[i].p_align);
#ifdef EMULATOR_BUILD
                mod->patch_blockid = emu_blockid;
                mod->patch_base = emu_arena_base;
                mod->patch_head = mod->patch_base;

                mod->text_blockid = emu_blockid;
                prog_data = (void *)emu_load_addr;
#else
                SceKernelAllocMemBlockKernelOpt opt;
                memset(&opt, 0, sizeof(SceKernelAllocMemBlockKernelOpt));
                opt.size = sizeof(SceKernelAllocMemBlockKernelOpt);
                opt.attr = 0x1;
                opt.field_C = (SceUInt32)load_addr - mod->patch_size;
                res = mod->patch_blockid = kuKernelAllocMemBlock("rx_block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RX, mod->patch_size, &opt);
                if (res < 0)
                    goto err_free_so;

                sceKernelGetMemBlockBase(mod->patch_blockid, &mod->patch_base);
                mod->patch_head = mod->patch_base;

                memset(&opt, 0, sizeof(SceKernelAllocMemBlockKernelOpt));
                opt.size = sizeof(SceKernelAllocMemBlockKernelOpt);
                opt.attr = 0x1;
                opt.field_C = (SceUInt32)load_addr;
                res = mod->text_blockid = kuKernelAllocMemBlock("rx_block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RX, prog_size, &opt);
                if (res < 0)
                    goto err_free_so;

                sceKernelGetMemBlockBase(mod->text_blockid, &prog_data);
#endif

                mod->phdr[i].p_vaddr += (Elf32_Addr)prog_data;

                mod->text_base = mod->phdr[i].p_vaddr;
                mod->text_size = mod->phdr[i].p_memsz;

                // Use the .text segment padding as a code cave
                // Word-align it to make it simpler for instruction arena allocation
                mod->cave_size = ALIGN_MEM(prog_size - mod->phdr[i].p_memsz, 0x4);
                mod->cave_base = mod->cave_head = prog_data + mod->phdr[i].p_memsz;
                mod->cave_base = ALIGN_MEM(mod->cave_base, 0x4);
                mod->cave_head = mod->cave_base;

                data_addr = (uintptr_t)prog_data + prog_size;
            } else {
                if (data_addr == 0)
                    goto err_free_so;

                if (mod->n_data >= MAX_DATA_SEG)
                    goto err_free_data;

                prog_size = ALIGN_MEM(mod->phdr[i].p_memsz + mod->phdr[i].p_vaddr - (data_addr - mod->text_base), mod->phdr[i].p_align);

#ifdef EMULATOR_BUILD
                mod->data_blockid[mod->n_data] = emu_blockid;
                prog_data = (void *)data_addr;
#else
                SceKernelAllocMemBlockKernelOpt opt;
                memset(&opt, 0, sizeof(SceKernelAllocMemBlockKernelOpt));
                opt.size = sizeof(SceKernelAllocMemBlockKernelOpt);
                opt.attr = 0x1;
                opt.field_C = (SceUInt32)data_addr;
                res = mod->data_blockid[mod->n_data] = kuKernelAllocMemBlock("rw_block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, prog_size, &opt);
                if (res < 0)
                    goto err_free_text;

                sceKernelGetMemBlockBase(mod->data_blockid[mod->n_data], &prog_data);
#endif
                data_addr = (uintptr_t)prog_data + prog_size;

                mod->phdr[i].p_vaddr += (Elf32_Addr)mod->text_base;

                mod->data_base[mod->n_data] = mod->phdr[i].p_vaddr;
                mod->data_size[mod->n_data] = mod->phdr[i].p_memsz;
                mod->n_data++;
            }

            char *zero = malloc(prog_size - mod->phdr[i].p_filesz);
            memset(zero, 0, prog_size - mod->phdr[i].p_filesz);
            ku_memcpy(prog_data + mod->phdr[i].p_filesz, zero, prog_size - mod->phdr[i].p_filesz);
            free(zero);

            ku_memcpy((void *)mod->phdr[i].p_vaddr, (void *)((uintptr_t)so_data + mod->phdr[i].p_offset), mod->phdr[i].p_filesz);
        }
    }

    for (int i = 0; i < mod->ehdr->e_shnum; i++) {
        char *sh_name = mod->shstr + mod->shdr[i].sh_name;
        uintptr_t sh_addr = mod->text_base + mod->shdr[i].sh_addr;
        size_t sh_size = mod->shdr[i].sh_size;
        if (strcmp(sh_name, ".dynamic") == 0) {
            mod->dynamic = (Elf32_Dyn *)sh_addr;
            mod->num_dynamic = sh_size / sizeof(Elf32_Dyn);
        } else if (strcmp(sh_name, ".dynstr") == 0) {
            mod->dynstr = (char *)sh_addr;
        } else if (strcmp(sh_name, ".dynsym") == 0) {
            mod->dynsym = (Elf32_Sym *)sh_addr;
            mod->num_dynsym = sh_size / sizeof(Elf32_Sym);
        } else if (strcmp(sh_name, ".rel.dyn") == 0) {
            mod->reldyn = (Elf32_Rel *)sh_addr;
            mod->num_reldyn = sh_size / sizeof(Elf32_Rel);
        } else if (strcmp(sh_name, ".rel.plt") == 0) {
            mod->relplt = (Elf32_Rel *)sh_addr;
            mod->num_relplt = sh_size / sizeof(Elf32_Rel);
        } else if (strcmp(sh_name, ".init_array") == 0) {
            mod->init_array = (void *)sh_addr;
            mod->num_init_array = sh_size / sizeof(void *);
        } else if (strcmp(sh_name, ".hash") == 0) {
            mod->hash = (void *)sh_addr;
        }
    }

    if (mod->dynamic == NULL ||
        mod->dynstr == NULL ||
        mod->dynsym == NULL ||
        mod->reldyn == NULL ||
        mod->relplt == NULL) {
        res = -2;
        goto err_free_data;
    }

    for (int i = 0; i < mod->num_dynamic; i++) {
        switch (mod->dynamic[i].d_tag) {
            case DT_SONAME:
                mod->soname = mod->dynstr + mod->dynamic[i].d_un.d_ptr;
                break;
            default:
                break;
        }
    }

    sceKernelFreeMemBlock(so_blockid);

    if (!head && !tail) {
        head = mod;
        tail = mod;
    } else {
        tail->next = mod;
        tail = mod;
    }

    return 0;

#ifdef EMULATOR_BUILD
    // patch/text/data_blockid[] all alias the single emu_blockid arena here,
    // so only free it once instead of once per alias.
    err_free_data:
    err_free_text:
    if (mod->patch_blockid >= 0)
        sceKernelFreeMemBlock(mod->patch_blockid);
    err_free_so:
    sceKernelFreeMemBlock(so_blockid);
#else
    err_free_data:
    for (int i = 0; i < mod->n_data; i++)
        sceKernelFreeMemBlock(mod->data_blockid[i]);
    err_free_text:
    sceKernelFreeMemBlock(mod->text_blockid);
    err_free_so:
    sceKernelFreeMemBlock(so_blockid);
#endif

    return res;
}

int so_mem_load(so_module *mod, void *buffer, size_t so_size, uintptr_t load_addr) {
    SceUID so_blockid;
    void *so_data;

    memset(mod, 0, sizeof(so_module));

    so_blockid = sceKernelAllocMemBlock("so block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, (so_size + 0xfff) & ~0xfff, NULL);
    if (so_blockid < 0)
        return so_blockid;

    sceKernelGetMemBlockBase(so_blockid, &so_data);
    sceClibMemcpy(so_data, buffer, so_size);

    return _so_load(mod, so_blockid, so_data, load_addr);
}

int so_file_load(so_module *mod, const char *filename, uintptr_t load_addr) {
    SceUID so_blockid;
    void *so_data;

    memset(mod, 0, sizeof(so_module));

    SceUID fd = sceIoOpen(filename, SCE_O_RDONLY, 0);
    if (fd < 0)
        return fd;

    size_t so_size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);

    so_blockid = sceKernelAllocMemBlock("so block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, (so_size + 0xfff) & ~0xfff, NULL);
    if (so_blockid < 0)
        return so_blockid;

    sceKernelGetMemBlockBase(so_blockid, &so_data);

    sceIoRead(fd, so_data, so_size);
    sceIoClose(fd);

    return _so_load(mod, so_blockid, so_data, load_addr);
}

int so_relocate(so_module *mod) {
    uintptr_t val;
    for (int i = 0; i < mod->num_reldyn + mod->num_relplt; i++) {
        Elf32_Rel *rel = i < mod->num_reldyn ? &mod->reldyn[i] : &mod->relplt[i - mod->num_reldyn];
        Elf32_Sym *sym = &mod->dynsym[ELF32_R_SYM(rel->r_info)];
        uintptr_t *ptr = (uintptr_t *)(mod->text_base + rel->r_offset);

        int type = ELF32_R_TYPE(rel->r_info);
        switch (type) {
            case R_ARM_ABS32:
                if (sym->st_shndx != SHN_UNDEF) {
                    val = *ptr + mod->text_base + sym->st_value;
                    ku_memcpy(ptr, &val, sizeof(uintptr_t));
                }
                break;
            case R_ARM_RELATIVE:
                val = *ptr + mod->text_base;
                ku_memcpy(ptr, &val, sizeof(uintptr_t));
                break;
            case R_ARM_GLOB_DAT:
            case R_ARM_JUMP_SLOT:
            {
                if (sym->st_shndx != SHN_UNDEF) {
                    val = mod->text_base + sym->st_value;
                    ku_memcpy(ptr, &val, sizeof(uintptr_t));
                }
                break;
            }
            case R_ARM_NONE:
                break;
            default:
                fatal_error("Error unknown relocation type %x\n", type);
                break;
        }
    }

    return 0;
}

uintptr_t so_resolve_link(so_module *mod, const char *symbol) {
    for (int i = 0; i < mod->num_dynamic; i++) {
        switch (mod->dynamic[i].d_tag) {
            case DT_NEEDED:
            {
                so_module *curr = head;
                while (curr) {
                    if (curr != mod && strcmp(curr->soname, mod->dynstr + mod->dynamic[i].d_un.d_ptr) == 0) {
                        uintptr_t link = so_symbol(curr, symbol);
                        if (link)
                            return link;
                    }
                    curr = curr->next;
                }

                break;
            }
            default:
                break;
        }
    }

    return 0;
}

void reloc_err(uintptr_t got0)
{
    so_module *curr = head;
    while (curr) {
        for (int i = 0; i < curr->num_reldyn + curr->num_relplt; i++) {
            Elf32_Rel *rel = i < curr->num_reldyn ? &curr->reldyn[i] : &curr->relplt[i - curr->num_reldyn];
            Elf32_Sym *sym = &curr->dynsym[ELF32_R_SYM(rel->r_info)];
            uintptr_t *ptr = (uintptr_t *)(curr->text_base + rel->r_offset);

            if (got0 == (uintptr_t)ptr) {
                fatal_error("Unknown symbol \"%s\" (%p).\n", curr->dynstr + sym->st_name, (void*)got0);
                return;
            }
        }
        curr = curr->next;
    }

    // Ooops, this shouldn't have happened.
    fatal_error("Unknown symbol \"???\" (%p).\n", (void*)got0);
}

__attribute__((naked)) void plt0_stub()
{
    register uintptr_t got0 asm("r12");
    reloc_err(got0);
}

int so_resolve(so_module *mod, so_default_dynlib *default_dynlib, int size_default_dynlib, int default_dynlib_only) {
    uintptr_t val;
    for (int i = 0; i < mod->num_reldyn + mod->num_relplt; i++) {
        Elf32_Rel *rel = i < mod->num_reldyn ? &mod->reldyn[i] : &mod->relplt[i - mod->num_reldyn];
        Elf32_Sym *sym = &mod->dynsym[ELF32_R_SYM(rel->r_info)];
        uintptr_t *ptr = (uintptr_t *)(mod->text_base + rel->r_offset);

        int type = ELF32_R_TYPE(rel->r_info);
        switch (type) {
            case R_ARM_ABS32:
            case R_ARM_GLOB_DAT:
            case R_ARM_JUMP_SLOT:
            {
                if (sym->st_shndx == SHN_UNDEF) {
                    int resolved = 0;
                    if (!default_dynlib_only) {
                        uintptr_t link = so_resolve_link(mod, mod->dynstr + sym->st_name);
                        if (link) {
                            if (type == R_ARM_ABS32) {
                                val = *ptr + link;
                                ku_memcpy(ptr, &val, sizeof(uintptr_t));
                            } else {
                                val = link;
                                ku_memcpy(ptr, &val, sizeof(uintptr_t));
                            }
                            resolved = 1;
                        }
                    }

                    for (int j = 0; j < size_default_dynlib / sizeof(so_default_dynlib); j++) {
                        if (strcmp(mod->dynstr + sym->st_name, default_dynlib[j].symbol) == 0) {
                            val = default_dynlib[j].func;
                            ku_memcpy(ptr, &val, sizeof(uintptr_t));
                            resolved = 1;
                            break;
                        }
                    }

                    if (!resolved) {
                        game_log("[so_util] Unresolved import: %s\n", mod->dynstr + sym->st_name);
                        if (type == R_ARM_JUMP_SLOT) {
                            *ptr = (uintptr_t)&plt0_stub;
                        }
                    }
                }

                break;
            }
            default:
                break;
        }
    }

    return 0;
}

int __ret0_dummy() {
    return 0;
}

int so_resolve_with_dummy(so_module *mod, so_default_dynlib *default_dynlib, int size_default_dynlib, int default_dynlib_only) {
    for (int i = 0; i < mod->num_reldyn + mod->num_relplt; i++) {
        Elf32_Rel *rel = i < mod->num_reldyn ? &mod->reldyn[i] : &mod->relplt[i - mod->num_reldyn];
        Elf32_Sym *sym = &mod->dynsym[ELF32_R_SYM(rel->r_info)];
        uintptr_t *ptr = (uintptr_t *)(mod->text_base + rel->r_offset);

        int type = ELF32_R_TYPE(rel->r_info);
        switch (type) {
            case R_ARM_ABS32:
            case R_ARM_GLOB_DAT:
            case R_ARM_JUMP_SLOT:
            {
                if (sym->st_shndx == SHN_UNDEF) {
                    for (int j = 0; j < size_default_dynlib / sizeof(so_default_dynlib); j++) {
                        if (strcmp(mod->dynstr + sym->st_name, default_dynlib[j].symbol) == 0) {
                            *ptr = (uintptr_t)&__ret0_dummy;
                            break;
                        }
                    }
                }

                break;
            }
            default:
                break;
        }
    }

    return 0;
}

void so_initialize(so_module *mod) {
    for (int i = 0; i < mod->num_init_array; i++) {
        if (mod->init_array[i] && (intptr_t)mod->init_array[i] != -1) {
            mod->init_array[i]();
        }
    }
}

uint32_t so_hash(const uint8_t *name) {
    uint64_t h = 0, g;
    while (*name) {
        h = (h << 4) + *name++;
        if ((g = (h & 0xf0000000)) != 0)
            h ^= g >> 24;
        h &= 0x0fffffff;
    }
    return h;
}

static int so_symbol_index(so_module *mod, const char *symbol)
{
    if (mod->hash) {
        uint32_t hash = so_hash((const uint8_t *)symbol);
        uint32_t nbucket = mod->hash[0];
        uint32_t *bucket = &mod->hash[2];
        uint32_t *chain = &bucket[nbucket];
        for (uint32_t i = bucket[hash % nbucket]; i; i = chain[i]) {
            if (mod->dynsym[i].st_shndx == SHN_UNDEF)
                continue;
            if (mod->dynsym[i].st_info != SHN_UNDEF && strcmp(mod->dynstr + mod->dynsym[i].st_name, symbol) == 0)
                return (int)i;
        }
    }

    for (uint32_t i = 0; i < mod->num_dynsym; i++) {
        if (mod->dynsym[i].st_shndx == SHN_UNDEF)
            continue;
        if (mod->dynsym[i].st_info != SHN_UNDEF && strcmp(mod->dynstr + mod->dynsym[i].st_name, symbol) == 0)
            return (int)i;
    }

    return -1;
}

/*
 * alloc_arena: allocates space on either patch or cave arenas,
 * range: maximum range from allocation to dst (ignored if NULL)
 * dst: destination address
*/
uintptr_t so_alloc_arena(so_module *so, uintptr_t range, uintptr_t dst, size_t sz) {
    // Is address in range?
#define inrange(lsr, gtr, range) \
		(((uintptr_t)(range) == (uintptr_t)NULL) || ((uintptr_t)(range) >= ((uintptr_t)(gtr) - (uintptr_t)(lsr))))
    // Space left on block
#define blkavail(type) (so->type##_size - (so->type##_head - so->type##_base))

    // keep allocations 4-byte aligned for simplicity
    sz = ALIGN_MEM(sz, 4);

    if (sz <= (blkavail(patch)) && inrange(so->patch_base, dst, range)) {
        so->patch_head += sz;
        return (so->patch_head - sz);
    } else if (sz <= (blkavail(cave)) && inrange(dst, so->cave_base, range)) {
        so->cave_head += sz;
        return (so->cave_head - sz);
    }

    return (uintptr_t)NULL;
}

static void trampoline_ldm(so_module *mod, uint32_t *dst) {
    uint32_t trampoline[1];
    uint32_t funct[20] = {0xFAFAFAFA};
    uint32_t *ptr = funct;

    int cur = 0;
    int baseReg = (int)(((*dst) >> 16) & 0xF);
    int bitMask = (int)((*dst) & 0xFFFF);

    uint32_t stored = 0;
    for (int i = 0; i < 16; i++) {
        if (bitMask & (1 << i)) {
            // If the register we're reading the offset from is the same as the one we're writing,
            // delay it to the very end so that the base pointer isn't clobbered
            if (baseReg == i)
                stored = LDR_OFFS(i, baseReg, cur).raw;
            else
                *ptr++ = LDR_OFFS(i, baseReg, cur).raw;
            cur += 4;
        }
    }

    // Perform the delayed load if needed
    if (stored) {
        *ptr++ = stored;
    }

    *ptr++ = (uint32_t) 0xe51ff004; // LDR PC, [PC, -0x4] ; jmp to [dst+0x4]
    *ptr++ = (uint32_t)(dst + 1); // .dword <...>	; [dst+0x4]

    size_t trampoline_sz =	((uintptr_t)ptr - (uintptr_t)&funct[0]);
    uintptr_t patch_addr = so_alloc_arena(mod, B_RANGE, (uintptr_t) B_OFFSET(dst), trampoline_sz);

    if (!patch_addr) {
        fatal_error("Failed to patch LDMIA at %p, unable to allocate space.\n", dst);
        return;
    }

    // Create sign extended relative address rel_addr
    trampoline[0] = B(dst, patch_addr).raw;

    ku_memcpy((void*)patch_addr, funct, trampoline_sz);
    ku_memcpy(dst, trampoline, sizeof(trampoline));
}

uintptr_t so_symbol(so_module *mod, const char *symbol) {
    int index = so_symbol_index(mod, symbol);
    if (index == -1)
        return (uintptr_t) NULL;

    return mod->text_base + mod->dynsym[index].st_value;
}

void so_symbol_fix_ldmia(so_module *mod, const char *symbol) {
    // This is meant to work around crashes due to unaligned accesses (SIGBUS :/) due to certain
    // kernels not having the fault trap enabled, e.g. certain RK3326 Odroid Go Advance clone distros.
    int idx = so_symbol_index(mod, symbol);
    if (idx == -1)
        return;

    uintptr_t st_addr = mod->text_base + mod->dynsym[idx].st_value;
    for (uintptr_t addr = st_addr; addr < st_addr + mod->dynsym[idx].st_size; addr+=4) {
        uint32_t inst = *(uint32_t*)(addr);

        //Is this an LDMIA instruction with a R0-R12 base register?
        if (((inst & 0xFFF00000) == 0xE8900000) && (((inst >> 16) & 0xF) < 13) ) {
            game_log("[so_util] Found possibly misaligned LDMIA on 0x%08X, patching...\n", (unsigned int) addr);
            trampoline_ldm(mod, (uint32_t *) addr);
        }
    }
}
