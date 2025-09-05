#include "SecuritySolver.h"

#include <assert.h>
#include <string.h>

#ifdef _WIN32
#include <WS2tcpip.h>
#include <Windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#define WSAEWOULDBLOCK EWOULDBLOCK
#define closesocket close
#endif

#include <stdio.h>
#include <zero/game/Logger.h>
#include <zero/game/Platform.h>
#include <zero/game/WorkQueue.h>

#define SECURITY_DEBUG_LOG 1

namespace zero {

#pragma pack(push, 1)
enum class RequestType : u8 { Keystream, Checksum };
enum class ResponseType : u8 { Keystream, Checksum };

struct KeystreamRequestPacket {
  RequestType type;
  u32 key2;
};

struct KeystreamResponsePacket {
  ResponseType type;
  u32 key2;
  u32 table[20];
};

struct ChecksumRequestPacket {
  RequestType type;
  u32 key;
};

struct ChecksumResponsePacket {
  ResponseType type;
  u32 key;
  u32 checksum;
};
#pragma pack(pop)

SecurityNetworkService::SecurityNetworkService(const char* service_ip, u16 service_port) {
  strcpy(this->ip, service_ip);
  this->port = service_port;
}

void SetBlocking(SocketType fd, bool blocking) {
  unsigned long mode = blocking ? 0 : 1;

#ifdef _WIN32
  ioctlsocket(fd, FIONBIO, &mode);
#else
  int flags = fcntl(fd, F_GETFL, 0);

  flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);

  fcntl(fd, F_SETFL, flags);
#endif
}

SocketType SecurityNetworkService::Connect() {
  struct addrinfo hints = {0}, *result = nullptr;

  SocketType socket = -1;

  if ((socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    return -1;
  }

  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  char str_port[16];
  sprintf(str_port, "%d", port);

  if (getaddrinfo(this->ip, str_port, &hints, &result) != 0) {
    closesocket(socket);
    return -1;
  }

  struct addrinfo* ptr = nullptr;
  for (ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
    struct sockaddr_in* sockaddr = (struct sockaddr_in*)ptr->ai_addr;

    if (::connect(socket, (struct sockaddr*)sockaddr, sizeof(struct sockaddr_in)) == 0) {
      break;
    }
  }

  freeaddrinfo(result);

  if (!ptr) {
    closesocket(socket);
    return -1;
  }

  SetBlocking(socket, true);

  return socket;
}

int ProcessRequest(SocketType socket, void* request, int request_size, void* response, int response_size) {
  if (send(socket, (const char*)request, request_size, 0) != request_size) {
    return -1;
  }

  int total_received = 0;

  while (total_received < response_size) {
    int bytes_received = recv(socket, (char*)response + total_received, response_size - total_received, 0);

    if (bytes_received <= 0) {
      closesocket(socket);
      break;
    }

    total_received += bytes_received;
  }

  return total_received;
}

void ExpansionWorkRun(Work* work) {
  SecurityNetworkWork* expansion_work = (SecurityNetworkWork*)work->user;
  SecurityNetworkService* service = &expansion_work->solver->service;

  Log(LogLevel::Jabber, "ExpansionWorkRun(%08X): Starting", expansion_work->expansion.key2);

  SocketType socket = service->Connect();

  if (socket == -1) {
    Log(LogLevel::Jabber, "ExpansionWorkRun(%08X): Connect failure", expansion_work->expansion.key2);
    return;
  }

  expansion_work->socket = socket;

  KeystreamRequestPacket request;
  request.type = RequestType::Keystream;
  request.key2 = expansion_work->expansion.key2;

  KeystreamResponsePacket response;

  Log(LogLevel::Jabber, "ExpansionWorkRun(%08X): ProcessRequest start", expansion_work->expansion.key2);
  int response_size = ProcessRequest(socket, &request, sizeof(request), &response, sizeof(response));

  if (response_size == sizeof(response) && response.type == ResponseType::Keystream) {
    memcpy(expansion_work->expansion.table, response.table, sizeof(expansion_work->expansion.table));

    expansion_work->state = SecurityWorkState::Success;
  }

  closesocket(socket);

  Log(LogLevel::Jabber, "ExpansionWorkRun(%08X) result: %d", expansion_work->expansion.key2, expansion_work->state);
}

void ExpansionWorkComplete(Work* work) {
  SecurityNetworkWork* expansion_work = (SecurityNetworkWork*)work->user;

  u32* table = nullptr;

  if (expansion_work->state == SecurityWorkState::Success) {
    table = expansion_work->expansion.table;
  }

  expansion_work->callback(table);

  expansion_work->solver->FreeWork(expansion_work);
}

const WorkDefinition kExpansionDefinition = {ExpansionWorkRun, ExpansionWorkComplete};

void ChecksumWorkRun(Work* work) {
  SecurityNetworkWork* checksum_work = (SecurityNetworkWork*)work->user;
  SecurityNetworkService* service = &checksum_work->solver->service;

  Log(LogLevel::Jabber, "ChecksumWorkRun(%08X): Starting", checksum_work->checksum.key);
  SocketType socket = service->Connect();

  if (socket == -1) {
    Log(LogLevel::Jabber, "ChecksumWorkRun(%08X): Connect failure", checksum_work->checksum.key);
    return;
  }

  checksum_work->socket = socket;

  ChecksumRequestPacket request;
  request.type = RequestType::Checksum;
  request.key = checksum_work->checksum.key;

  ChecksumResponsePacket response;

  Log(LogLevel::Jabber, "ChecksumWorkRun(%08X): ProcessRequest start", checksum_work->checksum.key);
  int response_size = ProcessRequest(socket, &request, sizeof(request), &response, sizeof(response));

  if (response_size == sizeof(response) && response.type == ResponseType::Checksum) {
    checksum_work->checksum.checksum = response.checksum;
    checksum_work->state = SecurityWorkState::Success;
  }

  closesocket(socket);
  Log(LogLevel::Jabber, "ChecksumWorkRun(%08X) result: %d", checksum_work->checksum.key, checksum_work->state);
}

void ChecksumWorkComplete(Work* work) {
  SecurityNetworkWork* checksum_work = (SecurityNetworkWork*)work->user;

  u32* checksum = nullptr;

  if (checksum_work->state == SecurityWorkState::Success) {
    checksum = &checksum_work->checksum.checksum;
  }

  checksum_work->callback(checksum);

  checksum_work->solver->FreeWork(checksum_work);
}

const WorkDefinition kChecksumDefinition = {ChecksumWorkRun, ChecksumWorkComplete};

SecuritySolver::SecuritySolver(WorkQueue& work_queue, const char* service_ip, u16 service_port)
    : work_queue(work_queue), service(service_ip, service_port) {
  memset(work, 0, sizeof(work));
  for (size_t i = 0; i < ZERO_ARRAY_SIZE(work); ++i) {
    work[i].state = SecurityWorkState::Idle;
  }
}

void SecuritySolver::ExpandKey(u32 key2, SecurityCallback callback) {
  SecurityNetworkWork* work = AllocateWork();

  work->state = SecurityWorkState::Working;
  work->type = SecurityRequestType::Expansion;
  work->expansion.key2 = key2;
  work->callback = callback;
  work->solver = this;

  Log(LogLevel::Jabber, "Submitting work_queue ExpandKey: %08X", key2);
  work_queue.Submit(kExpansionDefinition, work);
}

void SecuritySolver::GetChecksum(u32 key, SecurityCallback callback) {
  SecurityNetworkWork* work = AllocateWork();

  work->state = SecurityWorkState::Working;
  work->type = SecurityRequestType::Checksum;
  work->checksum.key = key;
  work->callback = callback;
  work->solver = this;

  Log(LogLevel::Jabber, "Submitting work_queue Checksum: %08X", key);
  work_queue.Submit(kChecksumDefinition, work);
}

SecurityNetworkWork* SecuritySolver::AllocateWork() {
  std::lock_guard<std::mutex> guard(mutex);

  for (size_t i = 0; i < ZERO_ARRAY_SIZE(work); ++i) {
    if (work[i].state == SecurityWorkState::Idle) {
      work[i].state = SecurityWorkState::Working;
      return work + i;
    }
  }

  return nullptr;
}

void SecuritySolver::FreeWork(SecurityNetworkWork* security_work) {
  std::lock_guard<std::mutex> guard(mutex);

  security_work->state = SecurityWorkState::Idle;
}

void SecuritySolver::ClearWork() {
  std::lock_guard<std::mutex> guard(mutex);

  for (size_t i = 0; i < ZERO_ARRAY_SIZE(work); ++i) {
    SecurityNetworkWork* security_work = work + i;

    if (security_work->state != SecurityWorkState::Idle) {
      closesocket(security_work->socket);
      security_work->state = SecurityWorkState::Idle;
    }
  }
}

}  // namespace zero
