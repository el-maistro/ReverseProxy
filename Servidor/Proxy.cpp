#include "Proxy.hpp"
#include "../common/misc.hpp"

Proxy::Proxy(int _puerto)
	: iPuertoEscucha(_puerto) {
	WSACleanup();
	DEBUG_MSG("Iniciando configuracion...");
	if (WSAStartup(MAKEWORD(2, 2), &this->wsa) != 0) {
		DEBUG_ERR("WSAStartup error");
		return;
	}
	if (this->m_InitSocket(this->sckLocalSocket, this->iPuertoEscucha) && this->m_InitSocket(this->sckRemoteSocket, 7777)) {
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

		//int enable = 1;
		//if (setsockopt(nSocket, SOL_SOCKET, SO_KEEPALIVE, (const char*)&enable, sizeof(enable)) == SOCKET_ERROR) {
		//	DEBUG_ERR("[m_Aceptar] KEEP_ALIVE");
		//}
	}

	return nSocket;
}

void Proxy::EsperarConexiones() {
	struct timeval timeout;
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	fd_set fdMaster;
	FD_ZERO(&fdMaster);

	if(this->sckLocalSocket == INVALID_SOCKET || this->sckRemoteSocket == INVALID_SOCKET){
		DEBUG_MSG("Los sockets no estan configurados...");
		return;
	}

	//Timeout para el socket de escucha local
	//DWORD timeout_local_socket = 100; //100 miliseconds timeout
	//setsockopt(this->sckLocalSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_local_socket, sizeof timeout_local_socket);

	FD_SET(this->sckLocalSocket, &fdMaster);
	FD_SET(this->sckRemoteSocket, &fdMaster);
	
	SOCKET sckTemp_Proxy_Remota = INVALID_SOCKET;

	bool isRunning = true;

	while (isRunning) {
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
					
					//FD_SET(nSocketLocal, &fdMaster);
					std::thread th(&Proxy::th_Handle_Session, this, sckTemp_Proxy_Remota, RandomID(),  nSocketLocal);
					th.detach();
				}

			}
			else if (temp_socket == this->sckRemoteSocket) {
				//Conexion remota del proxy
				
				SOCKET nSocketProxyRemota = this->m_Aceptar(this->sckRemoteSocket);
				if (nSocketProxyRemota != INVALID_SOCKET) {
					
					DEBUG_MSG("[!] Conexion de proxy remota aceptada");

					sckTemp_Proxy_Remota = nSocketProxyRemota;

					FD_SET(nSocketProxyRemota, &fdMaster);

					//Eliminar socket del FD ya que solo se necesita una conexion remota con el proxy?
					FD_CLR(this->sckRemoteSocket, &fdMaster);
					closesocket(this->sckRemoteSocket);
				}

			}
			else if (temp_socket == sckTemp_Proxy_Remota){
				//Datos del proxy remoto
				
				std::vector<char> vcData;
				int iConexionID = 0;
				int iRecibido = this->cRecv(temp_socket, vcData, iConexionID);

				if (!procRespuestaProxy(iRecibido, vcData, temp_socket, iConexionID, fdMaster)) {
					//Error leyendo datos del proxy remoto
					isRunning = false;
					break;
				}

			}
		}
	}

}

std::vector<char> Proxy::readAll(SOCKET& _socket, int& _out_recibido) {
	_out_recibido = SOCKET_ERROR;
	std::vector<char> vcOut;
	int iTotalRecibido = 0;
	int iRetrys = RETRY_COUNT;
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
						if (iRetrys-- > 0) {
							DEBUG_MSG("[!] Intento lectura...");
							std::this_thread::sleep_for(std::chrono::milliseconds(10));
							continue;
						}
					}else {
						DEBUG_ERR("[!] Error recibiendo los datos");
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
	int iChunk         = 1024;
	int iTotalRecibido = 0;
	int iRetrys        = RETRY_COUNT;
	int iRecibido      = 0;
	while (1) {
		char cTempBuffer[1024];
		iRecibido = recv(_socket, cTempBuffer, iChunk, 0);
		if (iRecibido == 0) {
			_out_recibido == SOCKET_ERROR ? iRecibido : _out_recibido;
			break;
		}else if (iRecibido == SOCKET_ERROR) {
			int error_wsa = WSAGetLastError();
			if (error_wsa == WSAEWOULDBLOCK) {
				if (iRetrys-- > 0) {
					//DEBUG_MSG("[!] WSAEWOULDBLOCK Intento lectura...");
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
					continue;
				}
			}else {
				DEBUG_MSG("[!] readAllLocal ERR:" + std::to_string(error_wsa));
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
	int iRetrys = RETRY_COUNT;
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
				if (iRetrys-- > 0) {
					DEBUG_MSG("[sendAll] Intento escritura...");
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
					continue;
				}
			} else{
				return iTotalEnviado == 0 ? SOCKET_ERROR : iTotalEnviado;
			}
		}

		iTotalEnviado += iEnviado;
	}

	return iTotalEnviado;
}

