#include <unistd.h>
#include <sys/resource.h>

#include "TunnelSock.h"
#include "Process.h"
#include "Client.h"
 

//////////////// For Client

bool g_IsServer = true;
char g_szTunnelIP[32] = {0};
unsigned short g_uTunnelPort = 8888;
unsigned short g_uServerlPort = 8888;

///////////////

CMutex muConsole;

/*可变参数列表函数  */
void ConsoleOutput(bool bClient, char* lpszFmt, ...)
{  
	/*FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE  
 * 	客户端线程输出：FOREGROUND_RED  
 * 		服务器线程输出：FOREGROUND_GREEN  */
	va_list pArgLst;    
 
	va_start(pArgLst, lpszFmt);    
	muConsole.lock();    

        struct timeval tv;
        struct timezone tz;
        gettimeofday (&tv , &tz);
        printf("%d-%d ",tv.tv_sec,tv.tv_usec);
        
	vfprintf(stdout, lpszFmt, pArgLst);    

	muConsole.unlock();    
	va_end(pArgLst);    
}  

// Connect Web server , Game server  or other
void ConnectFunc(string &strIP, unsigned short uPort, unsigned long lSock, unsigned long lLocalSockID, IServer *pServer);

// Connect tunnel
void ConnectTunnelFunc(string &strIP, unsigned short uPort, IServer *pServer);

  
void CServer::CreateTunnel()
{
    if (!g_IsServer)
    {
        string strIP = g_szTunnelIP;
        thread oClientThread(ConnectTunnelFunc, strIP, g_uTunnelPort, this);//Start tunnel thread
        oClientThread.detach();
    }
}

CServer::CServer(int iPort) : oAcceptor(oIoSrv, CEndPt(ip::tcp::v4(), iPort)),m_Process(this)
{
    Accept();
    
    CreateTunnel();
}

void CServer::Run()
{
    oIoSrv.run();
    ConsoleOutput(false, "Thread exit %d\n", getpid());
}

void CServer::Accept()  
{  
    CSocketPtr poSock(new CSocket(oIoSrv));  
    oAcceptor.async_accept(*poSock, boost::bind(&CServer::AcceptHandler, this, boost::asio::placeholders::error, poSock));  
}  

void CServer::AcceptHandler(const boost::system::error_code& errorcode, CSocketPtr& sock)  
{  
    if(errorcode)
    {
        ConsoleOutput(false, "Error AcceptHandler  code = %s\n", errorcode.message().c_str());
        return;
    }

    static unsigned long lSockID = 0;
    CNetBuf *pNetBuf = new CNetBuf;
    {
        CAutoLock lock(m_MutexBuf);
        lSockID++;
        pNetBuf->SetSockID(lSockID);
        pNetBuf->SetSock(sock);
        m_MapNetBuf[lSockID] = pNetBuf;
    }

    int iSize = 0;
    char *pRecvBuf = pNetBuf->GetRecvFreeBuf(iSize);
    sock->async_read_some(buffer(pRecvBuf, iSize), boost::bind(&CServer::ReadHandler, this, boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred, lSockID));  
    ConsoleOutput(false, "RemoteClient : %s\n", sock->remote_endpoint().address().to_string().c_str());

    Accept();/*发送完毕后继续监听，否则io_service将认为没有事件处理而结束运行  */
}

// 获取套接字对应的数据缓存
CNetBuf *CServer::GetNetBuf(unsigned long lSock)
{
    CNetBuf *pNetBuf = NULL;
    
    CAutoLock lock(m_MutexBuf);
    map<unsigned long, CNetBuf*>::iterator ite = m_MapNetBuf.find(lSock);
    if (ite != m_MapNetBuf.end())
        pNetBuf = (*ite).second;
        
    return pNetBuf;
}

IClient* CServer::GetTunnel(unsigned long lLocalSockID)
{
    IClient *pClient = NULL;
    
    CAutoLock Lock(m_MutexSession);
    list<IClient*>::iterator ite = m_ListTunnel.begin();
    if (ite != m_ListTunnel.end())
        pClient = (*ite);
    else
    {
        ConsoleOutput(true, "Error no tunnel!Create it.\n");
        CreateTunnel();
    }
    
    return pClient;
}
    
