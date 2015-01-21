#ifndef HELPER_HPP
#define HELPER_HPP

#include "qmq.h"
#include "stdarg.h"
#include "slre.h"
#include "../include/zmq.hpp"

// all functions get from czmq

#if (defined (Q_OS_UNIX))
#   include <fcntl.h>
#   include <netdb.h>
#   include <unistd.h>
#   include <pthread.h>
#   include <dirent.h>
#   include <pwd.h>
#   include <grp.h>
#   include <utime.h>
#   include <inttypes.h>
#   include <syslog.h>
#   include <sys/types.h>
#   include <sys/param.h>
#   include <sys/socket.h>
#   include <sys/time.h>
#   include <sys/stat.h>
#   include <sys/ioctl.h>
#   include <sys/file.h>
#   include <sys/wait.h>
#   include <sys/un.h>
#   include <sys/uio.h>             //  Let CZMQ build with libzmq/3.x
#   include <netinet/in.h>          //  Must come before arpa/inet.h
#   if (!defined (__UTYPE_ANDROID)) && (!defined (__UTYPE_IBMAIX)) \
    && (!defined (__UTYPE_HPUX))
#       include <ifaddrs.h>
#   endif
#   if defined (__UTYPE_SUNSOLARIS) || defined (__UTYPE_SUNOS)
#       include <sys/sockio.h>
#   endif
#   if (!defined (__UTYPE_BEOS))
#       include <arpa/inet.h>
#       if (!defined (TCP_NODELAY))
#           include <netinet/tcp.h>
#       endif
#   endif
#   if (defined (__UTYPE_IBMAIX) || defined(__UTYPE_QNX))
#       include <sys/select.h>
#   endif
#   if (defined (__UTYPE_BEOS))
#       include <NetKit.h>
#   endif
#   if ((defined (_XOPEN_REALTIME) && (_XOPEN_REALTIME >= 1)) \
     || (defined (_POSIX_VERSION)  && (_POSIX_VERSION  >= 199309L)))
#       include <sched.h>
#   endif
#   if (defined (__UTYPE_OSX))
#       include <crt_externs.h>         //  For _NSGetEnviron()
#       include <mach/clock.h>
#       include <mach/mach.h>           //  For monotonic clocks
#   endif
#endif

//  --------------------------------------------------------------------------
//  Free a provided string, and nullify the parent pointer. Safe to call on
//  a null pointer.
void zstr_free (char **string_p);

//  --------------------------------------------------------------------------
//  Format a string with variable arguments, returning a freshly allocated
//  buffer. If there was insufficient memory, returns NULL. Free the returned
//  string using zstr_free().
char * zsys_vprintf (const char *format, va_list argptr);
char * zsys_sprintf (const char *format, ...);

#define MAX_HITS 100            //  Should be enough for anyone :)

//  Structure of our class

struct zrex_t {
    struct slre slre;           //  Compiled regular expression
    bool valid;                 //  Is expression valid or not?
    const char *strerror;       //  Error message if any
    uint hits;                  //  Number of hits matched
    uint hit_set_len;           //  Length of hit set
    char *hit_set;              //  Captured hits as single string
    char *hit [MAX_HITS];       //  Pointers into hit_set
    struct cap caps [MAX_HITS]; //  Position/length for each hit
};


//  --------------------------------------------------------------------------
//  Constructor. Optionally, sets an expression against which we can match
//  text and capture hits. If there is an error in the expression, reports
//  zrex_valid() as false and provides the error in zrex_strerror(). If you
//  set a pattern, you can call zrex_matches() to test it against text.
zrex_t * zrex_new (const char *expression);


//  --------------------------------------------------------------------------
//  Destructor
void zrex_destroy (zrex_t **self_p);


//  --------------------------------------------------------------------------
//  Return true if the expression was valid and compiled without errors.
bool zrex_valid (zrex_t *self);


//  --------------------------------------------------------------------------
//  Return the error message generated during compilation of the expression.
const char * zrex_strerror (zrex_t *self);


//  --------------------------------------------------------------------------
//  Returns true if the text matches the previously compiled expression.
//  Use this method to compare one expression against many strings.
bool zrex_matches (zrex_t *self, const char *text);


//  --------------------------------------------------------------------------
//  Returns true if the text matches the supplied expression. Use this
//  method to compare one string against several expressions.
bool zrex_eq (zrex_t *self, const char *text, const char *expression);


//  --------------------------------------------------------------------------
//  Returns number of hits from last zrex_matches or zrex_eq. If the text
//  matched, returns 1 plus the number of capture groups. If the text did
//  not match, returns zero. To retrieve individual capture groups, call
//  zrex_hit ().
int zrex_hits (zrex_t *self);


//  --------------------------------------------------------------------------
//  Returns the Nth capture group from the last expression match, where
//  N is 0 to the value returned by zrex_hits(). Capture group 0 is the
//  whole matching string. Sequence 1 is the first capture group, if any,
//  and so on.
const char * zrex_hit (zrex_t *self, uint index);


//  --------------------------------------------------------------------------
//  Fetches hits into string variables provided by caller; this makes for
//  nicer code than accessing hits by index. Caller should not modify nor
//  free the returned values. Returns number of strings returned. This
//  method starts at hit 1, i.e. first capture group, as hit 0 is always
//  the original matched string.
int zrex_fetch (zrex_t *self, const char **string_p, ...);


//  --------------------------------------------------------------------------
//  Selftest
void zrex_test (bool verbose);

int64_t clock_mono (void);

#endif // HELPER_HPP
