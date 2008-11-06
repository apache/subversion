/*
 * cmdline.c:  command-line processing
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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

/* ==================================================================== */


/*** Includes. ***/
#include "svn_client.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_opt.h"
#include "svn_utf.h"

#include "client.h"

#include "private/svn_opt_private.h"

#include "svn_private_config.h"


/*** Code. ***/

#define DEFAULT_ARRAY_SIZE 5

/* Return true iff ARG is a repository-relative URL: specifically that
 * it starts with the characters "^/".
 * ARG is in UTF-8 encoding.
 * Do not check whether ARG is properly URI-encoded, canonical, or valid
 * in any other way. */
static svn_boolean_t
arg_is_repos_relative_url(const char *arg)
{
  return (0 == strncmp("^/", arg, 2));
}

/* Set *ABSOLUTE_URL to the absolute URL represented by RELATIVE_URL
 * relative to REPOS_ROOT_URL.
 * *ABSOLUTE_URL will end with a peg revision specifier if RELATIVE_URL did.
 * RELATIVE_URL is in repository-relative syntax:
 * "^/[REL-URL][@PEG]",
 * REPOS_ROOT_URL is the absolute URL of the repository root.
 * All strings are in UTF-8 encoding.
 * Allocate *ABSOLUTE_URL in POOL.
 *
 * REPOS_ROOT_URL and RELATIVE_URL do not have to be properly URI-encoded,
 * canonical, or valid in any other way.  The caller is expected to perform
 * canonicalization on *ABSOLUTE_URL after the call to the function.
 */
static svn_error_t *
resolve_repos_relative_url(const char **absolute_url,
                           const char *relative_url,
                           const char *repos_root_url,
                           apr_pool_t *pool)
{
  if (! arg_is_repos_relative_url(relative_url))
    return svn_error_createf(SVN_ERR_BAD_URL, NULL,
                             _("Improper relative URL '%s'"),
                             relative_url);

  /* No assumptions are made about the canonicalization of the input
   * arguments, it is presumed that the output will be canonicalized after
   * this function, which will remove any duplicate path separator.
   */
  *absolute_url = apr_pstrcat(pool, repos_root_url, relative_url + 1, NULL);

  return SVN_NO_ERROR;
}


/* Attempt to find the repository root url for TARGET, possibly using CTX for
 * authentication.  If one is found and *ROOT_URL is not NULL, then just check
 * that the root url for TARGET matches the value given in *ROOT_URL and
 * return an error if it does not.  If one is found and *ROOT_URL is NULL then
 * set *ROOT_URL to the root url for TARGET, allocated from POOL.
 * If a root url is not found for TARGET because it does not exist in the
 * repository, then return with no error.
 *
 * TARGET is a UTF-8 encoded string that is fully canonicalized and escaped.
 */
static svn_error_t *
check_root_url_of_target(const char **root_url,
                         const char *target,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *pool)
{
  svn_error_t *error;
  const char *tmp_root_url;
  const char *truepath;
  svn_opt_revision_t opt_rev;

  SVN_ERR(svn_opt_parse_path(&opt_rev, &truepath, target, pool));

  if ((error = svn_client__get_repos_root(&tmp_root_url,
                                          truepath,
                                          &opt_rev,
                                          NULL, ctx, pool)))
    {
      /* It is OK if the given target does not exist, it just means
       * we will not be able to determine the root url from this particular
       * argument.
       */
      if ((error->apr_err == SVN_ERR_ENTRY_NOT_FOUND)
          || (error->apr_err == SVN_ERR_WC_NOT_DIRECTORY))
        {
          svn_error_clear(error);
          return SVN_NO_ERROR;
        }
      else
        return error;
     }
   else if (*root_url != NULL)
     {
       if (strcmp(*root_url, tmp_root_url) != 0)
         return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                  _("All non-relative targets must have "
                                    "the same root URL"));
     }
   else
     *root_url = tmp_root_url;

   return SVN_NO_ERROR;
}

/* Note: This is substantially copied from svn_opt__args_to_target_array() in
 * order to move to libsvn_client while maintaining backward compatibility. */