void CServer::RemoveTunnel()
{
    CAutoLock Lock(m_MutexSession);
    list<IClient*>::iterator iteTunnel = m_ListTunnel.begin();
    for (;iteTunnel!=m_ListTunnel.end(); iteTunnel++)
    {
        m_ListTunnel.erase(iteTunnel);
        break;
    }
}

IClient* CServer::GetServerPtr(unsigned long lSock)
{
    IClient* pClient = NULL;
    CAutoLock Lock(m_MutexSession);
        map<unsigned long, IClient*>::iterator ite = m_MapSession.find(lSock);
        if (ite != m_MapSession.end())
            pClient = ite->second;
        
        return pClient;
}

void CServer::RemoveServer(unsigned long lSock)
{
    CAutoLock Lock(m_MutexSession);
    ConsoleOutput(false, "1CServer::RemoveServer lSockID=%d server count=%d\n", lSock,m_MapSession.size());
    m_MapSession.erase(lSock);
}

void CServer::ClearSock(unsigned long lSock)
{
    CAutoLock lock(m_MutexBuf);

   map<unsigned long, CNetBuf*>::iterator ite = m_MapNetBuf.find(lSock);
   if (ite != m_MapNetBuf.end())
   {
       CNetBuf *pNetBuf = (*ite).second;
       delete pNetBuf;
       m_MapNetBuf.erase(ite);
   }       
}

int CServer::AsynWrite(unsigned long lSock, char *pBuf, int len)
{
    CNetBuf *pNetBuf = GetNetBuf(lSock);
    if (pNetBuf == NULL)
    {
        return -1;
    }
    
    if (g_IsServer)
    {
        HeadPack *pHead = (HeadPack*) pBuf;
        if (pHead->iSize > 20 && NULL==GetServerPtr(pHead->ID))//有可能是认证消息
        {
            ConsoleOutput(false, "CServer::AsynWrite ID=%d not exist \n", pHead->ID);
            return -1;
        }
    }
    
    pNetBuf->CacheSendBuf(pBuf, len);    

    if (!pNetBuf->GetTransferState())
    {
        pNetBuf->SetTransferState(true);
        asio::streambuf *pSB = pNetBuf->GetSendBuf();

        pNetBuf->SetStreamByteSend(pSB->size());
        CSocketPtr &Sock = pNetBuf->GetSock();
        Sock->async_write_some(buffer(pSB->data(), pSB->size()), bind(&CServer::WriteHandler, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred, lSock, pSB));
    }

    return len;
}

void CServer::WriteHandler(const boost::system::error_code& errorcode, size_t bytes_transferred, unsigned long lSock, asio::streambuf *pSB)
{
    if (errorcode)
    {
       ConsoleOutput(false, "Send client %d  %s\n", bytes_transferred, errorcode.message().c_str());
//       ClearSock(lSock);
       return;
    }
//        ConsoleOutput(false, "Send client %d complete\n", bytes_transferred);
    
    CNetBuf *pNetBuf = GetNetBuf(lSock);
    if (pNetBuf == NULL) return; //error

    CSocketPtr &Sock = pNetBuf->GetSock();
    size_t byteSend = pNetBuf->GetStreamByteSend();
    if (bytes_transferred < byteSend)
    {
        size_t byteLeft = byteSend - bytes_transferred;
        pNetBuf->SetStreamByteSend(byteLeft);

        Sock->async_write_some(buffer(pSB->data() + (pSB->size() - byteLeft), byteLeft), bind(&CServer::WriteHandler, this,
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred, lSock, pSB));

        ConsoleOutput(false, "Send client task=%d complete=%d \n", byteSend, bytes_transferred);
    }
    else
    {
        pNetBuf->Recycle(pSB);

//        asio::streambuf *pSBNext = pNetBuf->GetSendBuf();



        asio::streambuf *pSBNext = NULL;
		pSBNext = pNetBuf->GetSendBuf();


		if (g_IsServer)
		{
			do
			{
				if (pSBNext && pSBNext->size() >= sizeof(HeadPack))
				{
					HeadPack *pHead = (HeadPack*) asio::buffer_cast<const char*>(buffer(pSBNext->data(),sizeof(HeadPack) ));
			
			
					if (NULL == GetServerPtr(pHead->ID)) //�ͻ����Ѿ�������
					{
						ConsoleOutput(false, "ID=%d not exist \n", pHead->ID);
						pNetBuf->Recycle(pSBNext);
						pSBNext = pNetBuf->GetSendBuf();
						continue;
					}
					else
						break;
				}
				else
					break;

			}
			while (1);
		}

        

        if (pSBNext != NULL)
        {
            pNetBuf->SetStreamByteSend(pSBNext->size());
            Sock->async_write_some(buffer(pSBNext->data(), pSBNext->size()), bind(&CServer::WriteHandler, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred, lSock, pSBNext));   
        }
        else
        {
            pNetBuf->SetTransferState(false);
            pNetBuf->SetStreamByteSend(0);
        }
    }       
}

