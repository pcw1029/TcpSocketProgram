/**
 * @file tcpClient.c
 * @brief TCP 클라이언트 프로그램으로, 서버와의 송수신을 관리합니다.
 * 
 * 이 프로그램은 TCP 클라이언트로서 서버와의 통신을 위해 두 개의 스레드를 생성합니다.
 * 하나의 스레드는 사용자가 입력한 메시지를 서버로 전송하고, 다른 하나는 서버로부터 수신한 메시지를 출력합니다.
 * 
 * 송신 스레드는 사용자의 입력을 기다리며, "exit" 명령을 입력하면 종료됩니다. 
 * 수신 스레드는 서버로부터 메시지를 수신하고, 일정 시간(10초) 동안 응답이 없으면 타임아웃 처리를 합니다.
 * 두 스레드가 모두 종료되면 클라이언트 소켓을 다시 생성하여 재연결 할 수 있는 기능도 제공합니다.
 * 
 * @author 박철우
 * @date 2015.05
 */

#include "tcpSock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>  // errno 사용을 위해 추가
#include <unistd.h>
#include <pthread.h>  // pthread 사용을 위해 추가
#include <sys/select.h>  // select() 사용을 위해 추가
#include <stdbool.h>

#define PORT 8080
#define SERVER_IP "127.0.0.1"

/**
 * @brief 클라이언트 정보를 저장하는 구조체.
 * 
 * @details 클라이언트 소켓, 송수신 버퍼, 수신 및 송신 스레드 ID를 포함하며,
 *          클라이언트 상태를 관리하기 위한 플래그와 동기화를 위한 뮤텍스를 포함합니다.
 */
typedef struct {
    int iSock;                      /**< 클라이언트 소켓 파일 디스크립터 */
    bool bIsRunning;                /**< 클라이언트 실행 상태 플래그 */
    char achBuffer[BUFFER_SIZE];    /**< 데이터 송수신을 위한 버퍼 */
    pthread_t recvThreadId;         /**< 데이터 수신 스레드 ID */
    pthread_t sendThreadId;         /**< 데이터 송신 스레드 ID */
    pthread_mutex_t uRunningMutex;  /**< 실행 상태 동기화를 위한 뮤텍스 */
} CLIENT_INFO;

/**
 * @brief 메시지 송신을 담당하는 스레드 함수
 * 
 * @details 사용자가 입력한 메시지를 서버로 전송합니다. 
 *          "exit" 입력 시 클라이언트를 종료합니다.
 * 
 * @param pvData CLIENT_INFO 구조체 포인터
 * @return void*
 */
void *sendMessages(void *pvData) {
    CLIENT_INFO* pstClientInfo = (CLIENT_INFO *)pvData;
    char achBuffer[BUFFER_SIZE];
    // 입력 대기를 타임아웃 처리하기 위해 select() 사용
    fd_set stReadFds;
    struct timeval stTimeout;
    fprintf(stderr,"Enter message('exit' to quit): ");
    while (1) {
        pthread_mutex_lock(&pstClientInfo->uRunningMutex);
        if (!pstClientInfo->bIsRunning) {
            pthread_mutex_unlock(&pstClientInfo->uRunningMutex);
            break;
        }
        pthread_mutex_unlock(&pstClientInfo->uRunningMutex);
        
        FD_ZERO(&stReadFds);
        FD_SET(STDIN_FILENO, &stReadFds);

        stTimeout.tv_sec = 1;  // 1초 타임아웃
        stTimeout.tv_usec = 0;
        int activity = select(STDIN_FILENO + 1, &stReadFds, NULL, NULL, &stTimeout);
        if (activity > 0 && FD_ISSET(STDIN_FILENO, &stReadFds)) {
            fgets(achBuffer, BUFFER_SIZE, stdin);
            achBuffer[strcspn(achBuffer, "\n")] = 0;

            if (strcmp(achBuffer, "exit") == 0) {
                pthread_mutex_lock(&pstClientInfo->uRunningMutex);
                pstClientInfo->bIsRunning = false;
                pthread_mutex_unlock(&pstClientInfo->uRunningMutex);
                printf("Client Exit\n");
                break;
            }

            if (write(pstClientInfo->iSock, achBuffer, strlen(achBuffer)) < 0) {
                perror("Write error");
                pthread_mutex_lock(&pstClientInfo->uRunningMutex);
                pstClientInfo->bIsRunning = false;
                pthread_mutex_unlock(&pstClientInfo->uRunningMutex);
                pthread_exit(NULL);
            }
            fprintf(stderr,"Enter message('exit' to quit): ");
        }
    }

    pthread_exit(NULL);
}

