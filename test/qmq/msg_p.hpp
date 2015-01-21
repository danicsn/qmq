#ifndef MSG_P_HPP
#define MSG_P_HPP

#include "helper.h"

#include <QQueue>

/*
 * messaging private
 *
 * ******************************/

class FramePrivate {
public:
    FramePrivate() {
        zmq_msg_init(&msg);
        more = 0;
    }
    FramePrivate(const void* data, int size) {
        more = 0;
        if (size) {
            zmq_msg_init_size(&msg, size);
            if (data)
                memcpy(zmq_msg_data(&msg), data, size);
        }
        else
            zmq_msg_init(&msg);
    }
    virtual ~FramePrivate() {
        zmq_msg_close(&msg);
    }

    const void* data() const {
        return static_cast<const void *>(zmq_msg_data(const_cast<zmq_msg_t*>(&msg)));
    }
    int size() const {
        return zmq_msg_size(const_cast<zmq_msg_t*>(&msg));
    }

    zmq_msg_t msg;
    int more;
};

class MessagesPrivate {
public:
    MessagesPrivate(){
        contentsize = 0;
    }
    ~MessagesPrivate() {
        if(frames.count() > 1)
            qDeleteAll(frames);
    }

    QQueue<Frame*> frames;
    int contentsize;
};

#endif // MSG_P_HPP
