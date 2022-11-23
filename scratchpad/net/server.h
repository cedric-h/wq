// vim: sw=2 ts=2 expandtab smartindent

#include "hal_net.h"
#include "hash32.h"

// #include <unistd.h>
#include <stdio.h>
#include <io.h>
#include <string.h>

#define BUF_SIZE 500

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
    exit(1);

  printf("Hashed %s:%s\n", host, service), fflush(stdout);

  return hash32(   host, strlen(   host),
         hash32(service, strlen(service), 0));
}

typedef struct {
  int active;
  uint32_t addr_hash;
  struct sockaddr_storage addr;
  uint32_t addr_len;
} Client;
#define CLIENTS_MAX (1 << 8)

static Client clients[CLIENTS_MAX] = {0};
static int sfd = 0;

static Client *clients_find(uint32_t addr_hash) {
  for (int i = 0; i < CLIENTS_MAX; i++)
    if (clients[i].active && clients[i].addr_hash == addr_hash)
      return clients + i;
  return NULL;
}

static Client *clients_storage(void) {
  for (int i = 0; i < CLIENTS_MAX; i++)
    if (!clients[i].active)
      return clients + i;
  fprintf(stderr, "too many clients!");
  return NULL;
}

void server_init(char *port) {
  WSADATA wsa_data;
  WSAStartup(MAKEWORD(2, 2), &wsa_data);

  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int s;
  size_t nread;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
  hints.ai_flags = AI_PASSIVE;    /* For wildcard IP address */
  hints.ai_protocol = 0;          /* Any protocol */
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;

  s = getaddrinfo(NULL, port, &hints, &result);
  if (s != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
    exit(EXIT_FAILURE);
  }

  /* getaddrinfo() returns a list of address structures.
     Try each address until we successfully bind(2).
     If socket(2) (or bind(2)) fails, we (close the socket
     and) try the next address. */

  for (rp = result; rp != NULL; rp = rp->ai_next) {
    sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sfd == -1)
      continue;

    if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
      break;                  /* Success */

    closesocket(sfd);
  }

  if (rp == NULL) {               /* No address succeeded */
    fprintf(stderr, "Could not bind\n");
    exit(EXIT_FAILURE);
  }

  freeaddrinfo(result);           /* No longer needed */

  /* we don't want our sockets to block */
  int polling_api = 1;
  ioctlsocket(sfd, FIONBIO, &polling_api);
}

static void server_poll(void) {
  if (sfd == 0) return;

  /* read until recvfrom blocks */
  for (;;) {

    /* try to read into buf */
    char buf[BUF_SIZE] = {0};
    struct sockaddr_storage peer_addr;
    socklen_t peer_addr_len = sizeof(struct sockaddr_storage);
    size_t nread = recvfrom(
      sfd, buf, BUF_SIZE, 0,
      (struct sockaddr *) &peer_addr, &peer_addr_len
    );

    /* no more to read, come back later */
    if (nread == -1) {
      if (WSAGetLastError() != WSAEWOULDBLOCK)
        printf("failed request\n");
      return;
    }

    printf("Received %zd bytes: \"%s\"\n", nread, buf), fflush(stdout);

    /* store in clients list so we can broadcast to you later */
    {
      uint32_t hash = sockaddr_hash(
        (struct sockaddr *) &peer_addr,
        peer_addr_len
      );

      if (!clients_find(hash))
        *clients_storage() = (Client) {
          .active = 1,
          .addr = peer_addr,
          .addr_len = peer_addr_len,
          .addr_hash = hash
        };
    }

    /* broadcast out */
    for (int i = 0; i < CLIENTS_MAX; i++) {
      Client *c = clients + i;
      if (!c->active) continue;

      if (sendto(sfd, buf, nread, 0,
            (struct sockaddr *) &c->addr,
            c->addr_len) != nread)
        fprintf(stderr, "Error sending response\n");
    }
  }
}
