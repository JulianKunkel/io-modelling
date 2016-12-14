#include <pthread.h>

struct proc_stats{
  float time;
};

typedef struct proc_stats proc_stats_t;

struct proc_stats_pp{
  float time;

  uint64_t rchar;
  uint64_t wchar;
  uint64_t syscr; // read syscalls
  uint64_t syscw; // write syscalls
  uint64_t read_bytes; // actually fetched from the storage layer
  uint64_t write_bytes; // actually written to the storage layer
};

typedef struct proc_stats_pp proc_stats_pp_t;

static int finish_background_thread = 0;

static pthread_t thread;
static proc_stats_t * proc_stats;
static proc_stats_pp_t * proc_stats_pp;

static void addProcPPStats(int pos, float time){
  proc_stats_t * p = proc_stats;
}

static void addProcStats(int pos, float time){
  proc_stats_pp_t * p = proc_stats_pp;
}


static void * background_thread(void * arg){
  Timer t;
  int i=0;
  while(! finish_background_thread){
    timerStart(& t);
    float ft = timeToFloat(t);
    addProcPPStats(i, ft);
    addProcStats(i, ft);
    // store current values from proc into: proc_stats_t
    sleep(1);
  }
  return NULL;
}


static void * background_thread_proc(void * arg){
  Timer t;
  int i=0;
  while(! finish_background_thread){
    timerStart(& t);
    float ft = timeToFloat(t);
    addProcPPStats(i, ft);
    sleep(1);
    i++;
  }
  return NULL;
}


void start_background_threads(int rank, int repeats){
  // assume at most one second per I/O operation ...
  if (rank == 0){
    proc_stats = (proc_stats_t*) mmalloc(sizeof(proc_stats_t) * repeats);
    pthread_create(& thread, NULL, background_thread, NULL);
  }else{
    proc_stats_pp = (proc_stats_pp_t*) mmalloc(sizeof(proc_stats_pp_t) * repeats);
    pthread_create(& thread, NULL, background_thread_proc, NULL);
  }
}

void stop_background_threads(int rank){
  finish_background_thread = 1;
  int retval;
  pthread_join(thread, (void *) & retval);
}