svn_error_t *
svn_client_args_to_target_array(apr_array_header_t **targets_p,
                                apr_getopt_t *os,
                                const apr_array_header_t *known_targets,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *pool)
{
  int i;
  svn_boolean_t rel_url_found = FALSE;
  const char *root_url = NULL;
  svn_error_t *err = SVN_NO_ERROR;
  apr_array_header_t *input_targets =
    apr_array_make(pool, DEFAULT_ARRAY_SIZE, sizeof(const char *));
  apr_array_header_t *output_targets =
    apr_array_make(pool, DEFAULT_ARRAY_SIZE, sizeof(const char *));

  /* Step 1:  create a master array of targets that are in UTF-8
     encoding, and come from concatenating the targets left by apr_getopt,
     plus any extra targets (e.g., from the --targets switch.)
     If any of the targets are relative urls, then set the rel_url_found
     flag.*/

  for (; os->ind < os->argc; os->ind++)
    {
      /* The apr_getopt targets are still in native encoding. */
      const char *raw_target = os->argv[os->ind];
      const char *utf8_target;

      SVN_ERR(svn_utf_cstring_to_utf8(&utf8_target,
                                      raw_target, pool));

      if (arg_is_repos_relative_url(utf8_target))
        rel_url_found = TRUE;

      APR_ARRAY_PUSH(input_targets, const char *) = utf8_target;
    }

  if (known_targets)
    {
      for (i = 0; i < known_targets->nelts; i++)
        {
          /* The --targets array have already been converted to UTF-8,
             because we needed to split up the list with svn_cstring_split. */
          const char *utf8_target = APR_ARRAY_IDX(known_targets,
                                                  i, const char *);

          if (arg_is_repos_relative_url(utf8_target))
            rel_url_found = TRUE;

          APR_ARRAY_PUSH(input_targets, const char *) = utf8_target;
        }
    }

  /* Step 2:  process each target.  */

  for (i = 0; i < input_targets->nelts; i++)
    {
      const char *utf8_target = APR_ARRAY_IDX(input_targets, i, const char *);

      /* Relative urls will be canonicalized when they are resolved later in
       * the function
       */
      if (arg_is_repos_relative_url(utf8_target))
        {
          APR_ARRAY_PUSH(output_targets, const char *) = utf8_target;
        }
      else
        {
          const char *true_target;
          const char *peg_rev;
          const char *target;

          /*
           * This is needed so that the target can be properly canonicalized,
           * otherwise the canonicalization does not treat a ".@BASE" as a "."
           * with a BASE peg revision, and it is not canonicalized to "@BASE".
           * If any peg revision exists, it is appended to the final
           * canonicalized path or URL.  Do not use svn_opt_parse_path()
           * because the resulting peg revision is a structure that would have
           * to be converted back into a string.  Converting from a string date
           * to the apr_time_t field in the svn_opt_revision_value_t and back to
           * a string would not necessarily preserve the exact bytes of the
           * input date, so its easier just to keep it in string form.
           */
          SVN_ERR(svn_opt__split_arg_at_peg_revision(&true_target, &peg_rev,
                                                     utf8_target, pool));

          /* URLs and wc-paths get treated differently. */
          if (svn_path_is_url(true_target))
            {
              SVN_ERR(svn_opt__arg_canonicalize_url(&true_target,
                                                    true_target, pool));
            }
          else  /* not a url, so treat as a path */
            {
              char *base_name;

              SVN_ERR(svn_opt__arg_canonicalize_path(&true_target,
                                                     true_target, pool));

              /* If the target has the same name as a Subversion
                 working copy administrative dir, skip it. */
              base_name = svn_path_basename(true_target, pool);

              if (svn_wc_is_adm_dir(base_name, pool))
                {
                  err = svn_error_createf(SVN_ERR_RESERVED_FILENAME_SPECIFIED,
                                          err,
                                          _("'%s' ends in a reserved name"),
                                          utf8_target);
                  continue;
                }
            }

          target = apr_pstrcat(pool, true_target, peg_rev, NULL);

          if (rel_url_found)
            {
              SVN_ERR(check_root_url_of_target(&root_url, target,
                                               ctx, pool));
            }

          APR_ARRAY_PUSH(output_targets, const char *) = target;
        }
    }

  /* Only resolve relative urls if there were some actually found earlier. */
  if (rel_url_found)
    {
      /*
       * Use the current directory's root url if one wasn't found using the
       * arguments.
       */
      if (root_url == NULL)
        SVN_ERR(svn_client_root_url_from_path(&root_url, "", ctx, pool));

      *targets_p = apr_array_make(pool, output_targets->nelts,
                                  sizeof(const char *));

      for (i = 0; i < output_targets->nelts; i++)
        {
          const char *target = APR_ARRAY_IDX(output_targets, i,
                                             const char *);

          if (arg_is_repos_relative_url(target))
            {
              const char *abs_target;
              const char *true_target;
              const char *peg_rev;

              SVN_ERR(svn_opt__split_arg_at_peg_revision(&true_target, &peg_rev,
                                                         target, pool));

              SVN_ERR(resolve_repos_relative_url(&abs_target, true_target,
                                                 root_url, pool));

              SVN_ERR(svn_opt__arg_canonicalize_url(&true_target, abs_target,
                                                    pool));

              target = apr_pstrcat(pool, true_target, peg_rev, NULL);
            }

          APR_ARRAY_PUSH(*targets_p, const char *) = target;
        }
    }
  else
    *targets_p = output_targets;

  return err;
}
