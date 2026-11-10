/* 
Copyright (c) 2015-2020 Harry W. Waddell
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <limits.h>
#include <gdbm.h>
#include <curl/curl.h>
/* to get gdbm_open flags */
#include <fcntl.h>

/* local stuff */
#include "sha2.h"
#include "version.h"
#include "wrap_runner.h"
#include "my_asprintf.h"

#ifndef LOG_AUTHPRIV
#define LOG_AUTHPRIV LOG_AUTH
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef TRUE
#ifdef true
#define TRUE true
#define FALSE false
#endif
#ifndef true
#define TRUE 1
#define FALSE 0
#endif
#endif

/* aix and linux have this in unistd.h already 
 #if defined(__APPLE__) || defined(__hpux__) || defined(__sun__)
 #endif
*/

/* external vars */
extern char **environ;

/* some global variables */

char syslog_id[PATH_MAX + 1 + 11];
char myname[PATH_MAX + 1];
runnercfg mycfg = { CACHE_ONLY, 0, 0, NULL };

/* curl foo */
CURL *curl = NULL;
CURLcode curl_result;

/* 
   parse_cfg(char* cfg_file_path)

   read the config file, if it exists, and update
   the runnercfg accordingly 
*/
void parse_cfg(char* cfg_file_path)
{
  FILE *fp;
  char linebuf[10000];
  char *p;
  
  if((fp = fopen(cfg_file_path, "r")) == NULL){
    if(errno == ENOENT){
      /* no config file means we run with the defaults we 
	 initialized, which produce the previous behaviour. */
      return;
    }else{
      perror("fopen");
      exit(1);
    }
  }

  /* step through the cfg file, line by line. NOTE, fgets retains the \n */
  while( fgets(linebuf, 10000, fp ) != NULL ){
    if(strlen(linebuf) == 9999 ){
      fprintf(stderr, "config file line too long");
      exit(1);
    }
    if ((p = strtok(linebuf, " \t")) == NULL ){
      continue;
    }

    if(strcmp("mode:", p) == 0){
      if ((p = strtok(NULL, " \t\n")) == NULL ){
	continue;
      }else{
	if(strcmp("query_only", p) == 0){
	  mycfg.mode=QUERY_ONLY;
	}else if(strcmp("cache_only", p) == 0){
          mycfg.mode=CACHE_ONLY;
	}else if(strcmp("cache_first", p) == 0){
	  mycfg.mode=CACHE_FIRST;
	}else if(strcmp("cache_pass", p) == 0){
	  mycfg.mode=CACHE_PASS;
	}else{
	  fprintf(stderr, "%s is not a valid value for config setting \'mode\'\n", p);
	  exit(99);
	}
      }
    }else if(strcmp("cache_expiry:", p) == 0){ /* consider a regex of p for these cases */
      if ((p = strtok(NULL, " \t\n")) == NULL ){
	continue;
      }else{
	mycfg.cache_expiry=strtol(p, NULL, 10); 
      }
    }else if(strcmp("cache_refresh:", p) == 0){
      if ((p = strtok(NULL, " \t\n")) == NULL ){
        continue;
      }else{
	mycfg.cache_refresh=strtol(p, NULL, 10);
      }
    }else if(strcmp("query_server:", p) == 0){
      if ((p = strtok(NULL, " \t\n")) == NULL ){
        continue;
      }else{
	mycfg.query_server=my_asprintf(strlen(p)+1, "%s", p);
      }
    }
  }
  if((mycfg.mode != CACHE_ONLY) && ( mycfg.query_server == NULL )){
    fprintf(stderr, "Configuration error: mode is %s but query_server is not defined",
	    (mycfg.mode == CACHE_FIRST ? "cache_first":"query_only"));
    exit(99);
  }
}


/* 
   Use curl to obtain a new copy of the local cache database.
   init_string() and writefunc() are used by curl in update_gdbm_cache()
*/

/* we want to write what we get from curl into a buffer.
   This uses realloc to grow the buffer as needed. */

struct string {
  char *ptr;
  size_t len;
};

void init_string(struct string *s) {
  s->len = 0;
  s->ptr = malloc(s->len+1);
  if (s->ptr == NULL) {
    fprintf(stderr, "malloc() failed\n");
    exit(EXIT_FAILURE);
  }
  s->ptr[0] = '\0';
}

