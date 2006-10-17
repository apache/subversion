/*
 * target-test.c -- test the target condensing function
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#define APR_WANT_STDIO
#include <apr_want.h>
#include <apr_general.h>

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "svn_error.h"

int main(int argc, char **argv)
{
  apr_pool_t *pool;
  svn_error_t *err;
  apr_array_header_t *targets;
  apr_array_header_t *condensed_targets;
  const char *common_path = 0;
  const char *common_path2 = 0;
  int i;

  if (argc < 2) {
    fprintf(stderr, "USAGE: %s <list of entries to be compared>\n", argv[0]);
    return EXIT_FAILURE;
  }

  /* Initialize the app. */
  if (svn_cmdline_init("target-test", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  /* Create our top-level pool. */
  pool = svn_pool_create(NULL);

  /* Create the target array */
  targets = apr_array_make(pool, argc - 1, sizeof(const char *));
  for (i = 1; i < argc; i++)
    {
      const char *path_utf8;
      err = svn_utf_cstring_to_utf8(&path_utf8, argv[i], pool);
      if (err != SVN_NO_ERROR)
        svn_handle_error2(err, stderr, TRUE, "target-test: ");
      *((const char **)apr_array_push(targets)) = 
        svn_path_internal_style(path_utf8, pool);
    }


  /* Call the function */
  err = svn_path_condense_targets(&common_path, &condensed_targets, targets,
                                  TRUE, pool);
  if (err != SVN_NO_ERROR)
    svn_handle_error2(err, stderr, TRUE, "target-test: ");

  /* Display the results */
  {
    const char *common_path_stdout;
    err = svn_utf_cstring_from_utf8(&common_path_stdout, common_path, pool);
    if (err != SVN_NO_ERROR)
      svn_handle_error2(err, stderr, TRUE, "target-test: ");
    printf("%s: ", common_path_stdout);
  }
  for (i = 0; i < condensed_targets->nelts; i++)
    {
      const char * target = ((const char**)condensed_targets->elts)[i];
      if (target)
        {
          const char *target_stdout;
          err = svn_utf_cstring_from_utf8(&target_stdout, target, pool);
          if (err != SVN_NO_ERROR)
            svn_handle_error2(err, stderr, TRUE, "target-test: ");
          printf("%s, ", target_stdout);
        }
      else
        printf("NULL, "); 
    }
  printf("\n");

  /* Now ensure it works without the pbasename */
  err = svn_path_condense_targets(&common_path2, NULL, targets, TRUE, pool);
  if (err != SVN_NO_ERROR)
    svn_handle_error2(err, stderr, TRUE, "target-test: ");

  if (strcmp(common_path, common_path2) != 0)
    {
      printf("Common path without getting targets does not match common path "
             "with targets\n");
      return EXIT_FAILURE;
    }


  return EXIT_SUCCESS;
}
