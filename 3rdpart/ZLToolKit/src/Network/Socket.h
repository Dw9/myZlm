﻿/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/xiongziliang/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef NETWORK_SOCKET_H
#define NETWORK_SOCKET_H

#include <memory>
#include <string>
#include <deque>
#include <mutex>
#include <vector>
#include <atomic>
#include <sstream>
#include <functional>
#include "Util/util.h"
#include "Util/onceToken.h"
#include "Util/uv_errno.h"
#include "Util/TimeTicker.h"
#include "Util/ResourcePool.h"
#include "Poller/Timer.h"
#include "Poller/EventPoller.h"
#include "Network/sockutil.h"
#include "Buffer.h"
using namespace std;

namespace toolkit {

#if defined(MSG_NOSIGNAL)
#define FLAG_NOSIGNAL MSG_NOSIGNAL
#else
#define FLAG_NOSIGNAL 0
#endif //MSG_NOSIGNAL

#if defined(MSG_MORE)
#define FLAG_MORE MSG_MORE
#else
#define FLAG_MORE 0
#endif //MSG_MORE

#if defined(MSG_DONTWAIT)
#define FLAG_DONTWAIT MSG_DONTWAIT
#else
#define FLAG_DONTWAIT 0
#endif //MSG_DONTWAIT

//默认的socket flags:不触发SIGPIPE,非阻塞发送
#define SOCKET_DEFAULE_FLAGS (FLAG_NOSIGNAL | FLAG_DONTWAIT )
    
//发送超时时间，如果在规定时间内一直没有发送数据成功，那么将触发onErr事件
#define SEND_TIME_OUT_SEC 10
    
//错误类型枚举
typedef enum {
    Err_success = 0, //成功
    Err_eof, //eof
    Err_timeout, //超时
    Err_refused,//连接被拒绝
    Err_dns,//dns解析失败
    Err_shutdown,//主动关闭
    Err_other = 0xFF,//其他错误
} ErrCode;

//错误信息类
class SockException: public std::exception {
public:
    SockException(ErrCode errCode = Err_success,
                  const string &errMsg = "",
                  int customCode = 0) {
        _errMsg = errMsg;
        _errCode = errCode;
        _customCode = customCode;
    }

    //重置错误
    void reset(ErrCode errCode, const string &errMsg) {
        _errMsg = errMsg;
        _errCode = errCode;
    }
    //错误提示
    virtual const char* what() const noexcept {
        return _errMsg.c_str();
    }
    //错误代码
    ErrCode getErrCode() const {
        return _errCode;
    }
    //判断是否真的有错
    operator bool() const{
        return _errCode != Err_success;
    }
    //用户自定义错误代码
    int getCustomCode () const{
        return _customCode;
    }
    //获取用户自定义错误代码
    void setCustomCode(int code) {
        _customCode = code;
    };
private:
    string _errMsg;
    ErrCode _errCode;
    int _customCode = 0;
};

class SockNum{
public:
    typedef enum {
        Sock_TCP = 0,
        Sock_UDP = 1
    } SockType;

    typedef std::shared_ptr<SockNum> Ptr;
    SockNum(int fd,SockType type){
        _fd = fd;
        _type = type;
    }
    ~SockNum(){
#if defined (OS_IPHONE)
        unsetSocketOfIOS(_fd);
#endif //OS_IPHONE
        ::shutdown(_fd, SHUT_RDWR);
        close(_fd);
    }

    int rawFd() const{
        return _fd;
    }

    SockType type(){
        return _type;
    }

    void setConnected(){
#if defined (OS_IPHONE)
        setSocketOfIOS(_fd);
#endif //OS_IPHONE
    }
private:
    SockType _type;
    int _fd;
#if defined (OS_IPHONE)
    void *readStream=NULL;
    void *writeStream=NULL;
    bool setSocketOfIOS(int socket);
    void unsetSocketOfIOS(int socket);
#endif //OS_IPHONE
};

//socket 文件描述符的包装
//在析构时自动溢出监听并close套接字
//防止描述符溢出
class SockFD : public noncopyable {
public:
    typedef std::shared_ptr<SockFD> Ptr;
    /**
     * 创建一个fd对象
     * @param num 文件描述符，int数字
     * @param poller 事件监听器
     */
    SockFD(int num,SockNum::SockType type,const EventPoller::Ptr &poller){
        _num = std::make_shared<SockNum>(num,type);
        _poller = poller;
    }

