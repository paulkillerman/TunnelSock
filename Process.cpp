#include "Process.h"

#include <boost/pool/pool.hpp>

extern void ConsoleOutput(bool bClient, char* lpszFmt, ...);
extern bool g_IsServer;

unsigned short key = 5;

void Encrypt(unsigned char *pBuf, int iLen)
{
    for (int i=0; i< iLen; i++)
    {
        unsigned char temp = pBuf[i]&0x01;
        pBuf[i] = pBuf[i]>>1;
        pBuf[i] = pBuf[i] | (temp<<7);
    }
}

void DeEncrypt(unsigned char *pBuf, int iLen)
{
     for (int i=0; i< iLen; i++)
    {
        unsigned char temp = pBuf[i]&(0x01<<7);
        pBuf[i] = pBuf[i]<<1;
        pBuf[i] = pBuf[i] | (temp>>7);
    }
}

CProcess::CProcess(IServer *pServer)
{
    m_pServer = pServer;

//    for (unsigned char i=0; i<255; i++)
//    {
//        unsigned char t = i;
//        Encrypt((unsigned char*)&t, 1);
//        unsigned char temp = t;
//        DeEncrypt((unsigned char*)&temp, 1);
//        if (i!=temp)
//        {
//            ConsoleOutput(false, "CProcess error %c %c \n", i, temp);
//        }
//    }

}

CProcess::~CProcess()
{

}


//     As SockServer
//处理客户端发来的消息
//解密
int CProcess::ProcessClient(unsigned long lSock, unsigned long lLocalSockID, char *pBuf, int iLen)
{
    DeEncrypt((unsigned char*)pBuf, iLen);
    
    if (iLen == 3 && pBuf[0] == 0x05)
    {
        if (pBuf[0] == 0x05 &&//sock5
            pBuf[1] == 0x01) //
        {
            if (lLocalSockID > 0)
            {
                char szBuf[sizeof(HeadPack)+2] = {0};
                HeadPack *pHead = (HeadPack*)szBuf;
                pHead->ID = lLocalSockID;
                pHead->iSize = sizeof(szBuf);
                
                szBuf[sizeof(HeadPack)] = 0x05;
                szBuf[sizeof(HeadPack)+1] = 0x00; //0x00 : NO AUTHENTICATION REQUIRED 

                Encrypt((unsigned char*)&szBuf[sizeof(HeadPack)], 2);
                
                m_pServer->SendToClient(lSock, lLocalSockID, szBuf, sizeof(szBuf));
            }
            else
            {
                char szBuf[2] = {0};
                szBuf[0] = 0x05;
                szBuf[1] = 0x00; //0x00 : NO AUTHENTICATION REQUIRED 

                m_pServer->SendToClient(lSock, lLocalSockID, szBuf, sizeof(szBuf));
            }
            
            return iLen;
        }

    }
    else if (iLen > 4 && pBuf[0] == 0x05)
    {
        if (pBuf[0] == 0x05) //sock5
        {
            if (pBuf[1] == 0x01)// 0x01 : CONNECT
            {
                if (pBuf[3] == 0x01 || pBuf[3] == 0x03)//  0x01 : IPV4 0x03 : 域名  0x04 : IPV6
                {
                    int nLen = pBuf[4];
                    char szHostName[264] = {0};
                    memcpy(szHostName, &pBuf[5], nLen);
                    szHostName[nLen] = '\0';
                    string strHostName = szHostName;

                    unsigned short uPort = ntohs(*((unsigned short*)(&pBuf[5] + nLen)));
                    m_pServer->ConnectServer(lSock, lLocalSockID, strHostName, uPort);
                    return iLen;
                }
            }
        }
    }
    else
    {
//        Encrypt((unsigned char*)pBuf, iLen);
        m_pServer->SendToServer(lSock, lLocalSockID, pBuf, iLen);
        return iLen;
    }
    
    return 0;
}

//处理服务端发来的消息
//加密
int CProcess::ProcessServer(unsigned long lSock, unsigned long lLocalSockID, char *pBuf, int iLen)
{
    Encrypt((unsigned char*)pBuf, iLen);
    
 //   if (lLocalSockID == 0)
 //       return m_pServer->SendToClient(lSock, lLocalSockID, pBuf, iLen);
 //   else
    {
        pool<> pl(iLen+sizeof(HeadPack)); 
        char *pB = (char*)pl.malloc();
        
      //  char szBuf[MAX_BUF_LEN] = {0};
        HeadPack *pHead = (HeadPack*)pB;
        pHead->ID = lLocalSockID;
        pHead->iSize = sizeof(HeadPack) + iLen;
        memcpy(pB+sizeof(HeadPack), pBuf, iLen);
        return m_pServer->SendToClient(lSock, lLocalSockID, pB, pHead->iSize);
    }
    return 0;
}

//     As SockClient
//     local <===>SockClient<===Encryption===>SockServer
//处理本地客户端发来的消息
//加密
int CProcess::ProcessLocal(unsigned long lSock, char *pBuf, int iLen)
{
    Encrypt((unsigned char*)pBuf, iLen);
    
  //  char szBuf[MAX_BUF_LEN] = {0};
    pool<> pl(iLen+sizeof(HeadPack)); 
    char *pB = (char*)pl.malloc();
    
    HeadPack *pHead = (HeadPack*)pB;
    pHead->ID = lSock;
    pHead->iSize = sizeof(HeadPack) + iLen;
    memcpy(pB+sizeof(HeadPack), pBuf, iLen);
    
    return m_pServer->SendToTunnel(lSock, 0, pB, pHead->iSize);
}

//处理Tunnel传来的消息
//解密
int CProcess::ProcessTunnel(unsigned long lLocalSockID,const char *pBuf, int iLen)
{
    DeEncrypt((unsigned char*)pBuf, iLen);
/*    if (2==iLen && pBuf[0]==0x05)
    {
        ConsoleOutput(false, "CProcess::ProcessTunnel no password %d %d \n", pBuf[0], pBuf[1]);
    }
    else if (10==iLen && pBuf[0]==0x05)
    {
        ConsoleOutput(false, "CProcess::ProcessTunnel connect ok %d %d \n", pBuf[0], pBuf[1]);
    }
    else
        ConsoleOutput(false, "CProcess::ProcessTunnel recv svr data %d \n", iLen);
*/  

    return m_pServer->SendToClient(lLocalSockID, 0, (char*)pBuf, iLen);
    return 0;
}
