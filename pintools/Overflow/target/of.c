#include <stdio.h>
#include <stdlib.h>

int main() {
  char* p = malloc(10);
  for (int i = 0; i < 10; ++i) {
    p[i] = '0' + i;
  }
  p[10] = '0';
  printf("p = %s\n", p);
}
