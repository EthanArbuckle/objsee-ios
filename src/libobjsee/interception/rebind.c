//
//  rebind.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach/vm_map.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <dlfcn.h>
#include "tracer_internal.h"
#include "rebind.h"

struct symbol_rebinding_internal_t {
    uint64_t slide;
    const char *string_table;
    struct nlist_64 *symbol_table;
    uint32_t *indirect_symbol_table;
};


kern_return_t rebind_symbol_64(const char *symbol_to_hook, void *replacement_func, struct symbol_rebinding_internal_t *img_symbol_info, struct section_64 *symbol_section) {
    uint32_t *indirect_symbols = (uint32_t *)((uintptr_t)img_symbol_info->indirect_symbol_table + symbol_section->reserved1 * sizeof(uint32_t));
    uint64_t *sym_bindings = (uint64_t *)(symbol_section->addr + img_symbol_info->slide);
    uint64_t num_entries = symbol_section->size / sizeof(uint64_t);
    
    for (uint64_t i = 0; i < num_entries; i++) {
        uint32_t idx = indirect_symbols[i];
        if (idx & (INDIRECT_SYMBOL_ABS | INDIRECT_SYMBOL_LOCAL)) {
            continue;
        }
        
        struct nlist_64 *sym = (struct nlist_64 *)img_symbol_info->symbol_table + idx;
        uint32_t string_table_offset = sym->n_un.n_strx;
        const char *symbol_name = (const char *)img_symbol_info->string_table + string_table_offset;
        if (symbol_name == NULL || strlen(symbol_name) < 1 || strcmp(&symbol_name[1], symbol_to_hook) != 0) {
            continue;
        }
        
        kern_return_t kr;
        kr = vm_protect(mach_task_self(), (vm_address_t)sym_bindings, symbol_section->size, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
        if (kr != KERN_SUCCESS) {
            return KERN_FAILURE;
        }
        
        sym_bindings[i] = (uint64_t)replacement_func;
        return KERN_SUCCESS;
    }
    
    return KERN_FAILURE;
}

kern_return_t rebind_symbol_32(const char *symbol_to_hook, void *replacement_func, struct symbol_rebinding_internal_t *img_symbol_info, struct section *symbol_section) {
    uint32_t *indirect_symbols = (uint32_t *)((uintptr_t)img_symbol_info->indirect_symbol_table + symbol_section->reserved1 * sizeof(uint32_t));
    uint32_t *sym_bindings = (uint32_t *)(symbol_section->addr + img_symbol_info->slide);
    uint32_t num_entries = symbol_section->size / sizeof(uint32_t);
    
    for (uint32_t i = 0; i < num_entries; i++) {
        uint32_t idx = indirect_symbols[i];
        if (idx & (INDIRECT_SYMBOL_ABS | INDIRECT_SYMBOL_LOCAL)) {
            continue;
        }
        
        struct nlist *sym = (struct nlist *)img_symbol_info->symbol_table + idx;
        uint32_t string_table_offset = sym->n_un.n_strx;
        const char *symbol_name = (const char *)img_symbol_info->string_table + string_table_offset;
        if (symbol_name == NULL || strlen(symbol_name) < 1 || strcmp(&symbol_name[1], symbol_to_hook) != 0) {
            continue;
        }
        
        kern_return_t kr;
        kr = vm_protect(mach_task_self(), (vm_address_t)sym_bindings, symbol_section->size, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
        if (kr != KERN_SUCCESS) {
            return KERN_FAILURE;
        }
        
        sym_bindings[i] = (uint32_t)(uintptr_t)replacement_func;
        return KERN_SUCCESS;
    }
    
    return KERN_FAILURE;
}

void for_load_command_in_mach_header(struct mach_header *mh, void (^callback)(struct load_command *lc)) {
    if (mh == NULL || callback == NULL) {
        return;
    }
    
    bool is_64bit = (mh->magic == MH_MAGIC_64 || mh->magic == MH_CIGAM_64);
    size_t mh_struct_size = is_64bit ? sizeof(struct mach_header_64) : sizeof(struct mach_header);
    
    struct load_command *lc = (struct load_command *)((mach_vm_address_t)mh + mh_struct_size);
    for (int command = 0; command < mh->ncmds; command++) {
        
        callback(lc);
        lc = (struct load_command *)((mach_vm_address_t)lc + lc->cmdsize);
    }
}

kern_return_t hook_function_in_mach_header(const char *symbol_to_hook, void *replacement_func, struct mach_header *mh) {
    if (mh == NULL || symbol_to_hook == NULL || replacement_func == NULL) {
        return TRACER_ERROR_INVALID_ARGUMENT;
    }
    
    Dl_info info;
    if (dladdr((void *)mh, &info) == 0) {
        return TRACER_ERROR_INVALID_ARGUMENT;
    }
    
    bool is_64bit = (mh->magic == MH_MAGIC_64 || mh->magic == MH_CIGAM_64);
    
    struct symbol_rebinding_internal_t *img_symbol_info = (struct symbol_rebinding_internal_t *)malloc(sizeof(struct symbol_rebinding_internal_t));
    if (img_symbol_info == NULL) {
        return KERN_NO_SPACE;
    }
    
    __block struct symtab_command *symbol_table_cmd = NULL;
    __block struct dysymtab_command *dynamic_symbol_table_cmd = NULL;
    __block uint64_t linkedit_vmaddr = 0;
    __block uint64_t linkedit_fileoff = 0;
    __block uint64_t text_vmaddr = 0;
    __block bool found_linkedit = false;
    __block bool found_text = false;
    
    for_load_command_in_mach_header(mh, ^(struct load_command *lc) {
        switch (lc->cmd) {
                
            case LC_DYSYMTAB:
                dynamic_symbol_table_cmd = (struct dysymtab_command *)lc;
                break;
                
            case LC_SYMTAB:
                symbol_table_cmd = (struct symtab_command *)lc;
                break;
                
            case LC_SEGMENT_64: {
                
                struct segment_command_64 *seg = (struct segment_command_64 *)lc;
                if (strcmp(seg->segname, SEG_TEXT) == 0) {
                    text_vmaddr = seg->vmaddr;
                    found_text = true;
                } else if (strcmp(seg->segname, SEG_LINKEDIT) == 0) {
                    linkedit_vmaddr = seg->vmaddr;
                    linkedit_fileoff = seg->fileoff;
                    found_linkedit = true;
                }
                break;
            }
                
            case LC_SEGMENT: {
                struct segment_command *seg = (struct segment_command *)lc;
                if (strcmp(seg->segname, SEG_TEXT) == 0) {
                    text_vmaddr = seg->vmaddr;
                    found_text = true;
                } else if (strcmp(seg->segname, SEG_LINKEDIT) == 0) {
                    linkedit_vmaddr = seg->vmaddr;
                    linkedit_fileoff = seg->fileoff;
                    found_linkedit = true;
                }
                break;
            }
                
            default:
                break;
        }
    });

    if (!found_linkedit || symbol_table_cmd == NULL || dynamic_symbol_table_cmd == NULL || !found_text) {
        free(img_symbol_info);
        return KERN_FAILURE;
    }
    
    if (dynamic_symbol_table_cmd->nindirectsyms == 0) {
        free(img_symbol_info);
        return KERN_FAILURE;
    }
    
    img_symbol_info->slide = (intptr_t)mh - text_vmaddr;
    img_symbol_info->string_table = (void *)(linkedit_vmaddr + symbol_table_cmd->stroff - linkedit_fileoff + img_symbol_info->slide);
    img_symbol_info->symbol_table = (void *)(linkedit_vmaddr + symbol_table_cmd->symoff - linkedit_fileoff + img_symbol_info->slide);
    img_symbol_info->indirect_symbol_table = (void *)(linkedit_vmaddr + dynamic_symbol_table_cmd->indirectsymoff - linkedit_fileoff + img_symbol_info->slide);
    
    __block struct section_64 *lazy_symbol_section_64 = NULL;
    __block struct section_64 *non_lazy_symbol_section_64 = NULL;
    __block struct section *lazy_symbol_section_32 = NULL;
    __block struct section *non_lazy_symbol_section_32 = NULL;
    
    for_load_command_in_mach_header(mh, ^(struct load_command *lc) {
        if (lc->cmd == LC_SEGMENT) {
            struct segment_command *seg = (struct segment_command *)lc;
            if (strcmp(seg->segname, SEG_DATA) != 0 && strcmp(seg->segname, "__DATA_CONST") != 0) {
                return;
            }
            
            for (uint32_t i = 0; i < seg->nsects; i++) {
                struct section *sect = (struct section *)((uintptr_t)seg + sizeof(struct segment_command)) + i;
                switch (sect->flags & SECTION_TYPE) {
                    case S_LAZY_SYMBOL_POINTERS:
                        lazy_symbol_section_32 = sect;
                        break;
                        
                    case S_NON_LAZY_SYMBOL_POINTERS:
                        non_lazy_symbol_section_32 = sect;
                        break;
                        
                    default:
                        break;
                }
            }
        }
        else if (lc->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *seg = (struct segment_command_64 *)lc;
            if (strcmp(seg->segname, SEG_DATA) != 0 && strcmp(seg->segname, "__DATA_CONST") != 0) {
                return;
            }
            
            for (uint32_t i = 0; i < seg->nsects; i++) {
                struct section_64 *sect = (struct section_64 *)((uintptr_t)seg + sizeof(struct segment_command_64)) + i;
                switch (sect->flags & SECTION_TYPE) {
                    case S_LAZY_SYMBOL_POINTERS:
                        lazy_symbol_section_64 = sect;
                        break;
                        
                    case S_NON_LAZY_SYMBOL_POINTERS:
                        non_lazy_symbol_section_64 = sect;
                        break;
                        
                    default:
                        break;
                }
            }
        }
    });
    
    kern_return_t ret = KERN_FAILURE;
    if (is_64bit) {
        if (lazy_symbol_section_64 != NULL) {
            ret = rebind_symbol_64(symbol_to_hook, replacement_func, img_symbol_info, lazy_symbol_section_64);
        }
        if (ret != KERN_SUCCESS && non_lazy_symbol_section_64 != NULL) {
            ret = rebind_symbol_64(symbol_to_hook, replacement_func, img_symbol_info, non_lazy_symbol_section_64);
        }
    }
    else {
        if (lazy_symbol_section_32 != NULL) {
            ret = rebind_symbol_32(symbol_to_hook, replacement_func, img_symbol_info, lazy_symbol_section_32);
        }
        if (ret != KERN_SUCCESS && non_lazy_symbol_section_32 != NULL) {
            ret = rebind_symbol_32(symbol_to_hook, replacement_func, img_symbol_info, non_lazy_symbol_section_32);
        }
    }
    
    free(img_symbol_info);
    return ret;
}

struct symbol_rebinding_t * _Nullable hook_function(const char *symbol_to_hook, void *replacement_func) {
    if (symbol_to_hook == NULL || replacement_func == NULL) {
        return NULL;
    }

    void *function_to_hook_ptr = dlsym(RTLD_DEFAULT, symbol_to_hook);
    if (function_to_hook_ptr == NULL) {
        return NULL;
    }
    
    int hook_count = 0;
    for (int i = 0; i < _dyld_image_count(); i++) {
        struct mach_header *mh = (struct mach_header *)_dyld_get_image_header(i);
        if (hook_function_in_mach_header(symbol_to_hook, replacement_func, mh) == KERN_SUCCESS) {
            hook_count++;
        }
    }
    
    if (hook_count == 0) {
        return NULL;
    }
    
    // Inform the caller of the number of symbols rebound
    struct symbol_rebinding_t *rebinding = (struct symbol_rebinding_t *)malloc(sizeof(struct symbol_rebinding_t));
    rebinding->name = symbol_to_hook;
    rebinding->replacement = replacement_func;
    rebinding->num_symbols_rebound = hook_count;
    return rebinding;
}
