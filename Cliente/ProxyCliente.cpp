#include "ProxyCliente.hpp"

ProxyCliente::ProxyCliente() {
	WSACleanup();
	DEBUG_MSG("Iniciando configuracion...");
	if (WSAStartup(MAKEWORD(2, 2), &this->wsa) != 0) {
		DEBUG_ERR("WSAStartup error");
		return;
	}
}

bool ProxyCliente::m_ConectarServer(const char* _host, const char* _puerto) {
	struct addrinfo sAddress, * sP, * sServer;
	memset(&sAddress, 0, sizeof(sAddress));

	sAddress.ai_family = AF_UNSPEC;
	sAddress.ai_socktype = SOCK_STREAM;

	int iRes = getaddrinfo(_host, _puerto, &sAddress, &sServer);
	if (iRes != 0) {
		DEBUG_ERR("[X] getaddrinfo error");
		return false;
	}

	for (sP = sServer; sP != nullptr; sP = sP->ai_next) {
		if ((this->sckMainSocket = socket(sP->ai_family, sP->ai_socktype, sP->ai_protocol)) == INVALID_SOCKET) {
			//socket error
			continue;
		}

		if (connect(this->sckMainSocket, sP->ai_addr, sP->ai_addrlen) == -1) {
			//No se pudo conectar
			DEBUG_ERR("[X] No se pudo conectar");
			continue;
		}
		break;
	}

	if (sP == nullptr || this->sckMainSocket == INVALID_SOCKET) {
		freeaddrinfo(sServer);
		return false;
	}

	unsigned long int iBlock = 1;
	if (ioctlsocket(this->sckMainSocket, FIONBIO, &iBlock) != 0) {
		DEBUG_ERR("[X] No se pudo hacer non_block");
	}

	freeaddrinfo(sServer);

	return true;
}