void CServer::ReadHandler(const boost::system::error_code& errorcode, std::size_t bytes_transferred, unsigned long lSock)
{  
    if(errorcode)
    {
        ConsoleOutput(true, "Server ReadHandler error %s\n", errorcode.message().c_str());
        if (!g_IsServer)
        {
            CNetBuf *pNetBuf = NULL;
            pNetBuf = GetNetBuf(lSock);
            if (NULL != pNetBuf)
                m_Process.ProcessLocal(pNetBuf->GetSockID(), "", 0);//无内容表示断开
        }
        
        ClearSock(lSock);
        return;
    }
    
    CNetBuf *pNetBuf = GetNetBuf(lSock);
    if (NULL == pNetBuf) return; //error

    CSocketPtr &Sock = pNetBuf->GetSock();
    pNetBuf->SetRecvBuf(bytes_transferred);

    if (g_IsServer)     //Working on server mode
    {
        do
        {
            int iRecvSize = 0;
            char *pAllBuf = pNetBuf->GetAllRecvBuf(iRecvSize);

            if (iRecvSize >= sizeof(HeadPack))
            {
                HeadPack *pHead = (HeadPack*)pAllBuf;
                if (iRecvSize >= pHead->iSize)
                {
                    ConsoleOutput(false, "CServer::ReadHandler iRecvSize=%d pHead->ID=%d pHead->iSize=%d prosize=%d\n", iRecvSize, pHead->ID,pHead->iSize,pHead->iSize-sizeof(HeadPack));

                    if (pHead->ID > 0)
                    {
                        if (pHead->iSize == sizeof(HeadPack))//无内容表示断开
                        {
                            RemoveServer(pHead->ID);
                        }
                        else
                            m_Process.ProcessClient(pNetBuf->GetSockID(), pHead->ID, pAllBuf+sizeof(HeadPack), pHead->iSize-sizeof(HeadPack));
                    }

                    pNetBuf->ClearRecvBuf(pHead->iSize);
                }
                else
                {
                    break;
                }
            }
            else
                break;
        }
        while (1);
    }
    else                //Working on client mode
    {
        int iRecvSize = 0;
        do
        {
            char *pAllBuf = pNetBuf->GetAllRecvBuf(iRecvSize);

            if (iRecvSize > 0)
            {
//                    ConsoleOutput(false, "m_Process.ProcessLocal SockID=%d len=%d %s\n", pNetBuf->GetSockID(), iRecvSize, pAllBuf);
                ConsoleOutput(false, "m_Process.ProcessLocal SockID=%d len=%d \n", pNetBuf->GetSockID(), iRecvSize);
                iRecvSize = iRecvSize>(MAX_BUF_LEN-sizeof(HeadPack)) ? (MAX_BUF_LEN-sizeof(HeadPack)):iRecvSize;
                
        //        if (10 == iRecvSize)
        //        for (int i=0; i<10; i++)
        //            ConsoleOutput(false, " %d \n", pAllBuf[i]);
                
                m_Process.ProcessLocal(pNetBuf->GetSockID(), pAllBuf, iRecvSize);
                pNetBuf->ClearRecvBuf(iRecvSize);//
            }
        }
        while(iRecvSize > 0);
    }

//        ConsoleOutput(true, "Recv from Client:size=%d\n %s\n", bytes_transferred, pAllBuf);
//        ConsoleOutput(true, "Recv from Client: size=%d\n",  bytes_transferred);

     int iSizeFree = 0;
     char *pBufFree = pNetBuf->GetRecvFreeBuf(iSizeFree);
     if (iSizeFree > 0)
     {
         Sock->async_read_some(buffer(pBufFree, iSizeFree), boost::bind(&CServer::ReadHandler, this,
             boost::asio::placeholders::error,
             boost::asio::placeholders::bytes_transferred, lSock));
     }

}

