
/* Stupid program to test Subversion's bytestring library (libsvn_string).
   Ben Collins-Sussman, (C) 2000 Collab.Net */

#include <stdio.h>
#include <svn_string.h>   /* This includes <apr_*.h> */

int
main ()
{
  svn_string_t *a, *b, *c;
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
  svn_string_print (a);

  /* Alternate: create a bytestring from a part of an array */
  b = svn_string_ncreate ("a longish phrase of sorts", 16, pglobal);
  svn_string_print (b);

  /* Append b to a, growing a's storage if necessary */
  svn_string_appendstr (a, b, pglobal);
  svn_string_print (a);

  /* Do it again, with an inline string creation for kicks. */
  svn_string_appendstr (a, svn_string_create(" xtra", pglobal), pglobal);
  svn_string_print (a);

  /* Alternate:  append a specific number of bytes */
  svn_string_appendbytes (a, "some bytes to frob", 7, pglobal);
  svn_string_print (a);

  /* Duplicate a bytestring, then compare if they're equal */
  c = svn_string_dup (b, pglobal);
  svn_string_print (c);

  printf ("comparison of c and b is: %d\n", svn_string_compare (c, b));
  printf ("comparison of a and b is: %d\n", svn_string_compare (a, b));

  /* Set a bytestring to NULL, and query this fact. */
  svn_string_setnull (c);
  svn_string_print (c);
  
  printf ("is C null? : %d\n", svn_string_isnull (c));
  printf ("is A null? : %d\n", svn_string_isnull (a));
  
  /* Fill a bytestring with hash marks */
  svn_string_fillchar (a, '#');
  svn_string_print (a);


  /* Free our entire memory pool when done. */
  ap_destroy_pool (pglobal);
  ap_terminate();

  return 0;
}
