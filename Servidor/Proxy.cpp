#include "Proxy.hpp"

Proxy::Proxy(int _puerto)
	: iPuertoEscucha(_puerto) {
	WSACleanup();
	DEBUG_MSG("Iniciando configuracion...");
	if (WSAStartup(MAKEWORD(2, 2), &this->wsa) != 0) {
		DEBUG_ERR("WSAStartup error");
		return;
	}
	if (this->m_InitSocket(this->sckLocalSocket, 6666) && this->m_InitSocket(this->sckRemoteSocket, 7777)) {
		DEBUG_MSG("SOCKETS creados!!!");
	}else {
		DEBUG_ERR("[X]Error creando los sockets");
	}
}

bool Proxy::m_InitSocket(SOCKET& _socket, int _puerto) {
	_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (_socket == INVALID_SOCKET) {
		DEBUG_ERR("Error creando el socket");
		return false;
	}

	int iReuseAddr = 1;
	if (setsockopt(_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&iReuseAddr, sizeof(iReuseAddr)) < 0) {
		DEBUG_ERR("Error en setsockopt");
		return false;
	}

	unsigned long int iNonBlockMode = 1; //1 para activar 0 es por defecto (BLOCK_MODE)
	if (ioctlsocket(_socket, FIONBIO, &iNonBlockMode) != 0) {
		DEBUG_ERR("Error haciendo el socket non_block");
		return false;
	}

	struct sockaddr_in structServer;

	structServer.sin_family = AF_INET;
	structServer.sin_port = htons(_puerto);
	structServer.sin_addr.s_addr = INADDR_ANY;

	if (bind(_socket, (struct sockaddr*)&structServer, sizeof(struct sockaddr)) == -1) {
		DEBUG_ERR("Error en bind");
		return false;
	}

	if (listen(_socket, 10) == -1) {
		DEBUG_ERR("Error en listen");
		return false;
	}

	DEBUG_MSG("Socket configurado!!!");
	return true;
}

