
/* Stupid program to test Subversion's bytestring library (libsvn_string).
   Ben Collins-Sussman, (C) 2000 Collab.Net */

#include <stdio.h>
#include "svn_string.h"   /* This includes <apr_*.h> */

int
main ()
{
  svn_string_t *a, *b, *c;
  char *msg;
  ap_pool_t *pglobal;

  /* Initialize APR (Apache pools) */
  if (ap_initialize () != APR_SUCCESS)
    {
      printf ("ap_initialize() failed.\n");
      exit (1);
    }
  if (ap_create_pool (&pglobal, NULL) != APR_SUCCESS)
    {
      printf ("ap_create_pool() failed.\n");
      exit (1);
    }


  /* Create a bytestring from a null-terminated C string */
  a = svn_string_create ("hello", pglobal);
  svn_string_print (a, stdout, TRUE, TRUE);

  /* Alternate: create a bytestring from a part of an array */
  b = svn_string_ncreate ("a longish phrase of sorts", 16, pglobal);
  svn_string_print (b, stdout, TRUE, TRUE);

  /* Append b to a, growing a's storage if necessary */
  svn_string_appendstr (a, b, pglobal);
  svn_string_print (a, stdout, TRUE, TRUE);

  /* Do it again, with an inline string creation for kicks. */
  svn_string_appendstr (a, svn_string_create(" xtra", pglobal), pglobal);
  svn_string_print (a, stdout, TRUE, TRUE);

  /* Alternate:  append a specific number of bytes */
  svn_string_appendbytes (a, "some bytes to frob", 7, pglobal);
  svn_string_print (a, stdout, TRUE, TRUE);

  /* Duplicate a bytestring, then compare if they're equal */
  c = svn_string_dup (b, pglobal);
  svn_string_print (c, stdout, TRUE, TRUE);

  printf ("comparison of c and b is: %d\n", svn_string_compare (c, b));
  printf ("comparison of a and b is: %d\n", svn_string_compare (a, b));

  /* Set a bytestring to NULL, and query this fact. */
  svn_string_setempty (c);
  svn_string_print (c, stdout, TRUE, TRUE);
  
  printf ("is C empty? : %d\n", svn_string_isempty (c));
  printf ("is A empty? : %d\n", svn_string_isempty (a));
  
  /* Fill a bytestring with hash marks */
  svn_string_fillchar (a, '#');
  svn_string_print (a, stdout, TRUE, TRUE);

  /* Return a C string from a bytestring */
  msg = svn_string_2cstring (b, pglobal);
  printf ("The C string returned is: %s\n", msg);

  /* Compare the C string to the original bytestring */
  printf ("comparison of b and msg is: %d\n", 
          svn_string_compare_2cstring (b, msg));
  printf ("comparison of b and `foogle' is: %d\n", 
          svn_string_compare_2cstring (b, "foogle"));
  printf ("comparison of b and `a longish phrase' is: %d\n", 
          svn_string_compare_2cstring (b, "a longish phrase"));

  /* Free our entire memory pool when done. */
  ap_destroy_pool (pglobal);
  ap_terminate();

  return 0;
}