/**
 * @brief 메시지 수신을 담당하는 스레드 함수
 * 
 * @details 서버로부터 수신된 메시지를 출력하며, 일정 시간 동안 응답이 없을 경우 타임아웃을 처리합니다.
 * 
 * @param pvData CLIENT_INFO 구조체 포인터
 * @return void*
 */
void *receiveMessages(void *pvData) {
    CLIENT_INFO* pstClientInfo = (CLIENT_INFO *)pvData;
    char achBuffer[BUFFER_SIZE];
    fd_set stReadFds;
    struct timeval stTimeout;

    while (1) {
        pthread_mutex_lock(&pstClientInfo->uRunningMutex);
        if (!pstClientInfo->bIsRunning) {
            pthread_mutex_unlock(&pstClientInfo->uRunningMutex);
            break;
        }
        pthread_mutex_unlock(&pstClientInfo->uRunningMutex);

        FD_ZERO(&stReadFds);
        FD_SET(pstClientInfo->iSock, &stReadFds);

        stTimeout.tv_sec = 0;
        stTimeout.tv_usec = 500*1000;

        int activity = select(pstClientInfo->iSock + 1, &stReadFds, NULL, NULL, &stTimeout);

        if (activity < 0) {
            perror("Select error");
            pthread_mutex_lock(&pstClientInfo->uRunningMutex);
            pstClientInfo->bIsRunning = false;
            pthread_mutex_unlock(&pstClientInfo->uRunningMutex);
            break;
        } else if (activity != 0) {            
            if (FD_ISSET(pstClientInfo->iSock, &stReadFds)) {
                memset(achBuffer, 0x0, BUFFER_SIZE);
                int iReadSize = read(pstClientInfo->iSock, achBuffer, BUFFER_SIZE);
                if (iReadSize > 0) {
                    achBuffer[iReadSize] = '\0';
                    printf("Server: %s\n", achBuffer);
                } else if (iReadSize == 0) {
                    printf("Server disconnected.\n");
                    pthread_mutex_lock(&pstClientInfo->uRunningMutex);
                    pstClientInfo->bIsRunning = false;
                    pthread_mutex_unlock(&pstClientInfo->uRunningMutex);
                    pthread_exit(NULL);
                } else {
                    perror("Read error");
                    pthread_mutex_lock(&pstClientInfo->uRunningMutex);
                    pstClientInfo->bIsRunning = false;
                    pthread_mutex_unlock(&pstClientInfo->uRunningMutex);
                    pthread_exit(NULL);
                }
            }
        }
    }

    pthread_exit(NULL);
}

/**
 * @brief 메인 함수: TCP 클라이언트 소켓을 생성하고 서버와 통신을 처리
 * 
 * @details 클라이언트 소켓을 생성하여 서버와 연결을 시도합니다. 
 *          연결이 성공하면 송신 및 수신 스레드를 생성하여 데이터를 처리하며,
 *          연결 종료 후 재연결 여부를 사용자로부터 확인받습니다.
 * 
 * @return int 실행 결과
 */
int main() {
    CLIENT_INFO stClientInfo = {        \
                .iSock = 0,             \
                .bIsRunning = false,    \
                .achBuffer = {0},       \
                .recvThreadId = 0,      \
                .sendThreadId = 0,      \
                .uRunningMutex = PTHREAD_MUTEX_INITIALIZER  \
            };

    while(1){
        stClientInfo.iSock = createTcpClientSocket(SERVER_IP, PORT);
        if (stClientInfo.iSock < 0) {
            printf("Failed to connect to server\n");
            return -1;
        }
        stClientInfo.bIsRunning = true;

        // 송신 및 수신 스레드 생성
        if (pthread_create(&stClientInfo.sendThreadId, NULL, sendMessages, (void *)&stClientInfo) != 0) {
            perror("Failed to create send thread");
            close(stClientInfo.iSock);
            return -1;
        }

        if (pthread_create(&stClientInfo.recvThreadId, NULL, receiveMessages, (void *)&stClientInfo) != 0) {
            perror("Failed to create receive thread");
            close(stClientInfo.iSock);
            return -1;
        }

        // 스레드가 종료될 때까지 대기
        pthread_join(stClientInfo.sendThreadId, NULL);
        pthread_join(stClientInfo.recvThreadId, NULL);

        close(stClientInfo.iSock);

        // 다시 시도할지 여부 확인
        char retry;
        printf("Do you want to reconnect? (y/n): ");
        scanf(" %c", &retry);
        if (retry != 'y' && retry != 'Y') {
            break;
        }
        getchar();  // 입력 버퍼에 남아있는 개행 문자를 제거
    }
    return 0;
}
