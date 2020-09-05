#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/shm.h>
#include <netinet/in.h>
#include <signal.h>
#define BUFSIZE 1024
#define MAX_PARTICIPANTS 100

struct sockaddr_in addr;
socklen_t addrlen;
int maxport = 50000;

struct participants_data {
  int shbuf_id[MAX_PARTICIPANTS]; // shmgetによる共有メモリのid
  int id_is_valid[MAX_PARTICIPANTS]; // 其インデックスのidが有効か
  int id_size; // MAX_PARTICIPANTSの内どのインデックス迄使われているか
  int participants_num;
};
int participants_data_id;
struct participants_data *pdata;
int myindex = -1;
int myid_size;

struct sharebuf {
  char buf[BUFSIZE];
};
struct sharebuf *shbuf[MAX_PARTICIPANTS];

void remove_participants_data();

int set_sigaction(void (*func)(int)) {
  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = func;
  if(sigaction(SIGINT, &act, NULL) < 0) {
    perror("sigaction");
    return -1;
  }
  return 0;
}

void init_participants_data() {
  if((participants_data_id = shmget(IPC_PRIVATE, sizeof(struct participants_data), IPC_CREAT|0666)) < 0) {
    perror("shmget");
    exit(EXIT_FAILURE);
  }

  if((pdata = (struct participants_data *)shmat(participants_data_id, NULL, 0)) == (void *)-1) {
    perror("shmat");
    remove_participants_data();
    exit(EXIT_FAILURE);
  }

  // initialize
  memset(pdata->shbuf_id, 0, sizeof(int) * MAX_PARTICIPANTS);
  memset(pdata->id_is_valid, 0, sizeof(int) * MAX_PARTICIPANTS);
  pdata->id_size = 0;
  pdata->participants_num = 0;
}

void unmap_participants_data() {
  if(participants_data_id >= 0) {
    if(shmdt(pdata) < 0)
      perror("shmdt");
  }
}

void remove_participants_data() {
  unmap_participants_data();

  if(shmctl(participants_data_id, IPC_RMID, NULL) < 0) {
    perror("shmctl");
    exit(EXIT_FAILURE);
  }
}

void unmap_sharebuf(int id, struct sharebuf *buf) {
  if(id >= 0) {
    if(shmdt(buf) < 0)
      perror("shmdt");
  }
}

void remove_sharebuf(int id, struct sharebuf *buf) {
  unmap_sharebuf(id, buf);

  if(shmctl(id, IPC_RMID, NULL) < 0) {
    perror("shmctl");
  }
}

void unmap_all_sharebuf() {
  if(myindex >= 0) {
    memset(shbuf[myindex]->buf, 0, sizeof(char) * BUFSIZE); // 事故でバッファが残って、雑音が残り続けるのを防ぐ為
    pdata->id_is_valid[myindex] = 0;
    pdata->participants_num--;
  }

  for(int i = 0; i < myid_size; ++i) {
    // 自プロセスの担当するバッファだったらid自体消去する
    if(i == myindex)
      remove_sharebuf(pdata->shbuf_id[i], shbuf[i]);
    else if(pdata->id_is_valid[i])
      unmap_sharebuf(pdata->shbuf_id[i], shbuf[i]);
  }
  unmap_participants_data();
}

void signal_handler_forparent(int sig) {
  remove_participants_data();
  printf("Successfully shut down the server\n");
  exit(EXIT_SUCCESS);
}

void signal_handler_forchild(int sig) {
  unmap_all_sharebuf();
  printf("Successfully shut down port %d\n", maxport);
  exit(EXIT_SUCCESS);
}