int Proxy::sendAllLocal(SOCKET& _socket, const char* _cbuffer, int _buff_size, bool dbg) {
	int iEnviado = 0;
	int iRetrys = RETRY_COUNT;
	int iTotalEnviado = 0;

	if (dbg) {
		int _DEBUG_BREAK_DUMMY = 0;
		int _DEBUG_BREAK_DUMMY_2 = 0;
	}

	while (iTotalEnviado < _buff_size) {
		iEnviado = send(_socket, _cbuffer + iTotalEnviado, _buff_size - iTotalEnviado, 0);
		if (iEnviado == 0) {
			break;
		}else if (iEnviado == SOCKET_ERROR) {
			int error_code = WSAGetLastError();
			if (error_code == WSAEWOULDBLOCK) {
				if (iRetrys-- > 0) {
					DEBUG_MSG("[sendAllLocal] Intento escritura...");
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
					continue;
				}
			}else {
				return iTotalEnviado == 0 ? SOCKET_ERROR : iTotalEnviado;
			}
		}

		iTotalEnviado += iEnviado;
	}
	return iTotalEnviado;
}

int Proxy::cSend(SOCKET& _socket, const char* _cbuffer, size_t _buff_size, int _id_conexion) {
	//DEBUG_MSG("ID>>> " + std::to_string(_id_conexion));
	size_t nSize = _buff_size + int(sizeof(int));
	std::vector<char> finalData(nSize);

	//   DATA | ID_CONEXION
	memcpy(finalData.data(), _cbuffer, _buff_size);
	memcpy(finalData.data() + _buff_size, &_id_conexion, sizeof(int));
	
	return this->sendAll(_socket, finalData.data(), nSize);
}

int Proxy::cRecv(SOCKET& _socket, std::vector<char>& _out_buffer, int& _id_conexion) {
	int iRecibido = 0;
	int iMinimo = int(sizeof(int));

	_out_buffer = this->m_thS_ReadSocket(_socket, iRecibido);

	if (iRecibido == SOCKET_ERROR) {
		return iRecibido;
	}else if (iRecibido < iMinimo) {
		return 0;
	}

	int idOffset = iRecibido - iMinimo;

	memcpy(&_id_conexion, _out_buffer.data() + idOffset, sizeof(int));

	//DEBUG_MSG("ID<<< " + std::to_string(_id_conexion));

	_out_buffer.erase(_out_buffer.begin() + idOffset, _out_buffer.end());

	return idOffset;
}

std::vector<char> Proxy::m_thS_ReadSocket(SOCKET& _socket, int& _out_recibido) {
	std::unique_lock<std::mutex> lock(this->mtx_RemoteProxy_Read);
	return this->readAll(_socket, _out_recibido);
}

int Proxy::m_thS_WriteSocket(SOCKET& _socket, const char* _cbuffer, size_t _buff_size, int _id_conexion) {
	std::unique_lock<std::mutex> lock(this->mtx_RemoteProxy_Write);
	
	return this->cSend(_socket, _cbuffer, _buff_size, _id_conexion);
}

