// vim: sw=2 ts=2 expandtab smartindent

#include "hal_net.h"

// #include <unistd.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_SIZE 500

static void errmsg(void) {
  char err[256];
  FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
  printf("%s\n", err);//just for the safe case
  puts(err);
}

int main(int argc, char *argv[]) {
  WSADATA wsa_data;
  WSAStartup(MAKEWORD(2, 2), &wsa_data);

  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int sfd, s, j;
  size_t len;
  size_t nread;
  char buf[BUF_SIZE];

  if (argc < 3) {
    fprintf(stderr, "Usage: %s host port msg...\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  /* Obtain address(es) matching host/port */

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
  hints.ai_flags = 0;
  hints.ai_protocol = 0;          /* Any protocol */

  s = getaddrinfo(argv[1], argv[2], &hints, &result);
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

#ifdef __LINUX__
    close(sfd);
#else
    closesocket(sfd);
#endif
  }

  if (rp == NULL) {               /* No address succeeded */
    fprintf(stderr, "Could not connect\n");
    exit(EXIT_FAILURE);
  }

  freeaddrinfo(result);           /* No longer needed */

  /* Send remaining command-line arguments as separate
     datagrams, and read responses from server */

  for (j = 3; j < argc; j++) {
    len = strlen(argv[j]) + 1;
    /* +1 for terminating null byte */

    if (len > BUF_SIZE) {
      fprintf(stderr,
          "Ignoring long message in argument %d\n", j);
      continue;
    }

    if (send(sfd, argv[j], len, 0) != len) {
      fprintf(stderr, "partial/failed write\n");
      errmsg();
      exit(EXIT_FAILURE);
    }

    nread = recv(sfd, buf, BUF_SIZE, 0);
    if (nread == -1) {
      perror("read");
      exit(EXIT_FAILURE);
    }

    printf("Received %zd bytes: %s\n", nread, buf);
  }

  exit(EXIT_SUCCESS);
}