void ProxyCliente::m_LoopSession() {
	size_t iTam = sizeof(SOCKET);
	DEBUG_MSG("[!] Esperando por peticion...");
	
	struct timeval timeout;
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	fd_set fdMaster;
	FD_ZERO(&fdMaster);
	FD_SET(this->sckMainSocket, &fdMaster);

	bool isConnected = true;

	while (isConnected) {
		fd_set fdMaster_Copy = fdMaster;
		
		int iNumeroSockets = select(this->sckMainSocket + 1, &fdMaster_Copy, nullptr, nullptr, &timeout);

		for (int index = 0; index < iNumeroSockets; index++) {
			SOCKET temp_socket = fdMaster_Copy.fd_array[index];

			//Datos para leer del servidor
			if (temp_socket == this->sckMainSocket) {
				int iRecibido = 0;
				std::vector<char> vcData = this->readAll(temp_socket, iRecibido);
				
				if (iRecibido > 0) {
					if (this->isPrimerPaso(vcData, iRecibido)){ 
						char cRespuesta[2];
						cRespuesta[0] = vcData[0];
						cRespuesta[1] = 0x00;

						//Extraer socket local que mando datos atraves del servidor
						SOCKET socket_local_remoto = INVALID_SOCKET;
						memcpy(&socket_local_remoto, vcData.data() + 3, sizeof(SOCKET));

						if(this->cSend(temp_socket, cRespuesta, 2, socket_local_remoto, INVALID_SOCKET) == SOCKET_ERROR) {
							DEBUG_ERR("[X] No se pudo responder al primer paso");
						}
					}else if (this->isSegundoPaso(vcData, iRecibido)){
						//Segundo paquete, informacion de conexion 
						char cHostType = vcData[3];
						std::vector<char> cHost;

						if (cHostType != 0x03) {
							//Parsear ipv4/ipv6
							const uint8_t* uAddr = reinterpret_cast<uint8_t*>(vcData.data());
							uAddr += 4;
							cHost = this->strParseIP(uAddr, cHostType);
						}else {
							size_t socketsSize = sizeof(SOCKET) * 2;
							size_t head_len = 4; // VER | CMD |  RSV  | ATYP
							size_t port_len = 2;
							size_t host_len = iRecibido - socketsSize - head_len - port_len -1;
							cHost.resize(iRecibido - socketsSize - head_len - port_len);
							memcpy(cHost.data(), vcData.data() + 5, host_len);
						}

						//Extraer puerto al cual conectarse del paquete
						u_short nPort = 0;
						int iPort = 0;
						size_t nPortOffset = iRecibido - (sizeof(SOCKET) * 2) - 2;
						memcpy(&nPort, vcData.data() + nPortOffset, 2);
						iPort = static_cast<int>(ntohs(nPort));

						//Extraer socket local que mando datos atraves del servidor
						SOCKET socket_local_remoto = INVALID_SOCKET;
						size_t nSocketLocalRemotoOffset = iRecibido - (sizeof(SOCKET) * 2);
						memcpy(&socket_local_remoto, vcData.data() + nSocketLocalRemotoOffset, sizeof(SOCKET));

						std::string strPort = std::to_string(iPort);
						
						const char* cPort      = strPort.c_str();
						const char* cFinalHost =    cHost.data();
					
						std::string strMessage = "Peticion recibida\n\tHost: ";
						strMessage += cHost.data();
						strMessage += " Puerto: ";
						strMessage += std::to_string(iPort);
						strMessage += " SOCKET: " + std::to_string(socket_local_remoto);

						DEBUG_MSG("\t" + strMessage);

						SOCKET sckPuntoFinal = this->m_sckConectar(cFinalHost, cPort);

						size_t nSize = iRecibido - (sizeof(SOCKET) * 2);

						if (sckPuntoFinal != INVALID_SOCKET) {
							vcData[1] = 0x00;

							if (this->cSend(temp_socket, vcData.data(), nSize, socket_local_remoto, sckPuntoFinal) != SOCKET_ERROR) {
								//Crear thread que leer del punto final
								std::thread th(&ProxyCliente::th_Handle_Session, this, sckPuntoFinal, socket_local_remoto);
								th.detach();
								
								DEBUG_MSG("[!] Conexion con punto final completa! SCK-REMOTO:" + std::to_string(socket_local_remoto) + " SCK-PUNTO_FINAL:" + std::to_string(sckPuntoFinal));
							}else {
								DEBUG_ERR("[X] Conexion con punto final completa pero no se pudo enviar la confirmacion al servidor. Adios...");
								closesocket(sckPuntoFinal);
								FD_CLR(temp_socket, &fdMaster_Copy);
								closesocket(temp_socket);
								isConnected = false;
								break;
							}

						}else {
							vcData[1] = 0x04;
							DEBUG_ERR("[X] No se pudo conectar al punto final");
							if (this->cSend(temp_socket, vcData.data(), nSize, socket_local_remoto, sckPuntoFinal) == SOCKET_ERROR) {
								DEBUG_ERR("[X] No se pudo enviar respuest al servidor");
								FD_CLR(temp_socket, &fdMaster_Copy);
								closesocket(temp_socket);
								isConnected = false;
								break;
							}
						}
					}else {
						//Datos para enviar al punto final
						SOCKET socket_local_remoto = INVALID_SOCKET;  // <-- Sin uso por los momentos
						SOCKET socket_punto_final  = INVALID_SOCKET;

						size_t nSize = iRecibido - int(sizeof(SOCKET) * 2);

						memcpy(&socket_local_remoto, vcData.data() + nSize, sizeof(SOCKET));
						memcpy(&socket_punto_final, vcData.data() + nSize + sizeof(SOCKET), sizeof(SOCKET));
							
						if (socket_punto_final != INVALID_SOCKET) {
							if(this->sendAll(socket_punto_final, vcData.data(), nSize) == SOCKET_ERROR) {
								DEBUG_ERR("[X] Error enviando datos al punto final");
							}
						}else {
							DEBUG_ERR("[X] No se pudo parsear el SOCKET");
						}
					}
				}else if (iRecibido == SOCKET_ERROR) {
					DEBUG_ERR("[X] Error leyendo datos del socket del servidor");
					FD_CLR(temp_socket, &fdMaster_Copy);
					closesocket(temp_socket);
					isConnected = false;
					break;
				}
			}
		}
	}
}

SOCKET ProxyCliente::m_sckConectar(const char* _host, const char* _puerto) {
	struct addrinfo sAddress, * sP, * sServer;
	memset(&sAddress, 0, sizeof(sAddress));

	SOCKET temp_socket = INVALID_SOCKET;

	sAddress.ai_family = AF_UNSPEC;
	sAddress.ai_socktype = SOCK_STREAM;

	int iRes = getaddrinfo(_host, _puerto, &sAddress, &sServer);
	if (iRes != 0) {
		DEBUG_ERR("[X] getaddrinfo error");
		return temp_socket;
	}

	for (sP = sServer; sP != nullptr; sP = sP->ai_next) {
		if ((temp_socket = socket(sP->ai_family, sP->ai_socktype, sP->ai_protocol)) == INVALID_SOCKET) {
			//socket error
			continue;
		}

		if (connect(temp_socket, sP->ai_addr, sP->ai_addrlen) == -1) {
			//No se pudo conectar
			DEBUG_ERR("[X] No se pudo conectar");
			continue;
		}
		break;
	}

	if (sP == nullptr || temp_socket == INVALID_SOCKET) {
		freeaddrinfo(sServer);
		return temp_socket;
	}

	unsigned long int iBlock = 1;
	if (ioctlsocket(temp_socket, FIONBIO, &iBlock) != 0) {
		DEBUG_ERR("[X] No se pudo hacer non_block");
	}

	freeaddrinfo(sServer);

	return temp_socket;
}

