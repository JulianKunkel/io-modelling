#include <pthread.h>

#define WAIT_TIME 100000
#define BLOCK_DEV_STAT_COUNT 11

struct proc_stats{
  // /proc/meminfo
  // Buffers:         1046936 kB
  uint64_t buffers_kb;
  // Cached:         14549024 kB
  uint64_t cached_kb;
  // Dirty:               820 kB
  uint64_t dirty_kb;
  // Writeback:             0 kB
  uint64_t writeback_kb;

  // /sys/block/<dev/stats> https://www.kernel.org/doc/Documentation/block/stat.txt
  uint64_t blockdev_stats[BLOCK_DEV_STAT_COUNT];
};

typedef struct proc_stats proc_stats_t;

static char *stat_names_self_io[] = {"rchar:", "wchar:", "syscr:", "syscw:", "read_bytes:", "write_bytes:", "cancelled_write_bytes:", NULL};
static char *stat_names_meminfo[] = {"Buffers:", "Cached:", "Dirty:", "Writeback:", NULL};
static char *stat_names_blockdev[] = {"read I/Os", "read merges", "read sectors", "read ticks", "write I/Os", "write merges", "write sectors", "write ticks", "in_flight", "io_ticks", "time_in_queue", NULL};

struct proc_stats_pp{
  double time;

  // /proc/self/stat
  uint64_t rchar;
  uint64_t wchar;
  uint64_t syscr; // read syscalls
  uint64_t syscw; // write syscalls
  uint64_t read_bytes; // actually fetched from the storage layer
  uint64_t write_bytes; // actually written to the storage layer
  uint64_t cancelled_write_bytes; // e.g., data deleted before written
};

typedef struct proc_stats_pp proc_stats_pp_t;

static volatile int finish_background_thread = 0;

static pthread_t thread;
static proc_stats_t * proc_stats;
static proc_stats_pp_t * proc_stats_pp;
static int pos_proc_stats = 0;


static void addProcStats(int pos){
  proc_stats_t * p = proc_stats;
  FILE * f = fopen("/proc/meminfo", "r");
  char buff[2048];
  int ret;
  long long unsigned value;
  char ** curStatName = stat_names_meminfo;
  uint64_t * curValue = & p[pos].buffers_kb;

  while(*curStatName != NULL){
    ret = fscanf(f, "%s %llu", buff, & value);
    if (ret != 2){
      continue;
    }
    ret = strcmp(buff, *curStatName);
    if (ret == 0){
      *curValue = value;
      curStatName++;
      curValue++;
    }
  }
  fclose(f);

  sprintf(buff, "/sys/block/%s/stat", o.deviceName);
  f = fopen(buff, "r");
  if (f == NULL){
    printf("Error reading from %s\n", buff);
  }else{
    for(int i=0; i < BLOCK_DEV_STAT_COUNT; i++){
      ret = fscanf(f, "%lu", &  p[pos].blockdev_stats[i]);
    }
    fclose(f);
  }
}

static void addProcPPStats(int pos, double time){
  proc_stats_pp_t * p = proc_stats_pp;

  p[pos].time = time;

  FILE * f = fopen("/proc/self/io", "r");
  char buff[1023];
  int ret;
  long long unsigned value;

  uint64_t * curValue = & p[pos].rchar;

  for( int i=0; i < 7 ; i++ ){
     ret = fscanf(f, "%s %llu", buff, & value);
     if (ret != 2){
        printf("Error accessing /proc/self/io, read only %d tokens\n", ret);
        fclose(f);
        return;
     }
     ret = strcmp(buff, stat_names_self_io[i]);
     if (ret != 0){
        printf("%s %s", buff, stat_names_self_io[i]);
        printf("Error accessing /proc/self/io\n");
        fclose(f);
        return;
     }else{
        *curValue = value;
        curValue++;
     }
  }
  fclose(f);
}