    /**
     * 复制一个fd对象
     * @param that 源对象
     * @param poller 事件监听器
     */
    SockFD(const SockFD &that,const EventPoller::Ptr &poller){
        _num = that._num;
        _poller = poller;
        if(_poller == that._poller){
            throw invalid_argument("copy a SockFD with same poller!");
        }
    }

    ~SockFD() {
        auto num = _num;
        _poller->delEvent(_num->rawFd(), [num](bool) {});
    }

    void setConnected() {
        _num->setConnected();
    }

    int rawFd() const {
        return _num->rawFd();
    }

    SockNum::SockType type() {
        return _num->type();
    }

private:
    SockNum::Ptr _num;
    EventPoller::Ptr _poller;
};

template <class Mtx = recursive_mutex>
class MutexWrapper {
public:
    MutexWrapper(bool enable){
        _enable = enable;
    }
    ~MutexWrapper(){}

    inline void lock(){
        if(_enable){
            _mtx.lock();
        }
    }
    inline void unlock(){
        if(_enable){
            _mtx.unlock();
        }
    }
private:
    bool _enable;
    Mtx _mtx;
};

class SockInfo {
public:
    SockInfo() = default;
    virtual ~SockInfo() = default;

    //获取本机ip
    virtual string get_local_ip() = 0;
    //获取本机端口号
    virtual uint16_t get_local_port() = 0;
    //获取对方ip
    virtual string  get_peer_ip() = 0;
    //获取对方端口号
    virtual uint16_t get_peer_port() = 0;
    //获取标识符
    virtual string getIdentifier() const { return ""; }
};

#define TraceP(ptr) TraceL << ptr->getIdentifier() << "(" << ptr->get_peer_ip() << ":" << ptr->get_peer_port() << ") "
#define DebugP(ptr) DebugL << ptr->getIdentifier() << "(" << ptr->get_peer_ip() << ":" << ptr->get_peer_port() << ") "
#define InfoP(ptr) InfoL << ptr->getIdentifier() << "(" << ptr->get_peer_ip() << ":" << ptr->get_peer_port() << ") "
#define WarnP(ptr) WarnL << ptr->getIdentifier() << "(" << ptr->get_peer_ip() << ":" << ptr->get_peer_port() << ") "
#define ErrorP(ptr) ErrorL << ptr->getIdentifier() << "(" << ptr->get_peer_ip() << ":" << ptr->get_peer_port() << ") "

//异步IO Socket对象，包括tcp客户端、服务器和udp套接字
class Socket : public std::enable_shared_from_this<Socket>, public noncopyable, public SockInfo {
public:
    typedef std::shared_ptr<Socket> Ptr;
    //接收数据回调
    typedef function<void(const Buffer::Ptr &buf, struct sockaddr *addr, int addr_len)> onReadCB;
    //发生错误回调
    typedef function<void(const SockException &err)> onErrCB;
    //tcp监听接收到连接请求
    typedef function<void(Socket::Ptr &sock)> onAcceptCB;
    //socket发送缓存清空事件，返回true代表下次继续监听该事件，否则停止
    typedef function<bool()> onFlush;
    //在接收到连接请求前，拦截Socket默认生成方式
    typedef function<Ptr(const EventPoller::Ptr &poller)> onCreateSocket;

    /**
     * 构造socket对象，尚未有实质操作
     * @param poller 绑定的poller线程
     * @param enable_mutex 是否启用互斥锁(接口是否线程安全)
    */
    static Ptr createSocket(const EventPoller::Ptr &poller = nullptr, bool enable_mutex = true);
    Socket(const EventPoller::Ptr &poller = nullptr, bool enable_mutex = true);

