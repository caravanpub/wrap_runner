#include <unistd.h>
#include <stdio.h>

struct STRING {
  size_t len;
  char* str;
};

typedef struct STRING string;

struct STRING_ARRAY {
  int count;
  string* members;
};

typedef  struct STRING_ARRAY string_array;

string_array a = { 1, { 1, "a" } };

int main(int argc, char* argv[])
{
  printf("%d\n", a.count);
  return(0);
  
}
