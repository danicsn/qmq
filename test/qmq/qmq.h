#ifndef QMQ_H
#define QMQ_H

/*===============================================================
 * this library is writed by Daniel Nowrozi.
 * i think the zmq implementation is good enough for using directly in Qt
 * but for higher level implementatoins, i try czmq and i feel i can use
 * it more event based so i try to implement it in Qt.
 * ==============================================================*/

#include <QtGlobal>

#ifdef Q_OS_LINUX
#include <stdio.h>
#include <stdlib.h>
#endif

#ifndef QMQ_EXPORT

#if defined LIBQMQ_STATIC
#   define QMQ_EXPORT
#elif defined QMQ_LIBRARY
#   define QMQ_EXPORT Q_DECL_EXPORT
#else
#   define QMQ_EXPORT Q_DECL_IMPORT
#endif

#endif



// all pure C functions get from czmq library

//- Helper functions --------------------------------------------------------------

//  Replacement for malloc() which asserts if we run out of heap, and
//  which zeroes the allocated block.
static inline void *
safe_malloc (size_t size, const char *file, unsigned line)
{
//     printf ("%s:%u %08d\n", file, line, (int) size);
#if defined (__UTYPE_LINUX)
    //  On GCC we count zmalloc memory allocations
    __sync_add_and_fetch (&zsys_allocs, 1);
#endif
    void *mem = calloc (1, size);
    if (mem == NULL) {
        fprintf (stderr, "FATAL ERROR at %s:%u\n", file, line);
        fprintf (stderr, "OUT OF MEMORY (malloc returned NULL)\n");
        fflush (stderr);
        abort ();
    }
    return mem;
}

#define streq(s1,s2)    (!strcmp ((s1), (s2)))
#define strneq(s1,s2)   (strcmp ((s1), (s2)))

//  Provide random number from 0..(num-1)
#if (defined (Q_OS_WIN)) || (defined (__UTYPE_IBMAIX)) \
 || (defined (__UTYPE_HPUX)) || (defined (__UTYPE_SUNOS))
#   define randof(num)  (int) ((float) (num) * rand () / (RAND_MAX + 1.0))
#else
#   define randof(num)  (int) ((float) (num) * random () / (RAND_MAX + 1.0))
#endif

//  Define _ZMALLOC_DEBUG if you need to trace memory leaks using e.g. mtrace,
//  otherwise all allocations will claim to come from czmq_prelude.h. For best
//  results, compile all classes so you see dangling object allocations.
//  _ZMALLOC_PEDANTIC does the same thing, but its intention is to propagate
//  out of memory condition back up the call stack.
#if defined _ZMALLOC_DEBUG || _ZMALLOC_PEDANTIC
#   define zmalloc(size) calloc(1,(size))
#else
#   define zmalloc(size) safe_malloc((size), __FILE__, __LINE__)
#endif

#   if (!defined (va_copy))
    //  MSVC does not support C99's va_copy so we use a regular assignment
#       define va_copy(dest,src) (dest) = (src)
#   endif

//- Data types --------------------------------------------------------------

typedef unsigned char   byte;           //  Single unsigned byte = 8 bits

//- Inevitable macros -------------------------------------------------------

//  @interface
#define QFRAME_MORE     1
#define QFRAME_REUSE    2
#define QFRAME_DONTWAIT 4


// implemented classes


#include "context.h"
#include "socket.h"
#include "message.h"
#include "actor.h"
#include "poller.h"
#include "gossip.h"
#include "sevent.h"
#include "mdp.h"

#endif // QMQ_H