int CServer::SendToClient(unsigned long lSock, int index, char *pBuf, int iLen)
{
    return AsynWrite(lSock, pBuf, iLen);
}

int CServer::SendToServer(unsigned long lSock, int index, char *pBuf, int iLen)
{
    IClient *pClient = NULL;

    if (g_IsServer)
       pClient = GetServerPtr(index);
    else
        pClient = GetServerPtr(lSock);
    /*
    if (g_IsServer)
    {
        CAutoLock Lock(m_MutexSession);
        list<IClient*> & lClient = m_MapTunnel2Server[lSock];
        for (list<IClient*>::iterator ite = lClient.begin(); ite!=lClient.end(); ite++)
        {
            unsigned long uLocalSockID = 0;
            (*ite)->GetSockID(uLocalSockID);
            if (uLocalSockID == index)
            {
                pClient = (*ite);
                break;
            }
        }
    }
    else
    {
        CAutoLock Lock(m_MutexSession);
        map<unsigned long, IClient*>::iterator ite = m_MapSession.find(lSock);
        if (ite != m_MapSession.end())
            pClient = ite->second;
    }
*/
    if (pClient == NULL)    //error
        return -1;

    pClient->AsynWrite(pBuf, iLen);

    return 0;
}

int CServer::SendToTunnel(unsigned long lSock, int index, char *pBuf, int iLen)
{
    IClient *pClient = GetTunnel(lSock);
    if (NULL == pClient)
        return -1;

    pClient->AsynWrite(pBuf, iLen);
    return 0;
}

int CServer::ConnectServer( unsigned long lSock, unsigned long  lLocalSockID, string &strIP, unsigned short uPort)
{
    ConsoleOutput(true, "Server Host = %s : %d\n", strIP.c_str(), uPort);

    thread oClientThread(ConnectFunc, strIP, uPort, lSock, lLocalSockID, this);//启动客户端线程   
    oClientThread.detach();

    return 0;
}

int CServer::OnRecvServer(unsigned long lSockID, unsigned long lLocalSockID, const char *pBuf, int iLen, int iType)
{
    switch (iType)
    {
        case -2:
        {
            if (g_IsServer)
            {
                m_Process.ProcessServer(lSockID, lLocalSockID, "", 0);//无内容表示连接已断开
                RemoveServer(lLocalSockID);
            }
            
            return 0;
        }
        break;

        case -1:    //连接失败
        {
            char szBuf[16] = {0};
            szBuf[0] = 0x05;
            szBuf[1] = 0x01;//0x01 : general SOCKS server failure
            szBuf[2] = 0x00;//RSV : 保留字段，固定取值 0x00 
            szBuf[3] = 0x01;//0x01 : IPV4   0x03 : 域名 0x04 : IPV6
            szBuf[4] = 0x00;

            return m_Process.ProcessServer(lSockID, lLocalSockID, szBuf, 5);

    //        return SendToClient(lSockID, 0, szBuf, 5);
        }
            break;

        case 1:     //连接成功
        {
            char szBuf[10] = {0};
            memset(szBuf, 0, sizeof(szBuf));
            szBuf[0] = 0x05;
            szBuf[1] = 0x00;//0x00 : succeeded
            szBuf[2] = 0x00;//RSV : 保留字段，固定取值 0x00 
            szBuf[3] = 0x01;//0x01 : IPV4   0x03 : 域名 0x04 : IPV6

            return m_Process.ProcessServer(lSockID, lLocalSockID, szBuf, sizeof(szBuf));

     //       return SendToClient(lSockID, 0, szBuf, sizeof(szBuf));
        }
            break;

        case 0:     //数据
        {
            return m_Process.ProcessServer(lSockID, lLocalSockID, (char*)pBuf, iLen);

        //    return SendToClient(lSockID, 0, (char*)pBuf, iLen);
      //      ConsoleOutput(true, "WEB %s\n", pBuf);
        }
            break;
    }

    return 0;
}

