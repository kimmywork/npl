/* Explicit C memory for (npl ffi) (plan: F1, C memory core).
 *
 * A pointer box normally names an address owned by a C library.  This file
 * adds boxes that own a malloc'd block and views created by pointer+offset.
 * Foreign memory holds only C scalars and addresses; the collector never
 * scans it.  Loads and stores go through memcpy after an alignment and
 * (when the view has a known length) bounds check.
 *
 * Type codes are shared with lib/npl/ffi.sld:
 *   0 u8  1 u16  2 u32  3 u64  4 i8  5 i16  6 i32  7 i64
 *   8 float  9 double  10 pointer
 */

#define _DARWIN_C_SOURCE
#define _GNU_SOURCE

#include "port.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FFI_TYPE_COUNT 11
#define FFI_U8 0
#define FFI_U16 1
#define FFI_U32 2
#define FFI_U64 3
#define FFI_I8 4
#define FFI_I16 5
#define FFI_I32 6
#define FFI_I64 7
#define FFI_FLOAT 8
#define FFI_DOUBLE 9
#define FFI_POINTER 10

static size_t ffi_type_width(int code)
{
    switch (code) {
    case FFI_U8: case FFI_I8: return 1;
    case FFI_U16: case FFI_I16: return 2;
    case FFI_U32: case FFI_I32: case FFI_FLOAT: return 4;
    case FFI_U64: case FFI_I64: case FFI_DOUBLE: return 8;
    case FFI_POINTER: return sizeof(void *);
    default: return 0;
    }
}

typedef struct FfiLiveBase {
    void *base;
    struct FfiLiveBase *next;
} FfiLiveBase;

static FfiLiveBase *ffi_live_bases = NULL;

static void ffi_register_base(void *base)
{
    FfiLiveBase *entry = (FfiLiveBase *)malloc(sizeof(FfiLiveBase));
    if (!entry) npl_runtime_oom_internal();
    entry->base = base;
    entry->next = ffi_live_bases;
    ffi_live_bases = entry;
}

static void ffi_unregister_base(void *base)
{
    FfiLiveBase **link = &ffi_live_bases;
    while (*link && (*link)->base != base) link = &(*link)->next;
    if (*link) {
        FfiLiveBase *entry = *link;
        *link = entry->next;
        free(entry);
    }
}

static int ffi_base_live(void *base)
{
    FfiLiveBase *entry;
    for (entry = ffi_live_bases; entry; entry = entry->next)
        if (entry->base == base) return 1;
    return 0;
}

static NplPointer *ffi_pointer_object(NplVal value, const char *who)
{
    if (!npl_is_pointer(value))
        npl_primitive_error(who, "expected a pointer", value);
    return (NplPointer *)npl_header(value);
}

static size_t ffi_size_value(NplVal value, const char *who)
{
    uint64_t size = npl_ffi_to_uint64(value, (uint64_t)SIZE_MAX, "size_t", who);
    return (size_t)size;
}

static size_t ffi_alignment_value(NplVal value, const char *who)
{
    uint64_t alignment =
        npl_ffi_to_uint64(value, UINT64_C(1) << 20, "alignment", who);
    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
        npl_primitive_error(who,
            "alignment must be a power of two", value);
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    return (size_t)alignment;
}

static int ffi_type_code(NplVal value, const char *who)
{
    uint64_t code = npl_ffi_to_uint64(value, FFI_TYPE_COUNT - 1,
                                      "type code", who);
    return (int)code;
}

static NplVal ffi_alloc_block(size_t size, size_t alignment)
{
    void *memory = NULL;
    size_t actual = size == 0 ? 1 : size;
    if (posix_memalign(&memory, alignment, actual) != 0)
        npl_runtime_oom_internal();
    memset(memory, 0, actual);
    ffi_register_base(memory);
    {
        NplVal value = npl_pointer(memory);
        NplPointer *pointer = ffi_pointer_object(value, "foreign-alloc");
        pointer->base = memory;
        pointer->size = size;
        pointer->offset = 0;
        pointer->sized = 1;
        pointer->owned = 1;
        return value;
    }
}

NplVal npl_ffi_alloc(NplVal size_value, NplVal alignment_value)
{
    size_t size = ffi_size_value(size_value, "foreign-alloc");
    size_t alignment = ffi_alignment_value(alignment_value, "foreign-alloc");
    return ffi_alloc_block(size, alignment);
}

