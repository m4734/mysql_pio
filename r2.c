#define _GNU_SOURCE


#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
int main()
{
	    int fd,i;
		    void *buffer;
			    posix_memalign(&buffer, 4096*1024,4096*1024);
				    fd = open("data",O_RDONLY | O_CREAT | O_APPEND | O_DIRECT | O_LARGEFILE);
					    for (i=1;i<=1024;++i)
							        read(fd,buffer,4096*1024);
						//  printf("%c\n",(char*)buffer[4095]);
      close(fd);
          return 0;
          }
         
