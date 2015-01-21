#include "helper.h"

#include "slre.c"

/*
@header
    Wraps a very simple regular expression library (SLRE) as a CZMQ class.
    Supports this syntax:

        ^               Match beginning of a buffer
        $               Match end of a buffer
        ()              Grouping and substring capturing
        [...]           Match any character from set
        [^...]          Match any character but ones from set
        .               Match any character
        \s              Match whitespace
        \S              Match non-whitespace
        \d              Match decimal digit
        \D              Match non decimal digit
        \a              Match alphabetic character
        \A              Match non-alphabetic character
        \w              Match alphanumeric character
        \W              Match non-alphanumeric character
        \r              Match carriage return
        \n              Match newline
        +               Match one or more times (greedy)
        +?              Match one or more times (non-greedy)
        *               Match zero or more times (greedy)
        *?              Match zero or more times (non-greedy)
        ?               Match zero or once
        \xDD            Match byte with hex value 0xDD
        \meta           Match one of the meta character: ^$().[*+?\
@discuss
@end
*/

void zstr_free(char **string_p)
{
    assert (string_p);
    free (*string_p);
    *string_p = NULL;
}


char *zsys_vprintf(const char *format, va_list argptr)
{
    int size = 256;
    char *string = (char *) zmalloc (size);
    if (!string)
        return NULL;

    //  Using argptr is destructive, so we take a copy each time we need it
    //  We define va_copy for Windows in czmq_prelude.h
    va_list my_argptr;
    va_copy(my_argptr, argptr);
    int required = vsnprintf (string, size, format, my_argptr);
    va_end (my_argptr);
#ifdef Q_OS_WIN
    if (required < 0 || required >= size) {
        va_copy (my_argptr, argptr);
#ifdef _MSC_VER
        required = _vscprintf (format, argptr);
#else
        required = vsnprintf (NULL, 0, format, argptr);
#endif
        va_end (my_argptr);
    }
#endif
    //  If formatted string cannot fit into small string, reallocate a
    //  larger buffer for it.
    if (required >= size) {
        size = required + 1;
        free (string);
        string = (char *) zmalloc (size);
        if (string) {
            va_copy (my_argptr, argptr);
            vsnprintf (string, size, format, my_argptr);
            va_end (my_argptr);
        }
    }
    return string;
}


char *zsys_sprintf(const char *format, ...)
{
    va_list argptr;
    va_start (argptr, format);
    char *string = zsys_vprintf (format, argptr);
    va_end (argptr);
    return (string);
}


zrex_t *zrex_new(const char *expression)
{
    zrex_t *self = (zrex_t *) zmalloc (sizeof (zrex_t));
    if (self) {
        self->strerror = "No error";
        if (expression) {
            //  Returns 1 on success, 0 on failure
            self->valid = (slre_compile (&self->slre, expression) == 1);
            if (!self->valid)
                self->strerror = self->slre.err_str;
            assert (self->slre.num_caps < MAX_HITS);
        }
    }
    return self;
}


bool zrex_valid(zrex_t *self)
{
    assert (self);
    return self->valid;
}


void zrex_destroy(zrex_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        zrex_t *self = *self_p;
        zstr_free (&self->hit_set);
        free (self);
        *self_p = NULL;
    }
}


const char *zrex_strerror(zrex_t *self)
{
    assert (self);
    return self->strerror;
}


bool zrex_matches(zrex_t *self, const char *text)
{
    assert (self);
    assert (text);

    //  Free any previously-allocated hits
    self->hits = 0;

    bool matches = slre_match (&self->slre, text, strlen (text), self->caps) != 0;
    if (matches) {
        //  Count number of captures plus whole string
        self->hits = self->slre.num_caps + 1;
        if (self->hits > MAX_HITS)
            self->hits = MAX_HITS;

        //  Collect hits and prepare hit array, which is a single block of
        //  memory holding all hits as null-terminated strings
        uint index;
        //  First count total length of hit strings
        uint hit_set_len = 0;
        for (index = 0; index < self->hits; index++)
            hit_set_len += self->caps [index].len + 1;
        if (hit_set_len > self->hit_set_len) {
            zstr_free (&self->hit_set);
            self->hit_set = (char *) zmalloc (hit_set_len);
            self->hit_set_len = hit_set_len;
        }
        // FIXME: no way to return an error
        assert (self->hit_set);

        //  Now prepare hit strings for access by caller
        char *hit_set_ptr = self->hit_set;
        for (index = 0; index < self->hits; index++) {
            memcpy (hit_set_ptr, self->caps [index].ptr, self->caps [index].len);
            self->hit [index] = hit_set_ptr;
            hit_set_ptr += self->caps [index].len + 1;
        }
    }
    return matches;
}


