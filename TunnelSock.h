# include <stdio.h>        
#include <iostream>
#include <cstdarg>
#include <list>
#include <map>
#include <vector>
using namespace std;
  
#include <boost/shared_ptr.hpp>    
#include <boost/thread.hpp>    
#include <boost/thread/mutex.hpp>
#include <boost/lambda/lambda.hpp>  
#include <boost/asio.hpp>
#include <boost/asio/placeholders.hpp>    
#include <boost/system/error_code.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/bind/bind.hpp>    
using namespace boost;
using namespace boost::asio;

#include "Process.h"
#include "Client.h"
 

///////////////

/*可变参数列表函数  */
void ConsoleOutput(bool bClient, char* lpszFmt, ...);

// Connect Web server , Game server  or other
void ConnectFunc(string &strIP, unsigned short uPort, unsigned long lSock, unsigned long lLocalSockID, IServer *pServer);

// Connect tunnel
void ConnectTunnelFunc(string &strIP, unsigned short uPort, IServer *pServer);


void ConnectFunc(string &strIP, unsigned short uPort, unsigned long lSockID, unsigned long lLocalSockID, IServer *pServer);
   
void ConnectTunnelFunc(string &strIP, unsigned short uPort, IServer *pServer);

void ServerAccept(unsigned short uPort);

////////////////
class CServer : public IServer
{  
    typedef ip::tcp::acceptor CAcceptor;  
    typedef ip::tcp::endpoint CEndPt;  
    typedef ip::tcp::socket CSocket;  
    typedef ip::address CAddr;  
    typedef boost::shared_ptr<CSocket> CSocketPtr;  

//    typedef list<asio::streambuf*> BufList;
    
private:    
    io_service oIoSrv;  
    CAcceptor oAcceptor;
    
    CMutex m_MutexBuf;
    CMutex  m_MutexSession;
    CMutex m_MutexFreeBuf;
    
private:
    CProcess m_Process;
    
    map<unsigned long, CNetBuf*> m_MapNetBuf;
    map<unsigned long, IClient*> m_MapSession;
    list<CNetBuf*>               m_ListFreeNetBuf;
    
private:
    vector<IClient*>              m_VectorTunnel;

public:  
    CServer(int iPort);

    ~CServer();
    
    void Run();

    void Accept();

    void AcceptHandler(const boost::system::error_code& errorcode, /*CSocketPtr&*/CSocket* sock);

    int AsynWrite(unsigned long lSock, char *pBuf, int len);
    
    // 获取套接字对应的数据缓存
    CNetBuf *GetNetBuf(unsigned long lSock);
    
    // 获取IClient*
    IClient* GetServerPtr(unsigned long lSock);
    
    void RemoveServer(unsigned long lSock);
    
    IClient* GetTunnel(unsigned long lLocalSockID);
    
    void RemoveTunnel(IClient *pClient);
    
    void ClearSock(unsigned long lSock);
    
    void WriteHandler(const boost::system::error_code& errorcode, size_t bytes_transferred, unsigned long lSock, asio::streambuf *pSB);
	
    void ReadHandler(const boost::system::error_code& errorcode, std::size_t bytes_transferred, unsigned long lSock);
        
    virtual int SendToClient(unsigned long lSock, int index, char *pBuf, int iLen);
    
    virtual int SendToServer(unsigned long lSock, int index, char *pBuf, int iLen);
    
    virtual int SendToTunnel(unsigned long lSock, int index, char *pBuf, int iLen);
    
    virtual int ConnectServer( unsigned long lSock, unsigned long  lLocalSockID, string &strIP, unsigned short uPort);
    
    virtual int OnRecvServer(unsigned long lSockID, unsigned long lLocalSockID, const char *pBuf, int iLen, int iType);
    
    virtual int OnRecvTunnel(IClient *pClient, unsigned long lLocalSockID, const char *pBuf, int iLen, int iType);
    
    virtual void SetCallBack(IClient *pClient);
    
    virtual void SetTunnelCallBack(IClient *pClient);

private:
    void CreateTunnel();
};  