static void * background_thread(void * arg){
  Timer t;
  pos_proc_stats = 0;
  double ft;

  while(! finish_background_thread){
    timerStart(& t);
    ft = timeSinceStart(t);
    addProcPPStats(pos_proc_stats, ft);
    addProcStats(pos_proc_stats);
    // store current values from proc into: proc_stats_t
    usleep(WAIT_TIME);
    pos_proc_stats++;
  }
  timerStart(& t);
  ft = timeSinceStart(t);
  addProcPPStats(pos_proc_stats, ft);
  addProcStats(pos_proc_stats);
  pos_proc_stats++;
  return NULL;
}


static void * background_thread_proc(void * arg){
  Timer t;
  pos_proc_stats=0;
  double ft;
  while(! finish_background_thread){
    timerStart(& t);
    ft = timeSinceStart(t);
    addProcPPStats(pos_proc_stats, ft);
    sleep(1);
    pos_proc_stats++;
  }
  timerStart(& t);
  ft = timeSinceStart(t);
  addProcPPStats(pos_proc_stats, ft);
  pos_proc_stats++;

  return NULL;
}


void start_background_threads(int rank, int repeats){
  // assume at most 1s per I/O operation ...
  proc_stats_pp = (proc_stats_pp_t*) mmalloc(sizeof(proc_stats_pp_t) * repeats*10+2);
  if (rank == 0){
    proc_stats = (proc_stats_t*) mmalloc(sizeof(proc_stats_t) * repeats*10+2);
    pthread_create(& thread, NULL, background_thread, NULL);
  }else{
    pthread_create(& thread, NULL, background_thread_proc, NULL);
  }
}

void stop_background_threads(int rank){
  finish_background_thread = 1;
  int retval;
  pthread_join(thread, (void *) & retval);
}

void dumpStats(int rank, size_t done_repeats){
    char buff[4096];
    int pos = 0;
    for( int i=0; i < 7 ; i++ ){
      float delta = (float) *(& proc_stats_pp[pos_proc_stats-1].rchar + i) - *(& proc_stats_pp[0].rchar + i);
      pos += sprintf(buff + pos, "%s %.1f ", stat_names_self_io[i], delta / done_repeats);
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

    char fname[100];
    sprintf(fname, "stats-%d.csv", rank);
    FILE * out = fopen(fname, "w");
    proc_stats_pp_t * pp = proc_stats_pp;
    proc_stats_t * p = proc_stats;

    fprintf(out, "start_time");
    char ** curStatName = stat_names_self_io;
    while(*curStatName != NULL){
      fprintf(out, ",self_%s", *curStatName);
      curStatName++;
    }

    if (rank == 0){
      curStatName = stat_names_meminfo;
      while(*curStatName != NULL){
        fprintf(out, ",meminfo_%s", *curStatName);
        curStatName++;
      }

      for(int i=0; i < BLOCK_DEV_STAT_COUNT; i++){
        fprintf(out, ",blockdev_%s", stat_names_blockdev[i]);
      }
    }
    fprintf(out, "\n");

    for(int pos=0; pos < pos_proc_stats; pos++){
      fprintf(out, "%.9f", pp[pos].time);
      curStatName = stat_names_self_io;
      uint64_t * curValue = & pp[pos].rchar;
      while(*curStatName != NULL){
        fprintf(out, ",%ld", *curValue);
        curStatName++;
        curValue++;
      }

      if (rank == 0){
        curStatName = stat_names_meminfo;
        curValue = & p[pos].buffers_kb;
        while(*curStatName != NULL){
          fprintf(out, ",%ld", *curValue);
          curStatName++;
          curValue++;
        }

        for(int i=0; i < BLOCK_DEV_STAT_COUNT; i++){
          fprintf(out, ",%ld", p[pos].blockdev_stats[i]);
        }
      }

      fprintf(out, "\n");
    }

    fclose(out);
}
