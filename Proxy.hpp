#pragma once
#include "headers.hpp"

class Proxy{
	public:
		Proxy(int _puerto);

		void EsperarConexiones(); //Bucle principal

	private:
		WSADATA wsa;
		int iPuertoEscucha = 6666;

		SOCKET sckListenSocket = INVALID_SOCKET;
		struct sockaddr_in structServer;

		SOCKET m_Conectar(const char*& _host, const char*& _puerto);
		std::vector<char> readAll(SOCKET& _socket, int& _out_recibido);
		int sendAll(SOCKET& _socket, const char* _cbuffer, size_t _buff_size);

		void th_Handle_Session(SOCKET _socket);

		std::string strTestBanner();
};

