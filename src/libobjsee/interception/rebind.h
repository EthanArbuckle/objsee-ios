//
//  rebind.h
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

struct symbol_rebinding_t {
    const char * _Nonnull name;
    void * _Nonnull replacement;
    uint32_t num_symbols_rebound;
};

struct symbol_rebinding_t * _Nullable hook_function(const char * _Nonnull symbol_to_hook, void * _Nonnull replacement_func);
