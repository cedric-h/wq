#ifdef __LINUX__
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#else
#pragma comment(lib,"Ws2_32.lib")
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif
