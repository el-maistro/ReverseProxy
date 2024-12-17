LINK=-lws2_32
CFLAG=-Wall -Wextra -std=c++11 -Wpedantic
OBJSERVER=./Servidor/ReverseProxy.o ./Servidor/Proxy.o
OBJCLIENTE=./Cliente/ProxyCliente.o ./Cliente/Cliente.o
COMMON=./common/misc.o
BIN1=cliente.exe
BIN2=servidor.exe
CC=x86_64-w64-mingw32-g++.exe

RM=del

%.o: %.cpp
	$(CC) $(CFLAG) -c -o $@ $<

$(COMMON): ./common/misc.cpp
	$(CC) $(CFLAG) -c -o $(COMMON) ./common/misc.cpp

$(BIN1): $(COMMON) $(OBJCLIENTE)
	$(CC) -o $(BIN1) $(COMMON) $(OBJCLIENTE) $(LINK)

$(BIN2): $(COMMON) $(OBJSERVER)
	$(CC) -o $(BIN2) $(COMMON) $(OBJSERVER) $(LINK)

proxy: $(BIN1) $(BIN2)

clean:
	$(RM) $(OBJCLIENTE) $(OBJSERVER) $(COMMON) $(BIN1) $(BIN2)

.PHONY: proxy clean
