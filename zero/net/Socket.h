#ifndef ZERO_NET_SOCKET_H_
#define ZERO_NET_SOCKET_H_

namespace zero {

#ifdef _WIN64
using SocketType = long long;
#else
using SocketType = int;
#endif

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#endif

}  // namespace zero

#endif
