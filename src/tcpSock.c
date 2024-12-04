/**
 * @file tcpSock.c
 * @brief TCP 서버 및 클라이언트 소켓을 생성하고 연결을 관리하는 API
 *
 * 이 프로그램은 TCP 서버와 클라이언트를 생성하고 관리하기 위한 함수들을 포함하고 있습니다.
 * 서버 소켓을 생성하고 필요한 옵션을 설정하여 다수의 클라이언트와 연결할 수 있으며, 
 * TCP Keep-Alive 옵션을 통해 연결 상태를 지속적으로 모니터링합니다. 또한 클라이언트 소켓 생성과 
 * 연결 해제 처리에 대한 기능을 제공합니다. 
 *
 * 주요 기능:
 * - 서버 소켓 생성 및 설정 (TCP Keep-Alive 포함)
 * - 클라이언트 소켓 생성 및 서버 연결
 * - 클라이언트 연결 해제 및 연결 상태 모니터링
 * - 소켓의 RX 및 TX 버퍼 크기 설정
 *
 * @author 박철우
 * @date 2024-12-04
 */
#include "tcpSock.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int createServerSocket(int iPort, int iMaxClients)
{
    int iServerSock;
    struct sockaddr_in stSockAddr;
    int iSockOpt = 1;

    if ((iServerSock = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    /**
     * @brief 주소 재사용을 허용하기 위해 소켓 옵션 설정
     */
    if (setsockopt(iServerSock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &iSockOpt, sizeof(iSockOpt))) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    // TCP Keep-Alive 설정 추가
    int keepalive = 1;
    int keepidle = 10; // 첫 번째 Keep-Alive 패킷을 보내기까지의 대기 시간 (초)
    int keepinterval = 5; // 각 Keep-Alive 패킷 간의 간격 (초)
    int keepcount = 3; // 연결이 끊어졌다고 간주하기 전까지의 최대 Keep-Alive 패킷 수

    /**
     * @brief 클라이언트가 여전히 연결되어 있는지 확인하기 위해 TCP Keep-Alive를 활성화합니다.
     * 
     * SO_KEEPALIVE: 소켓에서 Keep-Alive 메시지를 활성화합니다.
     */
    if (setsockopt(iServerSock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) < 0) {
        perror("Setsockopt SO_KEEPALIVE failed");
        exit(EXIT_FAILURE);
    }
    /**
     * @brief 첫 번째 Keep-Alive 프로브를 보내기 전에 대기하는 시간을 설정합니다.
     * 
     * TCP_KEEPIDLE: Keep-Alive 프로브를 보내기 전까지 대기할 시간(초)입니다.
     */
    if (setsockopt(iServerSock, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle)) < 0) {
        perror("Setsockopt TCP_KEEPIDLE failed");
        exit(EXIT_FAILURE);
    }
    /**
     * @brief Keep-Alive 프로브 간의 간격을 설정합니다.
     * 
     * TCP_KEEPINTVL: 각 Keep-Alive 프로브 간의 시간 간격(초)입니다.
     */
    if (setsockopt(iServerSock, IPPROTO_TCP, TCP_KEEPINTVL, &keepinterval, sizeof(keepinterval)) < 0) {
        perror("Setsockopt TCP_KEEPINTVL failed");
        exit(EXIT_FAILURE);
    }
    /**
     * @brief 연결이 끊긴 것으로 간주하기 전까지 보낼 최대 Keep-Alive 프로브 수를 설정합니다.
     * 
     * TCP_KEEPCNT: 연결이 끊겼다고 선언하기 전 실패한 Keep-Alive 프로브의 수입니다.
     */
    if (setsockopt(iServerSock, IPPROTO_TCP, TCP_KEEPCNT, &keepcount, sizeof(keepcount)) < 0) {
        perror("Setsockopt TCP_KEEPCNT failed");
        exit(EXIT_FAILURE);
    }

    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_addr.s_addr = INADDR_ANY;
    stSockAddr.sin_port = htons(iPort);

    if (bind(iServerSock, (struct sockaddr *)&stSockAddr, sizeof(stSockAddr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(iServerSock, iMaxClients) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    return iServerSock;
}

int createClientSocket(const char *kpchIp, int iPort) {
    int iSock;
    struct sockaddr_in stSockServAddr;

    if ((iSock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return -1;
    }

    stSockServAddr.sin_family = AF_INET;
    stSockServAddr.sin_port = htons(iPort);

    if (inet_pton(AF_INET, kpchIp, &stSockServAddr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        return -1;
    }

    if (connect(iSock, (struct sockaddr *)&stSockServAddr, sizeof(stSockServAddr)) < 0) {
        perror("Connection failed");
        return -1;
    }

    return iSock;
}

void handleClientDisconnection(int iClientSockfd) {
    printf("Client disconnected, closing socket\n");
    close(iClientSockfd);
}

void checkClientConnections(int *piClientSockets, int iMaxClients) {
    for (int i = 0; i < iMaxClients; i++) {
        if (piClientSockets[i] != 0) {
            char buffer[1];
            int result = recv(piClientSockets[i], buffer, sizeof(buffer), MSG_PEEK | MSG_DONTWAIT);
            if (result == 0) {
                printf("Client socket %d appears to have disconnected\n", piClientSockets[i]);
                handleClientDisconnection(piClientSockets[i]);
                piClientSockets[i] = 0;
            }
        }
    }
}


void setSocketBufferSize(int iSock, int iRxSize, int iTxSize) 
{
    if (setsockopt(iSock, SOL_SOCKET, SO_RCVBUF, &iRxSize, sizeof(iRxSize)) < 0) {
        perror("Setsockopt SO_RCVBUF failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(iSock, SOL_SOCKET, SO_SNDBUF, &iTxSize, sizeof(iTxSize)) < 0) {
        perror("Setsockopt SO_SNDBUF failed");
        exit(EXIT_FAILURE);
    }
}