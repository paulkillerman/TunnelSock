#include "Client.h"

extern void ConsoleOutput(bool bClient, char* lpszFmt, ...);

CClient::CClient():m_Sock(m_oIoSrv),m_Resolver(m_oIoSrv)
{
    m_lSockID = 0;
    m_pServer = NULL;
    m_bSending = false;
    m_pBuffer = new char[MAX_BUF_LEN];
}

CClient::CClient(const char* lpszAddr, unsigned short uPort) : m_EndPt(CAddr::from_string(lpszAddr), uPort),m_Sock(m_oIoSrv),m_Resolver(m_oIoSrv)
{
    m_lSockID = 0;
    m_pServer = NULL;
    m_bSending = false;
    m_pBuffer = new char[MAX_BUF_LEN];
    Start();
}  

CClient::CClient(const char* lpszURL, const char *lpszPort):m_Sock(m_oIoSrv),m_Resolver(m_oIoSrv)
{
    m_lSockID = 0;
    m_pServer = NULL;
    m_bSending = false;
    m_pBuffer = new char[MAX_BUF_LEN];

//    io_service ios;
    //创建resolver对象
//    ip::tcp::resolver slv(m_oIoSrv);
    //创建query对象
    ip::tcp::resolver::query qry(lpszURL, lpszPort);
    //使用resolve迭代端点
//    ip::tcp::resolver::iterator ite = slv.resolve(qry);
//    ip::tcp::resolver::iterator end;
//
//    if (ite != end)
//    {
//        m_EndPt = (*ite).endpoint();
//        Start();
//    }
    
    m_Resolver.async_resolve(qry, boost::bind(&CClient::Handle_resolve, this,
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::iterator));
}

CClient::~CClient()
{
    delete []m_pBuffer;
}

void CClient::Handle_resolve(const boost::system::error_code& err, ip::tcp::resolver::iterator endpoint_iterator)
{
    if (!err)
    {
        m_EndPt = (*endpoint_iterator).endpoint();
        Start();
    }
    else
    {
        ConsoleOutput(false, "CClient::Handle_resolve erro %s \n", err.message().c_str());
//        std::cout << __FUNCTION__ << " Error: " << err.message() << "\n";
    }
}

void CClient::SetTag(unsigned long lSockID, unsigned long lLocalSockID)
{
    m_lSockID = lSockID;
    m_lLocalSockID = lLocalSockID;
}

unsigned long CClient::GetTag(){return m_lSockID;}

unsigned long CClient::GetSockID(unsigned long &uLocalSockID)
{
    uLocalSockID = m_lLocalSockID;
    return m_lSockID;
}

void CClient::SetSvr(IServer *pServer)
{
    m_pServer = pServer;
}

void CClient::RecycleStream(asio::streambuf *pSB)
{
    CAutoLock Lock(m_MuxRecycle);
    pSB->consume(pSB->size());
    m_ListFreeBuf.push_back(pSB);   
}

asio::streambuf *CClient::GetStream()
{
    CAutoLock Lock(m_MuxRecycle);
    BufList::iterator  ite = m_ListFreeBuf.begin();
    if (ite!=m_ListFreeBuf.end())
        return (*ite);

    return NULL;
}


void CClient::Run()
{
    m_pServer->SetCallBack(this);
    m_oIoSrv.run();
//    m_pServer->OnRecvServer(m_lSockID, m_lLocalSockID, NULL, 0, -2);
}

void CClient::Start()
{
//    CSocketPtr poSock(new CSocket(oIoSrv));
//    m_poSock = poSock;
    
    m_Sock.async_connect(m_EndPt, boost::bind(&CClient::ConnHandler, this, boost::asio::placeholders::error));
}  

