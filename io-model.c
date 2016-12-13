#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>

#include <mpi.h>

#include "mem-eater.c"

#ifndef VERSION
#warning "NO VERSION DEFINED"
#define VERSION "unknown"
#endif

static int rank;
static int size;

enum locality{
   LOCALITY_OFF0 = 0,
   LOCALITY_SEQUENTIAL = 1,
   LOCALITY_RANDOM = 2,
   LOCALITY_STRIDED = 3,
   LOCALITY_RANDOM_NEARBY = 4,
   LOCALITY_REVERSE_STRIDED = 5,
   LOCALITY_RANDOM_NEARBY_STRIDE = 6,
};

struct options{
   char * filename;
   size_t memoryBufferInMiB;
   size_t fileSizeInMiB;
   size_t maxRepeats;
   size_t accessSize;
   size_t preallocateMemoryInMiB;

   enum locality localityInMemory;
   enum locality localityInFile;

   // based on the locality setting
   size_t localityMemParameter;
   size_t localityFileParameter;

   size_t localityMemParameter2;
   size_t localityFileParameter2;

   int truncate;
   int preWriteMem;
   int preWriteFile;
   int isRead;
   int isWaitForProperSize;
   int printOffsets;
};

struct proc_stats{
  uint64_t rchar;
  uint64_t wchar;
  uint64_t syscr; // read syscalls
  uint64_t syscw; // write syscalls
  uint64_t read_bytes; // actually fetched from the storage layer
  uint64_t write_bytes; // actually written to the storage layer
};

typedef struct proc_stats proc_stats_t;

struct runtime{
   char * buff;
   FILE * outputFile;
   size_t memBufferInBytes;
   size_t fileSizeInByte;

   // which offset is the last one we can use to write a full accessSize from mem to file
   size_t lastMemOffset;
   size_t lastFileffset;

   // max number of repeats fitting in the file
   size_t repeatsToMatchFileSize;

   // actual number of repeats done
   size_t doneRepeats;

   // for sequential access and nearby access
   int64_t curMemPosition;
};

static struct runtime r;
static struct options o;

static char * mmalloc(size_t size){
   char * buff = malloc(size);
   if (buff == NULL){
      printf("Error could not allocate %llu MiB of memory\n", (long long unsigned) size/1024/1024);
      MPI_Abort(MPI_COMM_WORLD, 1);
   }
   return buff;
}

typedef struct timespec Timer;

static void timerStart(Timer *tp)
{
    clock_gettime(CLOCK_MONOTONIC, tp);
}

static float timeToFloat(Timer tp){
  return (tp.tv_sec) + 0.001*0.001*0.001 * (tp.tv_nsec);
}

static double timerEnd(Timer *start)
{
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return (tp.tv_sec - start->tv_sec) + 0.001*0.001*0.001 * (tp.tv_nsec -start->tv_nsec);
}

static int finish_background_thread = 0;

static char *stat_names[] = {"rchar:", "wchar:", "syscr:", "syscw:", "read_bytes:", "write_bytes:", "cancelled_write_bytes:"};
static size_t old_stats[7] = {0,0,0,0,0,0,0};

void * background_thread(void * arg){
  proc_stats_t * p = (proc_stats_t*) arg;

  Timer t;
  while(! finish_background_thread){
    timerStart(& t);
    float ft = timeToFloat(t);
    printf("%.5f\n", ft);
    // store current values from proc into: proc_stats_t
    sleep(1);
  }
  return NULL;
}


static int MYopen(const char *pathname, int flags, mode_t mode){
   int ret;
   ret = open(pathname, flags, mode);
   if(ret == 0){
      printf("Error opening %s: %s\n", pathname, strerror(errno));
   }
   return ret;
}

static off_t MYlseek(int fd, off_t offset, int whence){
   off_t ret;
   ret = lseek(fd, offset, whence);
   if(ret == (off_t) -1){
      printf("Error lseek to %llu: %s\n", (long long unsigned) offset, strerror(errno));
   }

   return ret;
}

static void checkIOError(size_t expected, size_t returned){
   if(expected != returned){
      printf("Error while checking for I/O access: \"%s\", %llu != %llu\n",
         strerror(errno),
         (long long unsigned) expected,
         (long long unsigned) returned
        );
   }
}