size_t writefunc(void *ptr, size_t size, size_t nmemb, struct string *s)
{
  size_t new_len = s->len + size*nmemb;
  s->ptr = realloc(s->ptr, new_len+1);
  if (s->ptr == NULL) {
    fprintf(stderr, "realloc() failed\n");
    exit(EXIT_FAILURE);
  }
  memcpy(s->ptr+s->len, ptr, size*nmemb);
  s->ptr[new_len] = '\0';
  s->len = new_len;

  return size*nmemb;
}

int update_gdbm_cache()
{
  GDBM_FILE dbf;
  char *urlbuf, *l, *w, *lastline, *lastword;
  datum value;
  datum key;
  
  struct string s;
  init_string(&s);

#ifdef DEVELOPER
  fprintf(stderr, "starting update_gdbm_cache\n");
#endif
  
  if(curl == NULL){
    if(!(curl=curl_easy_init())){
      fprintf(stderr, "Internal Error: Unable to initialize curl\n");
      exit(99);
    }
  }

  if((urlbuf = malloc(strlen(mycfg.query_server) + 14 + 1)) == NULL){
    perror("malloc() of urlbuffer failed");
    exit(99);
  }
  
  if(sprintf(urlbuf, "%s?action=export", mycfg.query_server) < 0){
    perror("snprintf of query_server url failed");
  }

  curl_easy_setopt(curl, CURLOPT_URL, urlbuf);

  free(urlbuf); /* libcurl makes its own copy */

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
  if((curl_result = curl_easy_perform(curl)) != CURLE_OK){
    syslog(LOG_WARNING, "curl could not obtain the exported database");
#ifdef DEVELOPER
    fprintf(stderr, "curl could not obtain the exported database\n"); 
#endif
    return(FALSE);
  }

  /*
    GDBM_NEWDB causes a new db to be created if missing.
    The creation of the dir containing the database and any 
    missing path components is the resposibility of the installation
    process.
  */
  if((dbf = gdbm_open(HASHDBFILE, 0, GDBM_NEWDB, O_RDWR, NULL)) == NULL){
    fprintf(stderr, "wrap_runner failed with an internal error\n");
    syslog(LOG_ERR, "error opening hash database: %s",
	   gdbm_strerror (gdbm_errno));
    closelog();
    exit(1);
  }

  /* read export of db into buffer */

  /* use strtok - for portability - to parse lines beginning 
     with "import" into database */
  for((l=strtok_r(s.ptr, "\n", &lastline)); l != NULL;
	l=strtok_r(NULL, "\n", &lastline)){
    w = strtok_r(l, " \t", &lastword);
    if((w == NULL) || (strlen(w) == 0)){
      continue;
    }else if(strcmp(w, "store") != 0){
      continue;
    }

    if((w = strtok_r(NULL, " \t", &lastword)) == NULL ){
      fprintf(stderr, "database export is corrupted,  missing key\n");
      exit(99);
    }
    key.dptr = w;
    key.dsize = strlen(w);

    if((w = strtok_r(NULL, " \t", &lastword)) == NULL){
      fprintf(stderr, "database export is corrupted, missing value\n");
      exit(99);
    }
    value.dptr = w;
    value.dsize = strlen(w);

    /* duplicate entries are not a big deal if the occur since 
       all we really care about is the existence of the key in the 
       database */
    if( gdbm_store(dbf, key, value, GDBM_REPLACE) != 0 ){
      fprintf(stderr, "Error adding record to local cache db: %s\n",
	      gdbm_strerror(gdbm_errno));
      exit(99);
    }
  }

  /* yes, we will reopen this later, but not for write, so closing now */
  gdbm_close(dbf);
  return (TRUE);
}


/* 
   search local cache for key=hashstring
*/

