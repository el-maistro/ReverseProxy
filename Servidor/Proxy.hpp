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
		SOCKET m_Aceptar(SOCKET& _socket);
		std::vector<char> readAll(SOCKET& _socket, int& _out_recibido);
		int sendAll(SOCKET& _socket, const char* _cbuffer, size_t _buff_size, bool dbg = false);
		int cSend(SOCKET& _socket, const char* _cbuffer, size_t _buff_size, SOCKET _socket_local_remoto, SOCKET _socket_punto_final);

		std::vector<char> m_thS_ReadSocket(SOCKET& _socket, int& _out_recibido);
		int m_thS_WriteSocket(SOCKET& _socket, const char* _cbuffer, size_t _buff_size, SOCKET _socket_local_remoto, SOCKET _socket_punto_final);

		bool isRespuestaSegundoPaso(const std::vector<char>& _vcdata, int _recibido);

		void th_Handle_Session(SOCKET _socket_cliente_local, SOCKET _socket_proxy_remoto, SOCKET _socket_punto_final);

		std::vector<char> strParseIP(const uint8_t* addr, uint8_t addr_type);
		std::string strTestBanner();
};

