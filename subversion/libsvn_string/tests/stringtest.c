
/* Stupid program to test bytestring library for Subversion. 
   Ben Collins-Sussman, (C) 2000 Collab.Net */


#include <svn_string.h>

int
main ()
{
  svn_string_t *a, *b, *c;

  a = svn_string_create ("hello");
  svn_string_print (a);

  return 0;
}