std::vector<char> ProxyCliente::readAll(SOCKET& _socket, int& _out_recibido) {
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

int ProxyCliente::sendAll(SOCKET& _socket, const char* _cbuffer, size_t _buff_size) {
	int iEnviado = 0;
	int iTotalEnviado = 0;

	while (iTotalEnviado < _buff_size) {
		iEnviado = send(_socket, _cbuffer + iTotalEnviado, _buff_size - iTotalEnviado, 0);
		if (iEnviado == 0) {
			break;
		}
		else if (iEnviado == SOCKET_ERROR) {
			int error_code = WSAGetLastError();
			if (error_code == WSAEWOULDBLOCK) {
				continue;
			}
			else if (error_code == WSAECONNRESET) {
				return SOCKET_ERROR;
			}
			else {
				return SOCKET_ERROR;
			}
		}

		iTotalEnviado += iEnviado;
	}

	return iTotalEnviado;
}

int ProxyCliente::cSend(SOCKET& _socket, const char* _cbuffer, size_t _buff_size, SOCKET _socket_local_remoto, SOCKET _socket_punto_final) {
	size_t nSize = _buff_size + (sizeof(SOCKET) * 2);
	std::vector<char> finalData(nSize);
	
	memcpy(finalData.data(), _cbuffer, _buff_size);
	memcpy(finalData.data() + _buff_size, &_socket_local_remoto, sizeof(SOCKET));
	memcpy(finalData.data() + _buff_size + sizeof(SOCKET), &_socket_punto_final, sizeof(SOCKET));

	return this->sendAll(_socket, finalData.data(), nSize);
}

std::vector<char> ProxyCliente::strParseIP(const uint8_t* addr, uint8_t addr_type) {
	int addr_size = 0;
	int iFamily = 0;
	if (addr_type == 0x01) { //IPv4
		addr_size = INET_ADDRSTRLEN;
		iFamily = AF_INET;
	}
	else if (addr_type == 0x04) { //IPv6
		addr_size = INET6_ADDRSTRLEN;
		iFamily = AF_INET6;
	}
	std::vector<char> vc_ip(addr_size);
	inet_ntop(iFamily, addr, vc_ip.data(), addr_size);

	return vc_ip;
}

void ProxyCliente::th_Handle_Session(SOCKET _socket_punto_final, SOCKET _socket_remoto) {
	// this->sckMainsocket = SOCKET servidor 
	// _socket_punto_final = SOCKET con punto final
	// socket_remoto       = SOCKET de servidor con cliente/browser
	struct timeval timeout;
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	fd_set fdMaster;
	FD_ZERO(&fdMaster);
	FD_SET(_socket_punto_final, &fdMaster);

	while (1) {
		fd_set fdMaster_Copy = fdMaster;

		int iNumeroSockets = select(_socket_punto_final + 1, &fdMaster_Copy, nullptr, nullptr, &timeout);
		for (int index = 0; index < iNumeroSockets; index++) {
			SOCKET temp_socket = fdMaster_Copy.fd_array[index];

			if (temp_socket == _socket_punto_final) {
				//Datos del punto final. Reenviar a servidor junto con informacion de SOCKETS
				int iRecibido = 0;
				std::vector<char> vcData = this->readAll(_socket_punto_final, iRecibido);
				if(iRecibido > 0){
					if(this->cSend(this->sckMainSocket, vcData.data(), iRecibido, _socket_remoto, _socket_punto_final) == SOCKET_ERROR) {
						DEBUG_ERR("[X] Error enviado respuesta del punto final");
						FD_CLR(temp_socket, &fdMaster);
						closesocket(temp_socket);
						break;
					}
				}else if (iRecibido == SOCKET_ERROR) {
					FD_CLR(_socket_punto_final, &fdMaster);
					closesocket(_socket_punto_final);
					break;
				}
			}
		}
	}

	return;
}

bool ProxyCliente::isPrimerPaso(const std::vector<char>& _vcdata, int _recibido) {
	if (_recibido != 19) {
		return false;
	}

	if (_vcdata[0] == 0x5 || _vcdata[0] == 0x04) {      // SOCKS4 o SOCKS5
		if (_vcdata[1] == 0x01 && _vcdata[2] == 0x00) { // Sin Autenticacion
			return true;
		}
	}

	return false;
}

bool ProxyCliente::isSegundoPaso(const std::vector<char>& _vcdata, int _recibido) {
	//  https://datatracker.ietf.org/doc/html/rfc1928
	/*  +----+-----+-------+------+----------+----------+
		|VER | REP |  RSV  | ATYP | BND.ADDR | BND.PORT |
		+----+-----+-------+------+----------+----------+
		| 1  |  1  | X'00' |  1   | Variable |    2     |
		+----+-----+-------+------+----------+----------+*/
	if (_recibido < 4) {
		return false;
	}

	if (_vcdata[0] == 0x05 || _vcdata[0] == 0x04) {		                          //SOCKS4 o SOCKS5
		if (   _vcdata[1] == 0x01                                                 // CMD
			&& _vcdata[2] == 0x00                                                 // RESERVADO
			&&(_vcdata[3] == 0x01 || _vcdata[3] == 0x03 || _vcdata[3] == 0x04)    // IPV4 IPV6 Domain Name
			) {
			return true;
		}
	}
	return false;
}