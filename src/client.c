#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include "client.h"

int grandom(int min, int max)
{
  return min + (rand() % (max - min));
}

char *create_rrq(char *filename)
{
  char *packet;
  int packet_size = 2 + (int) strlen(filename) + 1 + (int) strlen(MODE) + 1;

  packet = malloc(packet_size);
  memset(packet, 0, packet_size);

  // packet = RRQ_OPCODE;

  strncpy(packet, RRQ_OPCODE, 2);

  strncat(&packet[2], filename, strlen(filename));
  strncat(&packet[2 + strlen(filename) + 1], MODE, 8);
  packet[packet_size-1] = '\0';
  return packet;
}

char *create_ack(char *block_num)
{
  char *packet;
  int packet_size = ACK_SIZE;
  packet = malloc(packet_size);
  memset(packet, 0, packet_size);


  // packet[0] = ACK_OPCODE;
  
  strncpy(packet, ACK_OPCODE, 2);

  packet[2] = block_num[0];
  packet[3] = block_num[1];

  return packet;
}

int get(char *target, char *port, char *filename)
{
  // init
  // struct addrinfo hints, *servinfo;
  struct addrinfo hints, *servinfo, *temp_sock;
  int addrResult;
  int fd;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;

  struct sockaddr_storage their_addr;
  socklen_t addr_len;
  char recv_buffer[BUFFER_LENGTH];

  char tID[6];
  srand(time(NULL));

  if ((addrResult = getaddrinfo(target, INIT_PORT, &hints, &servinfo)) != 0)
  {
    printf("failed at 1");
    return 1;
  }

  for (temp_sock = servinfo; temp_sock != NULL; temp_sock = temp_sock->ai_next)
  {
    if ((fd = socket(temp_sock->ai_family, temp_sock->ai_socktype, temp_sock->ai_protocol)) == -1)
      continue;
    break;
  }

  if (temp_sock == NULL)
  {
    printf("failed at 2");
    return 2;
  }

  int numbytes;
  int acknumbytes;

  // create RRQ

  char *msg = create_rrq(filename);
  size_t msg_size = 2+strlen(filename)+1+strlen(MODE)+1;

  if ((numbytes = sendto(fd, msg, msg_size, 0, temp_sock->ai_addr, temp_sock->ai_addrlen)) == -1)
  {
    perror("CLIENT: sendto");
    exit(1);
  }
  printf("CLIENT: sent %d bytes to %s\n", numbytes, target);

  // send out RRQ

  // loop: listen for DATA, send ACK, each time check for
  do
  {
    addr_len = sizeof their_addr;
    if ((numbytes = recvfrom(fd, recv_buffer, BUFFER_LENGTH - 1, 0, (struct sockaddr *)&their_addr, &addr_len)) == -1)
    {
      perror("CLIENT: recvfrom");
      exit(1);
    }

    printf("Received %d bytes from server\n", numbytes);

    recv_buffer[numbytes] = '\0';

    // ACK

    char block_num[2];

    block_num[0] = recv_buffer[2];
    block_num[1] = recv_buffer[3];

    char *ack = create_ack(block_num);

    if ((acknumbytes = sendto(fd, ack, ACK_SIZE, 0, (struct sockaddr *)&their_addr, addr_len)) == -1)
    {
      perror("CLIENT ACK: sendto");
      exit(1);
    }
    printf("acked: %d%d\n", block_num[0], block_num[1]);

  } while (numbytes == 516);

  return 0;
}

int main(int argc, char *argv[])
{
  if (argc != 4) {
    printf("Bad usage");
    return 2;
  }

  char *target = argv[1];
  char *port = argv[2];
  char *file = argv[3];

  printf("Getting %s from %s:%s\n", file, target, port);

  get(target, port, file);
}
