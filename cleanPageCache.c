#include <stdlib.h>
#include <string.h>


int main(){
	int i;
	printf("CLEANING CACHE\n");
	while(1){
   	char * x = (char*) malloc(1024*1024*10);
   	for(i=0; i < 1024*1024*10; i+=512){
   		x[i] = (char) i;
   	}
 	}
	prinf("DONE\n");
}

