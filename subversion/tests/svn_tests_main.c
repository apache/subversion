/*
 * tests-main.c:  shared main() & friends for SVN test-suite programs
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */



#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <apr_pools.h>
#include <apr_strings.h>
#include <apr_general.h>
#include <apr_lib.h>

#include "svn_pools.h"
#include "svn_error.h"


/* All Subversion test programs include an array of function pointers
   (all of our sub-tests) that begins and ends with a NULL entry. */
extern svn_error_t *(*test_funcs[])(const char **msg, 
                                    svn_boolean_t msg_only,
                                    apr_pool_t *pool);


/* Some Subversion test programs may want to parse options in the
   argument list, so we remember it here. */
int test_argc;
char **test_argv;


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
do_test_num (const char *progname, 
             int test_num, 
             svn_boolean_t msg_only,
             apr_pool_t *pool)
{
  svn_error_t *err;
  int array_size = get_array_size();
  const char *msg = 0;  /* the message this individual test prints out */

  /* Check our array bounds! */
  if ((test_num > array_size) || (test_num <= 0))
    {
      printf ("FAIL: %s: THERE IS NO TEST NUMBER %2d\n", progname, test_num);
      return 1;  /* BAIL, this test number doesn't exist. */
    }

  /* Do test */
  err = test_funcs[test_num](&msg, msg_only, pool);

  /* If we got an error, print it out.  */
  if (err)
    svn_handle_error (err, stdout, 0);

  if (msg_only)
    {
      printf ("%s %2d: %s\n",
              progname,
              test_num,
              msg ? msg : "(test did not provide name)");
    }
  else
    {
      printf ("%s: %s %2d: %s\n", 
              err ? "FAIL" : "PASS",
              progname,
              test_num, 
              msg ? msg : "(test did not provide name)");
    }

  return err != SVN_NO_ERROR;
}



/* Standard svn test program */
int
main (int argc, char *argv[])
{
  char *prog_name;
  int test_num;
  int i;
  int got_error = 0;
  apr_pool_t *pool, *test_pool;
  int ran_a_test = 0;

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
    {
      /* Just check if this is that weird platform that uses \ instead
         of / for the path separator. */
      prog_name = strrchr (argv[0], '\\');
      if (prog_name)
        prog_name++;
      else
        prog_name = argv[0];
    }

  /* Remember the command line */
  test_argc = argc;
  test_argv = argv;

  if (argc >= 2)  /* notice command-line arguments */
    {
      if (! strcmp (argv[1], "list"))
        {
          ran_a_test = 1;

          /* run all tests with MSG_ONLY set to TRUE */
          test_pool = svn_pool_create (pool);
          for (i = 1; i <= array_size; i++)
            {
              if (do_test_num (prog_name, i, TRUE, test_pool))
                got_error = 1;
            }
          /* Clear the per-function pool */
          svn_pool_destroy (test_pool);
        }
      else
        {
          for (i = 1; i < argc; i++)
            {
              if (apr_isdigit (argv[i][0]))
                {
                  ran_a_test = 1;
                  test_num = atoi (argv[i]);
                  if (do_test_num (prog_name, test_num, FALSE,
                                   test_pool = svn_pool_create (pool)))
                    got_error = 1;

                  /* Clear the per-function pool */
                  svn_pool_destroy (test_pool);
                }
              else if (argv[i][0] != '-')
                {
                  /* (probably) a source directory pathname */
                  printf ("notice: ignoring argument %d\n", i);
                }
            }
        }
    }

  if (! ran_a_test)
    {
      /* just run all tests */
      for (i = 1; i <= array_size; i++)
        {
          if (do_test_num (prog_name, i, FALSE,
                           test_pool = svn_pool_create (pool)))
            got_error = 1;

          /* Clear the per-function pool */
          svn_pool_destroy (test_pool);
        }
    }

  /* Clean up APR */
  svn_pool_destroy (pool);
  apr_terminate();

  exit (got_error);
  return got_error;
}



/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
