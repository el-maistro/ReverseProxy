#pragma once

#include "../common/headers.hpp"

class ProxyCliente {
	public:
		ProxyCliente();
		bool m_Conectar(const char* _host, const char* _puerto);
		void m_LoopSession();
	private:
		WSADATA wsa;
		SOCKET sckMainSocket = INVALID_SOCKET;

		std::vector<char> readAll(SOCKET& _socket, int& _out_recibido);
		int sendAll(SOCKET& _socket, const char* _cbuffer, size_t _buff_size);
};