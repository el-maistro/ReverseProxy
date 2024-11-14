#include<ws2tcpip.h>
#include<windows.h>
#include<iostream>
#include<iomanip>
#include<memory>
#include<string>
#include<vector>
#include<ctime>
#include<fcntl.h>

#pragma comment(lib, "ws2_32.lib")

#define DEBUG_MSG(x) std::cout<<"[DBG] "<<x<<"\n";
#define DEBUG_ERR(x) std::cout<<"[DBG] "<<x<<" [ERR]: "<<GetLastError()<<"\n";