bool Proxy::procRespuestaProxy(int _recibido, const std::vector<char>& _vcdata, SOCKET _proxy_remota, int _id_conexion, FD_SET& _fd) {
	if (_recibido > 0) {
		
		SOCKET socket_local_remoto = this->getLocalSocket(_id_conexion);

		if (socket_local_remoto != INVALID_SOCKET) {
			
			if (this->isRespuestaSegundoPaso(_vcdata, _recibido)) {
				if (FD_ISSET(socket_local_remoto, &_fd)) {
					FD_CLR(socket_local_remoto, &_fd);
				}
				//std::thread th = std::thread(&Proxy::th_Handle_Session, this, _proxy_remota, _id_conexion);
				//th.detach();
				DEBUG_MSG("[!] Conexion con punto final completa");
			}

			if (this->sendAllLocal(socket_local_remoto, _vcdata.data(), _recibido) == SOCKET_ERROR) {
				DEBUG_ERR("[X] Error enviado respuesta de proxy remota a cliente local. Bytes: " + std::to_string(_recibido));
			}

		}else {
			DEBUG_MSG("[X] No se pudo encontrar un socket con ID " + std::to_string(_id_conexion));
		}

	}else if (_recibido == SOCKET_ERROR) {
		DEBUG_ERR("[!] Error leyendo datos del proxy remoto. Reiniciando servidor...");

		std::this_thread::sleep_for(std::chrono::milliseconds(1000));

		FD_CLR(_proxy_remota, &_fd);
		closesocket(_proxy_remota);
		_proxy_remota = INVALID_SOCKET;

		//Reiniciar el socket y agregarlo al FD
		if (this->m_InitSocket(this->sckRemoteSocket, 7777)) {
			if (this->sckRemoteSocket != INVALID_SOCKET) {
				FD_SET(this->sckRemoteSocket, &_fd);
				DEBUG_MSG("[!] Se reinicio correctamente");
			}else {
				DEBUG_MSG("[X] El socket remoto es invalido");
				return false;
			}
		}else {
			DEBUG_ERR("[X] No se pudo reiniciar el socket del proxy remoto");
			return false;
		}
	}else {
		DEBUG_MSG("[!] Paquete muy pequeno: " + std::to_string(_recibido));
	}

	return true;
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

void Proxy::th_Handle_Session(SOCKET _socket_proxy_remoto, int _id_conexion, SOCKET _socket_local) {
	//Generar ID y agregar el socket al map
	this->addLocalSocket(_id_conexion, _socket_local);
	DEBUG_MSG("[!] Conexion local aceptada " + std::to_string(_socket_local));

	//std::string strPre = "SCK[" + std::to_string(_id_conexion) + "]";
	
	bool isRunning = true;
	bool isHandShakeDone = false;
	struct timeval timeout;
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	fd_set fdClienteMaster;
	FD_ZERO(&fdClienteMaster);
	FD_SET(_socket_local, &fdClienteMaster);

	//DEBUG_MSG(strPre + " th_Running...");
	while (isRunning) {
		fd_set fdMaster_copy = fdClienteMaster;
		
		int iNumeroSocket = select(_socket_local + 1, &fdMaster_copy, nullptr, nullptr, &timeout);

		for (int index = 0; index < iNumeroSocket; index++) {
			SOCKET temp_socket = fdMaster_copy.fd_array[index];
			//Leer datos del navegador/app y reenviarlos al socket remoto (proxy remota) 
			// agregandole el numero de SOCKET del cliente local y el SOCKET remoto con punto final
			if (temp_socket == _socket_local) {
				int iRecibido = 0;
				std::vector<char> vcData = this->readAllLocal(temp_socket, iRecibido);
				if (iRecibido > 0) {
					//Escribir al socket con el proxy remoto
					if(this->m_thS_WriteSocket(_socket_proxy_remoto, vcData.data(), iRecibido, _id_conexion) == SOCKET_ERROR) {
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

	if (FD_ISSET(_socket_local, &fdClienteMaster)) {
		FD_CLR(_socket_local, &fdClienteMaster);
	}
	this->eraseLocalSocket(_id_conexion);
	//DEBUG_MSG(strPre + " closing...");
}

SOCKET Proxy::getLocalSocket(int _id){
	std::unique_lock<std::mutex> lock(this->mtx_MapSockets);
	auto it = this->map_sockets.find(_id);
	if (it != this->map_sockets.end()) {
		return it->second;
	}

	return INVALID_SOCKET;
}

void Proxy::addLocalSocket(int _id, SOCKET _socket) {
	std::unique_lock<std::mutex> lock(this->mtx_MapSockets);
	this->map_sockets.insert({ _id, _socket });
}

bool Proxy::eraseLocalSocket(int _id){
	std::unique_lock<std::mutex> lock(this->mtx_MapSockets);
	if (this->map_sockets.erase(_id) == 1) {
		return true;
	}
	return false;
}

int Proxy::getSocketID(SOCKET _socket) {
	std::unique_lock<std::mutex> lock(this->mtx_MapSockets);
	for (auto& it : this->map_sockets) {
		if (it.second == _socket) {
			return it.first;
		}
	}

	return -1;
}
