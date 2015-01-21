#ifndef QMESSAGE_H
#define QMESSAGE_H

#include <QStringList>

#ifndef QMQ_EXPORT
#define QMQ_EXPORT
#endif

class SocketBase;

class FramePrivate;
class QMQ_EXPORT Frame
{
public:
    Frame();
    Frame(const Frame& data);
    Frame(const QByteArray& data);
    Frame(const void *data, int length);
    virtual ~Frame();

    void reset(const QByteArray& data);
    void reset(const void* data, int length);

    QByteArray bdata() const;
    void* data();
    const void* constData() const;

    QString hexString();
    QString toString() const;
    QByteArray toByteArray() const;

    int size() const;
    bool isEmpty();
    bool hasMore() const;
    void setMore(bool more);

    //
    virtual int send(SocketBase& socket, int flags);
    virtual bool recv(SocketBase &socket);
    virtual int recvnowait(SocketBase& socket);

    //
    void swap(const Frame& other);

    Frame& operator=(const Frame& other);
    Frame& operator=(const QString& other);

    friend bool operator ==(const Frame& f, const Frame& f1);
    inline friend bool operator ==(const Frame& f, const QString& str);
    inline friend bool operator ==(const QString& str, const Frame& f);

    static void test();

protected:
    Frame(FramePrivate& d);
    FramePrivate* d_ptr;

private:
    Q_DECLARE_PRIVATE(Frame)
};

bool operator ==(const Frame& f, const QString& str)
{
    return f.toString() == str;
}

bool operator ==(const QString& str, const Frame& f)
{
    return f.toString() == str;
}
//
//
//

class MessagesPrivate;

/// message list class
class QMQ_EXPORT Messages
{
public:
    Messages();
    Messages(const Messages& msg);
    ~Messages();

    //
    int send(SocketBase &socket);
    // this function cause clear list
    bool recv(SocketBase& socket);
    void recvnowait(SocketBase& socket);

    void decode(const QByteArray& buffer);
    void decode(const uchar* data, int length);

    QByteArray encode();

    // this function cause remove current list
    int load(QIODevice *file);
    int save(QIODevice *file);

    // take owner ship of frame

    void append(const Frame& frame);
    void prepend(const Frame& frame);

    void append(Messages &mlist);
    void append(Frame* frame);
    void appendmem(const void *data, int length);
    void append(const QString& data);
    int append(const char *format, ...);
    void wrap(Frame* frame);

    void prepend(Frame* frame);
    void push(Frame* frame);
    void push(const void* data, int length);
    void push(const QString& data);

    Frame* pop();
    QString popstr();
    Messages *popmsg();


    Frame* unwrap();
    Frame* remove(int i);
    int removeAll(Frame* f);
    void clear();

    Frame* first();
    Frame* last();
    Frame* next(Frame* before);
    Frame* at(int i);

    QString firstStr();
    QString lastStr();
    QStringList toStringList();

    int size();
    int contentSize();

    static void test();

    Messages& operator =(const Messages& msgs);

protected:
    MessagesPrivate* d_ptr;

private:
    Q_DECLARE_PRIVATE(Messages)
};

#endif // QMESSAGE_H