static inline void initMemPos(){
   switch(o.localityInMemory){
      case(LOCALITY_SEQUENTIAL):{
         r.curMemPosition = 0;
         return;
      }
      case(LOCALITY_OFF0):{
         r.curMemPosition = 0;
         return;
      }
      case(LOCALITY_RANDOM):{
         r.curMemPosition = (((size_t) rand())*128) % r.lastMemOffset;
         return;
      }
      case(LOCALITY_RANDOM_NEARBY):{
         r.curMemPosition = -((int64_t) o.localityMemParameter) + ((size_t) rand()) % (2*o.localityMemParameter);
         return;
      }
      case(LOCALITY_STRIDED):{
         r.curMemPosition = o.localityMemParameter;
         return;
      }
      case(LOCALITY_RANDOM_NEARBY_STRIDE):{
         r.curMemPosition = o.localityMemParameter + -((int64_t) o.localityMemParameter2) + ((size_t) rand()) % (2*o.localityMemParameter2);
         if (r.curMemPosition > r.lastMemOffset ){
            r.curMemPosition = r.curMemPosition % r.lastMemOffset;
         }
         return;
      }
      case(LOCALITY_REVERSE_STRIDED):{
         r.curMemPosition = r.lastMemOffset - o.localityMemParameter;
         return;
      }
   }
}

static inline int64_t pickNextMemPos(){
   switch(o.localityInMemory){
      case(LOCALITY_SEQUENTIAL):{
         int64_t oldPos = r.curMemPosition;
         r.curMemPosition += o.accessSize;
         if (r.curMemPosition > r.lastMemOffset){
            r.curMemPosition = r.curMemPosition % r.lastMemOffset;
         }
         return oldPos;
      }
      case(LOCALITY_OFF0):{
         return 0;
      }
      case(LOCALITY_RANDOM):{
         return (((size_t) rand())*128) % r.lastMemOffset;
      }
      case(LOCALITY_RANDOM_NEARBY):{
         int64_t randomValue = - ((int64_t) o.localityMemParameter) + ((size_t) rand()) % (2*o.localityMemParameter);
         int64_t targetPos = r.curMemPosition + randomValue;

         randomValue += randomValue < 0 ? - o.accessSize : o.accessSize;

         if ( targetPos < 0 || targetPos > r.lastMemOffset ){
            targetPos = r.curMemPosition - randomValue;
         }
         return targetPos;
      }
      case(LOCALITY_STRIDED):{
         int64_t oldPos = r.curMemPosition;
         r.curMemPosition += o.accessSize + o.localityMemParameter;
         if (r.curMemPosition > r.lastMemOffset){
            r.curMemPosition = r.curMemPosition % r.lastMemOffset;
         }
         return oldPos;
      }
      case(LOCALITY_RANDOM_NEARBY_STRIDE):{
         int64_t oldPos = r.curMemPosition;
         r.curMemPosition += o.accessSize + o.localityMemParameter;

         int64_t randomValue = - ((int64_t) o.localityMemParameter2) + ((size_t) rand()) % (2*o.localityMemParameter);

         r.curMemPosition = r.curMemPosition + randomValue;
         if (r.curMemPosition > r.lastMemOffset ){
            r.curMemPosition = r.curMemPosition % r.lastMemOffset;
         }
         return oldPos;
      }
      case(LOCALITY_REVERSE_STRIDED):{
         int64_t oldPos = r.curMemPosition;

         r.curMemPosition = r.curMemPosition - (o.accessSize + o.localityMemParameter);
         if (r.curMemPosition < 0){
            r.curMemPosition = r.curMemPosition % r.lastMemOffset;
         }
         return oldPos;
      }
   }
}

