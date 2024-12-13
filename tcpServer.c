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
#include <stdbool.h>
#include <sys/time.h>

#define PORT 8080


// 공유 데이터 구조체
typedef struct {
    char chData[BUFFER_SIZE];
    bool hasData;                // 데이터가 존재하는지 확인하는 플래그
    pthread_mutex_t mutex;
    pthread_cond_t cond;         // 조건 변수
} SHARED_DATA;

/**
 * @brief 클라이언트 정보를 저장하는 구조체.
 * @details 클라이언트 소켓, 송수신 버퍼, 수신 및 송신 스레드 ID를 포함.
 */
typedef struct {
    int iClientSock;                ///< 클라이언트 소켓 파일 디스크립터
    bool bExitFlag;    
    SHARED_DATA stSharedData;
    pthread_t recvThreadId;         ///< 데이터 수신 스레드 ID
    pthread_t sendThreadId;         ///< 데이터 송신 스레드 ID 

    pthread_mutex_t exitFlagMutex;
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
    char achBuffer[BUFFER_SIZE];
    fd_set stReadFds;
    struct timeval stTimeout;
    sleep(1);

    if (getpeername(pstClientInfo->iClientSock, (struct sockaddr *)&stSockClientAddr, &uiClientAddrLen) == -1) {
        perror("getpeername failed");
        pthread_exit(NULL);
    }

    while (1) {
        FD_ZERO(&stReadFds);
        FD_SET(pstClientInfo->iClientSock, &stReadFds);

        stTimeout.tv_sec = 0;
        stTimeout.tv_usec = 500*1000;
        int activity = select(pstClientInfo->iClientSock + 1, &stReadFds, NULL, NULL, &stTimeout);
        if (activity < 0) {
            perror("Select error");            
            break;
        } else if (activity != 0) {
            memset(achBuffer, 0x0, BUFFER_SIZE);
            int iReadSize = read(pstClientInfo->iClientSock, achBuffer, BUFFER_SIZE);
            if (iReadSize <= 0) {
                if (iReadSize == 0) {                    
                    break;
                } else {
                    perror("Read error");
                }
                handleTcpClientDisconnection(pstClientInfo->iClientSock);
                pstClientInfo->iClientSock = 0;
            } else {
                achBuffer[iReadSize] = '\0';
                pthread_mutex_lock(&pstClientInfo->stSharedData.mutex);
                pstClientInfo->stSharedData.hasData = true;  // 데이터가 존재함을 알림
                memset(pstClientInfo->stSharedData.chData, 0x0, BUFFER_SIZE);
                memcpy(pstClientInfo->stSharedData.chData, achBuffer, strlen(achBuffer));
                pthread_cond_signal(&pstClientInfo->stSharedData.cond);
                pthread_mutex_unlock(&pstClientInfo->stSharedData.mutex);
                printf("Received from client %d: %s\n", pstClientInfo->iClientSock, achBuffer);
                usleep(100*1000);
            }
        }
    }
    getpeername(pstClientInfo->iClientSock, (struct sockaddr *)&stSockClientAddr, &uiClientAddrLen);
    printf("%s():%d Host disconnected, socket ip is : %s, port : %d\n", \
            __func__,__LINE__, \
            inet_ntoa(stSockClientAddr.sin_addr), \
            ntohs(stSockClientAddr.sin_port));

    pthread_mutex_lock(&pstClientInfo->exitFlagMutex);
    pstClientInfo->bExitFlag = true;
    pthread_mutex_unlock(&pstClientInfo->exitFlagMutex);
    pthread_exit(NULL);
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
    struct sockaddr_in stSockClientAddr;
    socklen_t uiClientAddrLen = sizeof(stSockClientAddr);
    char achBuffer[BUFFER_SIZE];
    bool bSendFlag=false;
    bool bExitFlag=false;
    struct timeval stNow;
    struct timespec stTimeout; 

    if (getpeername(pstClientInfo->iClientSock, (struct sockaddr *)&stSockClientAddr, &uiClientAddrLen) == -1) {
        perror("getpeername failed");
        pthread_exit(NULL);
    }   

    while (1) {
        gettimeofday(&stNow, NULL);    
        // 5초 대기 시간 설정
        stTimeout.tv_sec = stNow.tv_sec + 1;
        stTimeout.tv_nsec = stNow.tv_usec * 1000;
        pthread_mutex_lock(&pstClientInfo->exitFlagMutex);
        bExitFlag = pstClientInfo->bExitFlag;
        pthread_mutex_unlock(&pstClientInfo->exitFlagMutex);
        if(bExitFlag){
            break;
        }
        // 데이터가 준비될 때까지 대기
        int ret = pthread_cond_timedwait(&pstClientInfo->stSharedData.cond, &pstClientInfo->stSharedData.mutex, &stTimeout);
        if (ret == ETIMEDOUT) {
            fprintf(stderr,".");
        }else{
            //printf("Thread 2: 읽은 데이터: %s\n", pstClientInfo->stSharedData.chData);
            memset(achBuffer, 0x0, BUFFER_SIZE);
            // pthread_mutex_lock(&pstClientInfo->stSharedData.mutex);
            pstClientInfo->stSharedData.hasData = false;// 데이터 사용 완료 플래그 초기화
            memcpy(achBuffer, pstClientInfo->stSharedData.chData, strlen(pstClientInfo->stSharedData.chData));            
            pthread_mutex_unlock(&pstClientInfo->stSharedData.mutex);
            bSendFlag=true;
        }
        if(bSendFlag){
            write(pstClientInfo->iClientSock, achBuffer, strlen(achBuffer));
        }
        bSendFlag=false;        
    }
    getpeername(pstClientInfo->iClientSock, (struct sockaddr *)&stSockClientAddr, &uiClientAddrLen);
    printf("%s():%d Host disconnected, socket ip is : %s, port : %d\n", \
            __func__,__LINE__, \
            inet_ntoa(stSockClientAddr.sin_addr), \
            ntohs(stSockClientAddr.sin_port));
    pthread_mutex_lock(&pstClientInfo->exitFlagMutex);
    pstClientInfo->bExitFlag = true;
    pthread_mutex_unlock(&pstClientInfo->exitFlagMutex);
    pthread_exit(NULL);
}

int main() {
    int iServerSock, iClientSock;
    struct sockaddr_in stSockClientAddr;
    socklen_t uiClientAddrLen = sizeof(stSockClientAddr);
    
    fd_set stReadFds;
    CLIENT_INFO stClientGroup[MAX_CLIENTS] = {0};

    iServerSock = createTcpServerSocket(PORT, MAX_CLIENTS);
    printf("Server listening on port %d\n", PORT);

    while (1) {
        FD_ZERO(&stReadFds);
        FD_SET(iServerSock, &stReadFds);
        int iMaxSock = iServerSock;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int iSock = stClientGroup[i].iClientSock;
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
                if (stClientGroup[i].iClientSock == 0) {
                    stClientGroup[i].iClientSock = iClientSock;

                    if (pthread_mutex_init(&stClientGroup[i].stSharedData.mutex, NULL) != 0) {
                        perror("pthread_mutex_init 실패");                
                    }
                    stClientGroup[i].bExitFlag=false;
                    printf("Adding to list of sockets as %d\n", i);
                    // 수신 및 송신 스레드 생성
                    pthread_create(&stClientGroup[i].recvThreadId, NULL, receiveThread, &stClientGroup[i]);
                    pthread_create(&stClientGroup[i].sendThreadId, NULL, sendThread, &stClientGroup[i]);
                    break;
                }
            }
        }
    }

    return 0;
}
