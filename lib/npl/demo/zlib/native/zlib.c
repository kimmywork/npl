/* (npl demo zlib): external native package (mode B) over the system zlib.
 *
 * Caller-owned bytevectors only; no NPL runtime link or allocation.  Return
 * values are lengths (>= 0) or negative zlib status codes.  The NPL wrapper
 * turns negative values into errors and trims the output bytevector.
 */

#include "npl.h"

#include <string.h>
#include <zlib.h>

static const NplBytevector *bytevector_arg(NplVal value)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_BYTEVECTOR)
        return NULL;
    return (const NplBytevector *)npl_header(value);
}

static NplBytevector *bytevector_mutable(NplVal value)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_BYTEVECTOR)
        return NULL;
    return (NplBytevector *)npl_header(value);
}

NplVal npl_demo_zlib_version(NplVal buffer_value)
{
    NplBytevector *buffer = bytevector_mutable(buffer_value);
    const char *version = zlibVersion();
    size_t length = strlen(version);
    if (!buffer || buffer->length < length) return npl_fixnum(-1);
    memcpy(buffer->data, version, length);
    return npl_fixnum((int64_t)length);
}

NplVal npl_demo_zlib_bound(NplVal length_value)
{
    if (!npl_is_fixnum(length_value) || npl_fixnum_value(length_value) < 0)
        return npl_fixnum(-1);
    return npl_fixnum((int64_t)compressBound(
        (uLong)npl_fixnum_value(length_value)));
}

NplVal npl_demo_zlib_compress(NplVal source_value, NplVal destination_value)
{
    const NplBytevector *source = bytevector_arg(source_value);
    NplBytevector *destination = bytevector_mutable(destination_value);
    uLongf length;
    int status;
    if (!source || !destination) return npl_fixnum(-1);
    length = (uLongf)destination->length;
    status = compress(destination->data, &length, source->data,
                      (uLong)source->length);
    if (status != Z_OK) return npl_fixnum(status);
    return npl_fixnum((int64_t)length);
}

NplVal npl_demo_zlib_uncompress(NplVal source_value,
                                NplVal destination_value)
{
    const NplBytevector *source = bytevector_arg(source_value);
    NplBytevector *destination = bytevector_mutable(destination_value);
    uLongf length;
    int status;
    if (!source || !destination) return npl_fixnum(-1);
    length = (uLongf)destination->length;
    status = uncompress(destination->data, &length, source->data,
                        (uLong)source->length);
    if (status != Z_OK) return npl_fixnum(status);
    return npl_fixnum((int64_t)length);
}
