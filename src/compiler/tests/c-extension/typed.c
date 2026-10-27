/* Plain C functions for the define-c-function tests (typed.npl): no npl.h,
 * only C types, as an existing C library would be. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

int demo_add(int a, int b) { return a + b; }

double demo_scale(double x, long factor) { return x * (double)factor; }

size_t demo_count(const char *text, int c)
{
    size_t count = 0;
    for (; *text; text++)
        if (*text == c) count++;
    return count;
}

bool demo_even(int64_t n) { return n % 2 == 0; }

/* NULL when give is false: the result is #f. */
const char *demo_greeting(bool give) { return give ? "hello, world" : NULL; }

/* A heap box holding value; status 0, or -1 when value is negative. */
int demo_box_make(int64_t value, void **out)
{
    if (value < 0) return -1;
    int64_t *box = malloc(sizeof *box);
    if (!box) return -1;
    *box = value;
    *out = box;
    return 0;
}

int64_t demo_box_value(void *box) { return *(int64_t *)box; }

static int g_freed = 0;

void demo_box_free(void *box)
{
    free(box);
    g_freed++;
}

int demo_freed(void) { return g_freed; }

/* Two outputs and no result: the procedure returns two values. */
void demo_split(int64_t value, void **low, void **high)
{
    *low = (void *)(uintptr_t)(value & 0xff);
    *high = (void *)(uintptr_t)(value >> 8);
}
