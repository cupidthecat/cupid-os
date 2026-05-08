#ifndef ASSERT_H
#define ASSERT_H

#include "panic.h"

#ifdef DEBUG
    #define ASSERT(cond) \
        do { \
            if (!(cond)) { \
                kernel_panic("Assertion failed: " #cond \
                             "\n  at %s:%d", __FILE__, __LINE__); \
            } \
        } while(0)

    #define ASSERT_MSG(cond, msg, ...) \
        do { \
            if (!(cond)) { \
                kernel_panic("Assertion failed: " msg \
                             "\n  at %s:%d", \
                             ##__VA_ARGS__, __FILE__, __LINE__); \
            } \
        } while(0)
#else
    #define ASSERT(cond)              ((void)0)
    #define ASSERT_MSG(cond, msg, ...) ((void)0)
#endif

#endif