static inline off_t setNextFilePos(int fd){
   switch(o.localityInFile){
      case(LOCALITY_SEQUENTIAL):{
         // nothing needed
         return lseek(fd, 0, SEEK_CUR);
      }
      case(LOCALITY_OFF0):{
         MYlseek(fd, 0, SEEK_SET);
         return 0;
      }
      case(LOCALITY_RANDOM):{
         off_t newPos = ((off_t) rand())*10 % r.lastFileffset;
         MYlseek(fd, newPos, SEEK_SET);
         return newPos;
      }
      case(LOCALITY_RANDOM_NEARBY):{
         off_t   newPos = lseek(fd, 0, SEEK_CUR);
         int64_t randomValue = - ((int64_t) o.localityFileParameter) + ((size_t) rand()) % (o.localityFileParameter*2);
         // add or subtract the lastly accessed data block.
         //printf("xq to %lld %lld\n", (long long int) randomValue, (long long int) newPos);
         if (randomValue < 0 ) {
            randomValue -= 2*o.accessSize;
         }

         if (newPos + randomValue > r.lastFileffset || newPos + randomValue < 0 ){
            newPos = newPos - randomValue;
         }else{
            newPos = newPos + randomValue;
         }
         //printf("jumping to %lld\n", (long long int) newPos);
         MYlseek(fd, newPos, SEEK_SET);
         return newPos;
      }
      case(LOCALITY_STRIDED):{
         off_t  newPos = lseek(fd, 0, SEEK_CUR) + o.localityFileParameter;
         if (newPos > r.lastFileffset){
            newPos = newPos % r.lastFileffset;
         }
         MYlseek(fd, newPos, SEEK_SET);
         return newPos;
      }
      case(LOCALITY_REVERSE_STRIDED):{
         off_t  newPos = lseek(fd, 0, SEEK_CUR) - 2* o.accessSize - o.localityFileParameter;
         if (newPos < 0 ){
            newPos = newPos % r.lastFileffset;
         }
         MYlseek(fd, newPos, SEEK_SET);
         return newPos;
      }
      case(LOCALITY_RANDOM_NEARBY_STRIDE):{
         off_t  newPos = lseek(fd, 0, SEEK_CUR) + o.localityFileParameter;
         int64_t randomValue = - ((int64_t) o.localityFileParameter2) + ((size_t) rand()) % (o.localityFileParameter2*2);

         // add or subtract the lastly accessed data block.
         //printf("xq to %lld %lld\n", (long long int) randomValue, (long long int) newPos);
         newPos += randomValue;

         if (newPos > r.lastFileffset){
            newPos = newPos % r.lastFileffset;
         }
         MYlseek(fd, newPos, SEEK_SET);
         return newPos;
      }
   }

}


typedef ssize_t(*iooperation) (int fd, void *buf, size_t count);

static void runBenchmark(int fd, double * times, float * start_times, size_t repeats, off_t * offsets){
   Timer t;

   iooperation op;
   if (o.isRead){
      op = read;
   }else{
      op = (iooperation) write;
   }

   size_t ret;
   int64_t memPos;

   initMemPos();

   for (size_t i = 0 ; i < repeats; i++){
      timerStart(& t);

      memPos = pickNextMemPos();
      off_t offset = setNextFilePos(fd);

      #ifdef DEBUG
         printf("Pos mem:%llu file:%llu\n", (long long unsigned) memPos, (long long unsigned) lseek(fd, 0, SEEK_CUR));
      #endif

      ret = op(fd, r.buff + memPos, o.accessSize);
      checkIOError(o.accessSize, ret);

      times[i] = timerEnd(& t);
      start_times[i] = timeToFloat(t);
      offsets[i] = offset;
   }
}

