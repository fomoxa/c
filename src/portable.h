#ifndef FOMOXA_INTERNAL_PORTABLE_H
#define FOMOXA_INTERNAL_PORTABLE_H

#if !defined(_WIN32)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#endif

#endif /* FOMOXA_INTERNAL_PORTABLE_H */