    virtual ~Socket();

    /**
     * 创建tcp客户端并异步连接服务器
     * @param url 目标服务器ip或域名
     * @param port 目标服务器端口
     * @param con_cb 结果回调
     * @param timeout_sec 超时时间
     * @param local_ip 绑定本地网卡ip
     * @param local_port 绑定本地网卡端口号
     */
    virtual void connect(const string &url, uint16_t port, onErrCB con_cb, float timeout_sec = 5,
                 const string &local_ip = "0.0.0.0", uint16_t local_port = 0);
    /**
     * 创建tcp监听服务器
     * @param port 监听端口，0则随机
     * @param local_ip 监听的网卡ip
     * @param backlog tcp最大积压数
     * @return 是否成功
     */
    virtual bool listen(uint16_t port, const string &local_ip = "0.0.0.0", int backlog = 1024);

    /**
     * 创建udp套接字,udp是无连接的，所以可以作为服务器和客户端
     * @param port 绑定的端口为0则随机
     * @param local_ip 绑定的网卡ip
     * @return 是否成功
     */
    virtual bool bindUdpSock(uint16_t port, const string &local_ip = "0.0.0.0");

    ////////////设置事件回调////////////

    /**
     * 设置数据接收回调,tcp或udp客户端有效
     * @param cb 回调对象
     */
    virtual void setOnRead(onReadCB cb);

    /**
     * 设置异常事件(包括eof等)回调
     * @param cb 回调对象
     */
    virtual void setOnErr(onErrCB cb);

    /**
     * 设置tcp监听接收到连接回调
     * @param cb 回调对象
     */
    virtual void setOnAccept(onAcceptCB cb);

    /**
     * 设置socket写缓存清空事件回调
     * 通过该回调可以实现发送流控
     * @param cb 回调对象
     */
    virtual void setOnFlush(onFlush cb);

    /**
     * 设置accept时，socket构造事件回调
     * @param cb 回调
     */
    virtual void setOnBeforeAccept(onCreateSocket cb);

    ////////////发送数据相关接口////////////

    /**
     * 发送数据指针
     * @param buf 数据指针
     * @param size 数据长度
     * @param addr 目标地址
     * @param addr_len 目标地址长度
     * @param try_flush 是否尝试写socket
     * @return -1代表失败(socket无效)，0代表数据长度为0，否则返回数据长度
     */
    int send(const char *buf, int size = 0, struct sockaddr *addr = nullptr, socklen_t addr_len = 0, bool try_flush = true);

    /**
     * 发送string
     */
    int send(string buf, struct sockaddr *addr = nullptr, socklen_t addr_len = 0, bool try_flush = true);

    /**
     * 发送Buffer对象，Socket对象发送数据的统一出口
     */
    virtual int send(Buffer::Ptr buf, struct sockaddr *addr = nullptr, socklen_t addr_len = 0, bool try_flush = true);

    /**
     * 关闭socket且触发onErr回调，onErr回调将在poller线程中进行
     * @param err 错误原因
     * @return 是否成功触发onErr回调
     */
    virtual bool emitErr(const SockException &err);

    /**
     * 关闭或开启数据接收
     * @param enabled 是否开启
     */
    virtual void enableRecv(bool enabled);

    /**
     * 获取裸文件描述符，请勿进行close操作(因为Socket对象会管理其生命周期)
     * @return 文件描述符
     */
    virtual int rawFD() const;

    /**
     * 设置发送超时主动断开时间;默认10秒
     * @param second 发送超时数据，单位秒
     */
    virtual void setSendTimeOutSecond(uint32_t second);

    /**
     * 从缓存池获取一片缓存
     * @return 一片缓存
     */
    virtual BufferRaw::Ptr obtainBuffer();

    /**
     * 套接字是否忙，如果套接字写缓存已满则返回true
     * @return 套接字是否忙
     */
    virtual bool isSocketBusy() const;

