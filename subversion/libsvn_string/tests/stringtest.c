/*
 * stringtest.c:  test libsvn_string
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLAB.NET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of Collab.Net.
 */



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



/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */

