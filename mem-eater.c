#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Author J.K. 2008

long getValue(char * what){
        char buff[1024];
	
        int fd = open("/proc/meminfo", O_RDONLY);
        int ret = read(fd, buff, 1023);

        buff[ret>1023 ? 1023: ret] = 0;
	
        char * line = strstr(buff, what);

	if (line == 0){
		printf("Error %s not found in %s \n", what, buff);
		exit(1);
	}

	line += strlen(what) + 1;


        while(line[0] == ' '){
                line++;
        }

        int pos = 0;
        while(line[pos] != ' '){
                pos++;
        }
        line[pos] = 0;

        close(fd);

        return atoi(line);
}

long getFreeRamKB(){
	return getValue("\nMemFree:") +getValue("\nCached:") + getValue("\nBuffers:");	
}

int preallocate(long long int maxRAMinKB){
	long long int currentRAMinKB = getFreeRamKB();

	printf ("starting to malloc RAM currently \n %lld KiB => goal %lld KiB\n", currentRAMinKB, maxRAMinKB);
	
	while(currentRAMinKB > maxRAMinKB){
		long long int delta = currentRAMinKB - maxRAMinKB;
		long long int toMalloc = (delta < 500 ? delta : 500) * 1024;

		char * allocP = malloc(toMalloc);
		if(allocP == 0){
			printf("could not allocate more RAM - retrying - free:%lld \n", currentRAMinKB);
			sleep(5);
		}else{
			memset(allocP, '1', toMalloc);
		}
		currentRAMinKB = getFreeRamKB();
	}

	printf ("Finished now \n %lld - %lld\n", currentRAMinKB, maxRAMinKB);
}	


