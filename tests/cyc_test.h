#ifndef CYC_TEST_H
#define CYC_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cyc_test_failures = 0;

#define CYC_CHECK(cond)                                                                            \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("    %s:%d: %s\n", __FILE__, __LINE__, #cond);                                  \
            cyc_test_failures += 1;                                                                \
        }                                                                                          \
    } while (0)

#define CYC_RUN(fn)                                                                                \
    do {                                                                                           \
        int before = cyc_test_failures;                                                            \
        fn();                                                                                      \
        printf("%s %s\n", cyc_test_failures == before ? "ok  " : "FAIL", #fn);                     \
    } while (0)

#define CYC_DONE() return cyc_test_failures == 0 ? 0 : 1

#endif /* CYC_TEST_H */
