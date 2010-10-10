/**
 * Some random tests for random functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <parser.h>

int main(){

  /* Test the string_join function. */

  char *s1 = "Hello ";
  char *s2 = "World";
  printf("%s\n", string_join(s1, s2));

  printf("s1 len=%d\n", (int)strlen(s1));
  printf("s2 len=%d\n", (int)strlen(s2));
  printf("s3 len=%d\n", (int)strlen(string_join(s1, s2)));

  return 0;

}
