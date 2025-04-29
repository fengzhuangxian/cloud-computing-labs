//此文件是老师上课写的代理服务器代码，某些地方上课带大家修改

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#define MAXREQ 256

pthread_mutex_t mutexConcurrentThreadNum;
int concurrentThreadNum=0;

typedef struct {
  int consockfd;  //fd between client and proxy
  int forwardClientSockfd;  //fd between proxy and server
  struct sockaddr_in destAddr;
} ThreadParas;

int sendAllChunk(int sock, char* buf, int chunkSize)
{
  int sentBytes=0;
  int len;
  while(1) {
    if(chunkSize-sentBytes==0)//This data chunk has all been sent
      break;
    len=send(sock,buf+sentBytes,chunkSize-sentBytes,0);
    if(len<0)
    {
      perror("TCP send");
      return -1;//Error
    }
    sentBytes=sentBytes+len;
  }
  return 0;//Success
}

int recvAllChunk(int sock, char* buf, int chunkSize)
{
  int receivedBytes=0;
  int len;
  while(1) {
    if(chunkSize-receivedBytes==0)//This data chunk has all been received
      break;
    len=recv(sock,buf+receivedBytes,chunkSize-receivedBytes,0);
    if(len<=0) {
      perror("TCP recv");
      return -1;//Error
    } 
    receivedBytes=receivedBytes+len;
  }
  return 0;//Success
}

void* proxy_send(void* args) {
  pthread_mutex_lock(&mutexConcurrentThreadNum);
  concurrentThreadNum++;
  pthread_mutex_unlock(&mutexConcurrentThreadNum);

  ThreadParas* para = (ThreadParas*) args;
  int consockfd=para->consockfd;
  int forwardClientSockfd=para->forwardClientSockfd;

  char recvbuf[MAXREQ];
  int n;
  
  while(1)                  
  { 
    memset(recvbuf,0, MAXREQ);
    n = read(consockfd,recvbuf,MAXREQ-1); /* Recv data*/
    if (n <= 0) {
      perror("Error: read from consockfd");
      break;
    }
    printf("recv %d bytes data from client:\n", n);
    n=write(STDOUT_FILENO, recvbuf, strlen(recvbuf));
    fflush(stdout);
    if(sendAllChunk(forwardClientSockfd,recvbuf,n)==-1) //Forward data 
      break;
    printf("send %d bytes data to server:\n", n);
  }
  close(consockfd);
  close(forwardClientSockfd);

  pthread_mutex_lock(&mutexConcurrentThreadNum);
  concurrentThreadNum--;
  pthread_mutex_unlock(&mutexConcurrentThreadNum);
  return NULL;
}

void* proxy_recv(void* args) {
  pthread_mutex_lock(&mutexConcurrentThreadNum);
  concurrentThreadNum++;
  pthread_mutex_unlock(&mutexConcurrentThreadNum);

  ThreadParas* para = (ThreadParas*) args;
  int consockfd=para->consockfd;
  int forwardClientSockfd=para->forwardClientSockfd;

  char recvbuf[MAXREQ];
  int n;
  
  while(1)                  
  { 
    memset(recvbuf,0, MAXREQ);
    n = read(forwardClientSockfd,recvbuf,MAXREQ-1); /* Recv data*/
    if (n <= 0) {
      perror("Error: read from forwardClientSockfd");
      break;
    }
    if(sendAllChunk(consockfd,recvbuf,n)==-1) //Forward data 
      break;
  }
  close(consockfd);
  close(forwardClientSockfd);

  pthread_mutex_lock(&mutexConcurrentThreadNum);
  concurrentThreadNum--;
  pthread_mutex_unlock(&mutexConcurrentThreadNum);
  return NULL;
}

