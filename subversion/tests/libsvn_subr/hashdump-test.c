/*
 * hashdump-test.c :  testing the reading/writing of hashes
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
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
 * software developed by CollabNet (http://www.CollabNet/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
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
 * individuals on behalf of CollabNet.
 */



#include <stdio.h>       /* for sprintf() */
#include <stdlib.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_hash.h"


/* Global variables */

apr_pool_t *pool = NULL;
apr_hash_t *proplist, *new_proplist;
svn_string_t *key;
apr_file_t *f = NULL;     /* init to NULL very important! */
apr_status_t err;

char *review =
"A forthright entrance, yet coquettish on the tongue, its deceptively\n"
"fruity exterior hides the warm mahagony undercurrent that is the\n"
"hallmark of Chateau Fraisant-Pitre.  Connoisseurs of the region will\n"
"be pleased to note the familiar, subtle hints of mulberries and\n"
"carburator fluid.  Its confident finish is marred only by a barely\n"
"detectable suggestion of rancid squid ink.";




static int
test1()
{
  apr_status_t result;

  /* Build a hash in memory, and fill it with test data. */
  proplist = apr_make_hash (pool);

  key = svn_string_create ("color", pool);
  apr_hash_set (proplist, key->data, key->len,
               svn_string_create ("red", pool));
  
  key = svn_string_create ("wine review", pool);
  apr_hash_set (proplist, key->data, key->len,
               svn_string_create (review, pool));
  
  key = svn_string_create ("price", pool);
  apr_hash_set (proplist, key->data, key->len,
               svn_string_create ("US $6.50", pool));

  /* Test overwriting: same key both times, but different values. */
  key = svn_string_create ("twice-used property name", pool);
  apr_hash_set (proplist, key->data, key->len,
               svn_string_create ("This is the FIRST value.", pool));
  apr_hash_set (proplist, key->data, key->len,
               svn_string_create ("This is the SECOND value.", pool));

  /* Dump the hash to a file. */
  apr_open (&f, "hashdump.out",
            (APR_WRITE | APR_CREATE),
            APR_OS_DEFAULT, pool);

  result = hash_write (proplist, svn_unpack_bytestring, f);

  apr_close (f);

  return ((int) result);
}




static int
test2()
{
  apr_status_t result;

  new_proplist = apr_make_hash (pool);

  apr_open (&f, "hashdump.out", APR_READ, APR_OS_DEFAULT, pool);
  result = hash_read (&new_proplist, svn_pack_bytestring, f, pool);
  apr_close (f);

  return ((int) result);
}





/*
   ====================================================================
   If you add a new test to this file, update these two arrays.

*/

/* An array of all test functions */
int (*test_funcs[])() = 
{
  NULL,
  test1,
  test2,
};

/* Descriptions of each test we can run */
static char *descriptions[] = 
{
  NULL,
  "test 1: write a hash to a file",
  "test 2: read a file into a hash"
};

/* ================================================================= */



/* Execute a test number TEST_NUM.  Pretty-print test name and dots
   according to our test-suite spec, and return the result code. */
static int
do_test_num (const char *progname, int test_num)
{
  int retval;
  int numdots, i;
  int (*func)();
  int array_size = sizeof(test_funcs)/sizeof(int (*)()) - 1;

  /* Check our array bounds! */
  if ((test_num > array_size) 
      || (test_num <= 0))
    {
      char *msg = (char *) apr_psprintf (pool, "%s test %d: NO SUCH TEST",
                                         progname, test_num);
      printf ("%s", msg);
      numdots = 75 - strlen (msg);
      if (numdots > 0)
        for (i = 0; i < numdots; i++)
          printf (".");
      else
        printf ("...");
      printf ("FAIL\n");

      return 1;  /* BAIL, this test number doesn't exist. */
    }

  /* Do test */
  func = test_funcs[test_num];
  retval = (*func)();

  /* Pretty print results */
  printf ("%s %s", progname, descriptions[test_num]);

  /* (some cute trailing dots) */
  numdots = 74 - (strlen (progname) + strlen (descriptions[test_num]));
  if (numdots > 0)
    for (i = 0; i < numdots; i++)
      printf (".");
  else
    printf ("...");

  if (! retval)
    printf ("PASS\n");
  else
    printf ("FAIL\n");

  return retval;
}



int
main (int argc, char *argv[])
{
  int test_num;
  int i;
  int got_error = 0;

  /* How many tests are there? */
  int array_size = sizeof(test_funcs)/sizeof(int (*)()) - 1;
  
  /* Initialize APR (Apache pools) */
  if (apr_initialize () != APR_SUCCESS)
    {
      printf ("apr_initialize() failed.\n");
      exit (1);
    }
  if (apr_create_pool (&pool, NULL) != APR_SUCCESS)
    {
      printf ("apr_create_pool() failed.\n");
      exit (1);
    }

  /* Notice if there's a command-line argument */
  if (argc >= 2) 
    {
      test_num = atoi (argv[1]);
      got_error = do_test_num (argv[0], test_num);
    }
  else /* just run all tests */
    for (i = 1; i <= array_size; i++)
      if (do_test_num (argv[0], i))
        got_error = 1;

  /* Clean up APR */
  apr_destroy_pool (pool);
  apr_terminate();

  return got_error;
}










/* -----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
