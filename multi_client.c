#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#define BUFSIZE 1024

struct sockaddr_in addr;
socklen_t addrlen;
char buf[BUFSIZE];
FILE *fp;

int init_sock(char ip_address[], int port, int type){
  int sock;
  if((sock = socket(AF_INET, type, 0)) < 0){
    perror("socket");
    return -1;
  }

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(ip_address);
  addrlen = sizeof(addr);

  if(type == SOCK_STREAM){
    connect(sock, (struct sockaddr *)&addr, addrlen);
  }
  return sock;
}

int request(char ip_address[]){
  // 先ずはTCPでサーバの50000ポートにリクエストして、専用のポート番号を貰う
  // 返り値はUDPのソケット
  int req_sock;
  if((req_sock = init_sock(ip_address, 50000, SOCK_STREAM)) < 0) {
    return -1;
  }
  int response;
  int resplen = 0;
  int ret_sock;
  send(req_sock, "req", 3*sizeof(char), 0);

  while(resplen == 0){
    if((resplen = recv(req_sock, &response, sizeof(response), 0)) < 0){
      perror("recv");
      return -1;
    }
  }
  close(req_sock);

  if((ret_sock = init_sock(ip_address, response, SOCK_DGRAM)) < 0){
    return -1;
  }


  // サーバの準備が終わる迄soxを起動しないよう待機
  sleep(1);
  sendto(ret_sock, "SYN", 3*sizeof(char), 0, (struct sockaddr *)&addr, addrlen);

  char ack[3] = {0};
  if(recvfrom(ret_sock, ack, sizeof(ack), 0, (struct sockaddr *)&addr, &addrlen) < 0) {
    perror("recvfrom");
    return -1;
  }
  if(memcmp(ack, "ACK", 3*sizeof(char)) != 0){
    perror("could not receive ACK");
    return -1;
  }

  sleep(1);
  return ret_sock;
}

void send_data(int sock){
  sendto(sock, buf, BUFSIZE, 0, (struct sockaddr *)&addr, addrlen);
}

void read_stdin(){
  fseek(fp, 0L, SEEK_END);
//  fseek(fp, (long)-BUFSIZE, SEEK_CUR);
  fread(buf, sizeof(char), BUFSIZE, fp);
}

void recv_data(int sock){
  recvfrom(sock, buf, BUFSIZE, 0, (struct sockaddr *)&addr, &addrlen);
  write(STDOUT_FILENO, buf, BUFSIZE);
}

int main(int argc, char *argv[]){
  if(argc != 2) {
    fprintf(stderr, "Usage: %s [IP address]\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  // 予めsox playのバッファにデータを流しておくとそこで遅延しない
  char tmpbuf[BUFSIZE] = {0};
  write(STDOUT_FILENO, tmpbuf, BUFSIZE);

  int sock;
  if((sock = request(argv[1])) < 0) {
    perror("request");
    exit(EXIT_FAILURE);
  }

  char cmd[] = "rec -q -V0 -t raw -b 16 -c 1 -e s -r 44100 --buffer 128 -";
  if((fp = popen(cmd, "r")) == NULL) {
    perror("popen");
    exit(EXIT_FAILURE);
  }

  while(1){
    read_stdin();
    send_data(sock);
    recv_data(sock);
  }
  pclose(fp);
  close(sock);
  return 0;
}
