// vim: sw=2 ts=2 expandtab smartindent

#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "hal_net.h"
#include "server.h"

#define BUF_SIZE 500

static void fatal_err(char *msg) {
  fprintf(stderr, msg);

  char err[256];
  FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
  printf("%s\n", err);//just for the safe case
  puts(err);

  exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
  WSADATA wsa_data;
  WSAStartup(MAKEWORD(2, 2), &wsa_data);

  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int sfd, s, j;
  char buf[BUF_SIZE];

  if (argc < 3) {
    fprintf(stderr, "Usage: %s host port ...\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  /* Obtain address(es) matching host/port */

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
  hints.ai_flags = 0;
  hints.ai_protocol = 0;          /* Any protocol */

  char *host = argv[1];
  char *port = argv[2];
  s = getaddrinfo(host, port, &hints, &result);
  if (s != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
    exit(EXIT_FAILURE);
  }

  /* getaddrinfo() returns a list of address structures.
     Try each address until we successfully connect(2).
     If socket(2) (or connect(2)) fails, we (close the socket
     and) try the next address. */

  for (rp = result; rp != NULL; rp = rp->ai_next) {
    sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sfd == -1)
      continue;

    if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
      break;                  /* Success */

    closesocket(sfd);
  }

  if (rp == NULL) {               /* No address succeeded */
    fprintf(stderr, "Could not connect\n");
    exit(EXIT_FAILURE);
  }

  freeaddrinfo(result);           /* No longer needed */

  char t = 't';
  puts("here");
  
  if ((send(sfd, &t, 1, 0), recv(sfd, &t, 1, 0)) < 1) {
    puts("couldn't reach a server, launching our own");
    server_init(port);

    send(sfd, &t, 1, 0);
    server_poll();

    if (recv(sfd, &t, 1, 0) < 1)
      fatal_err("launched server, couldn't reach it");
  }

  /* we don't want our sockets to block */
  int polling_api = 1;
  ioctlsocket(sfd, FIONBIO, &polling_api);

  for (;;) {
    server_poll();

    char c = 0;
    if (_kbhit()) {
      char c = _getch();

      if (send(sfd, &c, 1, 0) < 1)
        fatal_err("partial/failed write\n");
    }

    int nread = recv(sfd, buf, BUF_SIZE, 0);
    if (WSAGetLastError() == WSAEWOULDBLOCK)
      continue;
    else if (nread == -1)
      fatal_err("failed read\n");

    if (nread > 0)
      printf("Received %d bytes: %s\n", nread, buf);

    fflush(stdout);
  }

  exit(EXIT_SUCCESS);
}
