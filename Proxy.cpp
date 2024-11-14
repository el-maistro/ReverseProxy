#include "Proxy.hpp"

Proxy::Proxy(int _puerto)
	: iPuertoEscucha(_puerto) {
	WSACleanup();
	DEBUG_MSG("Iniciando configuracion...");
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		DEBUG_ERR("WSAStartup error");
		return;
	}

	this->sckListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (this->sckListenSocket == INVALID_SOCKET) {
		DEBUG_ERR("Error creando el socket");
		return;
	}

	int iReuseAddr = 1;
	if (setsockopt(this->sckListenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&iReuseAddr, sizeof(iReuseAddr)) < 0) {
		DEBUG_ERR("Error en setsockopt");
		return;
	}

	unsigned long int iNonBlockMode = 1; //1 para activar 0 es por defecto (BLOCK_MODE)
	if (ioctlsocket(this->sckListenSocket, FIONBIO, &iNonBlockMode) != 0) {
		DEBUG_ERR("Error haciendo el socket non_block");
		return;
	}

	this->structServer.sin_family = AF_INET;
	this->structServer.sin_port = htons(this->iPuertoEscucha);
	this->structServer.sin_addr.s_addr = INADDR_ANY;

	if (bind(this->sckListenSocket, (struct sockaddr*)&this->structServer, sizeof(struct sockaddr)) == -1) {
		DEBUG_ERR("Error en bind");
		return;
	}

	if (listen(this->sckListenSocket, 10) == -1) {
		DEBUG_ERR("Error en listen");
		return;
	}

	DEBUG_MSG("Servidor configurado!!!");
}

