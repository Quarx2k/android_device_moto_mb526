#ifndef __MESSAGEQUEUE_H__
#define __MESSAGEQUEUE_H__

struct Message
{
    unsigned int command;
    void*        arg1;
    void*        arg2;
    void*        arg3;
    void*        arg4;
};

class MessageQueue
{
public:
    MessageQueue();
    ~MessageQueue();
    int get(Message*);
    int getInFd();
    int put(Message*);
    bool isEmpty();
private:
    int fd_read;
    int fd_write;
};

#endif
