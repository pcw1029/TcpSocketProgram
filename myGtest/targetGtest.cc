#include <gtest/gtest.h>
#include "tcpSock.h"
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>

/**
 * @brief 포트가 사용 중인지 확인하는 함수
 *
 * 서버 소켓이 바인딩하려는 포트가 이미 사용 중인지 확인합니다.
 *
 * @param port 확인할 포트 번호
 * @return true 사용 중인 경우
 * @return false 사용 중이 아닌 경우
 */
bool isPortAvailable(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    // 소켓을 특정 포트에 바인딩 시도
    int result = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);

    return result == 0;
}

/**
 * @brief TCP 소켓 테스트 클래스
 *
 * TcpSocketTest 클래스는 TCP 서버와 클라이언트 소켓의 동작을 테스트하기 위한 
 * GoogleTest 기반 테스트 클래스로, 서버 소켓 생성, 클라이언트 연결, 
 * TCP Keep-Alive 옵션 설정, 클라이언트 연결 종료 등을 테스트합니다.
 *
 * 각 테스트 케이스는 서버와 클라이언트 간의 통신을 설정하고 
 * 다양한 시나리오에서 소켓의 동작을 검증합니다.
 * 테스트 픽스처(TEST_F) 클래스를 사용하여 공통된 초기화(Setup) 및 종료(TearDown) 코드를 공유합니다.
 */
class TcpSocketTest : public ::testing::Test {
protected:
    int iServerSock;
    int iClientSock;
    const int kiPort = 8080;
    const int kiMaxClients = 5;

    void SetUp() override {
        // 서버 소켓 생성 전에 포트가 사용 중인지 확인
        if (!isPortAvailable(kiPort)) {
            GTEST_SKIP() << "Port " << kiPort << " is already in use. Skipping test.";
        }

        // 서버 소켓 생성
        iServerSock = createTcpServerSocket(kiPort, kiMaxClients);
        ASSERT_GE(iServerSock, 0) << "Server socket creation failed.";
    }

    void TearDown() override {
        // 모든 소켓 닫기
        if (iClientSock > 0) {
            close(iClientSock);
        }
        if (iServerSock > 0) {
            close(iServerSock);
        }
    }
};

/**
 * @brief 서버 소켓 생성 테스트
 * 
 * 서버 소켓이 올바르게 생성되었는지 확인합니다.
 */
TEST_F(TcpSocketTest, CreateServerSocket) {
    ASSERT_GE(iServerSock, 0) << "Failed to create server socket.";
}

/**
 * @brief 클라이언트 소켓 생성 및 서버 연결 테스트
 * 
 * 서버 스레드를 생성하여 서버가 클라이언트를 수락할 준비가 되었는지 확인하고, 
 * 클라이언트 소켓이 성공적으로 서버에 연결되는지 테스트합니다.
 */
TEST_F(TcpSocketTest, CreateClientSocket) {
    // 서버 스레드 생성하여 서버를 대기 상태로 만듭니다.
    std::thread server_thread([this]() {
        struct sockaddr_in stSockAddr;
        socklen_t uiSockAddrLen = sizeof(stSockAddr);
        iClientSock = accept(iServerSock, (struct sockaddr *)&stSockAddr, &uiSockAddrLen);
        ASSERT_GE(iClientSock, 0) << "Failed to accept client connection.";
    });

    // 클라이언트 소켓 생성 및 서버에 연결 시도
    int _iClientSocket = createTcpClientSocket("127.0.0.1", kiPort);
    ASSERT_GE(_iClientSocket, 0) << "Failed to create client socket.";

    // 서버가 클라이언트를 수락하도록 약간의 대기 시간 부여
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 서버 스레드가 종료되도록 대기
    server_thread.join();

    // 클라이언트 소켓 파일 디스크립터 유효성 확인
    ASSERT_GE(iClientSock, 0) << "Server failed to accept client connection.";
    close(_iClientSocket);
}

/**
 * @brief TCP Keep-Alive 옵션 설정 테스트
 * 
 * 서버 소켓에 설정된 TCP Keep-Alive 옵션들이 올바르게 설정되었는지 확인합니다.
 * SO_KEEPALIVE, TCP_KEEPIDLE, TCP_KEEPINTVL, TCP_KEEPCNT 옵션을 검사합니다.
 */
