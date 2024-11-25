#include "ProxyCliente.hpp"

int main(int argc, char**argv){
	const char* cDHost = "127.0.0.1";
	const char* cDPort = "7777";
	bool isDefault = false;
	if (argc != 3) {
		std::cout << "Uso: " << argv[0] << " <host-remoto> <puerto remoto>\nUsando informacion por defecto...\n";
		isDefault = true;
	}

	ProxyCliente* nProxy = new ProxyCliente();

	if (nProxy->m_ConectarServer(isDefault ? cDHost : argv[1], isDefault ? cDPort : argv[2])) {
		//Loop para esperar por paquete
		nProxy->m_LoopSession();
	}else {
		DEBUG_ERR("[X] No se pudo conectar al servidor");
	}

	delete nProxy;
	nProxy = nullptr;
}