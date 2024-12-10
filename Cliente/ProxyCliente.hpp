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
		std::map<int, SOCKET> map_sockets;

		std::mutex mtx_WriteSocket;
		std::mutex mtx_MapSockets;

		SOCKET m_sckConectar(const char* _host, const char* _puerto);

		std::vector<char> readAll(SOCKET& _socket, int& _out_recibido);
		std::vector<char> readAllLocal(SOCKET& _socket, int& _out_recibido);
		int sendAll(SOCKET& _socket, const char* _cbuffer, size_t _buff_size);
		int sendAllLocal(SOCKET& _socket, const char* _cbuffer, int _buff_size);
		int cSend(SOCKET& _socket, const char* _cbuffer, size_t _buff_size, SOCKET _socket_local_remoto, SOCKET _socket_punto_final);
		int cRecv(SOCKET& _socket, std::vector<char>& _out_buffer, SOCKET& _socket_local_remoto, SOCKET& _socket_punto_final);

		int m_thS_WriteSocket(SOCKET& _socket, const char* _cbuffer, size_t _buff_size, int _id_conexion);

		//Test Map
		int cSend(SOCKET& _socket, const char* _cbuffer, size_t _buff_size, int _id_conexion);
		int cRecv(SOCKET& _socket, std::vector<char>& _out_buffer, int& _id_conexion);


		std::vector<char> SckToVCchar(SOCKET _socket);
		SOCKET VCcharToSck(const char* _cdata);
		std::vector<char> strParseIP(const uint8_t* addr, uint8_t addr_type);

		void th_Handle_Session(int _id_conexion, std::string _host);

		bool isSocksPrimerPaso(const std::vector<char>& _vcdata, int _recibido);
		bool isSocksSegundoPaso(const std::vector<char>& _vcdata, int _recibido);

		//Mapa de sockets
		SOCKET getLocalSocket(int _id);
		void addLocalSocket(int _id, SOCKET _socket);
		bool eraseLocalSocket(int _id);
		int getSocketID(SOCKET _socket);

};