    /**
     * 获取poller线程对象
     * @return poller线程对象
     */
    virtual const EventPoller::Ptr &getPoller() const;

    /**
     * 从另外一个Socket克隆
     * 目的是一个socket可以被多个poller对象监听，提高性能
     * @param other 原始的socket对象
     * @return 是否成功
     */
    virtual bool cloneFromListenSocket(const Socket &other);

    /**
     * 设置UDP发送数据时的目标地址，后续发送时就不用再单独指定了
     * @param dst_addr 目标地址
     * @param addr_len 目标地址长度
     * @return 是否成功
     */
    virtual bool setSendPeerAddr(const struct sockaddr *dst_addr, socklen_t addr_len = 0);

    /**
     * 设置发送flags
     * @param flags 发送的flag
     */
    virtual void setSendFlags(int flags = SOCKET_DEFAULE_FLAGS);

    /**
     * 关闭套接字
     */
    virtual void closeSock();

    /**
     * 获取发送缓存包个数(不是字节数)
     */
    virtual int getSendBufferCount();

    /**
     * 获取上次socket发送缓存清空至今的毫秒数,单位毫秒
     */
    virtual uint64_t elapsedTimeAfterFlushed();

    ////////////SockInfo override////////////
    string get_local_ip() override;
    uint16_t get_local_port() override;
    string get_peer_ip() override;
    uint16_t get_peer_port() override;
    string getIdentifier() const override;

private:
    SockFD::Ptr setPeerSock(int fd);
    SockFD::Ptr makeSock(int sock,SockNum::SockType type);
    int onAccept(const SockFD::Ptr &sock, int event);
    int onRead(const SockFD::Ptr &sock, bool is_udp = false);
    void onError(const SockFD::Ptr &sock);
    void onWriteAble(const SockFD::Ptr &sock);
    void onConnected(const SockFD::Ptr &sock, const onErrCB &cb);
    void onFlushed(const SockFD::Ptr &pSock);
    void startWriteAbleEvent(const SockFD::Ptr &sock);
    void stopWriteAbleEvent(const SockFD::Ptr &sock);
    bool listen(const SockFD::Ptr &sock);
    bool flushData(const SockFD::Ptr &sock, bool poller_thread);
    bool attachEvent(const SockFD::Ptr &sock, bool is_udp = false);

private:
    //send socket时的flag
    int _sock_flags = SOCKET_DEFAULE_FLAGS;
    //最大发送缓存，单位毫秒，距上次发送缓存清空时间不能超过该参数
    uint32_t _max_send_buffer_ms = SEND_TIME_OUT_SEC * 1000;
    //控制是否接收监听socket可读事件，关闭后可用于流量控制
    atomic<bool> _enable_recv {true};
    //标记该socket是否可写，socket写缓存满了就不可写
    atomic<bool> _sendable {true};

    //tcp连接超时定时器
    Timer::Ptr _con_timer;
    //tcp连接结果回调对象
    std::shared_ptr<function<void(int)> > _async_con_cb;

    //记录上次发送缓存(包括socket写缓存、应用层缓存)清空的计时器
    Ticker _send_flush_ticker;
    //复用的socket读缓存，每次read socket后，数据存放在此
    BufferRaw::Ptr _read_buffer;
    //socket fd的抽象类
    SockFD::Ptr _sock_fd;
    //本socket绑定的poller线程，事件触发于此线程
    EventPoller::Ptr _poller;
    //跨线程访问_sock_fd时需要上锁
    mutable MutexWrapper<recursive_mutex> _mtx_sock_fd;

    //socket异常事件(比如说断开)
    onErrCB _on_err;
    //收到数据事件
    onReadCB _on_read;
    //socket缓存清空事件(可用于发送流速控制)
    onFlush _on_flush;
    //tcp监听收到accept请求事件
    onAcceptCB _on_accept;
    //tcp监听收到accept请求，自定义创建peer Socket事件(可以控制子Socket绑定到其他poller线程)
    onCreateSocket _on_before_accept;
    //设置上述回调函数的锁
    MutexWrapper<recursive_mutex> _mtx_event;

