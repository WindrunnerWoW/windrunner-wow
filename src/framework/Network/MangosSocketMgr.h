#ifndef MANGOSSOCKETMGR_H
#define MANGOSSOCKETMGR_H

#include <ace/Basic_Types.h>
#include <string>

template <typename T>
class MangosSocketAcceptor;
template <typename T>
class ReactorRunnable;
class ACE_Event_Handler;

/// Manages all sockets connected to peers and network threads
template <typename SocketType>
class MangosSocketMgr
{
    public:

        /// Start network, listen at address:port .
        int StartNetwork(ACE_UINT16 port, std::string& address);

        /// Stops all network threads, It will wait for all running threads .
        void StopNetwork();

        /// Wait untill all network threads have "joined" .
        void Wait();

        void SetOutKBuff(int v) { m_SockOutKBuff = v; }
        void SetOutUBuff(int v) { m_SockOutUBuff = v; }
        void SetThreads(int v) { m_NetThreadsCount = v; }
        void SetTcpNodelay(bool v) { m_UseNoDelay = v; }
        void SetInterval(int v) { m_Interval = v * 1000; /* to microseconds */ }

        int Connect(int port, std::string const& address, SocketType*& sock);
    protected:
        int OnSocketOpen(SocketType* sock);
        int StartReactiveIO(ACE_UINT16 port, const char* address);
        int StartThreadsIfNeeded();

        MangosSocketMgr();
        // Explicitly noexcept. ACE_Singleton<WorldSocketMgr, ACE_Thread_Mutex>
        // holds a WorldSocketMgr by value and derives from ACE_Cleanup, whose
        // destructor is virtual and therefore implicitly noexcept. Every member
        // here destructs without throwing, so gcc deduces noexcept and the
        // override is fine - MSVC does not, and rejects the generated
        // ~ACE_Singleton with C2694 for having a weaker exception specification
        // than the base. Saying it out loud costs nothing and builds everywhere.
        ~MangosSocketMgr() noexcept;

        ReactorRunnable<SocketType>* m_NetThreads;
        size_t m_NetThreadsCount;

        int m_SockOutKBuff;
        int m_SockOutUBuff;
        bool m_UseNoDelay;
        int m_Interval;

        std::string m_addr;
        ACE_UINT16 m_port;

        MangosSocketAcceptor<SocketType>* m_Acceptor;
};

#endif // MANGOSSOCKETMGR_H
