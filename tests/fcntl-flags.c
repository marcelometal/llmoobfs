/* SPDX-License-Identifier: GPL-2.0-only */
/* Standalone reproducer for journal 0144 R1; argument must be a new file. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv) {
 char buf[16]={0}; int fd, flags; ssize_t n;
 if (argc!=2) return 2;
 fd=open(argv[1],O_CREAT|O_EXCL|O_RDWR|O_APPEND,0600);
 if(fd<0) {perror("open");return 2;}
 if(write(fd,"AAAA",4)!=4) {perror("write");return 2;}
 flags=fcntl(fd,F_GETFL);
 if(flags<0 || fcntl(fd,F_SETFL,flags & ~O_APPEND)) {perror("fcntl");return 2;}
 if(pwrite(fd,"Z",1,0)!=1) {perror("pwrite");return 2;}
 n=pread(fd,buf,sizeof(buf)-1,0);
 if(n<0) {perror("pread");return 2;}
 printf("FCNTL_RESULT path=%s bytes=%zd content=%s append_flag=%d\n",argv[1],n,buf,!!(fcntl(fd,F_GETFL)&O_APPEND));
 close(fd);
 return !(n==4 && !memcmp(buf,"ZAAA",4));
}
