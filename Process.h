#ifndef PROCESS_HHH
#define PROCESS_HHH

#include <boost/thread/mutex.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <list>
#include <map>
#include <string>

#include <boost/asio.hpp>

using namespace std;
using namespace boost;
using namespace boost::asio;

typedef boost::mutex CMutex;
//typedef boost::mutex::scoped_lock CMutex;


typedef struct 
{
    int iSize;
    int ID;
}__attribute__ ((packed, aligned(1))) HeadPack;

#define MAX_BUF_LEN   (1024*6)

//typedef boost::mutex::scoped_lock CAutoLock;


class CAutoLock
{
private:
    CMutex & m_Mut;
public:
    CAutoLock(CMutex & m) : m_Mut(m)
    {m_Mut.lock();}
    
    ~CAutoLock(){m_Mut.unlock();}
};

class CNetBuf
{
     typedef list<asio::streambuf*> BufList;
     typedef ip::tcp::socket CSocket; 
     typedef boost::shared_ptr<CSocket> CSocketPtr;  
     
 private:
    char m_szBufferRecv[MAX_BUF_LEN];
    int m_iRecv;
    BufList m_ListSend;
    BufList m_ListRecycle;
    CMutex m_MuxSend;
    unsigned long m_lSockID;
    bool m_bTransferState;
    size_t m_byteSend;
    
    CSocketPtr  m_sock;
   
    
 public:
     CNetBuf() :m_iRecv(0),m_bTransferState(false),m_byteSend(0)
     {
     }
     
     ~CNetBuf()
     {
         CAutoLock Lock(m_MuxSend);
         BufList::iterator ite = m_ListSend.begin();
         for (; ite!=m_ListSend.end(); ite++)
             delete (*ite);
         m_ListSend.clear();
     }
     
     void SetStreamByteSend(size_t byteSend)
     {
         CAutoLock Lock(m_MuxSend);
         m_byteSend = byteSend;
     };
     
     size_t GetStreamByteSend()
     {
         CAutoLock Lock(m_MuxSend);
         return m_byteSend;
     };
     
     void SetTransferState(bool b)
     {
         CAutoLock Lock(m_MuxSend);
         m_bTransferState = b;
     }
     
     bool GetTransferState()
     {
         CAutoLock Lock(m_MuxSend);
         return m_bTransferState;
     }
     
     void SetSock(CSocketPtr sock){m_sock = sock;}
     
     CSocketPtr &GetSock(){return m_sock;}
     
     void SetSockID(unsigned long lSockID)
     {
         m_lSockID = lSockID;
     }
     
     unsigned long GetSockID()
     {
         return m_lSockID;
     }
     
     void SetRecvBuf(int iSize)
     {
         CAutoLock Lock(m_MuxSend);
         m_iRecv += iSize;
     }
     
     char *GetRecvFreeBuf(int & iSize)
     {
         CAutoLock Lock(m_MuxSend);
         
         iSize = 0;
         char *pBuf = NULL;
         if (m_iRecv < MAX_BUF_LEN)
         {
             pBuf = &m_szBufferRecv[m_iRecv];
             iSize = MAX_BUF_LEN - m_iRecv;
         }
         
         return pBuf;
     }
     
     char *GetAllRecvBuf(int & iSize)
     {
         CAutoLock Lock(m_MuxSend);
         iSize = m_iRecv;
         return m_szBufferRecv;
     }
     
     void ClearRecvBuf(int iSize)
     {
         CAutoLock Lock(m_MuxSend);
         
         if (iSize < m_iRecv)
         {
             memmove(m_szBufferRecv, &m_szBufferRecv[iSize], m_iRecv-iSize);
             m_iRecv -= iSize;
         }
         else
             m_iRecv = 0;
     }
     
     void Recycle(asio::streambuf *pB)
     {
         CAutoLock Lock(m_MuxSend);
         pB->consume(pB->size());
         m_ListRecycle.push_back(pB);
     }
     
     long unsigned int SizeSendBuf()
     {
        CAutoLock Lock(m_MuxSend);
        return m_ListSend.size();
     }
     
     long unsigned int CacheSendBuf(char *pBuf, int len)
     {
        CAutoLock Lock(m_MuxSend);
         
        asio::streambuf *pB = NULL;
        
        if (m_ListRecycle.size() > 0)
        {
            BufList::iterator ite = m_ListRecycle.begin();
            pB = (*ite);
            m_ListRecycle.pop_front();
        }
        else
            pB = new asio::streambuf;
        
        std::ostream os(pB);
        os.write(pBuf, len);
        m_ListSend.push_back(pB);
        long unsigned int uSize = m_ListSend.size();
                
        return uSize;
     }
     
     asio::streambuf *GetSendBuf()
     {
         CAutoLock Lock(m_MuxSend);
         
         asio::streambuf *pB = NULL;
        BufList::iterator ite = m_ListSend.begin();
        if (ite != m_ListSend.end())
        {
            pB = (*ite);
            m_ListSend.pop_front();
        }
        
        return pB;
     }
     
     size_t GetSendBufCount()
     {
        CAutoLock Lock(m_MuxSend);
        return m_ListSend.size();
     }
} ;

class IClient
{
public:
    IClient(){}
    ~IClient(){}
    
public:
    virtual void AsynWrite(char *pBuf, int len) = 0;
    
    virtual void Quit() = 0;
    
    virtual unsigned long GetSockID(unsigned long &uLocalSockID) = 0;
};

class IServer
{
public:
    IServer(){}
    
    ~IServer(){}

public:
    virtual int SendToClient(unsigned long lSock, int index, char *pBuf, int iLen) = 0;
    
    virtual int SendToServer(unsigned long lSock, int index, char *pBuf, int iLen) = 0;
    
    virtual int ConnectServer(unsigned long lSock, unsigned long lLocalSockID, string &strIP, unsigned short uPort) = 0;
    
public: //For Client
    virtual int OnRecvServer(unsigned long lSock, unsigned long lLocalSockID, const char *pBuf, int iLen, int iType) = 0;
    
    virtual void SetCallBack(IClient *pClient) = 0;
    
public:
    virtual void SetTunnelCallBack(IClient *pClient) = 0;
    
    virtual int SendToTunnel(unsigned long lSock, int index, char *pBuf, int iLen) = 0;
    
    virtual int OnRecvTunnel(unsigned long lLocalSockID, const char *pBuf, int iLen, int iType) = 0;
};


class CProcess
{
private:
    IServer *m_pServer;
    
public:
    CProcess(IServer *pServer);
    
    ~CProcess();
    
    //处理客户端发来的消息
    int ProcessClient(unsigned long lSock, unsigned long lLocalSockID, char *pBuf, int iLen);
    
    //处理服务端发来的消息
    int ProcessServer(unsigned long lSock, unsigned long lLocalSockID, char *pBuf, int iLen);
    
    //Working on Client mode
public:
    //     local <===>SockClient<===Encryption===>SockServer
    //处理本地客户端发来的消息
    int ProcessLocal(unsigned long lSock, char *pBuf, int iLen);
    
    //处理Tunnel传来的消息
    int ProcessTunnel(unsigned long lLocalSockID, const char *pBuf, int iLen);
    
    
};


#endif