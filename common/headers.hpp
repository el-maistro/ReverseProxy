#include<ws2tcpip.h>
#include<windows.h>
#include<iostream>
#include<thread>
#include<mutex>
#include<iomanip>
#include<memory>
#include<string>
#include<cstring>
#include<vector>
#include<map>
#include<random>
#include<ctime>
#include<fcntl.h>
#include<stdlib.h>

#pragma comment(lib, "ws2_32.lib")

#define DEBUG_MSG(x) std::cout<<" [DBG] "<<x<<std::endl;
#define DEBUG_ERR(x) std::cout<<" [DBG] "<<x<<" [ERR]: "<<GetLastError()<<std::endl;
#define RETRY_COUNT 10
