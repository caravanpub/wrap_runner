#ifndef WRAP_RUNNER_H
#define WRAP_RUNNER_H
#define QUERY_ONLY 0
#define CACHE_ONLY 1
#define CACHE_FIRST 2
#define CACHE_PASS 3

typedef struct RUNNERCFG {
  int mode;
  int cache_expiry;
  int cache_refresh;
  char* query_server;
} runnercfg;

#define MAX_FILE_SIZE 20000000
#define MAX_WRAPPED_ID 10240


#endif 