int CServer::OnRecvTunnel(unsigned long lLocalSockID, const char *pBuf, int iLen, int iType)
{
    switch (iType)
    {
        case -2:
        {
           RemoveTunnel();
            ConsoleOutput(true, "Tunnel closed!\n");

   //             thread oServerThread(ServerAccept, g_uServerlPort);//启动服务器线程
   //             oServerThread.detach();
        }
        break;

        case -1:
        {
           RemoveTunnel();
            ConsoleOutput(true, "Construct tunnel failed!\n");

    //        thread oServerThread(ServerAccept, g_uServerlPort);//启动服务器线程
    //        oServerThread.detach();
        }
        break;

        case 1:
        {
            ConsoleOutput(true, "Construct tunnel %s:%d successfully!\n",pBuf, iLen);
        }
        break;

        case 0:
        {
            if (iLen > 0)
            {
                int iRet = m_Process.ProcessTunnel(lLocalSockID, pBuf, iLen); 
//                ConsoleOutput(false, "CServer::OnRecvTunnel lLocalSockID=%d iRet=%d len=%d %s\n", lLocalSockID, iRet, iLen, pBuf);
                ConsoleOutput(false, "CServer::OnRecvTunnel lLocalSockID=%d iRet=%d len=%d\n", lLocalSockID, iRet, iLen);
                return iRet;
            }
            else
            {
                ConsoleOutput(false, "CServer::OnRecvTunnel remote server closed lLocalSockID=%d \n", lLocalSockID);
                
                CNetBuf *pNetBuf = GetNetBuf(lLocalSockID);
                if (NULL == pNetBuf)
                    return 0;
                
                CSocketPtr Sock = pNetBuf->GetSock();
                if (Sock->is_open())
                {
                    boost::system::error_code ec;
                    Sock->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
                    Sock->close();
                }
            }
        }
        break;
    }

    return 0;
}

void CServer::SetCallBack(IClient *pClient)
{
    CAutoLock Lock(m_MutexSession);
    unsigned long lLocalSock = 0;//当作为远程服务端 lLocalSock为原地客户端的ID
    unsigned long lSock = pClient->GetSockID(lLocalSock);

    if (g_IsServer)
        m_MapSession[lLocalSock] = pClient;
    else
        m_MapSession[lSock] = pClient;
}

void CServer::SetTunnelCallBack(IClient *pClient)
{
    CAutoLock Lock(m_MutexSession);
    m_ListTunnel.push_back(pClient);
}


void ConnectFunc(string &strIP, unsigned short uPort, unsigned long lSockID, unsigned long lLocalSockID, IServer *pServer)
{
    char szPort[8] = {0};
    sprintf(szPort, "%d", uPort);
    
    CClient oClient(strIP.c_str(), szPort);
    oClient.SetTag(lSockID, lLocalSockID);
    oClient.SetSvr(pServer);
    
    oClient.Run();
}
   
