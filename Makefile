# 파일 경로 설정
SRC_DIR = src
INCLUDE_DIR = include
TCP_SERVER_SRCS = ./tcpServer.c
TCP_SERVER_OBJS = ./tcpServer.o
TCP_CLIENT_SRCS = ./tcpClient.c
TCP_CLIENT_OBJS = ./tcpClient.o
SOCKET_SRCS = $(wildcard $(SRC_DIR)/*.c)
SOCKET_OBJS = $(patsubst %.c, %.o, $(SOCKET_SRCS))
CFLAGS = -Wall -g -I$(INCLUDE_DIR)
TCP_SERVER = tcpServer
TCP_CLIENT = tcpClient


# 구글테스트 관련 설정
GTEST_DIR = /home/pcw1029/googletest
GTEST_INCLUDE_DIR = $(GTEST_DIR)/googletest/include
GTEST_LIB_DIR = $(GTEST_DIR)/build/lib

FOR_GTEST_SRCS = $(wildcard $(SRC_DIR)/*.c)
FOR_GTEST_OBJS = $(patsubst %.c, %_gtest.o, $(FOR_GTEST_SRCS))
MY_GTEST_DIR = myGtest
MY_GTEST_SRCS = $(MY_GTEST_DIR)/targetGtest.cc
MY_GTEST_OBJS = $(patsubst %.cc, %.o, $(MY_GTEST_SRCS))
GTEST_TARGET = gTestbench

# 변수 정의
CC = gcc
CXX = g++
GTEST_CFLAGS = -Wall -g -I$(INCLUDE_DIR) -I$(GTEST_INCLUDE_DIR) -std=c++11
GTEST_LDFLAGS = -L$(GTEST_LIB_DIR) -lgtest -lgtest_main -lpthread

# 기본 타겟
all: $(SOCKET_OBJS) $(TCP_SERVER) $(TCP_CLIENT)

# 실행 파일 생성
$(TCP_SERVER): $(TCP_SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(SOCKET_OBJS) -lpthread

$(TCP_CLIENT): $(TCP_CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(SOCKET_OBJS) -lpthread

# 구글테스트 빌드 및 실행
gtest: $(MY_GTEST_OBJS) $(FOR_GTEST_OBJS)
	$(CXX) $(GTEST_CFLAGS) -o $(GTEST_TARGET) $(MY_GTEST_OBJS) $(FOR_GTEST_OBJS) $(GTEST_LDFLAGS)
	
# 패턴 규칙: .c 파일을 .o 파일로 컴파일 (일반 빌드)
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 패턴 규칙: .c 파일을 .gtest.o 파일로 컴파일 (구글테스트 빌드)
%_gtest.o: %.c
	$(CXX) $(GTEST_CFLAGS) -c $< -o $@

# 패턴 규칙: .cc 파일을 .o 파일로 컴파일 (구글테스트)
%.o: %.cc
	$(CXX) $(GTEST_CFLAGS) -c $< -o $@

# clean 타겟: 빌드 파일 정리
.PHONY: clean
clean:
	rm -f $(SOCKET_OBJS) $(TCP_SERVER) $(TCP_CLIENT) $(FOR_GTEST_OBJS) $(MY_GTEST_OBJS) $(GTEST_TARGET) $(TCP_SERVER_OBJS) $(TCP_CLIENT_OBJS)
