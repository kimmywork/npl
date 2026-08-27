/*
 * symtab.c — Symbol table implementation for stage0 compiler.
 */

#include "symtab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void symtab_init(SymTab *tab)
{
    memset(tab, 0, sizeof(SymTab));
}

Symbol *symtab_add_global_func(SymTab *tab, const char *name, AstNode *params, AstNode *body)
{
    if (tab->count >= SYMTAB_MAX) {
        fprintf(stderr, "symtab: overflow\n");
        exit(1);
    }
    Symbol *sym = &tab->entries[tab->count++];
    sym->name = sym_intern(name);
    sym->kind = SYM_GLOBAL_FUNC;
    sym->as.func.params = params;
    sym->as.func.body = body;
    return sym;
}

Symbol *symtab_add_global_var(SymTab *tab, const char *name, AstNode *value)
{
    if (tab->count >= SYMTAB_MAX) {
        fprintf(stderr, "symtab: overflow\n");
        exit(1);
    }
    Symbol *sym = &tab->entries[tab->count++];
    sym->name = sym_intern(name);
    sym->kind = SYM_GLOBAL_VAR;
    sym->as.var.value = value;
    return sym;
}

Symbol *symtab_add_local(SymTab *tab, const char *name)
{
    if (tab->count >= SYMTAB_MAX) {
        fprintf(stderr, "symtab: overflow\n");
        exit(1);
    }
    Symbol *sym = &tab->entries[tab->count++];
    sym->name = sym_intern(name);
    sym->kind = SYM_LOCAL;
    sym->as.local.index = tab->next_local++;
    /* Keep the first binding readable; shadowed bindings need distinct QBE
     * names because QBE temporaries cannot model lexical restoration. */
    char sanitized[64];
    qbe_sanitize_name(name, sanitized, sizeof(sanitized));
    int shadowed = 0;
    for (int i = tab->count - 2; i >= 0; i--) {
        if (tab->entries[i].kind == SYM_LOCAL &&
            strcmp(tab->entries[i].name, name) == 0) {
            shadowed = 1;
            break;
        }
    }
    if (shadowed) {
        snprintf(sym->as.local.qbe_name, sizeof(sym->as.local.qbe_name),
                 "%%%s_%d", sanitized, sym->as.local.index);
    } else {
        snprintf(sym->as.local.qbe_name, sizeof(sym->as.local.qbe_name),
                 "%%%s", sanitized);
    }
    return sym;
}

Symbol *symtab_lookup(SymTab *tab, const char *name)
{
    const char *interned = sym_intern(name);
    /* Search backwards (locals shadow globals) */
    for (int i = tab->count - 1; i >= 0; i--) {
        if (tab->entries[i].name == interned) {
            return &tab->entries[i];
        }
    }
    return NULL;
}

void qbe_sanitize_name(const char *src, char *dst, size_t dst_size)
{
    size_t i = 0;
    for (; *src && i < dst_size - 1; src++) {
        if (isalnum((unsigned char)*src) || *src == '_') {
            dst[i++] = *src;
        } else if (*src == '-' || *src == '?' || *src == '!' || *src == '=' ||
                   *src == '<' || *src == '>' || *src == '*' || *src == '/' ||
                   *src == '+' || *src == '%' || *src == '$' || *src == '@' ||
                   *src == '.') {
            dst[i++] = '_';
        }
        /* skip other chars */
    }
    dst[i] = '\0';
}
