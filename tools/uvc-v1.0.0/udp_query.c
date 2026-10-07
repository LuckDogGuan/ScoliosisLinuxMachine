#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
int main(int argc,char **argv) {
 if(argc!=2 || strlen(argv[1])>4096) return 2;
 int fd=socket(AF_INET,SOCK_DGRAM,0); if(fd<0) return 3;
 struct timeval tv={2,0}; setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
 struct sockaddr_in a={0}; a.sin_family=AF_INET; a.sin_port=htons(37020); inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);
 if(connect(fd,(struct sockaddr *)&a,sizeof(a))) return 4;
 if(send(fd,argv[1],strlen(argv[1]),0)<0) return 5;
 char b[32768]; ssize_t n=recv(fd,b,sizeof(b)-1,0); close(fd);
 if(n<=0) return 6;
 b[n]=0; puts(b); return 0;
}
