// Server.cpp : 定义控制台应用程序的入口点。
//
#include "stdafx.h"
#include <WINSOCK2.H>
#include <WS2tcpip.h>
#include <iostream>
#include <cstring>
#include <string>
#include <vector>
#include <ctime>
#include <cassert>

#pragma comment(lib,"ws2_32.lib")
using namespace std;
#define  CLOCKS_PER_SEC ((clock_t)1000)

namespace bytes_helper {
	template <class T> struct type {};

	template <typename T, class iter> 
	T read(iter it, type<T>) {
		T i = T();
		//[01][02][03][04]
		int T_len = sizeof(T);
		for (int idx = 0; idx < T_len; ++idx) {
			i |= *(it + idx)<<(3-idx)*8;
		}
		return  i;
	}

	template <typename T, class iter>
	int write(T v, iter it, int size ) {
		int i = 0;
		int T_len = sizeof(T);
		for (int idx = 0; idx < T_len; ++idx) {
			*(it + idx) = v>>(3-idx)*8 ;
			++i;
		}
		return i;
	}
}

enum packet_receive_state {
	S_READ_LEN,
	S_READ_CONTENT
};

int main()
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);//WSAStartup进行相应的socket库绑定
	SOCKET ListeningSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	SOCKADDR_IN ServerAddr;
	memset(&ServerAddr, 0, sizeof(ServerAddr));
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port = htons(8888); // 若port指定为0,则调用bind时，系统会为其指定一个可用的端口号
	ServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	int bindrt = bind(ListeningSocket, (SOCKADDR *)&ServerAddr, sizeof(ServerAddr));
	if (bindrt == SOCKET_ERROR)
	{
		int rt = ::WSAGetLastError();
		cout << "bind error: " << rt << endl;
		return rt;
	}

	//获取监听端口的端口号
	struct sockaddr_in connAddr;
	int len = sizeof(connAddr);
	getsockname(ListeningSocket, (SOCKADDR *)&connAddr, &len);
	int port = ntohs(connAddr.sin_port);
	cout << "current listen port: " << port << endl << endl;

	int listenrt = listen(ListeningSocket,128);
	if (listenrt == SOCKET_ERROR)
	{
		int err = ::WSAGetLastError();
		cout << "listen error: "<<err<< endl;
		return err;
	}

again_accept:
	//1秒内循环接受数据
	sockaddr_in remoteAddr;
	int remoteAddrLen = sizeof(remoteAddr);
	SOCKET accepted_fd = accept(ListeningSocket, (SOCKADDR *)&remoteAddr, &remoteAddrLen);
	if (accepted_fd == SOCKET_ERROR)
	{
		int err = ::WSAGetLastError();
		cout << "accept error: " << err << endl;
		return err;
	}

	cout << "current accept client IP: " << inet_ntoa(remoteAddr.sin_addr) << endl;
	cout << "current accept client port: " << ntohs(remoteAddr.sin_port) << endl;

	//QPS count
	const double time_span = 10;
	const int time_span_total = 600;
	int QPScount_idx = 0;
	int QPScount[time_span_total] = { 0 };
	int before_sum = 0;

	int nreceived = 0; //the total packet received 
	int message_len;
	const int BLEN = 1024;
	char buffer[BLEN] = { 0 };
	int buffer_read_idx = 0;
	int buffer_write_idx = 0;

	packet_receive_state s = S_READ_LEN;
	double current_duration_time = 0;
	clock_t starttime = clock();
	while (true)
	{
	read_again:
		int ret = recv(accepted_fd, buffer + buffer_write_idx, BLEN - buffer_write_idx, 0);//返回接收到的字节数

		while (current_duration_time > ((double)(QPScount_idx+1))*time_span)
		{
			//10s span and count
			QPScount[QPScount_idx] = nreceived;
			if (QPScount_idx >= 1)
			{
				before_sum += QPScount[QPScount_idx - 1];
				QPScount[QPScount_idx] = QPScount[QPScount_idx] - before_sum;
			}
			cout << QPScount_idx + 1 << "-10s     total: [" << QPScount[QPScount_idx] ;
			cout << "]      average: [" << QPScount[QPScount_idx] / 10 << "]" << endl;
			QPScount_idx++;
		}

		if (ret == 0)//client disconnect
		{
			//total time and count
			cout << "current total time: [" << current_duration_time << "]    the total packet received: [" << nreceived;
			cout << "]    average: [" << int((double)nreceived / current_duration_time) <<"]"<< endl;
			cout << endl;
			closesocket(accepted_fd);
			goto again_accept;
		}
		if (ret == SOCKET_ERROR) {
			//TODO
			assert(!"todo");
		}
		assert(ret >= 0);

		buffer_write_idx += ret;
	read_content:
		switch (s)
		{
			case S_READ_LEN:
			{
				if (buffer_write_idx < sizeof(int)-1 ) {
					goto read_again;
				}
				message_len = bytes_helper::read<int>(buffer, bytes_helper::type<int>() );
				buffer_read_idx += sizeof(int);
				s = S_READ_CONTENT;
				goto read_content;
			}
			case S_READ_CONTENT:
			{
				int wrlen = buffer_write_idx - buffer_read_idx;
				if (wrlen >= message_len)
				{
					char recv_content[BLEN] = { 0 };
					memcpy(recv_content, buffer + buffer_read_idx, message_len);
					buffer_read_idx += message_len;
					wrlen = buffer_write_idx - buffer_read_idx;

					//cout << "received bytes: " << recv_content << endl;

					memcpy(buffer, buffer + buffer_read_idx, wrlen);
					buffer_read_idx = 0;
					buffer_write_idx = wrlen;
					
					//send data (answer)
					char answerbuffer[BLEN] = { 0 };
					int answerbuffer_idx = 0;
					int wlen = bytes_helper::write<int>(message_len, answerbuffer, BLEN);
					answerbuffer_idx += wlen;
					memcpy(answerbuffer + answerbuffer_idx, recv_content, message_len);
					answerbuffer_idx += message_len;
					int sendrt = send(accepted_fd,answerbuffer, answerbuffer_idx, 0);
					if (sendrt == SOCKET_ERROR) {
						assert(!"TODO");
					}
					s = S_READ_LEN;
					//cout << "reply bytes count: " << sendrt << endl;
					current_duration_time = double(clock() - starttime) / CLOCKS_PER_SEC;
					nreceived++;
					break;
				}
				else 
				{
					goto read_again;
				}
			}
			default:
			{
				assert(!"what");
			}
		}	
	}
	closesocket(ListeningSocket);
	WSACleanup();
    return 0;
}