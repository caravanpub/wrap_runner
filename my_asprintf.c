/*
Copyright 2020 Caravan Electronic Publishing, LLC

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

/* any errors this encounters will cause the program to 
   terminate */

char* my_asprintf(size_t n, const char* format, ...)
{
  char* returnbuf;
  char* tmpbuf;
  va_list pvar;
  size_t len;

  /* malloc our own return buffer since not all systems have vasnprintf */
  
  if((tmpbuf=malloc(n)) == NULL){
    perror("my_asprintf");
    exit(errno);
  }

  va_start(pvar, format);

  if(vsnprintf(tmpbuf, n, format, pvar) < 0){
    perror("my_asprintf");
    exit(errno);
  }else{
    /* this could get called a lot, so make a new buffer that is sized to the 
       actual data and free the one we just used */
    len=strlen(tmpbuf);
    if((returnbuf=malloc(len+1)) == NULL){
      perror("my_asprintf");
      exit(errno);
    }
    strcpy(returnbuf, tmpbuf);
    free(tmpbuf);
  }
  return(returnbuf);
}