void print_data(char * buff){
  if(rank == 0){
     printf("%d: %s", 0, buff);
     for(int i=1; i < size; i++){
       MPI_Recv(buff, 4096, MPI_BYTE, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
       printf("%d: %s", i, buff);
     }
  }else{
   MPI_Send(buff, 4096, MPI_BYTE, 0, 0, MPI_COMM_WORLD);
  }
}

static void runBenchmarkWrapper(){
   int flags = o.isRead ? O_RDONLY : O_WRONLY | O_CREAT;
   int fd = MYopen(o.filename, flags, S_IRWXU);

   // malloc space to remember our internal measurement
   double * times = (double*) mmalloc(sizeof(double) * o.maxRepeats);
   float * start_times = (float*) mmalloc(sizeof(float) * o.maxRepeats);
   off_t * offsets = (off_t*) mmalloc(sizeof(off_t) * o.maxRepeats);

   Timer totalRunTimer;
   timerStart(& totalRunTimer);

   // now choose the benchmark to run based on the configuration

   // if sequential:
   if (o.localityInFile == LOCALITY_SEQUENTIAL){
      r.doneRepeats = r.fileSizeInByte / o.accessSize;
      if (r.doneRepeats > o.maxRepeats){
         r.doneRepeats = o.maxRepeats;
      }
   }else{
      r.doneRepeats = o.maxRepeats;
   }

   // sanity check
   if(r.doneRepeats == 0){
      printf("ERROR: number of repeats == 0\n");
      MPI_Abort(MPI_COMM_WORLD, 1);
   }
   pthread_t thread;
   proc_stats_t * proc_stats;
   if (rank == 0){
     proc_stats = (proc_stats_t*) mmalloc(sizeof(proc_stats_t) * o.maxRepeats + 10);
     // assume at most one second per I/O operation ...
     pthread_create(& thread, NULL, background_thread, proc_stats);
   }

   runBenchmark(fd, times, start_times, r.doneRepeats, offsets);
   double syncTime = timerEnd(& totalRunTimer);

   fsync(fd);
   close(fd);

   double totalRuntime = timerEnd(& totalRunTimer);
   if (rank == 0){
     int retval;
     finish_background_thread = 1;
     pthread_join(thread, (void *) & retval);
   }

   char buff[4096];

   sprintf(buff, "Runtime:%.12fs ops/s:%.2f  MiB/s:%.2f repeats:%llu syncTime:%.12fs \n",
      totalRuntime,
      ((float) r.doneRepeats) / totalRuntime,
      ((float) r.doneRepeats * o.accessSize) /1024.0 / 1024.0 / totalRuntime,
      (long long unsigned) r.doneRepeats,
      totalRuntime - syncTime
    );
    print_data(buff);

   // print statistics about the individual measurements

    char fname[100];
    sprintf(fname, "out-%d.csv", rank);
    FILE * out = fopen(fname, "w");
    fprintf(out, "start_time, duration, \n");
    for (size_t i = 1; i < r.doneRepeats; i++){
     fprintf(out, "%.3f,%.12f\n", start_times[i], times[i]);
    }
    fclose(out);


   if (o.printOffsets){
     printf("Offset per operation: %llu", (long long unsigned) offsets[0]);
     for (size_t i = 1; i < r.doneRepeats; i++){
        printf(", %llu",  (long long unsigned)  offsets[i]);
     }
     printf("\n");
   }

   free(times);
   free(offsets);

   if(rank == 0){
     free(proc_stats);
   }
}

static void dumpStats(const char * prefix, size_t repeats){
   // now dump the statistics from /proc/self/io
   FILE * f = fopen("/proc/self/io", "r");
   char buff[1023];
   int ret;
   long long unsigned value;
   size_t delta[7] = {0,0,0,0,0,0,0};

   for( int i=0; i < 7 ; i++ ){
      ret = fscanf(f, "%s %llu", buff, & value);
      if (ret != 2){
         printf("Error accessing /proc/self/io, read only %d tokens\n", ret);
         fclose(f);
         return;
      }
      ret = strcmp(buff, stat_names[i]);
      if (ret != 0){
         printf("%s %s", buff, stat_names[i]);
         printf("Error accessing /proc/self/io\n");
         fclose(f);
         return;
      }else{
         delta[i] = value - old_stats[i];
         old_stats[i] = value;
      }
   }


   if (prefix != NULL){
      //long size = ftell(f);
      //delta[0] -= size;
      delta[2] -= 1;

      char buff[4096];
      int pos = 0;
      pos += sprintf(buff, "%s perRepeat", prefix);
      for( int i=0; i < 7 ; i++ ){
         pos += sprintf(buff + pos, " %s %.1f", stat_names[i], (float) delta[i] / repeats);
      }
      pos += sprintf(buff + pos, "\n");

      if(rank == 0){
        fprintf(r.outputFile, "%d: %s", 0, buff);

        for(int i=1; i < size; i++){
           MPI_Recv(buff, 4096, MPI_BYTE, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
           fprintf(r.outputFile, "%d: %s", i, buff);
         }

      }else{
        MPI_Send(buff, 4096, MPI_BYTE, 0, 0, MPI_COMM_WORLD);
      }
   }
   fclose(f);
}

static void parseLocality(const char * str, enum locality * locality, size_t * out_arg, size_t * out_arg2){
   *out_arg = 0;
   *out_arg2 = 0;
   if (strncmp(str, "off0", 5) == 0){
      *locality = LOCALITY_OFF0;
      return ;
   }
   if (strncmp(str, "seq", 4) == 0){

      *locality = LOCALITY_SEQUENTIAL;
      return ;
   }
   if (strncmp(str, "rnd", 4) == 0){
      *locality = LOCALITY_RANDOM;
      return ;
   }
   if (strncmp(str, "rnd", 3) == 0){
      *out_arg = atoll(& str[3]);
      *locality = LOCALITY_RANDOM_NEARBY;
      return;
   }
   if (strncmp(str, "stride", 6) == 0){
      *out_arg = atoll(& str[6]);
      if (strstr(& str[6], ",") != NULL){
         // we also have a random value
         *locality = LOCALITY_RANDOM_NEARBY_STRIDE;
         *out_arg2 = atoll(strstr(& str[6], ",") + 1);
      }else{
         *locality = LOCALITY_STRIDED;
      }
      return;
   }
   if (strncmp(str, "reverse", 7) == 0){
      *out_arg = atoll(& str[7]);
      *locality = LOCALITY_REVERSE_STRIDED;
      return;
   }
   printf("Error cannot parse locality %s\n", str);
   MPI_Abort(MPI_COMM_WORLD, 1);
}

int main(int argc, char ** argv){
   MPI_Init(& argc, &argv);
   MPI_Comm_size(MPI_COMM_WORLD, & size);
   MPI_Comm_rank(MPI_COMM_WORLD, & rank);

   if (argc < 15 && rank == 0){
      printf("Synopsis: %s <file> <memoryBufferInMiB> <fileSizeInMiB> <MaxRepeats> <Truncate=0|1> <accessSize> <localityInMemory> <localityInFile> <preallocateMemoryRemainsInMiB or all=0> <preWriteMemBuffer> <preWriteFile> <R|W ReadOrWrite> <SEED> <WaitForProperSize=0|1>\n", argv[0]);

      printf("Locality:\n");
      printf("off0: always start at offset 0 == 0\n");
      printf("seq: Sequential == 1\n");
      printf("rnd: Random == 2\n");
      printf("rndX: Random with max X delta offset \n");
      printf("strideX: sequential + add hole of size X with X=0 is seq\n");
      printf("strideX,Y: sequential with stride X and y as +-randomOffset\n");
      printf("reverseX: inverse sequential + add hole of size X\n");


      printf("PreWrite means if the memory buffer or File should be completely written with the given size before doing any test\n");
      printf("programversion:%s\n", VERSION);

      MPI_Abort(MPI_COMM_WORLD, 1);
   }

   // prefix rank
   o.filename = malloc(10+strlen(argv[1]));
   sprintf(o.filename, "%s-%d", argv[1], rank);

   r.outputFile = stdout; //fopen(argv[2], "w");
   o.memoryBufferInMiB = atoll(argv[2]);
   o.fileSizeInMiB = atoll(argv[3]);
   o.maxRepeats = atoll(argv[4]);
   o.truncate = atoi(argv[5]);
   o.accessSize = atoll(argv[6]);
   parseLocality(argv[7], & o.localityInMemory, & o.localityMemParameter, & o.localityMemParameter2);
   parseLocality(argv[8], & o.localityInFile, & o.localityFileParameter, & o.localityFileParameter2);
   o.preallocateMemoryInMiB = atoll(argv[9]);
   o.preWriteMem = atoi(argv[10]);
   o.preWriteFile = atoi(argv[11]);
   o.isRead = (argv[12][0] == 'R');
   r.curMemPosition = 0;

   int seed = atoi(argv[13]);
   if (seed != -1){
     srand(seed);
   }else{
     printf("Using PID as seed\n");
     srand((int) getpid());
   }
   o.isWaitForProperSize = atoi(argv[14]);

   if(rank == 0){
     printf("%s file:%s memBuffer:%llu fileSizeInMiB:%llu maxRepeats:%llu truncate:%d accessSize:%llu localityMem:%d-%lld-%lld localityFile:%d-%lld-%lld preallocateMemoryInMiB:%llu preWriteMem:%d preWriteFile:%d isRead:%d seed:%s waitForProperSize:%d programversion:%s\n",
        argv[0],
        o.filename,
        (long long unsigned) o.memoryBufferInMiB,
        (long long unsigned) o.fileSizeInMiB,
        (long long unsigned) o.maxRepeats,
        o.truncate,
        (long long unsigned) o.accessSize,
        o.localityInMemory,
        (long long unsigned) o.localityMemParameter,
        (long long unsigned) o.localityMemParameter2,
        o.localityInFile,
        (long long unsigned) o.localityFileParameter,
        (long long unsigned) o.localityFileParameter2,
        (long long unsigned) o.preallocateMemoryInMiB,
        o.preWriteMem,
        o.preWriteFile,
        o.isRead,
        argv[13],
        o.isWaitForProperSize,
        VERSION
      );
    }

   // preallocate file?

   r.memBufferInBytes = o.memoryBufferInMiB*1024*1024;
   r.fileSizeInByte = o.fileSizeInMiB*1024*1024;

   r.lastMemOffset = r.memBufferInBytes - o.accessSize;
   r.lastFileffset = r.fileSizeInByte - o.accessSize;

   r.repeatsToMatchFileSize = r.fileSizeInByte / o.accessSize;

   // now allocate memory buffer for I/O
   r.buff = mmalloc(r.memBufferInBytes);

   // prefill memory buffer if needed
   if (o.preWriteMem){
      memset(r.buff, 0, r.memBufferInBytes);
   }

   // prefill file if needed
   if (o.truncate){
      int fd = MYopen(o.filename, O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU);
      close(fd);
   }

   if (o.preWriteFile){
      size_t ret;
      int fd = MYopen(o.filename, O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU);

      size_t repeats = o.fileSizeInMiB / o.memoryBufferInMiB;
      for(size_t i=0; i < repeats; i++){
         ret = write(fd, r.buff, r.memBufferInBytes);
         checkIOError(r.memBufferInBytes, ret);
      }

      size_t rest = (r.fileSizeInByte % r.memBufferInBytes);
      ret = write(fd, r.buff, rest);
      checkIOError(rest, ret);

      close(fd);
   }

   if ( strcmp(o.filename, "/dev/zero") != 0 && ( o.isRead || o.isWaitForProperSize) ){
      // check for validity of the file size.
      struct stat statBlock;

      int waitingIterations = 0;
      while(waitingIterations < 30){
         int ret = stat(o.filename, & statBlock);
         if (ret != 0 || statBlock.st_size * 512 < r.fileSizeInByte ){
            if (o.isWaitForProperSize){
               printf("Warning: the file %s should have a size of at least %llu for read tests but has %llu \n", o.filename, (long long unsigned) r.fileSizeInByte, (long long unsigned) statBlock.st_size * 512);
               sleep(1);
               waitingIterations++;
            }else{
               printf("Fatal: the file %s should have a size of at least %llu for read tests but has %llu \n", o.filename, (long long unsigned) r.fileSizeInByte, (long long unsigned) statBlock.st_size * 512);
               MPI_Abort(MPI_COMM_WORLD, 1);
            }
         }else{
            break;
         }
      }
      if (waitingIterations == 30){
         printf("Fatal: the file %s should have a size of at least %llu for read tests but has %llu \n", o.filename, (long long unsigned) r.fileSizeInByte, (long long unsigned) statBlock.st_size * 512);
         MPI_Abort(MPI_COMM_WORLD, 1);
      }

      if (waitingIterations > 0) {
         printf("Warning, waited for %d seconds until file size has matched\n", waitingIterations);
      }

      // check for allocations
      if ( statBlock.st_blocks * 512 < r.fileSizeInByte ){
         printf("Warning: the file is sparse with only %llu bytes allocated\n",  (long long unsigned) statBlock.st_blocks * 512);
      }
   }

   if( o.localityMemParameter > r.lastMemOffset){
      printf("Error the mem localization parameter is larger than the mem buffer\n");
      MPI_Abort(MPI_COMM_WORLD, 1);
   }
   if( o.localityFileParameter > r.lastFileffset){
      printf("Error the file localization parameter is larger than the file size\n");
      MPI_Abort(MPI_COMM_WORLD, 1);
   }

   // now preallocate
   if ( o.preallocateMemoryInMiB > 0){
      preallocate(o.preallocateMemoryInMiB*1024);
   }

   r.doneRepeats = 0;
   dumpStats(NULL, 1);
   runBenchmarkWrapper();
   dumpStats("stats", r.doneRepeats);

   fflush(r.outputFile);
   free(r.buff);
   MPI_Finalize();

   return 0;
}
