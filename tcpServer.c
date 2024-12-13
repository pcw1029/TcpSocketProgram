/**
 * @file tcpServer.c
 * @brief TCP 서버 소켓을 생성하여 여러 클라이언트와의 통신을 관리하는 서버 프로그램
 * 
 * @details 본 프로그램은 클라이언트와의 동시 통신을 지원하기 위해 수신 스레드와 송신 스레드를 사용하며, 
 *          클라이언트의 연결 및 연결 해제를 관리합니다. 서버는 지정된 포트에서 클라이언트의 연결 요청을 대기하고, 
 *          수신된 데이터를 처리한 뒤 클라이언트에게 응답을 보냅니다.
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
#include <errno.h>
#include <stdbool.h>
#include <sys/time.h>

#define PORT 8080

/**
 * @brief 클라이언트와의 데이터 공유를 위한 구조체
 * 
 * @details 클라이언트로부터 수신한 데이터를 저장하고, 데이터의 사용 상태를 관리하며, 
 *          동기화를 위해 뮤텍스와 조건 변수를 포함합니다.
 */
typedef struct {
    char chData[BUFFER_SIZE];       /**< 클라이언트로부터 수신한 데이터 */
    bool hasData;                   /**< 데이터 존재 여부 플래그 */
    pthread_mutex_t mutex;          /**< 데이터 접근 동기화를 위한 뮤텍스 */
    pthread_cond_t cond;            /**< 데이터 준비 상태를 알리는 조건 변수 */
} SHARED_DATA;

/**
 * @brief 클라이언트 정보를 저장하는 구조체
 * 
 * @details 클라이언트 소켓과 관련된 상태 정보를 저장하고, 클라이언트별 스레드와 데이터 공유 구조체를 포함합니다.
 */
typedef struct {
    int iClientSock;                /**< 클라이언트 소켓 파일 디스크립터 */
    bool bExitFlag;                 /**< 연결 종료 플래그 */
    SHARED_DATA stSharedData;       /**< 클라이언트와 공유되는 데이터 구조체 */
    pthread_t recvThreadId;         /**< 수신 스레드 ID */
    pthread_t sendThreadId;         /**< 송신 스레드 ID */
    pthread_mutex_t exitFlagMutex;  /**< 연결 종료 플래그 동기화를 위한 뮤텍스 */
} CLIENT_INFO;

/**
 * @brief 클라이언트로부터 데이터를 수신하는 스레드 함수
 * @param arg CLIENT_INFO 구조체 포인터
 * @return NULL
 * 
 * @details 클라이언트 소켓으로부터 데이터를 읽어 SHARED_DATA 구조체에 저장합니다. 
 *          데이터를 수신하면 조건 변수를 통해 송신 스레드에 알립니다.
 */
void *receiveThread(void *arg) {
    CLIENT_INFO *pstClientInfo = (CLIENT_INFO *)arg;
    struct sockaddr_in stSockClientAddr;
    socklen_t uiClientAddrLen = sizeof(stSockClientAddr);
    char achBuffer[BUFFER_SIZE];
    fd_set stReadFds;
    struct timeval stTimeout;
    sleep(1); /**< 초기화 지연 */

    if (getpeername(pstClientInfo->iClientSock, (struct sockaddr *)&stSockClientAddr, &uiClientAddrLen) == -1) {
        perror("getpeername 실패");
        pthread_exit(NULL);
    }

    while (1) {
        FD_ZERO(&stReadFds);
        FD_SET(pstClientInfo->iClientSock, &stReadFds);

        stTimeout.tv_sec = 0;
        stTimeout.tv_usec = 500 * 1000; /**< 500ms 타임아웃 설정 */

        int activity = select(pstClientInfo->iClientSock + 1, &stReadFds, NULL, NULL, &stTimeout);
        if (activity < 0) {
            perror("select 실패");
            break;
        } else if (activity != 0) {
            memset(achBuffer, 0x0, BUFFER_SIZE);
            int iReadSize = read(pstClientInfo->iClientSock, achBuffer, BUFFER_SIZE);
            if (iReadSize <= 0) {
                if (iReadSize == 0) {
                    /**< 클라이언트 연결 종료 */
                    break;
                } else {
                    perror("read 실패");
                }
                handleTcpClientDisconnection(pstClientInfo->iClientSock);
                pstClientInfo->iClientSock = 0;
            } else {
                /**< 데이터 수신 성공 */
                achBuffer[iReadSize] = '\0';
                pthread_mutex_lock(&pstClientInfo->stSharedData.mutex);
                pstClientInfo->stSharedData.hasData = true; /**< 데이터 존재 플래그 설정 */
                memset(pstClientInfo->stSharedData.chData, 0x0, BUFFER_SIZE);
                memcpy(pstClientInfo->stSharedData.chData, achBuffer, strlen(achBuffer));
                pthread_cond_signal(&pstClientInfo->stSharedData.cond); /**< 조건 변수 신호 전송 */
                pthread_mutex_unlock(&pstClientInfo->stSharedData.mutex);
                fprintf(stdout, "클라이언트 %d로부터 수신: %s\n", pstClientInfo->iClientSock, achBuffer);
                usleep(100 * 1000); /**< 짧은 대기 시간 */
            }
        }
    }

    fprintf(stdout, "%s():%d 클라이언트 연결 해제, 소켓 IP: %s, 포트: %d\n", 
            __func__, __LINE__, 
            inet_ntoa(stSockClientAddr.sin_addr), 
            ntohs(stSockClientAddr.sin_port));

    pthread_mutex_lock(&pstClientInfo->exitFlagMutex);
    pstClientInfo->bExitFlag = true;
    pthread_mutex_unlock(&pstClientInfo->exitFlagMutex);
    pthread_exit(NULL);
}

