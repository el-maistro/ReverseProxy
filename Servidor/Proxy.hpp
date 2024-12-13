#pragma once
#include "../common/headers.hpp"

class Proxy{
	public:
		Proxy(int _puerto);

		void EsperarConexiones(); //Bucle principal

	private:
		std::map<int, SOCKET> map_sockets;
		
		std::mutex mtx_RemoteProxy_Read;
		std::mutex mtx_RemoteProxy_Write;
		std::mutex mtx_MapSockets;

		WSADATA wsa;
		int iPuertoEscucha = 6666;

		SOCKET sckLocalSocket = INVALID_SOCKET;
		SOCKET sckRemoteSocket = INVALID_SOCKET;

		bool  m_InitSocket(SOCKET& _socket, int _puerto);
		SOCKET m_Conectar(const char*& _host, const char*& _puerto);
		SOCKET m_Aceptar(SOCKET& _socket);
		std::vector<char> readAll(SOCKET& _socket, int& _out_recibido);
		std::vector<char> readAllLocal(SOCKET& _socket, int& _out_recibido);
		int sendAll(SOCKET& _socket, const char* _cbuffer, int _buff_size, bool dbg = false);
		int sendAllLocal(SOCKET& _socket, const char* _cbuffer, int _buff_size, bool dbg = false);
		
		int cSend(SOCKET& _socket, const char* _cbuffer, size_t _buff_size, int _id_conexion);
		int cRecv(SOCKET& _socket, std::vector<char>& _out_buffer, int& _id_conexion);

		std::vector<char> m_thS_ReadSocket(SOCKET& _socket, int& _out_recibido);
		int m_thS_WriteSocket(SOCKET& _socket, const char* _cbuffer, size_t _buff_size, int _id_conexion);

		bool procRespuestaProxy(int _recibido, const std::vector<char>& _vcdata, SOCKET _proxy_remota, int _id_conexion, FD_SET& _fd);
		bool isRespuestaSegundoPaso(const std::vector<char>& _vcdata, int _recibido);
		void th_Handle_Session(SOCKET _socket_proxy_remoto, int _id_conexion, SOCKET _socket_local);

		//Parsing
		std::vector<char> strParseIP(const uint8_t* addr, uint8_t addr_type);
		std::string strTestBanner();

		//Mapa de sockets
		SOCKET getLocalSocket(int _id);
		void addLocalSocket(int _id, SOCKET _socket);
		bool eraseLocalSocket(int _id);
		int getSocketID(SOCKET _socket);
};

