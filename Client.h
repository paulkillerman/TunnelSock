#ifndef CLIENT_HHH
#define CLIENT_HHH

# include <stdio.h>        
#include <iostream>
#include <cstdarg>
#include <list>
#include <map>
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

extern bool g_IsServer;

typedef ip::tcp::endpoint CEndPt;
typedef ip::tcp::socket CSocket;
//typedef boost::shared_ptr<CSocket> CSocketPtr;
    
class CClient : public IClient
{
//    typedef CClient this_type;  
    typedef ip::tcp::acceptor CAcceptor;  

    typedef ip::address CAddr;  
    
    typedef list<asio::streambuf*> BufList;
    
protected:
    io_service m_oIoSrv;
    
private:     
    CMutex m_MuxSend;
    CMutex m_MuxRecycle;
    BufList m_ListBuf;
    BufList m_ListFreeBuf;
    
    unsigned long m_lLocalSockID;//local

    bool    m_bSending;
    
    unsigned long m_lSockID;//server
    
protected:
    
//    CSocketPtr m_poSock;
    char *m_pBuffer;  
    CEndPt m_EndPt;
    CSocket m_Sock;
    ip::tcp::resolver m_Resolver;
    
    IServer *m_pServer;

private:
    void Handle_resolve(const boost::system::error_code& err, ip::tcp::resolver::iterator endpoint_iterator);
    
public:
    CClient();
    
    ~CClient();
    
    CClient(const char* lpszAddr, unsigned short uPort);

    CClient(const char* lpszURL, const char *lpszPort);

    void SetTag(unsigned long lSockID, unsigned long lLocalSockID=0);
    
    unsigned long GetTag();
    
    virtual unsigned long GetSockID(unsigned long &uLocalSockID);
    
    void SetSvr(IServer *pServer);
    
    void RecycleStream(asio::streambuf *pSB);
    
    asio::streambuf *GetStream();
    
    
    virtual void Run();

    virtual void Start();

    virtual void ConnHandler(const boost::system::error_code& errorcode);

    virtual void ReadHandler(const boost::system::error_code& errorcode, std::size_t bytes_transferred); 

    virtual void Quit();
    
    void AsynWrite(char *pBuf, int len);

    void WriteHandler(const boost::system::error_code& err, size_t bytes_transferred, asio::streambuf *pSB);
};  


class CTunnel : public CClient
{
private:
    size_t  m_offsetRead;
    
public:
    CTunnel(const char* lpszURL, const char *lpszPort);
    
private:
    void Handle_resolve(const boost::system::error_code& err, ip::tcp::resolver::iterator endpoint_iterator);
    
public:
    virtual void Run();
    
    virtual void Start();
    
    virtual void ConnHandler(const boost::system::error_code& errorcode);
    
    virtual void ReadHandler(const boost::system::error_code& errorcode, std::size_t bytes_transferred);
};




#endif