    //一级发送缓存, socket可写时，会把一级缓存批量送入到二级缓存
    List<Buffer::Ptr> _send_buf_waiting;
    //一级发送缓存锁
    MutexWrapper<recursive_mutex> _mtx_send_buf_waiting;
    //二级发送缓存, socket可写时，会把二级缓存批量写入到socket
    List<BufferList::Ptr> _send_buf_sending;
    //二级发送缓存锁
    MutexWrapper<recursive_mutex> _mtx_send_buf_sending;
};

class SockSender {
public:
    SockSender() = default;
    virtual ~SockSender() = default;
    virtual int send(Buffer::Ptr buf) = 0;
    virtual void shutdown(const SockException &ex = SockException(Err_shutdown, "self shutdown")) = 0;

    //发送char *
    SockSender &operator << (const char *buf);
    //发送字符串
    SockSender &operator << (string buf);
    //发送Buffer对象
    SockSender &operator << (Buffer::Ptr buf);

    //发送其他类型是数据
    template<typename T>
    SockSender &operator << (T &&buf) {
        ostringstream ss;
        ss << std::forward<T>(buf);
        send(ss.str());
        return *this;
    }

    int send(string buf);
    int send(const char *buf, int size = 0);
};

//Socket对象的包装类
class SocketHelper : public SockSender, public SockInfo, public TaskExecutorInterface {
public:
    SocketHelper(const Socket::Ptr &sock);
    ~SocketHelper() override;

    ///////////////////// Socket util functions /////////////////////
    /**
     * 获取poller线程
     */
    const EventPoller::Ptr& getPoller() const;

    /**
     * 设置批量发送标记,用于提升性能
     * @param try_flush 批量发送标记
     */
    void setSendFlushFlag(bool try_flush);

    /**
     * 设置socket发送flags
     * @param flags socket发送flags
     */
    void setSendFlags(int flags);

    /**
     * 套接字是否忙，如果套接字写缓存已满则返回true
     */
    bool isSocketBusy() const;

    /**
     * 从缓存池中获取一片缓存
     * @param data 需要拷贝的数据
     * @param len 需要拷贝的数据长度
     * @return 缓存
     */
    BufferRaw::Ptr obtainBuffer(const void *data = nullptr, int len = 0);

    /**
     * 设置Socket创建器，自定义Socket创建方式
     * @param cb 创建器
     */
    void setOnCreateSocket(Socket::onCreateSocket cb);

    /**
     * 创建socket对象
     */
    Socket::Ptr createSocket();

    ///////////////////// SockInfo override /////////////////////
    string get_local_ip() override;
    uint16_t get_local_port() override;
    string get_peer_ip() override;
    uint16_t get_peer_port() override;

    ///////////////////// TaskExecutorInterface override /////////////////////
    /**
     * 任务切换到所属poller线程执行
     * @param task 任务
     * @param may_sync 是否运行同步执行任务
     */
    Task::Ptr async(TaskIn task, bool may_sync = true) override;
    Task::Ptr async_first(TaskIn task, bool may_sync = true) override;

    ///////////////////// SockSender override /////////////////////
    /**
     * 统一发送数据的出口
     */
    int send(Buffer::Ptr buf) override;

    /**
     * 触发onErr事件
     */
    void shutdown(const SockException &ex = SockException(Err_shutdown, "self shutdown")) override;

protected:
    void setPoller(const EventPoller::Ptr &poller);
    void setSock(const Socket::Ptr &sock);
    const Socket::Ptr& getSock() const;

private:
    bool _try_flush = true;
    uint16_t _peer_port = 0;
    uint16_t _local_port = 0;
    string _peer_ip;
    string _local_ip;
    Socket::Ptr _sock;
    EventPoller::Ptr _poller;
    Socket::onCreateSocket _on_create_socket;
};

}  // namespace toolkit
#endif /* NETWORK_SOCKET_H */