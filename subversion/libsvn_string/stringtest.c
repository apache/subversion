
/* Stupid program to test Subversion's bytestring library (libsvn_string).
   Ben Collins-Sussman, (C) 2000 Collab.Net */

#include <stdio.h>
#include <svn_string.h>

int
main ()
{
  svn_string_t *a, *b, *c;

  /* Create a bytestring from a null-terminated C string */
  a = svn_string_create ("hello");
  svn_string_print (a);

  /* Alternate: create a bytestring from a part of an array */
  b = svn_string_ncreate ("a longish phrase of sorts", 16);
  svn_string_print (b);

  /* Append b to a, growing a's storage if necessary */
  svn_string_appendstr (a, b);
  svn_string_print (a);

  /* Do it again.  (This is BAD, btw, since we can never free the
     "xtra" string below!) */

  svn_string_appendstr (a, svn_string_create(" xtra"));
  svn_string_print (a);

  /* Alternate:  append a specific number of bytes */
  svn_string_appendbytes (a, "some bytes to frob", 7);
  svn_string_print (a);

  /* Duplicate a bytestring, then compare if they're equal */
  c = svn_string_dup (b);
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

  /* Be free, my little stringies! */
  svn_string_free (a);
  svn_string_free (b);
  svn_string_free (c);

  return 0;
}
