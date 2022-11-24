// vim: sw=2 ts=2 expandtab smartindent

#ifdef __LINUX__
  #define closesocket close
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netdb.h>
#else
  #pragma comment(lib,"Ws2_32.lib")
  #include <WinSock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
#endif

#include "hash32.h"

static void wsa_log_err(char *extra_msg) {
  char err[256];
  FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, WSAGetLastError(),
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
  fprintf(stderr, "%s: %s\n", extra_msg, err);
}

static uint32_t sockaddr_hash(struct sockaddr *addr, int addr_len) {
  char host[NI_MAXHOST], service[NI_MAXSERV];
  int s = getnameinfo(
       addr, addr_len,
       host, NI_MAXHOST,
    service, NI_MAXSERV,
    NI_NUMERICSERV
  );
  if (s != 0)
    fprintf(stderr, "getnameinfo: %s\n", gai_strerror(s)),
    exit(EXIT_FAILURE);

  printf("hashing %s:%s\n", host, service), fflush(stdout);

  return hash32(   host, strlen(   host),
         hash32(service, strlen(service), 0));
}

/* calling "connect()" tells "send()" where to go */
/* returns 1 if connection successful */
static int clnt_sfd = INVALID_SOCKET;
static int try_connect(void) {
  struct addrinfo hints = {0};
  hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
  hints.ai_flags = 0;
  hints.ai_protocol = 0;          /* Any protocol */

  char *host = "localhost";
  char *port = "4269";
  struct addrinfo *result = 0;
  int s = getaddrinfo(host, port, &hints, &result);
  if (s != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
    exit(EXIT_FAILURE); /* if this dont work we got big issues */
  }

  /* getaddrinfo() returns a list of address structures.
     Try each address until we successfully connect(2).
     If socket(2) (or connect(2)) fails, we (close the socket
     and) try the next address. */

  struct addrinfo *rp;
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    clnt_sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (clnt_sfd == -1 || clnt_sfd == INVALID_SOCKET)
      continue;

    if (connect(clnt_sfd, rp->ai_addr, rp->ai_addrlen) != -1)
      break;                  /* Success */

    closesocket(clnt_sfd);
  }

  if (rp == NULL) {               /* No address succeeded */
    fprintf(stderr, "Could not connect\n");
    return 0;
  }

  freeaddrinfo(result);           /* No longer needed */

  /* we don't want our sockets to block */
  int polling_api = 1;
  ioctlsocket(clnt_sfd, FIONBIO, &polling_api);

  return 1;
}

/* NOTE: will fail if nobody's hosting, returns 1 if successful */
int env_send_to_host(uint8_t *buf, int len) {
  /* connect if we haven't yet, bail if we can't */
  if (clnt_sfd == INVALID_SOCKET)
    if (!try_connect())
      return 0;

  int nread = send(clnt_sfd, (const char *)buf, len, 0);
  if (nread < 1) wsa_log_err("partial/failed send");
  return nread > 0;
}

/* try to receive a message from the server, as a client.
 * returns 1 if found data for you to read */
int env_clnt_recv(uint8_t *buf, int *len) {
  int nread = recv(clnt_sfd, (char *)buf, *len, 0);
  *len = nread;

  if (nread > 0) printf("nread: %d\n", nread);

  if (WSAGetLastError() == WSAEWOULDBLOCK)
    return 0;

  if (nread == -1) {
    wsa_log_err("failed recv");
    return 0;
  }

  return 1;
}

static int host_sfd = INVALID_SOCKET;
static int try_bind(void) {
  struct addrinfo hints = {0};
  hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
  hints.ai_flags = AI_PASSIVE;    /* For wildcard IP address */
  hints.ai_protocol = 0;          /* Any protocol */
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;

  char *port = "4269";
  struct addrinfo *result = 0;
  int s = getaddrinfo(NULL, port, &hints, &result);
  if (s != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
    exit(EXIT_FAILURE); /* we got bigger problems if this dont work */
  }

  struct addrinfo *rp;
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    host_sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (host_sfd == -1 || host_sfd == INVALID_SOCKET)
      continue;

    if (bind(host_sfd, rp->ai_addr, rp->ai_addrlen) == 0)
      break;                  /* Success */

    closesocket(host_sfd);
  }

  if (rp == NULL) {               /* No address succeeded */
    fprintf(stderr, "Could not bind\n");
    exit(EXIT_FAILURE);
  }

  freeaddrinfo(result);           /* No longer needed */

  /* we don't want our sockets to block */
  int polling_api = 1;
  ioctlsocket(host_sfd, FIONBIO, &polling_api);

  return 1;
}

/* try to receive a message from any client, as the server.
 * if you start calling this you will become the server. (if possible)
 * returns 1 if found data for you to read */
int env_host_recv(Addr *addr, uint8_t *buf, int *len) {
  /* bind socket if we haven't yet, bail if we can't */
  if (host_sfd == INVALID_SOCKET)
    if (!try_bind())
      return 0;

  addr->_store_len = sizeof(struct sockaddr_storage);
  size_t nread = recvfrom(
    host_sfd, buf, *len, 0,
    (struct sockaddr *)addr->_store, &addr->_store_len
  );

  if (nread == -1) {
    if (WSAGetLastError() != WSAEWOULDBLOCK)
      wsa_log_err("server failed to recvfrom");
    return 0;
  }

  addr->hash = sockaddr_hash((struct sockaddr *)addr->_store, addr->_store_len);
  *len = nread;

  return 1;
}

/* send to an addr. useful if you are hosting. returns 1 if successful */
int env_send(Addr *addr, uint8_t *buf, int len) {
  if (sendto(host_sfd, buf, len, 0,
        (struct sockaddr *) &addr->_store,
        addr->_store_len) != len) {
    fprintf(stderr, "Error sending response\n");
    return 0;
  }

  return 1;
}