void Proxy::EsperarConexiones() {

	struct timeval timeout;

	
	fd_set fdMaster;
	FD_ZERO(&fdMaster);

	FD_SET(this->sckListenSocket, &fdMaster);

	while (1) {
		Sleep(100);
		fd_set fdMaster_copy = fdMaster;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		int iNumeroSocket = select(this->sckListenSocket + 1, &fdMaster_copy, nullptr, nullptr, &timeout);

		for (int index = 0; index < iNumeroSocket; index++) {
			SOCKET temp_socket = fdMaster_copy.fd_array[index];
			
			//Conexion entrante
			if (temp_socket == this->sckListenSocket) {
				struct sockaddr_in structCliente;
				int struct_size = sizeof(struct sockaddr_in);
				SOCKET nSocket = accept(this->sckListenSocket, (struct sockaddr*)&structCliente, &struct_size);
				if (nSocket != INVALID_SOCKET) {
					unsigned long int iBlock = 1;
					if (ioctlsocket(nSocket, FIONBIO, &iBlock) != 0) {
						DEBUG_MSG("Error configurando el socket NON_BLOCK");
					}
					DEBUG_MSG("Conexion aceptada");
					FD_SET(nSocket, &fdMaster);
				}
			}else {
				
				int iRecibido = 0;
				std::vector<char> vcData = this->readAll(temp_socket, iRecibido);
				if (iRecibido > 0) {
					int t = 2;
					if (iRecibido == 3 && (vcData[0] == 0x05 && vcData[1] == 0x01 && vcData[2] == 0x00)) {
						DEBUG_MSG("SOCKS5 INIT PACKET");
						char cPaquete[2];
						cPaquete[0] = 0x05;
						cPaquete[1] = 0x00;
						this->sendAll(temp_socket, cPaquete, 2);
					}else {
						if (iRecibido > 4 && (vcData[0] == 0x05 && vcData[1] == 0x01 && vcData[2] == 0x00 && vcData[3] == 0x03)) {
							DEBUG_MSG("SOCKS5 SEGUNDO PASO...");
							size_t host_len = iRecibido - 7;
							char* cHost = new char[iRecibido - 6];
							u_short nPort = 0;
							int iPort = 0;
							memcpy(cHost, vcData.data() + 5, host_len);
							memcpy(&nPort, vcData.data() + host_len + 5, 2);

							cHost[iRecibido - 7] = '\0';
							iPort = static_cast<int>(ntohs(nPort));
							
							std::string strMessage = "HOST DESTINO: ";
							strMessage += cHost;
							strMessage += " PUERTO: ";
							strMessage += std::to_string(iPort);
							DEBUG_MSG(strMessage);

							//Si conecta se responde con la misma secuencia de bytes modificando el resultado de la conexion
						/*
						* https://datatracker.ietf.org/doc/html/rfc1928
							+----+-----+-------+------+----------+----------+
							|VER | REP |  RSV  | ATYP | BND.ADDR | BND.PORT |
							+----+-----+-------+------+----------+----------+
							| 1  |  1  | X'00' |  1   | Variable |    2     |
							+----+-----+-------+------+----------+----------+

						 Where:

							  o  VER    protocol version: X'05'
							  o  REP    Reply field:
								 o  X'00' succeeded
								 o  X'01' general SOCKS server failure
								 o  X'02' connection not allowed by ruleset
								 o  X'03' Network unreachable
								 o  X'04' Host unreachable
								 o  X'05' Connection refused
								 o  X'06' TTL expired
								 o  X'07' Command not supported
								 o  X'08' Address type not supported
								 o  X'09' to X'FF' unassigned
												*/
							vcData[1] = 0x00;
							this->sendAll(temp_socket, vcData.data(), iRecibido);
							
							delete[] cHost;
							cHost = nullptr;
						}else {
							//DEBUG_MSG("INIT_DATA:");
							//DEBUG_MSG(vcData.data());
							//DEBUG_MSG("END_DATA:");
							DEBUG_MSG("SENDING DEFAULT BANNER");
							std::string strHTML = "<center><h1>PWNED!</h1></center>";
							std::string strBanner = "HTTP/1.1 200 OK\r\n"\
								"Date: Sat, 09 Dec 2025 03:10:00 GMT\r\n"\
								"Server: Apache/2.4.29 (Ubuntu)\r\n"\
								"Content-Length:"; 
							strBanner += std::to_string(strHTML.size());
							strBanner += "\r\nContent - type: text / plain\r\n\r\n";
							strBanner += strHTML;

							this->sendAll(temp_socket, strBanner.c_str(), strBanner.size());
						}
					}
				}else if (iRecibido == SOCKET_ERROR) {
					DEBUG_MSG("Cerrando cliente...");
					closesocket(temp_socket);
					FD_CLR(temp_socket, &fdMaster);
				}
			}
		}
	}

}

std::vector<char> Proxy::readAll(SOCKET& _socket, int& _out_recibido) {
	_out_recibido = SOCKET_ERROR;
	std::vector<char> vcOut;
	int iChunk = 1024;
	int iTotalRecibido = 0;
	while (1) {
		char cTempBuffer[1024];
		int iRecibido = recv(_socket, cTempBuffer, iChunk, 0);
		if (iRecibido == 0) {
			break;
		}else if (iRecibido == SOCKET_ERROR) {
			break;
		}
		for (int index = 0; index < iRecibido; index++) {
			vcOut.push_back(cTempBuffer[index]);
		}
		iTotalRecibido += iRecibido;
		_out_recibido = iTotalRecibido;
	}
	
	return vcOut;
}

int Proxy::sendAll(SOCKET& _socket, const char* _cbuffer, size_t _buff_size) {
	int iEnviado = 0;
	int iTotalEnviado = 0;

	while (iTotalEnviado < _buff_size) {
		iEnviado = send(_socket, _cbuffer + iTotalEnviado, _buff_size - iTotalEnviado, 0);
		if (iEnviado == 0) {
			break;
		}else if (iEnviado == SOCKET_ERROR) {
			int error_code = WSAGetLastError();
			if (error_code == WSAEWOULDBLOCK) {
				continue;
			}else if (error_code == WSAECONNRESET) {
				return SOCKET_ERROR;
			}else {
				return SOCKET_ERROR;
			}
		}

		iTotalEnviado += iEnviado;
	}

	return iTotalEnviado;
}