int cache_find(char* hashstring)
{
  GDBM_FILE dbf;
  datum value;
  datum key;
  char wrapped_id[MAX_WRAPPED_ID];
  struct stat fileinfo;

  /* assume cache is true - useful in exceptional circumstances
     and for benchmarking/testing */
  if(mycfg.mode == CACHE_PASS){
    return(TRUE);
  }
  
  /* make sure we have a cache and that it's not too old */
  if(stat(HASHDBFILE, &fileinfo) == -1){
    return(FALSE);
  }else if((mycfg.cache_expiry > 0 ) &&
	   ((time(NULL) - fileinfo.st_mtime) > (mycfg.cache_expiry * 3600))){
    syslog(LOG_WARNING, "cache_find(): database is too old");
    return(FALSE);
  }

  /* open the cache db for read */
  if((dbf = gdbm_open(HASHDBFILE, 0, GDBM_READER, O_RDONLY, NULL)) == NULL){
    fprintf(stderr, "wrap_runner failed with an internal error\n");
    syslog(LOG_ERR, "error opening hash database: %s",
	   gdbm_strerror (gdbm_errno));
    closelog();
    exit(1);
  }

  key.dptr = hashstring;
  key.dsize = strlen(hashstring);
  value = gdbm_fetch(dbf, key);
  gdbm_close(dbf); /* done with gdbm so close */
  
  if(value.dptr == NULL){ /* key not found */
    syslog(LOG_WARNING, "cache_find() no match for hashstring %s", hashstring);
    return(FALSE);
  }else{ /* key found */
    if(value.dsize > MAX_WRAPPED_ID - 1){
      syslog(LOG_ERR, "INTERNAL ERROR, wrapped file ID too long for buffer");
      fprintf(stderr, "INTERNAL ERROR, wrapped file ID too long");
      closelog();
      exit(99);
    }
    strncpy(wrapped_id, value.dptr, value.dsize);
    wrapped_id[value.dsize] = '\0'; /* not sure if gdbm dptr is NUL terminated */

    syslog(LOG_INFO, "CWD=\"%s\" ID=\"%s\" matched ",
	   getcwd(malloc(PATH_MAX + 1), PATH_MAX), wrapped_id);
    return(TRUE);
  }
}

/* you should consolidate the curl calls to fill the string into 
   a single function when time permits */

int web_api_find(char* hashstring)
{
  char *urlbuf, *l, *w, *lastline, *lastword;
  struct string s;
  
  init_string(&s);

  if((urlbuf = malloc(strlen(mycfg.query_server) +
		      22 + strlen(hashstring) + 1)) == NULL){
    perror("malloc() of urlbuffer failed");
    exit(99);
  }
  
  if(sprintf(urlbuf, "%s?action=verify&chksum=%s",
	     mycfg.query_server,
	     hashstring ) < 0){
    perror("snprintf of query_server url failed");
  }
  
  if(curl == NULL){
    if(!(curl=curl_easy_init())){
      fprintf(stderr, "Internal Error: Unable to initialize curl\n");
      exit(99);
    }
  }

  curl_easy_setopt(curl, CURLOPT_URL, urlbuf);

  free(urlbuf); /* libcurl makes its own copy */

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
  if((curl_result = curl_easy_perform(curl)) != CURLE_OK){
    return(FALSE);
  }

  if((l=strtok_r(s.ptr, "\n", &lastline))  == NULL){
    fprintf(stderr, "web api query returned no result\n");
    return(FALSE);
  }

  if((w=strtok_r(l, "\n", &lastword)) == NULL ){
    fprintf(stderr, "web api query result is empty\n");
    return(FALSE);
  }else if(strcmp("YES", w) == 0){
    syslog(LOG_INFO, "web_api_find() matched hashstring %s", hashstring);
    return(TRUE);
  }else if(strcmp("NO", w) == 0){
    syslog(LOG_WARNING, "web_api_find() no match for hashstring %s", hashstring);
    return(FALSE);
  }else{
    fprintf(stderr, "web api returned unexpected value %s\n", w);
    exit(99);
  }
}


/* 
   main()
*/

