#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
int main()
{
	    int fd,i;
		    char aaa[4096]={'a',};
			    void *buffer;
				    aaa[4096] = 0;
					    posix_memalign(&buffer, 4096,4096);
						    memcpy(buffer,aaa,4096);
							    fd = open("data",O_RDWR | O_CREAT | O_APPEND | O_DIRECT | O_LARGEFILE);
								    for (i=1;i<=1024*1024;++i)
										        write(fd,buffer,4096);
									    close(fd);
										    return 0;
}

