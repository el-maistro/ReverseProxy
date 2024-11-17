#pragma once
#include "../common/headers.hpp"

class Proxy{
	public:
		Proxy(int _puerto);

		void EsperarConexiones(); //Bucle principal

	private:
		std::mutex mtx_RemoteProxy_Read;
		std::mutex mtx_RemoteProxy_Write;
		WSADATA wsa;
		int iPuertoEscucha = 6666;

		SOCKET sckLocalSocket = INVALID_SOCKET;
		SOCKET sckRemoteSocket = INVALID_SOCKET;

		bool  m_InitSocket(SOCKET& _socket, int _puerto);
		SOCKET m_Conectar(const char*& _host, const char*& _puerto);
		std::vector<char> readAll(SOCKET& _socket, int& _out_recibido);
		int sendAll(SOCKET& _socket, const char* _cbuffer, size_t _buff_size);
		
		std::vector<char> m_thS_ReadSocket(SOCKET& _socket, int& _out_recibido);
		int m_thS_WriteSocket(SOCKET& _socket, const char* _cbuffer, size_t _buff_size);


		void th_Handle_Session(SOCKET _socket_local, SOCKET _socket_remoto);

		std::vector<char> strParseIP(const uint8_t* addr, uint8_t addr_type);
		std::string strTestBanner();
};

