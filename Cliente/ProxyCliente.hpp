#pragma once

#include "../common/headers.hpp"

class ProxyCliente {
	public:
		ProxyCliente();
		bool m_ConectarServer(const char* _host, const char* _puerto);
		void m_LoopSession();
	private:
		WSADATA wsa;
		SOCKET sckMainSocket = INVALID_SOCKET;

		SOCKET m_sckConectar(const char* _host, const char* _puerto);

		std::vector<char> readAll(SOCKET& _socket, int& _out_recibido);
		int sendAll(SOCKET& _socket, const char* _cbuffer, size_t _buff_size);

		std::vector<char> strParseIP(const uint8_t* addr, uint8_t addr_type);

		void th_Handle_Session(SOCKET _socket_local, SOCKET _socket_remoto);

		bool isPrimerPaso(const std::vector<char>& _vcdata, int _recibido);
		bool isSegundoPaso(const std::vector<char>& _vcdata, int _recibido);

};
