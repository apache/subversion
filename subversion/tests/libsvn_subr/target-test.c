/*
 * target-test.c -- test the target condensing function
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include <svn_path.h>
#include <stdio.h>
#include <apr_general.h>

int main(int argc, char **argv)
{
  apr_pool_t *pool;
  svn_error_t *err;
  apr_array_header_t *targets;
  apr_array_header_t *condensed_targets;
  svn_string_t *common_path = 0;
  svn_string_t *common_path2 = 0;
  int i;

  if (argc < 2) {
    fprintf(stderr, "USAGE: %s <list of entries to be compared>\n", argv[0]);
    return EXIT_FAILURE;
  }

  apr_initialize();
  pool = svn_pool_create(NULL);

  /* Create the target array */
  targets = apr_array_make(pool, argc - 1, sizeof(svn_string_t*));
  for (i = 1; i < argc; i++)
    {
      svn_string_t * target = svn_string_create(argv[i], pool);
      (*((svn_string_t **)apr_array_push(targets))) = target;
    }

  /* Call the function */
  err = svn_path_condense_targets(&common_path, &condensed_targets,
                                  targets, pool);
  if (err != SVN_NO_ERROR)
    svn_handle_error(err, stderr, 1);

  /* Display the results */
  printf("%s: ", common_path->data);
  for (i = 0; i < condensed_targets->nelts; i++)
    {
      svn_string_t * target = ((svn_string_t**)condensed_targets->elts)[i];
      if (target)
        printf("%s, ",
               ((svn_string_t **)condensed_targets->elts)[i]->data);
      else
        printf("NULL, "); 
    }
  printf ("\n");

  /* Now ensure it works without the pbasename */
  err = svn_path_condense_targets(&common_path2, NULL, targets, pool);
  if (err != SVN_NO_ERROR)
    svn_handle_error(err, stderr, 1);

  if (!svn_string_compare(common_path, common_path2))
    {
      printf("Common path without getting targets does not match common path "
             "with targets\n");
      return EXIT_FAILURE;
    }


  return EXIT_SUCCESS;
}
