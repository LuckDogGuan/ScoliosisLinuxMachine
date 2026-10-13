#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv){
 if(argc!=2)return 2;
 int fd=socket(AF_INET,SOCK_DGRAM,0); if(fd<0)return 3;
 struct timeval tv={3,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
 struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_port=htons(37020);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 if(sendto(fd,argv[1],strlen(argv[1]),0,(struct sockaddr*)&a,sizeof(a))<0)return 4;
 char b[16384];int n=recv(fd,b,sizeof(b)-1,0);if(n<0){perror("recv");return 5;}
 b[n]=0;puts(b);close(fd);return 0;
}
