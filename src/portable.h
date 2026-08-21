#ifndef CYCLONE_INTERNAL_PORTABLE_H
#define CYCLONE_INTERNAL_PORTABLE_H

#if !defined(_WIN32)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#endif

#endif /* CYCLONE_INTERNAL_PORTABLE_H */
