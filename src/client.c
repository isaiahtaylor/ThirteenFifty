#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>

#include "client.h"

int create_rrq(char *filename, char *packet, int rrq_packet_size)
{
  memset(packet, 0, rrq_packet_size);

  packet[0] = 0;
  packet[1] = RRQ_OPCODE;

  strncat(&packet[2], filename, strlen(filename));
  strncat(&packet[2 + strlen(filename) + 1], MODE, strlen(MODE));

  return 2 + strlen(filename) + 1 + strlen(MODE) + 1;
}

void create_ack(char *block_num, char *packet, int ack_packet_size)
{
  memset(packet, 0, ack_packet_size);

  packet[0] = 0;
  packet[1] = ACK_OPCODE;

  packet[2] = block_num[0];
  packet[3] = block_num[1];
}

int get(char *target, char *port, char *filename, struct OPTION options[], int option_count)
{
  // Initializing options
  int BLOCKSIZE_OPTION = (int)NULL;
  int WINDOWSIZE_OPTION = (int)NULL;

  int error_length = 2 + 2 + strlen(ERROR_CODES[7]) + 1;

  int option_length = 0;
  if (options)
  {
    option_length = find_option_length(options, option_count);
    set_option_flags(options, option_count, &BLOCKSIZE_OPTION, &WINDOWSIZE_OPTION);
  }

  // Initializing sockets
  struct addrinfo hints, *servinfo, *temp_sock;
  int addrResult;
  int fd;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  struct sockaddr_storage their_addr;
  socklen_t addr_len;
  char recv_buffer[BLOCKSIZE_OPTION ? BLOCKSIZE_OPTION + 50 : BUFFER_LENGTH];

  // Allocating packet buffers
  size_t rrq_packet_size = 2 + strlen(filename) + 1 + strlen(MODE) + 1 + option_length;
  char *rrq_packet = malloc(rrq_packet_size);
  size_t ack_packet_size = ACK_SIZE;
  char *ack_packet = malloc(ack_packet_size);

  if ((addrResult = getaddrinfo(target, port, &hints, &servinfo)) != 0)
  {
    printf("Could not get address info.");
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
    printf("Could not create a socket with those DNS addresses.");
    return 2;
  }

  int numbytes;
  int acknumbytes;

  // create RRQ
  int rrq_raw_len = create_rrq(filename, rrq_packet, rrq_packet_size);
  if (options)
    create_options(rrq_packet, rrq_raw_len, options, option_count);

  // send out RRQ
  if ((numbytes = sendto(fd, rrq_packet, rrq_packet_size, 0, temp_sock->ai_addr, temp_sock->ai_addrlen)) == -1)
  {
    perror("Sending RRQ");
    exit(1);
  }
  printf("Sent %d bytes to %s on port %s\n", numbytes, target, port);

  // loop: listen for DATA, send ACK
  int iter = 0;
  int window_iter = 1;
  do
  {
    addr_len = sizeof their_addr;
    if ((numbytes = recvfrom(fd, recv_buffer, BUFFER_LENGTH - 1, 0, (struct sockaddr *)&their_addr, &addr_len)) == -1)
    {
      perror("Receiving packet");
      exit(1);
    }
    recv_buffer[numbytes] = '\0';

    printf("Received %d bytes from server\n", numbytes);

    int opcode = recv_buffer[1];

    switch (opcode)
    {
    case 1:
      continue;
      break;

    case 2:
      continue;
      break;

    case 3:
    {
      printf("- DATA packet\n");

      if (iter == 0 && options)
      {
        printf("  Server rejected all options, proceeding WITHOUT OPTIONS.\n");
        WINDOWSIZE_OPTION = (int)NULL;
        BLOCKSIZE_OPTION = (int)NULL;
      }

      if (window_iter >= WINDOWSIZE_OPTION)
      {
        window_iter = 1;
        // Send the ACK
        char block_num[2];
        block_num[0] = recv_buffer[2];
        block_num[1] = recv_buffer[3];

        create_ack(block_num, ack_packet, ack_packet_size);
        if ((acknumbytes = sendto(fd, ack_packet, ack_packet_size, 0, (struct sockaddr *)&their_addr, addr_len)) == -1)
        {
          perror("Sending ACK");
          exit(1);
        }
        printf("  ACKed: %d\n", block_num[1]);
        break;
      }
      else
      {
        window_iter++;
      }
    }
    case 4:
      continue;
      break;

    case 5:
    {
      printf("- ERROR packet\n");
      int error_code = recv_buffer[3];
      char error_msg[strlen(recv_buffer + 4)];
      strcpy(error_msg, recv_buffer + 4);

      printf("  Error: %s\n  Message: %s\n", ERROR_CODES[error_code], error_msg);
      break;
    }

    case 6:
    {
      if (iter == 0 && options)
      {
        printf("- OACK\n");
        if (process_oack(recv_buffer, &BLOCKSIZE_OPTION, &WINDOWSIZE_OPTION) == 0)
        {
          // ACK with block num 0
          printf("  options accepted and loaded\n");
          create_ack("\0\0", ack_packet, ack_packet_size);
          if ((acknumbytes = sendto(fd, ack_packet, ack_packet_size, 0, (struct sockaddr *)&their_addr, addr_len)) == -1)
          {
            perror("Sending ACK");
            exit(1);
          }
          printf("  ACKed: 0\n");
        }
        else
        {
          // Send error code 8 and exit
          char error_packet[error_length];
          create_oack_err(error_packet, error_length);

          if ((sendto(fd, error_packet, error_length, 0, (struct sockaddr *)&their_addr, addr_len)) == -1)
          {
            perror("Sending ERROR");
          }

          exit(1);
        }
      }
      else
      {
        continue;
        break;
      }
      break;
    }
    }

    iter++;
  } while (numbytes == 516);

  free(rrq_packet);
  free(ack_packet);

  return 0;
}

int main(int argc, char *argv[])
{
  if (argc != 4)
  {
    printf("Bad usage");
    return 2;
  }

  char *target = argv[1];
  char *port = argv[2];
  char *file = argv[3];

  printf("Getting %s from %s:%s\n", file, target, port);

  struct OPTION myOptions[1];

  myOptions[0].name = "blocksize";
  myOptions[0].value = "1024";
  myOptions[0].silent = 0;

  // myOptions[1].name = "windowsize";
  // myOptions[1].value = "4";
  // myOptions[1].silent = 0;

  get(target, port, file, myOptions, 1);
}
