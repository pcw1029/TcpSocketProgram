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

#define PORT 8080
#define SERVER_IP "127.0.0.1"
#define TIMEOUT_SEC 10  // 타임아웃 시간 (초)

volatile int g_iIsRunning = 0;  // 프로그램 종료 플래그
pthread_mutex_t uRunningMutex = PTHREAD_MUTEX_INITIALIZER;  // running 플래그 보호를 위한 뮤텍스

/**
 * @brief 메시지 송신을 담당하는 스레드 함수
 * 
 * 사용자가 입력한 메시지를 서버로 전송합니다.
 * 
 * @param sock 서버 소켓 파일 디스크립터에 대한 포인터
 * @return void*
 */
void *sendMessages(void *sock) {
    int iSock = *(int *)sock;
    char achBuffer[BUFFER_SIZE];

    while (1) {
        pthread_mutex_lock(&uRunningMutex);
        if (!g_iIsRunning) {
            pthread_mutex_unlock(&uRunningMutex);
            break;
        }
        pthread_mutex_unlock(&uRunningMutex);

        printf("Enter message (type 'exit' to quit): ");

        // 입력 대기를 타임아웃 처리하기 위해 select() 사용
        fd_set stReadFds;
        struct timeval stTimeout;

        FD_ZERO(&stReadFds);
        FD_SET(STDIN_FILENO, &stReadFds);

        stTimeout.tv_sec = 2;  // 1초 타임아웃
        stTimeout.tv_usec = 0;
        int activity = select(STDIN_FILENO + 1, &stReadFds, NULL, NULL, &stTimeout);
        if (activity > 0 && FD_ISSET(STDIN_FILENO, &stReadFds)) {
            fgets(achBuffer, BUFFER_SIZE, stdin);
            achBuffer[strcspn(achBuffer, "\n")] = 0;

            if (strcmp(achBuffer, "exit") == 0) {
                pthread_mutex_lock(&uRunningMutex);
                g_iIsRunning = 0;
                pthread_mutex_unlock(&uRunningMutex);
                break;
            }

            if (write(iSock, achBuffer, strlen(achBuffer)) < 0) {
                perror("Write error");
                pthread_mutex_lock(&uRunningMutex);
                g_iIsRunning = 0;
                pthread_mutex_unlock(&uRunningMutex);
                pthread_exit(NULL);
            }
        }else{
            printf("\n");
        }
    }

    pthread_exit(NULL);
}
/**
 * @brief 메시지 수신을 담당하는 스레드 함수
 * 
 * 서버로부터 수신된 메시지를 출력하며, 일정 시간 동안 응답이 없을 경우 타임아웃을 처리합니다.
 * 
 * @param sock 서버 소켓 파일 디스크립터에 대한 포인터
 * @return void*
 */
void *receiveMessages(void *sock) {
    int iSock = *(int *)sock;
    char achBuffer[BUFFER_SIZE];

    while (1) {
        pthread_mutex_lock(&uRunningMutex);
        if (!g_iIsRunning) {
            pthread_mutex_unlock(&uRunningMutex);
            break;
        }
        pthread_mutex_unlock(&uRunningMutex);

        // select()를 사용하여 소켓이 읽기 가능한지 확인
        fd_set readfds;
        struct timeval timeout;

        FD_ZERO(&readfds);
        FD_SET(iSock, &readfds);

        timeout.tv_sec = TIMEOUT_SEC;
        timeout.tv_usec = 0;

        int activity = select(iSock + 1, &readfds, NULL, NULL, &timeout);

        if (activity < 0) {
            perror("Select error");
            pthread_mutex_lock(&uRunningMutex);
            g_iIsRunning = 0;
            pthread_mutex_unlock(&uRunningMutex);
            pthread_exit(NULL);
        } else if (activity == 0) {
            printf("No response from server for %d seconds. Timing out...\n", TIMEOUT_SEC);
            pthread_mutex_lock(&uRunningMutex);
            g_iIsRunning = 0;
            pthread_mutex_unlock(&uRunningMutex);
            pthread_exit(NULL);
        }

        if (FD_ISSET(iSock, &readfds)) {
            int iReadSize = read(iSock, achBuffer, BUFFER_SIZE);
            if (iReadSize > 0) {
                achBuffer[iReadSize] = '\0';
                printf("Server: %s\n", achBuffer);
            } else if (iReadSize == 0) {
                printf("Server disconnected.\n");
                pthread_mutex_lock(&uRunningMutex);
                g_iIsRunning = 0;
                pthread_mutex_unlock(&uRunningMutex);
                pthread_exit(NULL);
            } else {
                perror("Read error");
                pthread_mutex_lock(&uRunningMutex);
                g_iIsRunning = 0;
                pthread_mutex_unlock(&uRunningMutex);
                pthread_exit(NULL);
            }
        }
    }

    pthread_exit(NULL);
}

int main() {
    int iSock;
    pthread_t sendThread, receiveThread;

    while(1){
        iSock = createClientSocket(SERVER_IP, PORT);
        if (iSock < 0) {
            printf("Failed to connect to server\n");
            return -1;
        }
        g_iIsRunning = 1;

        // 송신 및 수신 스레드 생성
        if (pthread_create(&sendThread, NULL, sendMessages, (void *)&iSock) != 0) {
            perror("Failed to create send thread");
            close(iSock);
            return -1;
        }

        if (pthread_create(&receiveThread, NULL, receiveMessages, (void *)&iSock) != 0) {
            perror("Failed to create receive thread");
            close(iSock);
            return -1;
        }

        // 스레드가 종료될 때까지 대기
        pthread_join(sendThread, NULL);
        pthread_join(receiveThread, NULL);

        close(iSock);

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