void* proxy(void* args) {
  pthread_mutex_lock(&mutexConcurrentThreadNum);
  concurrentThreadNum++;
  pthread_mutex_unlock(&mutexConcurrentThreadNum);

  ThreadParas* para = (ThreadParas*) args;
  int consockfd=para->consockfd;
  struct sockaddr_in destAddr=para->destAddr;

//connect to dest
  int forwardClientSockfd;
  if((forwardClientSockfd=socket(AF_INET,SOCK_STREAM,0))<0)
  {
    perror("create forwardClientSockfd error");
    pthread_mutex_lock(&mutexConcurrentThreadNum);
    concurrentThreadNum--;
    pthread_mutex_unlock(&mutexConcurrentThreadNum);
    return NULL;
  }
  if(connect(forwardClientSockfd,(const struct sockaddr *)&destAddr,sizeof(destAddr))<0)
  {
    perror("connect forwardClientSockfd error");
    pthread_mutex_lock(&mutexConcurrentThreadNum);
    concurrentThreadNum--;
    pthread_mutex_unlock(&mutexConcurrentThreadNum);
    return NULL;
  }
  struct sockaddr_in localAddress;
  socklen_t addressLength = sizeof(localAddress);
  getsockname(forwardClientSockfd, (struct sockaddr*)&localAddress, &addressLength);
  printf("[setup proxy connection] I'm %s:%d \t connected to server %s:%d\n"
    ,inet_ntoa(localAddress.sin_addr),ntohs(localAddress.sin_port)
    ,inet_ntoa(destAddr.sin_addr),ntohs(destAddr.sin_port));


  ThreadParas *para2=(ThreadParas*)malloc(sizeof(ThreadParas));
  para2->consockfd=consockfd;
  para2->forwardClientSockfd=forwardClientSockfd;
//create send forward thread;
  pthread_t thSend;
  if(pthread_create(&thSend, NULL, proxy_send, para2)!=0)
  {
    perror("create proxy_send thread failed");
    pthread_mutex_lock(&mutexConcurrentThreadNum);
    concurrentThreadNum--;
    pthread_mutex_unlock(&mutexConcurrentThreadNum);
    return NULL;
  }
//create receive forward thread;  
  pthread_t thRecv;
  if(pthread_create(&thRecv, NULL, proxy_recv, para2)!=0)
  {
    perror("create proxy_recv thread failed");
    pthread_mutex_lock(&mutexConcurrentThreadNum);
    concurrentThreadNum--;
    pthread_mutex_unlock(&mutexConcurrentThreadNum);
    return NULL;
  }

  printf("proxy_send and proxy_recv threads created...\n");

  pthread_join(thSend, NULL);
  pthread_join(thRecv, NULL);
  free(para);
  free(para2);

  pthread_mutex_lock(&mutexConcurrentThreadNum);
  concurrentThreadNum--;
  pthread_mutex_unlock(&mutexConcurrentThreadNum);
  return NULL;
}


int main(int argc, char *argv[])
{
  if(argc!=4)
  {
    printf("usage: %s localPort destIP destPort\n",argv[0]);
    return 1;
  }
  int localPort=atoi(argv[1]);
  struct sockaddr_in my_addr; 
  my_addr.sin_family=AF_INET;
  my_addr.sin_addr.s_addr=INADDR_ANY;
  my_addr.sin_port=htons(localPort);

  struct in_addr destIP;
  if(!inet_aton(argv[2], &destIP)) 
    perror("inet_aton");
  struct sockaddr_in destAddr;
  memset(&destAddr,0,sizeof(destAddr));
  destAddr.sin_family=AF_INET;
  destAddr.sin_addr=destIP;
  int destPort=atoi(argv[3]);
  destAddr.sin_port=htons(destPort);

  int server_sockfd;
  if((server_sockfd=socket(AF_INET,SOCK_STREAM,0))<0)
  {  
    perror("socket");
    return 1;
  }

  int on=1;  
  if((setsockopt(server_sockfd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on)))<0)  
  {  
      perror("setsockopt failed");  
      return 1;  
  }  
  
  if (bind(server_sockfd,(struct sockaddr *)&my_addr,sizeof(my_addr))<0)
  {
    perror("bind");
    return 1;
  }

  socklen_t sin_size=sizeof(struct sockaddr_in);
  struct sockaddr_in remote_addr;
  if(listen(server_sockfd,5)<0)
  {
    perror("listen");
    return 1;
  }

  int inComeConnNum=0;
  pthread_mutex_init(&mutexConcurrentThreadNum,NULL);
  while (1) {
    pthread_mutex_lock(&mutexConcurrentThreadNum);
    if(concurrentThreadNum<=100)
      {
        pthread_mutex_unlock(&mutexConcurrentThreadNum);
        ;
      }
    else
      {
        pthread_mutex_unlock(&mutexConcurrentThreadNum);
        printf("Too many open threads (%d), wait a moment...\n", concurrentThreadNum);
        usleep(1000);
        continue;
      }

    int client_sockfd;
    if((client_sockfd=accept(server_sockfd,(struct sockaddr *)&remote_addr,&sin_size))<0)
    {
      perror("Error: accept");
      return 1;
    }
    inComeConnNum++;
    printf("[%d connections accepted] from client %s:%d\n",inComeConnNum,inet_ntoa(remote_addr.sin_addr),ntohs(remote_addr.sin_port));

    pthread_t th;
    ThreadParas *para=(ThreadParas*)malloc(sizeof(ThreadParas));
    para->consockfd=client_sockfd;
    para->destAddr=destAddr;
    if(pthread_create(&th, NULL, proxy, para)!=0)
    {
      perror("create proxy thread failed");
      exit(1);
    }
  }
  close(server_sockfd);

  return 0;
}