int init_sock(int port, int type){
  int retsock;
  if((retsock = socket(AF_INET, type, 0)) < 0) {
    perror("socket");
    return -1;
  }

  addrlen = sizeof(addr);
  memset((char *)&addr, 0, addrlen);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = INADDR_ANY;

  if(type == SOCK_DGRAM){
    // UDPならタイムアウトを3秒に設定
    struct timeval timeout = {3, 0};
    if(setsockopt(retsock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
      perror("setsockopt");
      close(retsock);
      return -1;
    }
  }

  if(bind(retsock, (struct sockaddr *)&addr, addrlen) < 0) {
    perror("bind");
    close(retsock);
    return -1;
  }

  if(type == SOCK_STREAM){
    // TCPなら受信待ちできるように設定
    if(listen(retsock, 5) < 0) {
      perror("listen");
      close(retsock);
      return -1;
    }
  }

  printf("port: %d\n", port);
  return retsock;
}

int first_synchronize_buffer() {
  // 共有メモリの内、自分のプロセスが書き込めるもののインデックスを発行
  myindex = pdata->id_size++;
  // 其メモリのidを発行
  if((pdata->shbuf_id[myindex] = shmget(IPC_PRIVATE, sizeof(struct sharebuf), IPC_CREAT|0666)) < 0) {
    perror("shmget");
    unmap_participants_data();
    return -1;
  }

  // 自分の共有メモリidを有効にする
  pdata->id_is_valid[myindex] = 1;
  pdata->participants_num++;

  // idを用いて、bufsの其々のsharebufに共有メモリを格納する
  for(int i = 0; i <= myindex; ++i) {
    if(pdata->id_is_valid[i]) {
      if((shbuf[i] = (struct sharebuf *)shmat(pdata->shbuf_id[i], NULL, 0)) == (void *)-1) {
        perror("shmat");
        unmap_all_sharebuf();
        return -1;
      }
    }
  }

  myid_size = myindex + 1;
  memset(shbuf[myindex]->buf, 0, sizeof(char) * BUFSIZE);
  return 0;
}

void synchronize_buffer() {
  if(myid_size < pdata->id_size) {
    for(int i = myid_size; i < pdata->id_size; ++i) {
      if(pdata->id_is_valid[i]) {
        if((shbuf[i] = (struct sharebuf *)shmat(pdata->shbuf_id[i], NULL, 0)) == (void *)-1) {
          perror("shmat");
          unmap_all_sharebuf();
          exit(EXIT_FAILURE);
        }
      }
    }

    myid_size = pdata->id_size;
  }
}

int synack(int new_sock) {
  char syn[3] = {0};
  if(recvfrom(new_sock, syn, 3*sizeof(char), 0, (struct sockaddr *)&addr, &addrlen) <= 0) {
    printf("syn: %s\n",syn);
    fprintf(stderr, "could not receive SYN from %d\n", maxport);
    return -1;
  }

  if(memcmp(syn, "SYN", 3*sizeof(char)) == 0){
    printf("received SYN from %d\n", maxport);
    sleep(1);
    sendto(new_sock, "ACK", 3*sizeof(char), 0, (struct sockaddr *)&addr, addrlen);
  } else {
    return -1;
  }

  return 0;
}

void recv_data(int sock){
  int active = 0;
  ssize_t recvlen;
  char sumbuf[BUFSIZE];
  char recvbuf[BUFSIZE]; //recvfromのタイミング

  while(1){
//    recvlen = recvfrom(sock, shbuf[myindex]->buf, BUFSIZE, 0, (struct sockaddr *)&addr, &addrlen);
    recvlen = recvfrom(sock, recvbuf, BUFSIZE, 0, (struct sockaddr *)&addr, &addrlen);
    if(recvlen > 0){
      active = 1;
    } else if(recvlen <= 0 && active == 1){
      // 接続が切れたら終わり
      break;
    } else if(recvlen < 0) {
      perror("recvfrom");
      unmap_all_sharebuf();
      exit(EXIT_FAILURE);
    }
    memcpy(shbuf[myindex]->buf, recvbuf, sizeof(char) * BUFSIZE);

    memset(sumbuf, 0, sizeof(sumbuf));
    for(int i = 0; i < myid_size; ++i) {
      if(pdata->id_is_valid[i]) {
        for(int j = 0; j < BUFSIZE; ++j) {
          sumbuf[j] += shbuf[i]->buf[j] / pdata->participants_num;
        }
      }
    }
    sendto(sock, sumbuf, BUFSIZE, 0, (struct sockaddr *)&addr, addrlen);

    synchronize_buffer();
  }
}

void accept_request(){
  int acc_sock, handshake_sock, new_sock;
  pid_t child_pid;
  if((acc_sock = init_sock(50000, SOCK_STREAM)) < 0){
    perror("init_sock");
    remove_participants_data();
    exit(EXIT_FAILURE);
  }

  while(1){
    if((handshake_sock = accept(acc_sock, (struct sockaddr *)&addr, &addrlen)) < 0){
      perror("accept");
      close(acc_sock);
      remove_participants_data();
      exit(EXIT_FAILURE);
    }
    if(++maxport > 65535){
      continue;
    }

    if((child_pid = fork()) < 0){
      perror("fork");
      close(acc_sock);
      close(handshake_sock);
      remove_participants_data();
      exit(EXIT_FAILURE);

    } else if(child_pid == 0) { // child
      close(acc_sock);
      close(handshake_sock);
      if(set_sigaction(signal_handler_forchild) < 0) {
        exit(EXIT_FAILURE);
      }
      if((pdata = (struct participants_data *)shmat(participants_data_id, NULL, 0)) == (void *)-1) {
        perror("shmat");
        unmap_participants_data();
        exit(EXIT_FAILURE);
      }

      if(first_synchronize_buffer() < 0) {
        perror("first_synchronize_buffer");
        exit(EXIT_FAILURE);
      }
      if((new_sock = init_sock(maxport, SOCK_DGRAM)) < 0) {
        perror("init_sock");
        unmap_all_sharebuf();
        exit(EXIT_FAILURE);
      }

      // Macだとプロセスが分岐してソケットを作る度にファイアウォールの設定を訊かれる。
      // 其間にクライアント側でsoxが起動すると、音声のバッファが溜まって遅延の原因になる。
      // だからこちらからokを送る迄待たせる
      if(synack(new_sock) < 0) {
        close(new_sock);
        unmap_all_sharebuf();
        exit(EXIT_FAILURE);
      }

      // 処理に入る
      recv_data(new_sock);
      unmap_all_sharebuf();
      close(new_sock);
      exit(EXIT_SUCCESS);
    }

    // parent
    if(send(handshake_sock, &maxport, sizeof(maxport), 0) < 0){
      perror("send");
      close(acc_sock);
      close(handshake_sock);
      remove_participants_data();
      exit(EXIT_FAILURE);
    }

    close(handshake_sock);
  }
  close(acc_sock);
  close(handshake_sock);
}

int main(){
  init_participants_data();
  if(set_sigaction(signal_handler_forparent) < 0) {
    exit(EXIT_FAILURE);
  }

  accept_request();
  remove_participants_data();
  return 0;
}
