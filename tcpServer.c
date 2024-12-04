/**
 * @file tcpServer.c
 * @brief 이 프로그램은 TCP 서버 소켓을 생성하여 여러 클라이언트의 접속을 처리하고 데이터를 송수신하는 서버 프로그램입니다.
 * 
 * @details 수신 및 송신 스레드를 통해 각 클라이언트와 동시에 통신을 수행하며, 클라이언트의 접속 및 연결 해제를 관리합니다.
 * 
 * @author 박철우
 * @date 2015.05
 */
#include "tcpSock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/select.h>
#include <errno.h>  // errno 사용을 위해 추가

#define PORT 8080

/**
 * @brief 클라이언트 정보를 저장하는 구조체.
 * @details 클라이언트 소켓, 송수신 버퍼, 수신 및 송신 스레드 ID를 포함.
 */
typedef struct {
    int iClientSock;                ///< 클라이언트 소켓 파일 디스크립터
    char achBuffer[BUFFER_SIZE];    ///< 클라이언트와의 데이터 송수신을 위한 버퍼
    pthread_t recvThreadId;         ///< 데이터 수신 스레드 ID
    pthread_t sendThreadId;         ///< 데이터 송신 스레드 ID
} CLIENT_INFO;

/**
 * @brief 클라이언트로부터 데이터를 수신하는 스레드 함수입니다.
 * 
 * @details 클라이언트의 연결이 종료되거나 오류가 발생할 경우 스레드를 종료합니다.
 * 
 * @param arg 클라이언트 정보를 담고 있는 CLIENT_INFO 구조체 포인터입니다.
 * @return void* 반환값이 없습니다.
 */

void *receiveThread(void *arg) {
    CLIENT_INFO *pstClientInfo = (CLIENT_INFO *)arg;
    struct sockaddr_in stSockClientAddr;
    socklen_t uiClientAddrLen = sizeof(stSockClientAddr);

    if (getpeername(pstClientInfo->iClientSock, (struct sockaddr *)&stSockClientAddr, &uiClientAddrLen) == -1) {
        perror("getpeername failed");
        pthread_exit(NULL);
    }

    while (1) {
        int iReadSize = read(pstClientInfo->iClientSock, pstClientInfo->achBuffer, BUFFER_SIZE);
        if (iReadSize <= 0) {
            if (iReadSize == 0) {
                getpeername(pstClientInfo->iClientSock, (struct sockaddr *)&stSockClientAddr, &uiClientAddrLen);
                printf("Host disconnected, socket ip is : %s, port : %d\n", \
                        inet_ntoa(stSockClientAddr.sin_addr), \
                        ntohs(stSockClientAddr.sin_port));
            } else {
                perror("Read error");
            }
            handleClientDisconnection(pstClientInfo->iClientSock);
            pstClientInfo->iClientSock = 0;
            pthread_exit(NULL);
        } else {
            pstClientInfo->achBuffer[iReadSize] = '\0';
            printf("Received from client %d: %s\n", pstClientInfo->iClientSock, pstClientInfo->achBuffer);
        }
    }
}

/**
 * @brief 클라이언트로 데이터를 송신하는 스레드 함수입니다.
 * 
 * @details 버퍼에 데이터가 있을 경우 해당 데이터를 클라이언트로 송신하고, 송신 후 버퍼를 초기화합니다.
 * 
 * @param arg 클라이언트 정보를 담고 있는 CLIENT_INFO 구조체 포인터입니다.
 * @return void* 반환값이 없습니다.
 */
void *sendThread(void *arg) {
    CLIENT_INFO *pstClientInfo = (CLIENT_INFO *)arg;
    while (1) {
        if (strlen(pstClientInfo->achBuffer) > 0) {
            write(pstClientInfo->iClientSock, pstClientInfo->achBuffer, strlen(pstClientInfo->achBuffer));
            printf("Sent to client %d: %s\n", pstClientInfo->iClientSock, pstClientInfo->achBuffer);
            memset(pstClientInfo->achBuffer, 0, BUFFER_SIZE); // 버퍼를 초기화하여 다음 데이터를 받을 준비를 합니다.
        }
        usleep(1000); // 송신 스레드의 과부하를 방지하기 위해 잠시 대기합니다.
    }
}

int main() {
    int iServerSock, iClientSock;
    struct sockaddr_in stSockClientAddr;
    socklen_t uiClientAddrLen = sizeof(stSockClientAddr);
    
    fd_set stReadFds;
    CLIENT_INFO stClientInfo[MAX_CLIENTS] = {0};

    iServerSock = createServerSocket(PORT, MAX_CLIENTS);
    printf("Server listening on port %d\n", PORT);

    while (1) {
        FD_ZERO(&stReadFds);
        FD_SET(iServerSock, &stReadFds);
        int iMaxSock = iServerSock;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int iSock = stClientInfo[i].iClientSock;
            if (iSock > 0)
                FD_SET(iSock, &stReadFds);
            if (iSock > iMaxSock)
                iMaxSock = iSock;
        }

        int iActivitySock = select(iMaxSock + 1, &stReadFds, NULL, NULL, NULL);
        if ((iActivitySock < 0) && (errno != EINTR)) {
            perror("Select error");
        }

        if (FD_ISSET(iServerSock, &stReadFds)) {
            if ((iClientSock = accept(iServerSock, (struct sockaddr *)&stSockClientAddr, &uiClientAddrLen)) < 0) {
                perror("Accept failed");
                exit(EXIT_FAILURE);
            }

            printf("New connection, socket fd is %d, ip is : %s, port : %d\n", \
                        iClientSock, \
                        inet_ntoa(stSockClientAddr.sin_addr), \
                        ntohs(stSockClientAddr.sin_port));
            
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (stClientInfo[i].iClientSock == 0) {
                    stClientInfo[i].iClientSock = iClientSock;
                    printf("Adding to list of sockets as %d\n", i);

                    // 수신 및 송신 스레드 생성
                    pthread_create(&stClientInfo[i].recvThreadId, NULL, receiveThread, &stClientInfo[i]);
                    pthread_create(&stClientInfo[i].sendThreadId, NULL, sendThread, &stClientInfo[i]);
                    break;
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int iSock = stClientInfo[i].iClientSock;
            if (FD_ISSET(iSock, &stReadFds)) {
                int iReadSize = read(iSock, stClientInfo[i].achBuffer, BUFFER_SIZE);
                if (iReadSize == 0) {
                    getpeername(iSock, (struct sockaddr *)&stSockClientAddr, &uiClientAddrLen);
                    printf("Host disconnected, ip %s, port %d\n", \
                            inet_ntoa(stSockClientAddr.sin_addr), \
                            ntohs(stSockClientAddr.sin_port));

                    handleClientDisconnection(iSock);
                    stClientInfo[i].iClientSock = 0;
                }
            }
        }
    }

    return 0;
}
