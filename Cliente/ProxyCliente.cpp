#include "ProxyCliente.hpp"

ProxyCliente::ProxyCliente() {
	WSACleanup();
	DEBUG_MSG("Iniciando configuracion...");
	if (WSAStartup(MAKEWORD(2, 2), &this->wsa) != 0) {
		DEBUG_ERR("WSAStartup error");
		return;
	}
}

bool ProxyCliente::m_Conectar(const char* _host, const char* _puerto) {
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
	DEBUG_MSG("[!] Esperando por peticion...");
	
	struct timeval timeout;
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	fd_set fdMaster;
	FD_ZERO(&fdMaster);
	FD_SET(this->sckMainSocket, &fdMaster);

	bool isConnected = true;

	while (isConnected) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		fd_set fdMaster_Copy = fdMaster;
		
		int iNumeroSockets = select(this->sckMainSocket + 1, &fdMaster_Copy, nullptr, nullptr, &timeout);

		for (int index = 0; index < iNumeroSockets; index++) {
			SOCKET temp_socket = fdMaster_Copy.fd_array[index];

			//Datos para leer
			if (temp_socket == this->sckMainSocket) {
				int iRecibido = 0;
				std::vector<char> vcData = this->readAll(temp_socket, iRecibido);
				
				if (iRecibido > 0) {
					if (iRecibido == 3 &&
						((vcData[0] == 0x05 || vcData[0] == 0x04) && //SOCKS4 o SOCKS5
						vcData[1] == 0x01 && vcData[2] == 0x00)){     //SIN AUTENTICACION
						//Paquete inicial

						char cRespuesta[2];
						cRespuesta[0] = vcData[0];
						cRespuesta[1] = 0x00;

						int iEnviado = this->sendAll(temp_socket, cRespuesta, 2);

						if (iEnviado != 2) {
							DEBUG_ERR("[X] No se pudo responder al primer paso");
						}
					}else if (iRecibido > 4 &&
							  (vcData[0] == 0x05 || vcData[0] == 0x04) &&						//SOCKS4 o SOCKS5
						       vcData[1] == 0x01 && vcData[2] == 0x00  &&						//CMD   | RESERVADO
						      (vcData[3] == 0x01 || vcData[3] == 0x03 || vcData[3] == 0x04)) {  // IPV4 IPV6 Domain Name
						//Segundo paquete, informacion de conexion 
						//  https://datatracker.ietf.org/doc/html/rfc1928
						/*  +----+-----+-------+------+----------+----------+
							|VER | REP |  RSV  | ATYP | BND.ADDR | BND.PORT |
							+----+-----+-------+------+----------+----------+
							| 1  |  1  | X'00' |  1   | Variable |    2     |
							+----+-----+-------+------+----------+----------+*/
					
						char cHostType = vcData[3];
						std::vector<char> cHost;

						if (cHostType != 0x03) {
							//Parsear ipv4/ipv6
							const uint8_t* uAddr = reinterpret_cast<uint8_t*>(vcData.data());
							uAddr += 4;
							cHost = this->strParseIP(uAddr, cHostType);
						}else {
							size_t host_len = iRecibido - sizeof(SOCKET) - 7;
							cHost.resize(iRecibido - 6 - sizeof(SOCKET));
							memcpy(cHost.data(), vcData.data() + 5, host_len);
						}

						u_short nPort = 0;
						int iPort = 0;
						memcpy(&nPort, vcData.data() + (iRecibido - sizeof(SOCKET) - 2), 2);

						iPort = static_cast<int>(ntohs(nPort));

						SOCKET socket_remoto = INVALID_SOCKET;
						memcpy(&socket_remoto, vcData.data() + (iRecibido - sizeof(SOCKET)), sizeof(SOCKET));

						std::string strPort = std::to_string(iPort);
						std::string strMessage = "Peticion recibida\n\tHost: ";
						strMessage += cHost.data();
						strMessage += " Puerto: ";
						strMessage += std::to_string(iPort);
						strMessage += " SOCKET: " + std::to_string(socket_remoto);
						DEBUG_MSG(strMessage);

						const char* cPort      = strPort.c_str();
						const char* cFinalHost =    cHost.data();

						SOCKET sckPuntoFinal = this->m_Conectar(cFinalHost, cPort);
						if (sckPuntoFinal != INVALID_SOCKET) {
							//Crear thread que leera del punto final
							vcData[1] = 0x00;

							size_t nSize = iRecibido + sizeof(SOCKET);
							vcData.resize(nSize);
							memcpy(vcData.data() + iRecibido, &sckPuntoFinal, sizeof(SOCKET));

							this->sendAll(temp_socket, vcData.data(), nSize);

							std::thread th(&ProxyCliente::th_Handle_Session, this, sckPuntoFinal, socket_remoto);
							th.detach();
						}else {
							vcData[1] = 0x00;
							DEBUG_ERR("[X] No se pudo conectar al punto final");
							this->sendAll(temp_socket, vcData.data(), iRecibido);
						}
					}else {
						//Datos para enviar al punto final
					}
				}else if (iRecibido == SOCKET_ERROR) {
					DEBUG_ERR("[X] Error leyendo datos del socket");
					FD_CLR(temp_socket, &fdMaster_Copy);
					isConnected = false;
					break;
				}
			}
		}
	}
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
		}
		else if (iRecibido == SOCKET_ERROR) {
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

void ProxyCliente::th_Handle_Session(SOCKET _socket_local, SOCKET socket_remoto) {
	return;
}