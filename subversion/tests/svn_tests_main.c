/*
 * tests-main.c:  shared main() & friends for SVN test-suite programs
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <apr_pools.h>
#include <apr_strings.h>
#include <apr_general.h>

#include "svn_error.h"


/* All Subversion test programs have a single global memory pool that
   main() initializes.  Individual sub-test routines can make subpools
   from it, should they wish.  */

extern apr_pool_t *pool;


/* All Subversion test programs include two global arrays that both
   begin and end with NULL: */

/* An array of function pointers (all of our sub-tests) */
extern int (*test_funcs[])(const char **msg);

/* ================================================================= */


/* Determine the array size of test_funcs[], the inelegant way.  :)  */
static int
get_array_size (void)
{
  int i;

  for (i = 1; test_funcs[i]; i++)
    {
    }

  return (i - 1);
}



/* Execute a test number TEST_NUM.  Pretty-print test name and dots
   according to our test-suite spec, and return the result code. */
static int
do_test_num (const char *progname, int test_num)
{
  int retval;
  int array_size = get_array_size();
  const char *msg = 0;  /* the message this individual test prints out */

  /* Check our array bounds! */
  if ((test_num > array_size) || (test_num <= 0))
    {
      char *err_msg = (char *) apr_psprintf (pool, "%s %2d: NO SUCH TEST",
                                             progname, test_num);
      printf ("FAIL: ");
      printf ("%s\n", err_msg);

      return 1;  /* BAIL, this test number doesn't exist. */
    }

  /* Do test */
  retval = test_funcs[test_num](&msg);

  /* Did the test set the message?  */
  if (! msg)
    msg = "(test did not provide name)";

  if (! retval)
    printf ("PASS: ");
  else
    printf ("FAIL: ");

  /* Pretty print results */
  printf ("%s %2d: %s\n", progname, test_num, msg);

  return retval;
}



/* Standard svn test program */
int
main (int argc, char *argv[])
{
  char *prog_name;
  int test_num;
  int i;
  int got_error = 0;

  /* How many tests are there? */
  int array_size = get_array_size();
  
  /* Initialize APR (Apache pools) */
  if (apr_initialize () != APR_SUCCESS)
    {
      printf ("apr_initialize() failed.\n");
      exit (1);
    }

  /* set up the global pool */
  pool = svn_pool_create (NULL);

  /* Strip off any leading path components from the program name.  */
  prog_name = strrchr (argv[0], '/');
  if (prog_name)
    prog_name++;
  else
    prog_name = argv[0];

  if (argc >= 2)  /* notice command-line arguments */
    {
      for (i = 1; i < argc; i++)
        {
          test_num = atoi (argv[i]);
          if (do_test_num (prog_name, test_num))
            got_error = 1;
        }
    }
  else            /* just run all tests */
    for (i = 1; i <= array_size; i++)
      if (do_test_num (prog_name, i))
        got_error = 1;

  /* Clean up APR */
  apr_pool_destroy (pool);
  apr_terminate();

  return got_error;
}



/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

