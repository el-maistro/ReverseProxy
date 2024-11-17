#include "Proxy.hpp"

int main() {
	Proxy* p_Proxy = new Proxy(6666);

	p_Proxy->EsperarConexiones();

	delete p_Proxy;
	p_Proxy = nullptr;
	return 0;
}