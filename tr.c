#define _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include <pthread.h>
void *rt(void* ta)
{
	    int fd,i,sp,offset;
		    void *buffer;
			    fd = ((int*)ta)[0];
				    sp = ((int*)ta)[1];
					    offset = ((int*)ta)[2];

						    posix_memalign(&buffer, 4096,4096);
							    for (i=0;i<offset;++i)
									    {
											        pread(fd,buffer,4096,sp*4096+i*4096);

													        }
								    free(buffer);
}

int main()
{
	    int fd,mt,status,unit,i;
		    int* ta[64];
			    pthread_t pthread[64];

				    fd = open("data",O_RDONLY | O_DIRECT);
					
					    scanf("%d",&mt);
						    unit = 1024*1024/mt;
							    for (i=0;i<mt;i++)
									    {
											        ta[i] = malloc(4*sizeof(int));
													        ta[i][0] = fd;
															        ta[i][1] = unit*i;
																	        ta[i][2] = unit;
																			        pthread_create(&pthread[i],NULL,rt,(void*)ta[i]);
																					        printf("s %d\n",i);
																							    }
								
								
								    for (i=0;i<mt;i++)
										    {
												        pthread_join(pthread[i],NULL);
														        free(ta[i]);
																        printf("f %d\n",i);
																		    }
									    close(fd);
										
										    return 0;
}
