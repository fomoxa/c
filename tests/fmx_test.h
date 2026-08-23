#ifndef FMX_TEST_H
#define FMX_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fmx_test_failures = 0;

#define FMX_CHECK(cond)                                                                            \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("    %s:%d: %s\n", __FILE__, __LINE__, #cond);                                  \
            fmx_test_failures += 1;                                                                \
        }                                                                                          \
    } while (0)

#define FMX_RUN(fn)                                                                                \
    do {                                                                                           \
        int before = fmx_test_failures;                                                            \
        fn();                                                                                      \
        printf("%s %s\n", fmx_test_failures == before ? "ok  " : "FAIL", #fn);                     \
    } while (0)

#define FMX_DONE() return fmx_test_failures == 0 ? 0 : 1

#endif /* FMX_TEST_H */
