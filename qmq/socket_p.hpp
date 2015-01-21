#ifndef SOCKET_P_HPP
#define SOCKET_P_HPP

#include "helper.h"

#include <QString>
/*
 * socket private
 *
 * *******************************/

class SocketBasePrivate {
public:
    SocketBasePrivate() {
        handle = 0;
        m_pcntxt = 0;
    }
    virtual ~SocketBasePrivate() {
    }

    void* handle;
    Context* m_pcntxt;
    int type;                   //  Socket type
};

class SocketPrivate : public SocketBasePrivate {
public:
    SocketPrivate() {
        cache = 0;
        cache_size = 0;
    }
    ~SocketPrivate()
    {
//        if(cache)
//            free(cache);
    }

    QString endpoint;             //  Last bound endpoint, if any
    char *cache;                //  Holds last zsock_brecv strings
    size_t cache_size;          //  Current size of cache
};

#endif // SOCKET_P_HPP