void ConnectTunnelFunc(string &strIP, unsigned short uPort, IServer *pServer)
{
    char szPort[8] = {0};
    sprintf(szPort, "%d", uPort);
    CTunnel oTunnel(strIP.c_str(), szPort);
    oTunnel.SetSvr(pServer);
    
    oTunnel.Run();
    ConsoleOutput(true, "Tunnel thread closed!\n" );
}

void ServerAccept(unsigned short uPort)
{
    CServer oServer(uPort);
    oServer.Run();
}

//后台执行
void InitDaemon();
 
int  main(int argc, char* argv[])
{
    g_uServerlPort = 8888;
    
    if (argc > 3)
    {
        if (argv[1][0] == 'c' || argv[1][0] == 'C')
        {
            printf("Working on client mode\n");
            g_IsServer = false;
        }
        
        if (!g_IsServer)
        {
            strcpy(g_szTunnelIP, argv[2]);
            g_uTunnelPort = atoi(argv[3]);
            g_uServerlPort = atoi(argv[4]);
            printf("Tunnel %s:%s\n", argv[2],argv[3]);
            printf("Local port=%s\n",argv[4]);
        }
    }
    else if (argc == 2)
    {
        g_uServerlPort = atoi(argv[1]);
//        InitDaemon();
    }

//      {
//        g_IsServer = false;
//        strcpy(g_szTunnelIP, "127.0.0.1");
//        g_uTunnelPort = 8888;
//        g_uServerlPort = 1080;
//    }
    
    thread oServerThread(ServerAccept, g_uServerlPort);//启动服务器线程
    oServerThread.join();
//    oServerThread.detach();
   
    getchar();
    
    return 0 ;
}  

static void exit_handler(int sig)
{
	printf("catch signal: %d", sig);
	exit(0);
}

//后台执行
void InitDaemon() 
{
	rlimit rlim,rlim_new;
	if (getrlimit(RLIMIT_NOFILE, &rlim)==0)
	{
		rlim_new.rlim_cur = rlim_new.rlim_max = 100000;
		if (setrlimit(RLIMIT_NOFILE, &rlim_new)!=0)
		{
			ConsoleOutput(true,"%s","[Main]:Setrlimit file Fail, use old rlimit!\n");
			rlim_new.rlim_cur = rlim_new.rlim_max = rlim.rlim_max;
			setrlimit(RLIMIT_NOFILE, &rlim_new);
		}
	}
	
	if (getrlimit(RLIMIT_CORE, &rlim)==0)
	{
		rlim_new.rlim_cur = rlim_new.rlim_max = RLIM_INFINITY;
		if (setrlimit(RLIMIT_CORE, &rlim_new)!=0)
		{
			ConsoleOutput(true,"%s","[Main]:Setrlimit core Fail, use old rlimit!\n");
			rlim_new.rlim_cur = rlim_new.rlim_max = rlim.rlim_max;
			setrlimit(RLIMIT_CORE, &rlim_new);
		}
	}

	pid_t pid;

    if ((pid = fork() ) != 0 ) 
    {   
        exit( 0); 
    }   

    setsid();

    struct sigaction sig;
    memset(&sig, 0, sizeof(sig));
    sig.sa_handler = SIG_IGN;
    sig.sa_flags = 0;
    sigemptyset( &sig.sa_mask);

    sigaction(SIGPIPE, &sig, NULL);
    //sigaction(SIGINT,  &sig, NULL);
    sigaction(SIGHUP,  &sig, NULL);
    sigaction(SIGQUIT, &sig, NULL);
    sigaction(SIGPIPE, &sig, NULL);
    sigaction(SIGTTOU, &sig, NULL);
    sigaction(SIGTTIN, &sig, NULL);
    sigaction(SIGCHLD, &sig, NULL);
    sigaction(SIGTERM, &sig, NULL);
    sigaction(SIGHUP,  &sig, NULL);

    if ((pid = fork() ) != 0 ) 
    {   
        exit(0);
    }   

    umask(0);
    setpgrp();

    sig.sa_handler = exit_handler;

    sigaction(SIGINT,  &sig, NULL);
}
