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

SOCKET Proxy::m_Aceptar(SOCKET& _socket) {
	struct sockaddr_in structCliente;
	int struct_size = sizeof(struct sockaddr_in);
	SOCKET nSocket = accept(_socket, (struct sockaddr*)&structCliente, &struct_size);
	if (nSocket != INVALID_SOCKET) {
		unsigned long int iBlock = 1;
		if (ioctlsocket(nSocket, FIONBIO, &iBlock) != 0) {
			DEBUG_MSG("Error configurando el socket NON_BLOCK");
		}
	}

	return nSocket;
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
	
	SOCKET sckTemp_Proxy_Remota = INVALID_SOCKET;

	bool isRunning = true;

	while (isRunning) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		fd_set fdMaster_copy = fdMaster;
		
		int iNumeroSocket = select(this->sckLocalSocket + 1, &fdMaster_copy, nullptr, nullptr, &timeout);

		for (int index = 0; index < iNumeroSocket; index++) {
			SOCKET temp_socket = fdMaster_copy.fd_array[index];
			
			if (temp_socket == this->sckLocalSocket) {
				//Conexion local entrante (browser, etc)

				SOCKET nSocketLocal = this->m_Aceptar(this->sckLocalSocket);

				if (nSocketLocal != INVALID_SOCKET) {
					if (sckTemp_Proxy_Remota == INVALID_SOCKET) {
						//DEBUG_MSG("[!] El cliente/proxy-remota no se ha conectado. Rechazando conexion");
						closesocket(nSocketLocal);
						continue;
					}
					
					FD_SET(nSocketLocal, &fdMaster);

					//DEBUG_MSG("Conexion local aceptada " + std::to_string(nSocketLocal));
				}

			}else if (temp_socket == this->sckRemoteSocket) {
				//Conexion remota del proxy
				
				SOCKET nSocketProxyRemota = this->m_Aceptar(this->sckRemoteSocket);
				if (nSocketProxyRemota != INVALID_SOCKET) {
					
					DEBUG_MSG("Conexion de proxy remota aceptada");

					sckTemp_Proxy_Remota = nSocketProxyRemota;

					FD_SET(nSocketProxyRemota, &fdMaster);

					//Eliminar socket del FD ya que solo se necesita una conexion remota con el proxy?
					FD_CLR(this->sckRemoteSocket, &fdMaster);
					closesocket(this->sckRemoteSocket);
				}

			}else if(temp_socket == sckTemp_Proxy_Remota){
				//Datos del proxy remoto
				SOCKET socket_local_remoto = INVALID_SOCKET;
				SOCKET socket_punto_final  = INVALID_SOCKET;
				std::vector<char> vcData;
				int iRecibido = this->cRecv(sckTemp_Proxy_Remota, vcData, socket_local_remoto, socket_punto_final);

				if (iRecibido > 0) {
					//Crear thread con puerto local y remoto
					// remover socket local del FD en este thread
					
					if (socket_local_remoto != INVALID_SOCKET && socket_punto_final != INVALID_SOCKET) {
						//crear thread con los dos sockets
						if (this->isRespuestaSegundoPaso(vcData, iRecibido)) {
							if (FD_ISSET(socket_local_remoto, &fdMaster)) {
								FD_CLR(socket_local_remoto, &fdMaster);
							}
							std::thread th = std::thread(&Proxy::th_Handle_Session, this, socket_local_remoto, sckTemp_Proxy_Remota, socket_punto_final);
							th.detach();
							//DEBUG_MSG("[!] Conexion con punto final completa");
						}
						
						if(this->sendAllLocal(socket_local_remoto, vcData.data(), iRecibido) == SOCKET_ERROR) {
							DEBUG_ERR("[X][0] Error enviado respuesta de proxy remota a cliente local");
						}
						
					}else if(socket_punto_final == INVALID_SOCKET && socket_local_remoto != INVALID_SOCKET) {
						//Aun no se ha conectado al host final en la proxy remota, reenviar datos al puerto local
						if(this->sendAllLocal(socket_local_remoto, vcData.data(), iRecibido) == SOCKET_ERROR) {
							DEBUG_ERR("[X][1] Error enviado respuesta de proxy remota a cliente local");
						}
					}else {
						DEBUG_MSG("[X] No se pudo parsear los sockets remoto y local");
					}

				}else if (iRecibido == SOCKET_ERROR) {
					DEBUG_ERR("Error leyendo datos del proxy remoto. Reiniciando servidor...");

					std::this_thread::sleep_for(std::chrono::milliseconds(1000));

					FD_CLR(sckTemp_Proxy_Remota, &fdMaster);
					closesocket(sckTemp_Proxy_Remota);
					sckTemp_Proxy_Remota = INVALID_SOCKET;
					
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
				}else {
					DEBUG_MSG("[!] Paquete muy pequeno: " + std::to_string(iRecibido));
				}
			}else {
				//Datos del navegador/cliente local
				//Al recibir confirmacion del servidor esto no se evalua ya que se remueve el socket del FD

				int iRecibido = 0;

				std::vector<char> vcData = this->readAllLocal(temp_socket, iRecibido);
				if (iRecibido > 0) {
					if( this->m_thS_WriteSocket(sckTemp_Proxy_Remota, vcData.data(), iRecibido, temp_socket, INVALID_SOCKET) == SOCKET_ERROR) {
						DEBUG_ERR("[X] Error reenviando el paquete al proxy remoto");
						FD_CLR(temp_socket, &fdMaster);
						closesocket(temp_socket);
					} 
				}else if (iRecibido == SOCKET_ERROR) {
					DEBUG_ERR("[X] Error leyendo datos del navegador/cliente local");
					FD_CLR(temp_socket, &fdMaster);
					closesocket(temp_socket);
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
	int iTotalRecibido = 0;
	int iRetrys = 5;
	int iRecibido = 0;

	//Leer un entero para determinar el tam del paquete;
	char cbuffsize[sizeof(int)];
	int itam_recibido = recv(_socket, cbuffsize, sizeof(int), 0);
	if (itam_recibido == sizeof(int)) {
		memcpy(&itam_recibido, cbuffsize, sizeof(int));
		if (itam_recibido > 0) {
			vcOut.resize(itam_recibido);
			while (iTotalRecibido < itam_recibido) {
				iRecibido = recv(_socket, vcOut.data()+iTotalRecibido, itam_recibido - iTotalRecibido, 0);
				if (iRecibido == 0) {
					//Se cerro el socket
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
				iTotalRecibido += iRecibido;
				_out_recibido = iTotalRecibido;
			}
		}else {
			DEBUG_MSG("\n\t[RECV] Error leyendo entero: " + std::to_string(itam_recibido));
		}
	}else {
		DEBUG_MSG("\n\t[RECV] No se pudo leer el tamanio del buffer");
		return vcOut;
	}
	
	return vcOut;
}

std::vector<char> Proxy::readAllLocal(SOCKET& _socket, int& _out_recibido) {
	_out_recibido = SOCKET_ERROR;
	std::vector<char> vcOut;
	int iChunk = 1024;
	int iTotalRecibido = 0;
	int iRetrys = 5;
	int iRecibido = 0;
	while (1) {
		char cTempBuffer[1024];
		iRecibido = recv(_socket, cTempBuffer, iChunk, 0);
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

		iTotalRecibido += iRecibido;
		
		vcOut.resize(iTotalRecibido);

		memcpy(vcOut.data() + (iTotalRecibido - iRecibido), cTempBuffer, iRecibido);

		_out_recibido = iTotalRecibido;
	}

	return vcOut;
}

int Proxy::sendAll(SOCKET& _socket, const char* _cbuffer, int _buff_size, bool dbg) {
	int iEnviado = 0;
	int iTotalEnviado = 0;

	if (dbg) {
		int _DEBUG_BREAK_DUMMY = 0;
		int _DEBUG_BREAK_DUMMY_2 = 0;
	}

	std::vector<char> cFinalBuffer(_buff_size + sizeof(int));
	memcpy(cFinalBuffer.data(), &_buff_size, sizeof(int));
	memcpy(cFinalBuffer.data() + sizeof(int), _cbuffer, _buff_size);

	_buff_size += sizeof(int);

	while (iTotalEnviado < _buff_size) {
		iEnviado = send(_socket, cFinalBuffer.data() + iTotalEnviado, _buff_size - iTotalEnviado, 0);
		if (iEnviado == 0) {
			break;
		}else if (iEnviado == SOCKET_ERROR) {
			int error_code = WSAGetLastError();
			if (error_code == WSAEWOULDBLOCK) {
				continue;
			} else{
				return SOCKET_ERROR;
			}
		}

		iTotalEnviado += iEnviado;
	}

	return iTotalEnviado;
}

int Proxy::sendAllLocal(SOCKET& _socket, const char* _cbuffer, int _buff_size, bool dbg) {
	int iEnviado = 0;
	int iTotalEnviado = 0;

	if (dbg) {
		int _DEBUG_BREAK_DUMMY = 0;
		int _DEBUG_BREAK_DUMMY_2 = 0;
	}

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
			else {
				return SOCKET_ERROR;
			}
		}

		iTotalEnviado += iEnviado;
	}

	return iTotalEnviado;
}

int Proxy::cSend(SOCKET& _socket, const char* _cbuffer, size_t _buff_size, SOCKET _socket_local_remoto, SOCKET _socket_punto_final) {
	//size_t nSize = _buff_size + (sizeof(SOCKET) * 2);
	size_t nSize = _buff_size + 12;
	std::vector<char> finalData(nSize);

	//   DATA | SOCKET_CLIENTE_LOCAL | SOCKET_PUNTO_FINAL
	
	memcpy(finalData.data(), _cbuffer, _buff_size);
	
	std::vector<char> vc_socket_local_remoto = this->SckToVCchar(_socket_local_remoto);
	std::vector<char> vc_socket_punto_final = this->SckToVCchar(_socket_punto_final);

	memcpy(finalData.data() + _buff_size, vc_socket_local_remoto.data(), 6);
	memcpy(finalData.data() + _buff_size + 6, vc_socket_punto_final.data(), 6);
	//memcpy(finalData.data() + _buff_size, &_socket_local_remoto, sizeof(SOCKET));
	//memcpy(finalData.data() + _buff_size + sizeof(SOCKET), &_socket_punto_final, sizeof(SOCKET));

	return this->sendAll(_socket, finalData.data(), nSize);
}

int Proxy::cRecv(SOCKET& _socket, std::vector<char>& _out_buffer, SOCKET& _socket_local_remoto, SOCKET& _socket_punto_final) {
	int iRecibido = 0;
	int iMinimo = 12; // (sizeof(SOCKET)*2)

	_out_buffer = this->m_thS_ReadSocket(_socket, iRecibido);

	if (iRecibido == SOCKET_ERROR) {
		return iRecibido;
	}
	else if (iRecibido < iMinimo) {
		return 0;
	}

	int iSocketsOffset = iRecibido - iMinimo;

	_socket_local_remoto = this->VCcharToSck(_out_buffer.data() + iSocketsOffset);
	_socket_punto_final = this->VCcharToSck(_out_buffer.data() + iSocketsOffset + 6);

	//memcpy(&_socket_local_remoto, _out_buffer.data() + iSocketsOffset, sizeof(SOCKET));
	//memcpy(&_socket_punto_final, _out_buffer.data() + iSocketsOffset + sizeof(SOCKET), sizeof(SOCKET));

	_out_buffer.erase(_out_buffer.begin() + iSocketsOffset, _out_buffer.end());


	return iSocketsOffset;
}

std::vector<char> Proxy::m_thS_ReadSocket(SOCKET& _socket, int& _out_recibido) {
	std::unique_lock<std::mutex> lock(this->mtx_RemoteProxy_Read);
	return this->readAll(_socket, _out_recibido);
}

int Proxy::m_thS_WriteSocket(SOCKET& _socket, const char* _cbuffer, size_t _buff_size, SOCKET _socket_local_remoto, SOCKET _socket_punto_final) {
	std::unique_lock<std::mutex> lock(this->mtx_RemoteProxy_Write);
	
	return this->cSend(_socket, _cbuffer, _buff_size, _socket_local_remoto, _socket_punto_final);
}

bool Proxy::isRespuestaSegundoPaso(const std::vector<char>& _vcdata, int _recibido) {
	//  https://datatracker.ietf.org/doc/html/rfc1928
	/*  +----+-----+-------+------+----------+----------+
		|VER | REP |  RSV  | ATYP | BND.ADDR | BND.PORT |
		+----+-----+-------+------+----------+----------+
		| 1  |  0  | X'00' |  1   | Variable |    2     |
		+----+-----+-------+------+----------+----------+*/
	if (_recibido < 4) {
		return false;
	}

	if (_vcdata[0] == 0x05 || _vcdata[0] == 0x04) {		                          //SOCKS4 o SOCKS5
		if (_vcdata[1] == 0x00                                                    // RESPUESTA
			&& _vcdata[2] == 0x00                                                 // RESERVADO
			&& (_vcdata[3] == 0x01 || _vcdata[3] == 0x03 || _vcdata[3] == 0x04)   // IPV4 IPV6 Domain Name
			) {
			return true;
		}
	}
	return false;
}

void Proxy::th_Handle_Session(SOCKET _socket_cliente_local, SOCKET _socket_proxy_remoto, SOCKET _socket_punto_final) {
	// _socket_cliente_local  = SOCKET_LOCAL / BROWSER / APP
	// _socket_remoto = SOCKET de la conexion realizada con la proxy remota
	// _socket_punto_final = SOCKET de la conexion con el punto final
	std::string strPre = "SCK[" + std::to_string(_socket_cliente_local) + "]";
	
	bool isRunning = true;
	bool isHandShakeDone = false;
	struct timeval timeout;
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	fd_set fdClienteMaster;
	FD_ZERO(&fdClienteMaster);
	FD_SET(_socket_cliente_local, &fdClienteMaster);


	//DEBUG_MSG(strPre + " th_Running...");
	while (isRunning) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		fd_set fdMaster_copy = fdClienteMaster;
		
		int iNumeroSocket = select(_socket_cliente_local + 1, &fdMaster_copy, nullptr, nullptr, &timeout);

		for (int index = 0; index < iNumeroSocket; index++) {
			SOCKET temp_socket = fdMaster_copy.fd_array[index];
			//Leer datos del navegador/app y reenviarlos al socket remoto (proxy remota) 
			// agregandole el numero de SOCKET del cliente local y el SOCKET remoto con punto final
			if (temp_socket == _socket_cliente_local) {
				int iRecibido = 0;
				std::vector<char> vcData = this->readAllLocal(temp_socket, iRecibido);
				if (iRecibido > 0) {
					//Escribir al socket con el proxy remoto
					if(this->m_thS_WriteSocket(_socket_proxy_remoto, vcData.data(), iRecibido, _socket_cliente_local, _socket_punto_final) == SOCKET_ERROR) {
						DEBUG_ERR("[X] No se pudo enviar el paquete al proxy remoto")
						closesocket(temp_socket);
						FD_CLR(temp_socket, &fdClienteMaster);
						isRunning = false;
					}
				}else if (iRecibido == SOCKET_ERROR) {
					closesocket(temp_socket);
					FD_CLR(temp_socket, &fdClienteMaster);
					isRunning = false;
				}
			}
		}	
	}

	if (FD_ISSET(_socket_cliente_local, &fdClienteMaster)) {
		FD_CLR(_socket_cliente_local, &fdClienteMaster);
	}
}

std::vector<char> Proxy::SckToVCchar(SOCKET _socket) {
	std::vector<char> vcout(6);
	for (char& c : vcout) {
		c = '-';
	}
	vcout[5] = '\0';

	if (_socket != INVALID_SOCKET) {
		std::string strSocket = std::to_string(_socket);
		memcpy(vcout.data(), strSocket.c_str(), strSocket.size());
	}
	return vcout;
}

SOCKET Proxy::VCcharToSck(const char* _cdata) {
	std::vector<char> vc_copy(6);
	memcpy(vc_copy.data(), _cdata, 6);
	for (char& c : vc_copy) {
		if (c == '-') {
			c = '\0';
		}
	}

	return atoi(vc_copy.data());;
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
	std::string strHTML = "<br><br><center><h1>Error :v</h1></center>";
	std::string strBanner = "HTTP/1.1 200 OK\r\n"\
		"Date: Sat, 10 Jan 2011 03:10:00 GMT\r\n"\
		"Server: Tanuki/1.0\r\n"\
		"Content-Length:";
	strBanner += std::to_string(strHTML.size());
	strBanner += "\r\nContent - type: text / plain\r\n\r\n";
	strBanner += strHTML;

	return strBanner;
}