NplVal npl_ffi_foreign_free(NplVal value)
{
    NplPointer *pointer = ffi_pointer_object(value, "foreign-free");
    if (pointer->address == NULL && !pointer->owned)
        return npl_unspecified();
    if (!pointer->owned)
        npl_primitive_error("foreign-free",
            "pointer was not allocated by foreign-alloc", value);
    ffi_unregister_base(pointer->base);
    free(pointer->base);
    pointer->address = NULL;
    pointer->base = NULL;
    pointer->size = 0;
    pointer->offset = 0;
    pointer->sized = 0;
    pointer->owned = 0;
    return npl_unspecified();
}

NplVal npl_ffi_foreign_size(NplVal value)
{
    NplPointer *pointer = ffi_pointer_object(value, "foreign-size");
    return npl_ffi_from_uint64((uint64_t)pointer->size);
}

NplVal npl_ffi_foreign_copy(NplVal value, NplVal size_value,
                            NplVal alignment_value)
{
    NplPointer *source = ffi_pointer_object(value, "foreign-copy");
    size_t size = ffi_size_value(size_value, "foreign-copy");
    size_t alignment = ffi_alignment_value(alignment_value, "foreign-copy");
    if (source->sized && !ffi_base_live(source->base))
        npl_primitive_error("foreign-copy", "allocation was freed", value);
    if (source->sized && size > source->size)
        npl_primitive_error("foreign-copy",
            "copy size exceeds the source allocation", size_value);
    if (size != 0 && source->address == NULL)
        npl_primitive_error("foreign-copy",
            "cannot copy from a null pointer", value);
    {
        NplVal result = ffi_alloc_block(size, alignment);
        if (size != 0)
            memcpy(ffi_pointer_object(result, "foreign-copy")->address,
                   source->address, size);
        return result;
    }
}

NplVal npl_ffi_pointer_add(NplVal value, NplVal offset_value)
{
    NplPointer *pointer = ffi_pointer_object(value, "pointer+offset");
    size_t offset = ffi_size_value(offset_value, "pointer+offset");
    if (pointer->sized && !ffi_base_live(pointer->base))
        npl_primitive_error("pointer+offset", "allocation was freed", value);
    if (pointer->address == NULL && offset != 0)
        npl_primitive_error("pointer+offset",
            "cannot offset a null pointer", value);
    if (pointer->sized && offset > pointer->size)
        npl_primitive_error("pointer+offset",
            "offset is beyond the allocation", offset_value);
    if (offset > (size_t)-1 - pointer->offset)
        npl_primitive_error("pointer+offset", "offset overflow", offset_value);
    {
        NplVal result = npl_pointer((char *)pointer->address + offset);
        NplPointer *view = ffi_pointer_object(result, "pointer+offset");
        view->base = pointer->base;
        view->offset = pointer->offset + offset;
        view->sized = pointer->sized;
        view->size = pointer->sized ? pointer->size - offset : 0;
        view->owned = 0;
        return result;
    }
}

NplVal npl_ffi_pointer_offset(NplVal value)
{
    NplPointer *pointer = ffi_pointer_object(value, "pointer-offset");
    return npl_ffi_from_uint64((uint64_t)pointer->offset);
}

NplVal npl_ffi_type_size(NplVal code_value)
{
    int code = ffi_type_code(code_value, "c-type-size");
    return npl_ffi_from_uint64((uint64_t)ffi_type_width(code));
}

static unsigned char *ffi_access_address(NplVal value, int code,
                                         size_t offset, const char *who)
{
    NplPointer *pointer = ffi_pointer_object(value, who);
    size_t width = ffi_type_width(code);
    if (pointer->address == NULL)
        npl_primitive_error(who, "null pointer access", value);
    if (pointer->sized && !ffi_base_live(pointer->base))
        npl_primitive_error(who, "allocation was freed", value);
    if (pointer->sized &&
        (offset > pointer->size || width > pointer->size - offset))
        npl_primitive_error(who, "offset is beyond the allocation",
                            npl_ffi_from_uint64((uint64_t)offset));
    if (((uintptr_t)pointer->address + offset) % (uintptr_t)width != 0)
        npl_primitive_error(who, "misaligned access",
                            npl_ffi_from_uint64((uint64_t)offset));
    return (unsigned char *)pointer->address + offset;
}

