
/* Stupid program to test Subversion's bytestring library (libsvn_string).
   Ben Collins-Sussman, (C) 2000 Collab.Net */

#include <stdio.h>
#include "svn_string.h"   /* This includes <apr_*.h> */

int
main ()
{
  svn_string_t *a, *b, *c;
  char *msg;
  apr_pool_t *pglobal;
  int e, f;

  /* Initialize APR (Apache pools) */
  if (apr_initialize () != APR_SUCCESS)
    {
      printf ("apr_initialize() failed.\n");
      exit (1);
    }
  if (apr_create_pool (&pglobal, NULL) != APR_SUCCESS)
    {
      printf ("apr_create_pool() failed.\n");
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

  /* Make sure our appended string is equal to this static one: */
  if (! svn_string_compare 
      (a, svn_string_create ("helloa longish phrase xtrasome by", pglobal)))
    {
      printf ("error in string-appending comparison.");
      apr_destroy_pool (pglobal);
      apr_terminate();
      exit (1);
    }


  /* Duplicate a bytestring, then compare if they're equal */
  c = svn_string_dup (b, pglobal);

  printf ("comparison of c and b is: %d\n", svn_string_compare (c, b));
  printf ("comparison of a and b is: %d\n", svn_string_compare (a, b));

  if (! svn_string_compare (c,b)) 
    {
      printf ("error in string-dup comparison.");
      apr_destroy_pool (pglobal);
      apr_terminate();
      exit (1);
    }

  /* Set a bytestring to NULL, and query this fact. */
  svn_string_setempty (c);
  svn_string_print (c, stdout, TRUE, TRUE);
  
  printf ("is C empty? : %d\n", svn_string_isempty (c));
  printf ("is A empty? : %d\n", svn_string_isempty (a));

  if (! svn_string_isempty (c)) 
    {
      printf ("error in string-empty test.");
      apr_destroy_pool (pglobal);
      apr_terminate();
      exit (1);
    }

  
  /* Fill a bytestring with hash marks */
  svn_string_fillchar (a, '#');
  svn_string_print (a, stdout, TRUE, TRUE);

  if (! svn_string_compare 
      (a, svn_string_create ("#################################", pglobal))) 
    {
      printf ("error in string-fill comparison.");
      apr_destroy_pool (pglobal);
      apr_terminate();
      exit (1);
    }


  /* Return a C string from a bytestring */
  msg = svn_string_dup2cstring (b, pglobal);
  printf ("The C string returned is: %s\n", msg);

  /* Compare the C string to the original bytestring */
  e = svn_string_compare_2cstring (b, msg);
  f = svn_string_compare_2cstring (b, "a longish phrase");

  if (! (e && f))
    {
      printf ("error in Cstring comparison.");
      apr_destroy_pool (pglobal);
      apr_terminate();
      exit (1);
    }
  

  /* Free our entire memory pool when done. */
  apr_destroy_pool (pglobal);
  apr_terminate();

  return 0;
}
