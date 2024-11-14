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
					//FD_SET(nSocket, &fdMaster);
					std::thread th_new = std::thread(&Proxy::th_Handle_Session, this, nSocket);
					th_new.detach();
				}
			}
		}
	}

}

SOCKET Proxy::m_Conectar(const char*& _host, const char*& _puerto) {
	SOCKET sckReturn = INVALID_SOCKET;

	struct addrinfo sAddress, *sP, *sServer;
	memset(&sAddress, 0, sizeof(sAddress));

	sAddress.ai_family = AF_UNSPEC;
	sAddress.ai_socktype = SOCK_STREAM;

	int iRes = getaddrinfo(_host, _puerto, &sAddress, &sServer);
	if (iRes != 0) {
		DEBUG_ERR("[X] getaddrinfo error");
		return false;
	}

	for (sP = sServer; sP != nullptr; sP = sP->ai_next) {
		if ((sckReturn = socket(sP->ai_family, sP->ai_socktype, sP->ai_protocol)) == INVALID_SOCKET) {
			//socket error
			continue;
		}

		if (connect(sckReturn, sP->ai_addr, sP->ai_addrlen) == -1) {
			//No se pudo conectar
			DEBUG_ERR("[X] No se pudo conectar");
			continue;
		}
		break;
	}

	if (sP == nullptr || sckReturn == INVALID_SOCKET) {
		freeaddrinfo(sServer);
		return INVALID_SOCKET;
	}

	unsigned long int iBlock = 1;
	if (ioctlsocket(sckReturn, FIONBIO, &iBlock) != 0) {
		DEBUG_ERR("[X] No se pudo hacer non_block");
	}

	freeaddrinfo(sServer);

	return sckReturn;
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

void Proxy::th_Handle_Session(SOCKET _socket) {
	std::string strPre = "[" + std::to_string(_socket) + "]";
	DEBUG_MSG(strPre + "[!] th_Handle_Session creada...");
	//loop de leer y enviar datos al cliente
	bool isRunning = true;
	bool isHandShakeDone = false;
	struct timeval timeout;

	SOCKET sckPuntoFinal = INVALID_SOCKET;

	fd_set fdClienteMaster;
	FD_ZERO(&fdClienteMaster);
	FD_SET(_socket, &fdClienteMaster);
	while (isRunning) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		
		fd_set fdMaster_copy = fdClienteMaster;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		int iNumeroSocket = select(_socket + 1, &fdMaster_copy, nullptr, nullptr, &timeout);

		for (int index = 0; index < iNumeroSocket; index++) {
			SOCKET temp_socket = fdMaster_copy.fd_array[index];
			if (temp_socket == _socket) {
				int iRecibido = 0;
				std::vector<char> vcData = this->readAll(temp_socket, iRecibido);
				if (iRecibido > 0) {
					if (!isHandShakeDone) {
						if (iRecibido == 3 && (vcData[0] == 0x05 && vcData[1] == 0x01 && vcData[2] == 0x00)) {
							DEBUG_MSG(strPre + "[1] SOCKS5 Paquete inicial...");
							char cPaquete[2];
							cPaquete[0] = 0x05;
							cPaquete[1] = 0x00;
							int iEnviado = this->sendAll(temp_socket, cPaquete, 2);
							if (iEnviado != 2) {
								DEBUG_ERR(strPre + "[X] No se pudo enviar la confirmacion del primer paso");
							}else {
								DEBUG_MSG(strPre + "[!]Completo!");
							}
						}else if (iRecibido > 4 && (vcData[0] == 0x05 && vcData[1] == 0x01 && vcData[2] == 0x00 && vcData[3] == 0x03)) {
							DEBUG_MSG(strPre + "[2] SOCKS5 Segundo paso...");
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
							DEBUG_MSG(strPre + strMessage);

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
							
							std::string strTemp = std::to_string(iPort);
							const char* cPort = strTemp.c_str();
							const char* cFinalHost = cHost;

							sckPuntoFinal = this->m_Conectar(cFinalHost, cPort);

							if (sckPuntoFinal != INVALID_SOCKET) {
								vcData[1] = 0x00;
								FD_SET(sckPuntoFinal, &fdClienteMaster);
								DEBUG_MSG(strPre + "[!] Conexion con host final completada");
							}else {
								vcData[1] = 0x04; //No se pudo conectar
								DEBUG_ERR(strPre + "[X] No se pudo conectar con el host final");
							}

							int iEnviado = this->sendAll(temp_socket, vcData.data(), iRecibido);

							delete[] cHost;
							cHost = nullptr;

							if (iEnviado == iRecibido) {
								DEBUG_MSG(strPre + "[!] Negociacion completa!");
								isHandShakeDone = true;
							}else {
								DEBUG_ERR(strPre + "[X] Ocurrio un error en el paso final");
							}
						}
					}else {
						//Ya se completo el handshake manejar datos aqui
						//El proceso de negociacion esta completo y el cliente esta haciendo un request
						if (sckPuntoFinal != INVALID_SOCKET) {
							int iEnviado = this->sendAll(sckPuntoFinal, vcData.data(), iRecibido);
							if (iEnviado == iRecibido) {
								DEBUG_MSG(strPre + "[>>>] Reenvio de datos completo");
							}else {
								DEBUG_ERR(strPre + "[X] No se pudo reenviar los datos");
							}
						}else {
							DEBUG_MSG(strPre + "[X] Conexion con punto final no establecida...");
						}
					}
				}else if (iRecibido == SOCKET_ERROR) {
					DEBUG_MSG(strPre + "Cerrando cliente...");
					closesocket(temp_socket);
					FD_CLR(temp_socket, &fdClienteMaster);
					isRunning = false;
				}
			} else if(temp_socket == sckPuntoFinal){
				//Respuesta del punto final
				//Reenviar al main socket
				DEBUG_MSG(strPre + "[!] Respuesta de punto final:");
				int iRecibido = 0;
				std::vector<char> vcData = this->readAll(temp_socket, iRecibido);
				if (iRecibido > 0) {
					//Reenviar al socket principal
					int iEnviado = this->sendAll(_socket, vcData.data(), iRecibido);
					if (iEnviado == iRecibido) {
						DEBUG_MSG(strPre + "[<<<] Reenvio completo!");
					}else {
						DEBUG_ERR(strPre + "[X] No se pudo reenviar los datos del punto final");
					}
				}else if (iRecibido == SOCKET_ERROR) {
					DEBUG_ERR(strPre + "[X] No se pudo recibir datos del host final");
					closesocket(temp_socket);
					FD_CLR(temp_socket, &fdClienteMaster);
					isRunning = false;
				}
			}

		}	
	}

	if (FD_ISSET(_socket, &fdClienteMaster)) {
		FD_CLR(_socket, &fdClienteMaster);
	}
	if (FD_ISSET(sckPuntoFinal, &fdClienteMaster)) {
		FD_CLR(sckPuntoFinal, &fdClienteMaster);
	}

	DEBUG_MSG(strPre + "[!] th_Handle_Session terminada...");
}

std::string Proxy::strTestBanner() {
	std::string strHTML = "<br><br><center><h1>Error conectando con el host final</h1></center>";
	std::string strBanner = "HTTP/1.1 200 OK\r\n"\
		"Date: Sat, 10 Jan 2011 03:10:00 GMT\r\n"\
		"Server: Tanuki/1.0\r\n"\
		"Content-Length:";
	strBanner += std::to_string(strHTML.size());
	strBanner += "\r\nContent - type: text / plain\r\n\r\n";
	strBanner += strHTML;

	return strBanner;
}