NplVal npl_ffi_pointer_ref(NplVal value, NplVal code_value,
                           NplVal offset_value)
{
    int code = ffi_type_code(code_value, "pointer-ref");
    size_t offset = ffi_size_value(offset_value, "pointer-ref");
    unsigned char *address = ffi_access_address(value, code, offset,
                                                "pointer-ref");
    switch (code) {
    case FFI_U8: {
        uint8_t item;
        memcpy(&item, address, sizeof item);
        return npl_ffi_from_uint64(item);
    }
    case FFI_U16: {
        uint16_t item;
        memcpy(&item, address, sizeof item);
        return npl_ffi_from_uint64(item);
    }
    case FFI_U32: {
        uint32_t item;
        memcpy(&item, address, sizeof item);
        return npl_ffi_from_uint64(item);
    }
    case FFI_U64: {
        uint64_t item;
        memcpy(&item, address, sizeof item);
        return npl_ffi_from_uint64(item);
    }
    case FFI_I8: {
        int8_t item;
        memcpy(&item, address, sizeof item);
        return npl_ffi_from_int64(item);
    }
    case FFI_I16: {
        int16_t item;
        memcpy(&item, address, sizeof item);
        return npl_ffi_from_int64(item);
    }
    case FFI_I32: {
        int32_t item;
        memcpy(&item, address, sizeof item);
        return npl_ffi_from_int64(item);
    }
    case FFI_I64: {
        int64_t item;
        memcpy(&item, address, sizeof item);
        return npl_ffi_from_int64(item);
    }
    case FFI_FLOAT: {
        float item;
        memcpy(&item, address, sizeof item);
        return npl_ffi_from_double((double)item);
    }
    case FFI_DOUBLE: {
        double item;
        memcpy(&item, address, sizeof item);
        return npl_ffi_from_double(item);
    }
    case FFI_POINTER: {
        void *item;
        memcpy(&item, address, sizeof item);
        return npl_pointer(item);
    }
    default:
        npl_primitive_error("pointer-ref", "unknown C type", code_value);
        return npl_false();
    }
}

NplVal npl_ffi_pointer_set(NplVal value, NplVal code_value,
                           NplVal offset_value, NplVal item)
{
    int code = ffi_type_code(code_value, "pointer-set!");
    size_t offset = ffi_size_value(offset_value, "pointer-set!");
    unsigned char *address = ffi_access_address(value, code, offset,
                                                "pointer-set!");
    switch (code) {
    case FFI_U8: {
        uint8_t narrowed = (uint8_t)npl_ffi_to_uint64(item, UINT8_MAX,
                                                      "uint8_t", "pointer-set!");
        memcpy(address, &narrowed, sizeof narrowed);
        break;
    }
    case FFI_U16: {
        uint16_t narrowed = (uint16_t)npl_ffi_to_uint64(item, UINT16_MAX,
                                                        "uint16_t", "pointer-set!");
        memcpy(address, &narrowed, sizeof narrowed);
        break;
    }
    case FFI_U32: {
        uint32_t narrowed = (uint32_t)npl_ffi_to_uint64(item, UINT32_MAX,
                                                        "uint32_t", "pointer-set!");
        memcpy(address, &narrowed, sizeof narrowed);
        break;
    }
    case FFI_U64: {
        uint64_t narrowed = npl_ffi_to_uint64(item, UINT64_MAX, "uint64_t",
                                              "pointer-set!");
        memcpy(address, &narrowed, sizeof narrowed);
        break;
    }
    case FFI_I8: {
        int8_t narrowed = (int8_t)npl_ffi_to_int64(item, INT8_MIN, INT8_MAX,
                                                   "int8_t", "pointer-set!");
        memcpy(address, &narrowed, sizeof narrowed);
        break;
    }
    case FFI_I16: {
        int16_t narrowed = (int16_t)npl_ffi_to_int64(item, INT16_MIN, INT16_MAX,
                                                     "int16_t", "pointer-set!");
        memcpy(address, &narrowed, sizeof narrowed);
        break;
    }
    case FFI_I32: {
        int32_t narrowed = (int32_t)npl_ffi_to_int64(item, INT32_MIN, INT32_MAX,
                                                     "int32_t", "pointer-set!");
        memcpy(address, &narrowed, sizeof narrowed);
        break;
    }
    case FFI_I64: {
        int64_t narrowed = npl_ffi_to_int64(item, INT64_MIN, INT64_MAX,
                                            "int64_t", "pointer-set!");
        memcpy(address, &narrowed, sizeof narrowed);
        break;
    }
    case FFI_FLOAT: {
        float narrowed = (float)npl_ffi_to_double(item, "pointer-set!");
        memcpy(address, &narrowed, sizeof narrowed);
        break;
    }
    case FFI_DOUBLE: {
        double narrowed = npl_ffi_to_double(item, "pointer-set!");
        memcpy(address, &narrowed, sizeof narrowed);
        break;
    }
    case FFI_POINTER: {
        void *narrowed = npl_pointer_address(item, "pointer-set!");
        memcpy(address, &narrowed, sizeof narrowed);
        break;
    }
    default:
        npl_primitive_error("pointer-set!", "unknown C type", code_value);
        break;
    }
    return npl_unspecified();
}