void CClient::ConnHandler(const boost::system::error_code& errorcode)
{  
    if(errorcode)
    {
        m_pServer->OnRecvServer(m_lSockID, m_lLocalSockID, NULL, 0, -1);
        return;
    }

    int iRet = m_pServer->OnRecvServer(m_lSockID, m_lLocalSockID, m_Sock.remote_endpoint().address().to_string().c_str(),
            m_Sock.remote_endpoint().port(), 1);
    if (iRet == -1)
    {
        m_Sock.close();
        return;
    }

//        ConsoleOutput(true, "Recv From %s : %d\n", sock->remote_endpoint().address().to_string().c_str(), sock->remote_endpoint().port());
    m_Sock.async_read_some(buffer(m_pBuffer, MAX_BUF_LEN-sizeof(HeadPack)), boost::bind(&CClient::ReadHandler, this, boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
}  

void CClient::ReadHandler(const boost::system::error_code& errorcode, std::size_t bytes_transferred)
{  
    if(errorcode)
    {
        m_pServer->OnRecvServer(m_lSockID, m_lLocalSockID, NULL, 0, -2);//
        return;
    }
//        ConsoleOutput(true, "Recv from Server: size=%d %s\n", bytes_transferred, szBuffer);
//    ConsoleOutput(true, "Recv from Server: size=%d \n", bytes_transferred);

    int iRet = m_pServer->OnRecvServer(m_lSockID, m_lLocalSockID, m_pBuffer, bytes_transferred, 0);
    if (iRet == -1)
    {
        m_Sock.close();
        return;
    }
    m_Sock.async_read_some(buffer(m_pBuffer,MAX_BUF_LEN-sizeof(HeadPack)), boost::bind(&CClient::ReadHandler, this, boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred));        
}  

void CClient::Quit()
{
    if (m_Sock.is_open())
    {
        boost::system::error_code ec;
        m_Sock.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        m_Sock.close();
    }
}

void CClient::AsynWrite(char *pBuf, int len)
{
    asio::streambuf *pSB = GetStream();
    if (NULL == pSB)
        pSB = new asio::streambuf;
    std::ostream os(pSB);
    os.write(pBuf, len);

    CAutoLock Lock(m_MuxSend);
    m_ListBuf.push_back(pSB);

    if (!m_bSending)
    {
        asio::streambuf *pSBSend = NULL;
        {
            BufList::iterator ite = m_ListBuf.begin();
            pSBSend = *ite;
            m_ListBuf.pop_front();
        }

        m_Sock.async_write_some(buffer(pSBSend->data(), pSBSend->size()), bind(&CClient::WriteHandler, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred, pSBSend));

        m_bSending = true;
    }
}

void CClient::WriteHandler(const boost::system::error_code& err, size_t bytes_transferred, asio::streambuf *pSB)
{
    if(err)
    {
//        m_pServer->OnRecvServer(m_lSockID, m_lLocalSockID, NULL, 0, -2);
        return;
    }

    CAutoLock Lock(m_MuxSend);
    
    if (bytes_transferred < pSB->size())
    {
        pSB->consume(bytes_transferred);
        m_Sock.async_write_some(buffer(pSB->data(), pSB->size()), bind(&CClient::WriteHandler, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred, pSB));
    }
    else
    {
        RecycleStream(pSB);

        if (m_ListBuf.size() > 0)
        {
            BufList::iterator ite = m_ListBuf.begin();
//            ConsoleOutput(true, "HandleTcpSend size=%d len=%d %s\n", m_ListBuf.size(), (*ite)->size(), (*ite)->data());

            m_Sock.async_write_some(buffer((*ite)->data(), (*ite)->size()), bind(&CClient::WriteHandler, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred, (*ite)));

            m_ListBuf.pop_front();
        }
        else
            m_bSending = false;
    }
}



///////////////////////////////////////////CTunnel
CTunnel::CTunnel(const char* lpszURL, const char *lpszPort):CClient()
{
    delete []m_pBuffer;
    m_pBuffer = new char[MAX_BUF_LEN*2];
     ip::tcp::resolver::query qry(lpszURL, lpszPort);
    
    m_Resolver.async_resolve(qry, boost::bind(&CTunnel::Handle_resolve, this,
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::iterator));
}

void CTunnel::Handle_resolve(const boost::system::error_code& err, ip::tcp::resolver::iterator endpoint_iterator)
{
    if (!err)
    {
        m_EndPt = (*endpoint_iterator).endpoint();
        Start();
    }
    else
    {
        ConsoleOutput(false, "CClient::Handle_resolve erro %s \n", err.message().c_str());
//        std::cout << __FUNCTION__ << " Error: " << err.message() << "\n";
    }
}

void CTunnel::Run()
{
    m_pServer->SetTunnelCallBack(this);
    m_oIoSrv.run();
    m_pServer->OnRecvTunnel(this, 0, NULL, 0, -2);
}

void CTunnel::Start()
{
//    CSocketPtr poSock(new CSocket(oIoSrv));
//    m_poSock = poSock;
    m_Sock.async_connect(m_EndPt, boost::bind(&CTunnel::ConnHandler, this, boost::asio::placeholders::error));
}

void CTunnel::ConnHandler(const boost::system::error_code& errorcode)
{
    if(errorcode)
    {
        m_pServer->OnRecvTunnel(this, 0, NULL, 0, -1);
        return;
    }

    m_pServer->OnRecvTunnel(this, 0, m_Sock.remote_endpoint().address().to_string().c_str(), 
            m_Sock.remote_endpoint().port(), 1);

    m_offsetRead = 0;
//        ConsoleOutput(true, "Recv From %s : %d\n", sock->remote_endpoint().address().to_string().c_str(), sock->remote_endpoint().port());
    m_Sock.async_read_some(buffer(m_pBuffer, MAX_BUF_LEN*2), boost::bind(&CTunnel::ReadHandler, this, boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
}

void CTunnel::ReadHandler(const boost::system::error_code& errorcode, std::size_t bytes_transferred)
{
    if(errorcode)
    {
        m_pServer->OnRecvTunnel(this, 0, NULL, 0, -2);
        return;
    }

    size_t indexRead = 0;
    m_offsetRead += bytes_transferred;

    while ((m_offsetRead-indexRead) >= sizeof(HeadPack))
    {
        HeadPack *pHead = (HeadPack*)(&m_pBuffer[indexRead]);
//            ConsoleOutput(true, "CTunnel::ReadHandler sockID=%d m_offsetRead = %d bytes_transferred=%d\n", pHead->ID, m_offsetRead, bytes_transferred);
        if ((m_offsetRead-indexRead) >= pHead->iSize)
        {
//            if (pHead->ID >= 0)
                m_pServer->OnRecvTunnel(this, pHead->ID, &m_pBuffer[indexRead]+sizeof(HeadPack), pHead->iSize - sizeof(HeadPack), 0);
            indexRead += pHead->iSize;
//            ConsoleOutput(true, "CTunnel::ReadHandler sockID=%d m_offsetRead = %d bytes_transferred=%d indexRead=%d\n", pHead->ID, m_offsetRead, bytes_transferred,indexRead);
        }
        else
        {
//            ConsoleOutput(true, "CTunnel::ReadHandler sockID=%d m_offsetRead=%d indexRead=%d pHead->iSize=%d bytes_transferred=%d \n", pHead->ID, m_offsetRead,indexRead,pHead->iSize, bytes_transferred);
            break;
        }
    }

    if (indexRead > 0)
    {
        if (m_offsetRead > indexRead)
                m_offsetRead -= indexRead;
        else
                m_offsetRead = 0;
    }

    memmove(m_pBuffer, &m_pBuffer[indexRead], m_offsetRead);

    m_Sock.async_read_some(buffer(m_pBuffer+m_offsetRead,MAX_BUF_LEN*2-m_offsetRead), boost::bind(&CClient::ReadHandler, this, boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred));
}