bool zrex_eq(zrex_t *self, const char *text, const char *expression)
{
    assert (self);
    assert (text);
    assert (expression);

    //  Compile the new expression
    self->valid = (slre_compile (&self->slre, expression) == 1);
    if (!self->valid)
        self->strerror = self->slre.err_str;
    assert (self->slre.num_caps < MAX_HITS);

    //  zrex_matches takes care of the rest for us
    if (self->valid)
        return zrex_matches (self, text);
    else
        return false;
}


int zrex_hits(zrex_t *self)
{
    assert (self);
    return self->hits;
}


const char *zrex_hit(zrex_t *self, uint index)
{
    assert (self);
    if (index < self->hits)
        return self->hit [index];
    else
        return NULL;
}


int zrex_fetch(zrex_t *self, const char **string_p, ...)
{
    assert (self);
    va_list args;
    va_start (args, string_p);
    uint index = 0;
    while (string_p) {
        *string_p = zrex_hit (self, ++index);
        string_p = va_arg (args, const char **);
    }
    va_end (args);
    return index;
}


void zrex_test(bool)
{
    printf (" * zrex: ");

    //  This shows the pattern of matching many lines to a single pattern
    zrex_t *rex = zrex_new ("\\d+-\\d+-\\d+");
    assert (rex);
    assert (zrex_valid (rex));
    bool matches = zrex_matches (rex, "123-456-789");
    assert (matches);
    assert (zrex_hits (rex) == 1);
    assert (streq (zrex_hit (rex, 0), "123-456-789"));
    assert (zrex_hit (rex, 1) == NULL);
    zrex_destroy (&rex);

    //  Here we pick out hits using capture groups
    rex = zrex_new ("(\\d+)-(\\d+)-(\\d+)");
    assert (rex);
    assert (zrex_valid (rex));
    matches = zrex_matches (rex, "123-456-ABC");
    assert (!matches);
    matches = zrex_matches (rex, "123-456-789");
    assert (matches);
    assert (zrex_hits (rex) == 4);
    assert (streq (zrex_hit (rex, 0), "123-456-789"));
    assert (streq (zrex_hit (rex, 1), "123"));
    assert (streq (zrex_hit (rex, 2), "456"));
    assert (streq (zrex_hit (rex, 3), "789"));
    zrex_destroy (&rex);

    //  This shows the pattern of matching one line against many
    //  patterns and then handling the case when it hits
    rex = zrex_new (NULL);      //  No initial pattern
    assert (rex);
    char *input = "Mechanism: CURVE";
    matches = zrex_eq (rex, input, "Version: (.+)");
    assert (!matches);
    assert (zrex_hits (rex) == 0);
    matches = zrex_eq (rex, input, "Mechanism: (.+)");
    assert (matches);
    assert (zrex_hits (rex) == 2);
    const char *mechanism;
    zrex_fetch (rex, &mechanism, NULL);
    assert (streq (zrex_hit (rex, 1), "CURVE"));
    assert (streq (mechanism, "CURVE"));
    zrex_destroy (&rex);
    printf ("OK\n");
}


int64_t clock_mono()
{
#if defined (Q_OS_MAC)
    clock_serv_t cclock;
    mach_timespec_t mts;
    host_get_clock_service (mach_host_self (), SYSTEM_CLOCK, &cclock);
    clock_get_time (cclock, &mts);
    mach_port_deallocate (mach_task_self (), cclock);
    return (int64_t) ((int64_t) mts.tv_sec * 1000 + (int64_t) mts.tv_nsec / 1000000);

#elif defined (Q_OS_UNIX)
    struct timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return (int64_t) ((int64_t) ts.tv_sec * 1000 + (int64_t) ts.tv_nsec / 1000000);

#elif (defined (Q_OS_WIN))
    //  System frequency does not change at run-time, cache it
    static int64_t frequency = 0;
    if (frequency == 0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency (&freq);
        // Windows documentation says that XP and later will always return non-zero
        assert (freq.QuadPart != 0);
        frequency = freq.QuadPart;
    }
    LARGE_INTEGER count;
    QueryPerformanceCounter (&count);
    return (int64_t) (count.QuadPart * 1000) / frequency;
#endif
}
