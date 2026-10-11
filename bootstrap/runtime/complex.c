/* complex.c — canonical rectangular complex representation and arithmetic.
 * Components are real numbers; component arithmetic reuses numeric dispatch. */

#include "npl.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int npl_is_complex(NplVal value)
{
    return npl_is_ptr(value) && npl_header(value)->tag == BOX_COMPLEX;
}

/* Canonical rectangular form: an exact-zero imaginary part denotes a real
 * number, and a complex box never mixes exact and inexact components. */
NplVal npl_complex_make(NplVal real, NplVal imag)
{
    if (!npl_number_p(real) || !npl_number_p(imag) ||
        npl_is_complex(real) || npl_is_complex(imag)) {
        fprintf(stderr, "make-rectangular: expected real numbers\n");
        exit(1);
    }
    if (imag == npl_fixnum(0)) return real;
    npl_gc_root(&real);
    npl_gc_root(&imag);
    if (npl_is_inexact(real) != npl_is_inexact(imag)) {
        real = npl_exact_to_inexact(real);
        imag = npl_exact_to_inexact(imag);
    }
    NplComplex *complex = (NplComplex *)npl_alloc_object(sizeof(NplComplex));
    complex->header.tag = BOX_COMPLEX;
    complex->header.flags = 0;
    complex->real = real;
    complex->imag = imag;
    npl_gc_unroot(&imag);
    npl_gc_unroot(&real);
    return (NplVal)(uintptr_t)complex | TAG_PTR;
}

NplVal npl_complex_p(NplVal value)
{
    return npl_is_complex(value) ? npl_true() : npl_false();
}

NplVal npl_complex_real(NplVal value)
{
    if (!npl_is_complex(value)) return value;
    return ((NplComplex *)npl_header(value))->real;
}

NplVal npl_complex_imag(NplVal value)
{
    if (!npl_is_complex(value)) return npl_fixnum(0);
    return ((NplComplex *)npl_header(value))->imag;
}

/* Every component and intermediate stays rooted across later allocations. */
NplVal npl_complex_arithmetic(NplVal left, NplVal right, char operation)
{
    enum { A, B, C, D, REAL, IMAG, T0, T1, T2, COUNT };
    NplVal v[COUNT] = {npl_complex_real(left), npl_complex_imag(left),
                       npl_complex_real(right), npl_complex_imag(right)};
    for (unsigned i = REAL; i < COUNT; i++) v[i] = npl_nil();
    for (unsigned i = 0; i < COUNT; i++) npl_gc_root(&v[i]);
    if (operation == '+' || operation == '-') {
        v[REAL] = operation == '+' ? npl_number_add(v[A], v[C])
                                   : npl_number_sub(v[A], v[C]);
        v[IMAG] = operation == '+' ? npl_number_add(v[B], v[D])
                                   : npl_number_sub(v[B], v[D]);
    } else if (operation == '*') {
        v[T0] = npl_number_mul(v[A], v[C]);
        v[T1] = npl_number_mul(v[B], v[D]);
        v[REAL] = npl_number_sub(v[T0], v[T1]);
        v[T0] = npl_number_mul(v[A], v[D]);
        v[T1] = npl_number_mul(v[B], v[C]);
        v[IMAG] = npl_number_add(v[T0], v[T1]);
    } else {
        v[T0] = npl_number_mul(v[C], v[C]);
        v[T1] = npl_number_mul(v[D], v[D]);
        v[T2] = npl_number_add(v[T0], v[T1]);
        v[T0] = npl_number_mul(v[A], v[C]);
        v[T1] = npl_number_mul(v[B], v[D]);
        v[T0] = npl_number_add(v[T0], v[T1]);
        v[REAL] = npl_number_div(v[T0], v[T2]);
        v[T0] = npl_number_mul(v[B], v[C]);
        v[T1] = npl_number_mul(v[A], v[D]);
        v[T0] = npl_number_sub(v[T0], v[T1]);
        v[IMAG] = npl_number_div(v[T0], v[T2]);
    }
    NplVal result = npl_complex_make(v[REAL], v[IMAG]);
    for (unsigned i = COUNT; i != 0; i--) npl_gc_unroot(&v[i - 1]);
    return result;
}