/**
 * @brief 클라이언트로 데이터를 송신하는 스레드 함수
 * @param arg CLIENT_INFO 구조체 포인터
 * @return NULL
 * 
 * @details SHARED_DATA 구조체에 저장된 데이터를 클라이언트 소켓으로 전송합니다. 
 *          수신 스레드에서 데이터가 준비되면 조건 변수를 통해 알림을 받습니다.
 */
void *sendThread(void *arg) {
    CLIENT_INFO *pstClientInfo = (CLIENT_INFO *)arg;
    struct sockaddr_in stSockClientAddr;
    socklen_t uiClientAddrLen = sizeof(stSockClientAddr);
    char achBuffer[BUFFER_SIZE];
    bool bSendFlag = false;
    bool bExitFlag = false;
    struct timeval stNow;
    struct timespec stTimeout;

    if (getpeername(pstClientInfo->iClientSock, (struct sockaddr *)&stSockClientAddr, &uiClientAddrLen) == -1) {
        perror("getpeername 실패");
        pthread_exit(NULL);
    }

    while (1) {
        gettimeofday(&stNow, NULL);
        stTimeout.tv_sec = stNow.tv_sec + 1; /**< 1초 타임아웃 설정 */
        stTimeout.tv_nsec = stNow.tv_usec * 1000;

        pthread_mutex_lock(&pstClientInfo->exitFlagMutex);
        bExitFlag = pstClientInfo->bExitFlag;
        pthread_mutex_unlock(&pstClientInfo->exitFlagMutex);

        if (bExitFlag) {
            break;
        }

        /**< 데이터 준비 상태 대기 */
        int ret = pthread_cond_timedwait(&pstClientInfo->stSharedData.cond, &pstClientInfo->stSharedData.mutex, &stTimeout);
        if (ret == ETIMEDOUT) {
            fprintf(stderr, ".");
        } else {
            memset(achBuffer, 0x0, BUFFER_SIZE);
            pstClientInfo->stSharedData.hasData = false; /**< 데이터 사용 완료 플래그 초기화 */
            memcpy(achBuffer, pstClientInfo->stSharedData.chData, strlen(pstClientInfo->stSharedData.chData));
            pthread_mutex_unlock(&pstClientInfo->stSharedData.mutex);
            bSendFlag = true;
        }

        if (bSendFlag) {
            write(pstClientInfo->iClientSock, achBuffer, strlen(achBuffer)); /**< 데이터 송신 */
        }
        bSendFlag = false;
    }

    fprintf(stdout, "%s():%d 클라이언트 연결 해제, 소켓 IP: %s, 포트: %d\n", 
            __func__, __LINE__, 
            inet_ntoa(stSockClientAddr.sin_addr), 
            ntohs(stSockClientAddr.sin_port));

    pthread_mutex_lock(&pstClientInfo->exitFlagMutex);
    pstClientInfo->bExitFlag = true;
    pthread_mutex_unlock(&pstClientInfo->exitFlagMutex);
    pthread_exit(NULL);
}

/**
 * @brief 메인 함수: TCP 서버 소켓을 생성하고 클라이언트 연결을 처리
 * @return int 실행 결과
 * 
 * @details 서버 소켓을 생성하고 클라이언트의 연결 요청을 대기합니다. 
 *          연결된 클라이언트별로 송신 및 수신 스레드를 생성하여 데이터를 처리합니다.
 */
int main() {
    int iServerSock, iClientSock;
    struct sockaddr_in stSockClientAddr;
    socklen_t uiClientAddrLen = sizeof(stSockClientAddr);
    fd_set stReadFds;
    CLIENT_INFO stClientGroup[MAX_CLIENTS] = {0}; /**< 클라이언트 정보 배열 초기화 */

    iServerSock = createTcpServerSocket(PORT, MAX_CLIENTS);
    fprintf(stdout, "포트 %d에서 서버 대기 중\n", PORT);

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
            perror("select 실패");
        }

        if (FD_ISSET(iServerSock, &stReadFds)) {
            if ((iClientSock = accept(iServerSock, (struct sockaddr *)&stSockClientAddr, &uiClientAddrLen)) < 0) {
                perror("accept 실패");
                exit(EXIT_FAILURE);
            }

            fprintf(stdout, "새 연결: 소켓 FD %d, IP %s, 포트 %d\n", 
                    iClientSock, 
                    inet_ntoa(stSockClientAddr.sin_addr), 
                    ntohs(stSockClientAddr.sin_port));

            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (stClientGroup[i].iClientSock == 0) {
                    /**< 빈 슬롯에 클라이언트 추가 */
                    stClientGroup[i].iClientSock = iClientSock;

                    if (pthread_mutex_init(&stClientGroup[i].stSharedData.mutex, NULL) != 0) {
                        perror("pthread_mutex_init 실패");
                    }

                    stClientGroup[i].bExitFlag = false;
                    fprintf(stdout, "소켓 목록에 추가: %d\n", i);

                    /**< 수신 및 송신 스레드 생성 */
                    pthread_create(&stClientGroup[i].recvThreadId, NULL, receiveThread, &stClientGroup[i]);
                    pthread_create(&stClientGroup[i].sendThreadId, NULL, sendThread, &stClientGroup[i]);
                    break;
                }
            }
        }
    }

    return 0;
}
