
/* Stupid program to test Subversion's bytestring library (libsvn_string).
   Ben Collins-Sussman, (C) 2000 Collab.Net */

#include <stdio.h>
#include "svn_string.h"   /* This includes <apr_*.h> */

int
main ()
{
  svn_string_t *a = NULL, *b = NULL, *c = NULL;
  const char *phrase_1 = "hello, ";
  const char *phrase_2 = "a longish phrase of sorts, longer than 16 anyway";
  char *msg;
  apr_pool_t *pglobal;
  int e, f;
  int test_number = 1;
  int written;
  int max_pad = 52;
  int i;

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


  printf ("Testing libsvn_string...\n");

  {
    printf ("   %d. svn_string from cstring %n",
            test_number, &written);
    a = svn_string_create (phrase_1, pglobal);

    /* Test that length, data, and null-termination are correct. */
    if ((a->len == strlen (phrase_1)) && ((strcmp (a->data, phrase_1)) == 0))
      {
        for (i = 0; i < (max_pad - written); i++)
          printf (".");
        printf (" OK\n");
      }
    else
      {
        for (i = 0; i < (max_pad - written); i++)
          printf (".");
        printf (" FAILED\n");
      }

    test_number++;
  }

  {
    printf ("   %d. svn_string from substring of cstring %n",
            test_number, &written);
    b = svn_string_ncreate (phrase_2, 16, pglobal);

    /* Test that length, data, and null-termination are correct. */
    if ((b->len == 16) && ((strncmp (b->data, phrase_2, 16)) == 0))
      {
        for (i = 0; i < (max_pad - written); i++)
          printf (".");
        printf (" OK\n");
      }
    else
      {
        for (i = 0; i < (max_pad - written); i++)
          printf (".");
        printf (" FAILED\n");
      }

    test_number++;
  }

  {
    char *tmp;
    size_t old_len;

    printf ("   %d. appending svn_string to svn_string %n",
            test_number, &written);
    tmp = apr_palloc (pglobal, (a->len + b->len + 1));
    strcpy (tmp, a->data);
    strcat (tmp, b->data);
    old_len = a->len;
    svn_string_appendstr (a, b, pglobal);

    /* Test that length, data, and null-termination are correct. */
    if ((a->len == (old_len + b->len)) && ((strcmp (a->data, tmp)) == 0))
      {
        for (i = 0; i < (max_pad - written); i++)
          printf (".");
        printf (" OK\n");
      }
    else
      {
        for (i = 0; i < (max_pad - written); i++)
          printf (".");
        printf (" FAILED\n");
      }

    test_number++;
  }

  {
    printf ("   %d. append bytes, then compare two strings %n",
            test_number, &written);
    svn_string_appendbytes (a, ", new bytes to append", 11, pglobal);

    /* Test that length, data, and null-termination are correct. */
    if (svn_string_compare 
        (a, svn_string_create ("hello, a longish phrase, new bytes", pglobal)))
      {
        for (i = 0; i < (max_pad - written); i++)
          printf (".");
        printf (" OK\n");
      }
    else
      {
        for (i = 0; i < (max_pad - written); i++)
          printf (".");
        printf (" FAILED\n");
      }
    
    test_number++;
  }

  {
    printf ("   %d. dup two strings, then compare %n",
            test_number, &written);
    c = svn_string_dup (a, pglobal);

    /* Test that length, data, and null-termination are correct. */
    if ((svn_string_compare (a, c)) && (! svn_string_compare (b, c)))
      {
        for (i = 0; i < (max_pad - written); i++)
          printf (".");
        printf (" OK\n");
      }
    else
      {
        for (i = 0; i < (max_pad - written); i++)
          printf (".");
        printf (" FAILED\n");
      }
    
    test_number++;
  }

  {
    char *tmp;
    size_t old_len;
    printf ("   %d. chopping a string %n",
            test_number, &written);

    old_len = c->len;
    tmp = apr_palloc (pglobal, old_len + 1);
    strcpy (tmp, c->data);

    svn_string_chop (c, 11);

    if ((c->len == (old_len - 11))
        && (strncmp (a->data, c->data, c->len) == 0)
        && (strcmp (a->data, c->data) != 0)
        && (c->data[c->len] == '\0'))
      {
        for (i = 0; i < (max_pad - written); i++)
          printf (".");
        printf (" OK\n");
      }
    else
      {
        for (i = 0; i < (max_pad - written); i++)
          printf (".");
        printf (" FAILED\n");
      }
    
    test_number++;
  }

  {
    printf ("   %d. emptifying  a string %n",
            test_number, &written);

    svn_string_setempty (c);

    /* Just for kicks, check again that c and a are separate objects, too. */
    if (((c->len == 0) && (c->data[0] == '\0'))
        && ((a->len != 0) && (a->data[0] != '\0')))
      {
        for (i = 0; i < (max_pad - written); i++)
          printf (".");
        printf (" OK\n");
      }
    else
      {
        for (i = 0; i < (max_pad - written); i++)
          printf (".");
        printf (" FAILED\n");
      }
    
    test_number++;
  }

  {
    printf ("   %d. filling a string with hashmarks %n",
            test_number, &written);

    svn_string_fillchar (a, '#');

    if ((strcmp (a->data, "###"))
        && ((strncmp (a->data, "############", 12)) == 0)
        && (a->data[(a->len - 1)] == '#')
        && (a->data[(a->len)] == '\0'))
      {
        for (i = 0; i < (max_pad - written); i++)
          printf (".");
        printf (" OK\n");
      }
    else
      {
        for (i = 0; i < (max_pad - written); i++)
          printf (".");
        printf (" FAILED\n");
      }
    
    test_number++;
  }

  /* Free our entire memory pool when done. */
  apr_destroy_pool (pglobal);
  apr_terminate();

  return 0;
}