void Proxy::EsperarConexiones() {

	struct timeval timeout;
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;

	fd_set fdMaster;
	FD_ZERO(&fdMaster);

	if(this->sckLocalSocket == INVALID_SOCKET || this->sckRemoteSocket == INVALID_SOCKET){
		DEBUG_MSG("Los sockets no estan configurados...");
		return;
	}
	FD_SET(this->sckLocalSocket, &fdMaster);
	FD_SET(this->sckRemoteSocket, &fdMaster);
	
	SOCKET sckTemp_Remoto = INVALID_SOCKET;

	bool isRunning = true;

	while (isRunning) {
		fd_set fdMaster_copy = fdMaster;
		
		int iNumeroSocket = select(this->sckLocalSocket + 1, &fdMaster_copy, nullptr, nullptr, &timeout);

		for (int index = 0; index < iNumeroSocket; index++) {
			SOCKET temp_socket = fdMaster_copy.fd_array[index];
			
			//Conexion local entrante (browser, etc)
			if (temp_socket == this->sckLocalSocket) {
				struct sockaddr_in structCliente;
				int struct_size = sizeof(struct sockaddr_in);
				SOCKET nSocket = accept(this->sckLocalSocket, (struct sockaddr*)&structCliente, &struct_size);
				if (nSocket != INVALID_SOCKET) {
					unsigned long int iBlock = 1;
					if (ioctlsocket(nSocket, FIONBIO, &iBlock) != 0) {
						DEBUG_MSG("Error configurando el socket NON_BLOCK");
					}
					DEBUG_MSG("Conexion local aceptada");
					std::thread th_new = std::thread(&Proxy::th_Handle_Session, this, nSocket, sckTemp_Remoto);
					th_new.detach();
				}
			}else if (temp_socket == this->sckRemoteSocket) {
				//Conexion remota del cliente
				struct sockaddr_in structCliente;
				int struct_size = sizeof(struct sockaddr_in);
				SOCKET nSocket = accept(this->sckRemoteSocket, (struct sockaddr*)&structCliente, &struct_size);
				if (nSocket != INVALID_SOCKET) {
					unsigned long int iBlock = 1;
					if (ioctlsocket(nSocket, FIONBIO, &iBlock) != 0) {
						DEBUG_MSG("Error configurando el socket NON_BLOCK");
					}
					DEBUG_MSG("Conexion remota aceptada");

					sckTemp_Remoto = nSocket;

					FD_SET(nSocket, &fdMaster);

					//Eliminar socket del FD ya que solo se necesita una conexion remota con el proxy?
					FD_CLR(this->sckRemoteSocket, &fdMaster);
					closesocket(this->sckRemoteSocket);
				}
			}else {
				//Datos del proxy remoto
				int iRecibido = 0;
				std::vector<char> vcData = this->readAll(temp_socket, iRecibido);
				if (iRecibido > 0) {
					DEBUG_MSG(vcData.data());
					//Parsear SOCKET a escribir y datos


				}else if (iRecibido == SOCKET_ERROR) {
					DEBUG_ERR("Error leyendo datos del proxy remoto. Reiniciando...");
					FD_CLR(temp_socket, &fdMaster);
					closesocket(temp_socket);
					
					//Reiniciar el socket y agregarlo al FD
					if (this->m_InitSocket(this->sckRemoteSocket, 7777)) {
						if (this->sckRemoteSocket != INVALID_SOCKET) {
							FD_SET(this->sckRemoteSocket, &fdMaster);
							DEBUG_MSG("[!] Se reinicio correctamente");
						}else {
							DEBUG_MSG("[X] El socket remoto es invalido");
							isRunning = false;
							break;
						}
					}else {
						DEBUG_ERR("[X] No se pudo reiniciar el socket del proxy remoto");
						isRunning = false;
						break;
					}
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
	int iRetrys = 5;
	while (1) {
		char cTempBuffer[1024];
		int iRecibido = recv(_socket, cTempBuffer, iChunk, 0);
		if (iRecibido == 0) {
			break;
		}else if (iRecibido == SOCKET_ERROR) {
			int error_wsa = WSAGetLastError();
			if (error_wsa == WSAEWOULDBLOCK) {
				if (iRetrys-- > 5) {
					DEBUG_MSG("[!] Intento lectura...");
					continue;
				}
			}
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

std::vector<char> Proxy::m_thS_ReadSocket(SOCKET& _socket, int& _out_recibido) {
	std::unique_lock<std::mutex> lock(this->mtx_RemoteProxy_Read);
	return this->readAll(_socket, _out_recibido);
}

int Proxy::m_thS_WriteSocket(SOCKET& _socket, const char* _cbuffer, size_t _buff_size) {
	std::unique_lock<std::mutex> lock(this->mtx_RemoteProxy_Write);
	return this->sendAll(_socket, _cbuffer, _buff_size);
}

void Proxy::th_Handle_Session(SOCKET _socket, SOCKET _socket_remoto) {
	std::string strPre = "SCK[" + std::to_string(_socket) + "]";
	
	bool isRunning = true;
	bool isHandShakeDone = false;
	struct timeval timeout;
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	SOCKET sckPuntoFinal = INVALID_SOCKET;

	fd_set fdClienteMaster;
	FD_ZERO(&fdClienteMaster);
	FD_SET(_socket, &fdClienteMaster);
	while (isRunning) {
		fd_set fdMaster_copy = fdClienteMaster;
		
		int iNumeroSocket = select(_socket + 1, &fdMaster_copy, nullptr, nullptr, &timeout);

		for (int index = 0; index < iNumeroSocket; index++) {
			SOCKET temp_socket = fdMaster_copy.fd_array[index];
			if (temp_socket == _socket) {
				int iRecibido = 0;
				std::vector<char> vcData = this->readAll(temp_socket, iRecibido);
				if (iRecibido > 0) {
					if (!isHandShakeDone) {
						if (iRecibido == 3 && 
							((vcData[0] == 0x05 || vcData[0] == 0x04)      //SOCKS4 o SOCKS5 
							&& vcData[1] == 0x01 && vcData[2] == 0x00)) {  //SIN AUTENTICACION
							
							char cPaquete[2];
							cPaquete[0] = vcData[0];
							cPaquete[1] = 0x00;
							int iEnviado = this->sendAll(temp_socket, cPaquete, 2);
							if (iEnviado != 2) {
								DEBUG_ERR(strPre + "[X] No se pudo enviar la confirmacion del primer paso");
							}
						}else if (iRecibido > 4 && 
								((vcData[0] == 0x05 || vcData[0] == 0x04)   //SOCKS4 o SOCKS5 
							   && vcData[1] == 0x01 && vcData[2] == 0x00 && //CMD | RESERV
									(vcData[3] == 0x01 ||                   //IPv4
									 vcData[3] == 0x03 ||                   //DOMAIN NAME
									 vcData[3] == 0x04))) {                 //IPv6
							
							char cHostType = vcData[3];
							std::vector<char> cHost;
							
							if (cHostType != 0x03) {
								//Parsear ipv4/ipv6
								const uint8_t* uAddr = reinterpret_cast<uint8_t*>(vcData.data());
								uAddr += 4;
								cHost = this->strParseIP(uAddr, cHostType);
							} else {
								size_t host_len = iRecibido - 7;
								cHost.resize(iRecibido - 6);
								memcpy(cHost.data(), vcData.data() + 5, host_len);
							}

							u_short nPort = 0;
							int iPort = 0;
							memcpy(&nPort, vcData.data() + (iRecibido - 2), 2);

							iPort = static_cast<int>(ntohs(nPort));

							std::string strMessage = "Host: ";
							strMessage += cHost.data();
							strMessage += " Puerto: ";
							strMessage += std::to_string(iPort);
							DEBUG_MSG(strPre + strMessage);

							//Si conecta se responde con la misma secuencia de bytes modificando el resultado de la conexion
							/*  https://datatracker.ietf.org/doc/html/rfc1928
								+----+-----+-------+------+----------+----------+
								|VER | REP |  RSV  | ATYP | BND.ADDR | BND.PORT |
								+----+-----+-------+------+----------+----------+
								| 1  |  1  | X'00' |  1   | Variable |    2     |
								+----+-----+-------+------+----------+----------+*/
							
							//########################################################
							// Enviar informacion de conexion al cliente remoto y
							// esperar confirmacion de conexion
							//########################################################
							std::string strPaquete = std::to_string(temp_socket);
							strPaquete.append(1, '|');
							strPaquete += cHost.data();
							strPaquete.append(1, '|');
							strPaquete += std::to_string(iPort);

							//  SOCKET | HOST | PUERTO
							size_t iPaqueteSize = strPaquete.size();
							int iEnviado = this->m_thS_WriteSocket(_socket_remoto, strPaquete.c_str(), iPaqueteSize);
							if (iEnviado != iPaqueteSize) {
								DEBUG_ERR("[X] No se pudo enviar todo el paquete al proxy remoto")
							}
							//std::string strTemp = std::to_string(iPort);
							//const char* cPort = strTemp.c_str();
							//const char* cFinalHost = cHost.data();

							//sckPuntoFinal = this->m_Conectar(cFinalHost, cPort);

							//if (sckPuntoFinal != INVALID_SOCKET) {
							//	vcData[1] = 0x00;
							//	FD_SET(sckPuntoFinal, &fdClienteMaster);
							//	DEBUG_MSG(strPre + "[!] Conexion con host final completada");
							//}else {
							//	vcData[1] = 0x04; //No se pudo conectar
							//	DEBUG_ERR(strPre + "[X] No se pudo conectar con el host final");
							//}

							//int iEnviado = this->sendAll(temp_socket, vcData.data(), iRecibido);

							//if (iEnviado == iRecibido) {
							//	isHandShakeDone = true;
							//}else {
							//	DEBUG_ERR(strPre + "[X] Ocurrio un error en el paso final");
							//}
							//########################################################
							//########################################################

						}
					}else {
						//Ya se completo el handshake manejar datos aqui
						//El proceso de negociacion esta completo y el cliente esta haciendo un request
						
						//########################################################
						//   Enviar request del cliente local al cliente remoto //
						//########################################################
						if (sckPuntoFinal != INVALID_SOCKET) {
							int iEnviado = this->sendAll(sckPuntoFinal, vcData.data(), iRecibido);
							if (iEnviado != iRecibido) {
								DEBUG_ERR(strPre + "[X] No se pudo reenviar los datos al punto final");
							}
						}else {
							DEBUG_MSG(strPre + "[X] Conexion con punto final no establecida...");
						}
						//########################################################
						//########################################################

					}
				}else if (iRecibido == SOCKET_ERROR) {
					closesocket(temp_socket);
					FD_CLR(temp_socket, &fdClienteMaster);
					isRunning = false;
				}
			} else if(temp_socket == sckPuntoFinal){
				//Respuesta del punto final reenviar al main socket
				int iRecibido = 0;
				
				std::vector<char> vcData = this->readAll(temp_socket, iRecibido);
				if (iRecibido > 0) {
					int iEnviado = this->sendAll(_socket, vcData.data(), iRecibido);
					if (iEnviado != iRecibido) {
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
}

std::vector<char> Proxy::strParseIP(const uint8_t* addr, uint8_t addr_type) {
	int addr_size = 0;
	int iFamily = 0;
	if (addr_type == 0x01) { //IPv4
		addr_size = INET_ADDRSTRLEN;
		iFamily = AF_INET;
	}else if (addr_type == 0x04) { //IPv6
		addr_size = INET6_ADDRSTRLEN;
		iFamily = AF_INET6;
	}
	std::vector<char> vc_ip(addr_size);
	inet_ntop(iFamily, addr, vc_ip.data(), addr_size);

	return vc_ip;
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