int main(int argc, char* argv[])
{
  /* SHA256 foo */
  sha256_ctx ctx[1];
  unsigned char hashval[SHA256_DIGEST_SIZE];
  char hashtuple[3];
  char hashstring[(SHA256_DIGEST_SIZE * 2) + 1];

  /* stuff that ID's the file being wrapped */
  char executable[PATH_MAX + 1];
  /* this ID is created on a control node which may have different limits
     so we use a large, fixed, value */
  
  char my_exec_profile;
  char *c;

  /* file foo */
  FILE* fp;
  char* linebuf;
  struct stat fileinfo;
  int bytes_read, i; 

  /* what is our name, and consequently, our execution profile */ 
  strncpy(myname, (c = strrchr(argv[0],'/')) == NULL ? argv[0] : &c[1], PATH_MAX);
  myname[PATH_MAX] = '\0'; /* argv[0] can't be more than PATH_MAX, but paranoia is ok */
  if(strcmp(myname, "wrap_runner") == 0){
    my_exec_profile = 'r';
  }else{
    fprintf(stderr, "no execution profile defined for my name %s\n", myname);
    exit(99);
  }
  
  /* make sure we are running as root */
  if(geteuid() != 0){
    fprintf(stderr, "wrap_runner must be run with an euid of root\n");
    exit(99);
  }else if(argc < 2){
    fprintf(stderr, "usage: wrap_runner executable [arg1] [...argn]\n");
    exit(99);
  }
  
  /* read our config file, if we have one */
  parse_cfg(CFGFILE);

  /* what are we wrapping/validating */
  strncpy(executable, argv[1], PATH_MAX);
  executable[PATH_MAX] = '\0'; /* paranoia */

  /* initialize syslog */
  if((c=getenv("SUDO_USER")) == NULL){
    sprintf(syslog_id, "%s(v. %s)(%s)[-]", myname, VERSION, executable);
  }else{
    sprintf(syslog_id, "%s(v. %s)(%s)[%s]", myname, VERSION, executable, c);
  }
  openlog(syslog_id, LOG_CONS, LOG_AUTHPRIV);

  /* check executable file size and mode bits */
  if(stat(executable, &fileinfo) != 0){
    perror(NULL);
    exit(1);
  }else if(fileinfo.st_size > MAX_FILE_SIZE ){
    syslog(LOG_ERR, "file %s is larger than %d, aborting", executable, MAX_FILE_SIZE);
    closelog();
    fprintf(stderr, "file %s is larger than %d, aborting\n", executable, MAX_FILE_SIZE);
    exit(1);
  }else if(my_exec_profile == 'r' && (fileinfo.st_mode  & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0){
    fprintf(stderr, "%s is not executable\n", executable);
    syslog(LOG_ERR, "first argument is not an executable");
    closelog();
    exit(99);
  }  
  
  /* open and read executable into a buffer */
  if ((fp = fopen(executable, "r")) == NULL){
    syslog(LOG_ERR, "fopen failed: %s", strerror(errno));
    closelog();
    perror(NULL);
    exit(1);
  }

  /* read entire file into buffer in one read, else fail.*/ 
  if ((linebuf = malloc(fileinfo.st_size)) == NULL){
   syslog(LOG_ERR, "malloc failed: %s", strerror(errno));
   perror(NULL);
   closelog();
   exit(1);
  }
  if((bytes_read = fread(linebuf, 1, fileinfo.st_size, fp)) != fileinfo.st_size){
    /* NOTE. This is a file. If reading from a pipe, then this would 
       need to use a loop to get partial reads */
    fprintf(stderr, "read %zd bytes, expected %zd\n", bytes_read, fileinfo.st_size);
    exit(1);
  }else{
    fclose(fp);
  }

  /* Compute the executable sha256 chksum. 
     The sha2 funtions we use don't return status */
  sha256_begin(ctx);
  sha256_hash(linebuf, bytes_read, ctx);
  sha256_end(hashval, ctx);
  hashstring[0] = '\0';
  /* make hash into a hex string like that produced by shasum(1) */
  for(i = 0; i < SHA256_DIGEST_SIZE; ++i){
    sprintf(&(hashstring[i*2]),"%02x", hashval[i]);
  }

  /* Unless we are only using web api queries, check the local cache. */
  if(mycfg.mode != QUERY_ONLY){
    /* see if we need to update our local cache db */
    if(mycfg.query_server != NULL){
      if(stat(HASHDBFILE, &fileinfo) == -1){
	fprintf(stderr, "could not stat cache db: %s\n", strerror(errno));
	update_gdbm_cache();
      }else if( (mycfg.cache_refresh > 0) &&
		(( time(NULL) - fileinfo.st_mtime) >
		 (mycfg.cache_refresh * 3600))){
	update_gdbm_cache();
      }
      /* cache_find makes sure db is not past expiration date */
    }

    if(cache_find(hashstring) == TRUE){
      closelog();
      exit(execve(executable, &(argv[1]), environ));
    }
  }

  /* make a web api query if we did not match above */
  if(mycfg.mode != CACHE_ONLY){
    if(web_api_find(hashstring) == TRUE){
      closelog();
      exit(execve(executable, &(argv[1]), environ));
    }
  }
  
  printf("%s is not registered\n", executable);
  syslog(LOG_WARNING, "no registered checksum for %s", executable);
  closelog();
  return(99);
}
  
  
  
  
  
