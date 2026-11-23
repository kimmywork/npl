/* (npl demo crc32): the first external native package sample.
 *
 * One stateless NplVal-ABI function.  It reads the bytevector layout from
 * npl.h inline accessors and does not link or call the NPL runtime, which is
 * the mode-B boundary.  The NPL wrapper validates the argument.
 */

#include "npl.h"

NplVal npl_demo_crc32(NplVal bytes_value)
{
    static uint32_t table[256];
    static int initialized = 0;
    const NplBytevector *bytes;
    uint32_t crc = 0xffffffffu;
    uint32_t index;

    if (!npl_is_ptr(bytes_value) ||
        npl_header(bytes_value)->tag != BOX_BYTEVECTOR)
        return npl_fixnum(-1);
    bytes = (const NplBytevector *)npl_header(bytes_value);

    if (!initialized) {
        uint32_t n;
        for (n = 0; n < 256; n++) {
            uint32_t value = n;
            int bit;
            for (bit = 0; bit < 8; bit++)
                value = (value & 1u) ? (0xedb88320u ^ (value >> 1))
                                     : (value >> 1);
            table[n] = value;
        }
        initialized = 1;
    }

    for (index = 0; index < bytes->length; index++)
        crc = table[(crc ^ bytes->data[index]) & 0xffu] ^ (crc >> 8);
    return npl_fixnum((int64_t)(crc ^ 0xffffffffu));
}