TEST_F(TcpSocketTest, TCPKeepAliveOptions) {
    // SO_KEEPALIVE 옵션이 올바르게 설정되었는지 확인
    int iKeepAlive;
    socklen_t uiOptlen = sizeof(iKeepAlive);
    ASSERT_EQ(getsockopt(iServerSock, SOL_SOCKET, SO_KEEPALIVE, &iKeepAlive, &uiOptlen), 0) << "Failed to get SO_KEEPALIVE option.";
    ASSERT_EQ(iKeepAlive, 1) << "SO_KEEPALIVE is not set correctly.";

    int iKeepIdle;
    uiOptlen = sizeof(iKeepIdle);
    ASSERT_EQ(getsockopt(iServerSock, IPPROTO_TCP, TCP_KEEPIDLE, &iKeepIdle, &uiOptlen), 0) << "Failed to get TCP_KEEPIDLE option.";
    ASSERT_EQ(iKeepIdle, 10) << "TCP_KEEPIDLE is not set correctly.";

    int iKeepInterval;
    uiOptlen = sizeof(iKeepInterval);
    ASSERT_EQ(getsockopt(iServerSock, IPPROTO_TCP, TCP_KEEPINTVL, &iKeepInterval, &uiOptlen), 0) << "Failed to get TCP_KEEPINTVL option.";
    ASSERT_EQ(iKeepInterval, 5) << "TCP_KEEPINTVL is not set correctly.";

    int iKeepCount;
    uiOptlen = sizeof(iKeepCount);
    ASSERT_EQ(getsockopt(iServerSock, IPPROTO_TCP, TCP_KEEPCNT, &iKeepCount, &uiOptlen), 0) << "Failed to get TCP_KEEPCNT option.";
    ASSERT_EQ(iKeepCount, 3) << "TCP_KEEPCNT is not set correctly.";
}

/**
 * @brief 클라이언트 연결 종료 테스트
 * 
 * 서버가 클라이언트 연결 종료를 올바르게 감지하는지 테스트합니다. 
 * 클라이언트가 연결을 종료한 후 서버에서 이를 인식하는지 확인합니다.
 */
TEST_F(TcpSocketTest, HandleClientDisconnection) {
    // 서버 스레드 생성하여 서버가 클라이언트를 대기하도록 함
    std::thread server_thread([this]() {
        struct sockaddr_in stSockAddr;
        socklen_t uiSockAddrLen = sizeof(stSockAddr);
        iClientSock = accept(iServerSock, (struct sockaddr *)&stSockAddr, &uiSockAddrLen);
        ASSERT_GE(iClientSock, 0) << "Failed to accept client connection.";
    });

    // 클라이언트 소켓 생성 및 서버에 연결
    int _iClientSsock = createTcpClientSocket("127.0.0.1", kiPort);
    ASSERT_GE(_iClientSsock, 0) << "Failed to create client socket.";

    // 서버가 클라이언트를 수락하도록 대기
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 서버 스레드 종료 대기
    server_thread.join();

    // 클라이언트 소켓 닫기 (서버에서 연결 종료를 감지해야 함)
    close(_iClientSsock);

    // 연결이 끊어졌는지 확인하기 위해 일정 시간 대기
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 서버에서 클라이언트 소켓이 유효하지 않은지 확인
    ASSERT_EQ(recv(iClientSock, nullptr, 0, MSG_PEEK | MSG_DONTWAIT), 0) << "Server did not detect client disconnection.";
}

/**
 * @brief 클라이언트 전송 후 서버 응답 미수신 처리 테스트
 * 
 * 클라이언트가 서버로 데이터를 전송한 후, 서버로부터 응답이 없을 경우에 대한 처리를 테스트합니다.
 */
TEST_F(TcpSocketTest, ClientSendWithoutServerResponse) {
    // 서버 스레드 생성하여 서버가 클라이언트를 대기하도록 함
    std::thread server_thread([this]() {
        struct sockaddr_in stSockAddr;
        socklen_t uiSockAddrLen = sizeof(stSockAddr);
        iClientSock = accept(iServerSock, (struct sockaddr *)&stSockAddr, &uiSockAddrLen);
        ASSERT_GE(iClientSock, 0) << "Failed to accept client connection.";

        // 서버는 응답을 보내지 않음
        std::this_thread::sleep_for(std::chrono::seconds(3));
    });

    // 클라이언트 소켓 생성 및 서버에 연결
    int _iClientSsock = createTcpClientSocket("127.0.0.1", kiPort);
    ASSERT_GE(_iClientSsock, 0) << "Failed to create client socket.";

    // 서버가 클라이언트를 수락하도록 대기
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 서버 스레드 종료 대기
    server_thread.join();

    // 클라이언트에서 서버로 데이터 전송 시도
    const char *kpchMessage = "Hello, server!";
    ssize_t iSentSize = send(_iClientSsock, kpchMessage, strlen(kpchMessage), 0);
    ASSERT_GT(iSentSize, 0) << "Failed to send data to server.";

    // 서버 응답 대기 (타임아웃 발생 시 처리 확인)
    fd_set stReadFds;
    FD_ZERO(&stReadFds);
    FD_SET(_iClientSsock, &stReadFds);

    struct timeval stTimeout;
    stTimeout.tv_sec = 1; // 3초 대기
    stTimeout.tv_usec = 0;

    int activity = select(_iClientSsock + 1, &stReadFds, nullptr, nullptr, &stTimeout);
    ASSERT_EQ(activity, 0) << "Server responded unexpectedly.";

    // 클라이언트 소켓 닫기
    close(_iClientSsock);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
