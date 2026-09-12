#ifndef _STATIC_ASSERT_H
#define _STATIC_ASSERT_H

/* Compile-time assertion.  Deliberately re-declarable: including this
 * header twice (directly and through linux/memmap.h) must not clash. */
#undef STATIC_ASSERT
#undef BUILD_BUG_ON

/* The body is a typedef, so a wrong assertion is a compile error with
 * the expression visible in a -Wunused-local-typedefs era diagnostic. */
#define STATIC_ASSERT(cond, tag) \
    typedef char static_assert_##tag[(cond) ? 1 : -1]

#define BUILD_BUG_ON(cond, tag) STATIC_ASSERT(!(cond), tag)

#endif /* _STATIC_ASSERT_H */
