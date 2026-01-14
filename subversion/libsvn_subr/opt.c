/*
 * opt.c :  option and argument parsing for Subversion command lines
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */



#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <apr_pools.h>
#include <apr_general.h>
#include <apr_lib.h>
#include <apr_file_info.h>

#include "svn_hash.h"
#include "svn_cmdline.h"
#include "svn_version.h"
#include "svn_types.h"
#include "svn_opt.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "svn_time.h"
#include "svn_props.h"
#include "svn_ctype.h"

#include "private/svn_opt_private.h"

#include "opt.h"
#include "svn_private_config.h"


/*** Code. ***/



/*** Parsing arguments. ***/
#define DEFAULT_ARRAY_SIZE 5


/* Copy STR into POOL and push the copy onto ARRAY. */
static void
array_push_str(apr_array_header_t *array,
               const char *str,
               apr_pool_t *pool)
{
  /* ### Not sure if this function is still necessary.  It used to
     convert str to svn_stringbuf_t * and push it, but now it just
     dups str in pool and pushes the copy.  So its only effect is
     transfer str's lifetime to pool.  Is that something callers are
     depending on? */

  APR_ARRAY_PUSH(array, const char *) = apr_pstrdup(pool, str);
}


void
svn_opt_push_implicit_dot_target(apr_array_header_t *targets,
                                 apr_pool_t *pool)
{
  if (targets->nelts == 0)
    APR_ARRAY_PUSH(targets, const char *) = ""; /* Ha! "", not ".", is the canonical */
  assert(targets->nelts);
}


svn_error_t *
svn_opt_parse_num_args(apr_array_header_t **args_p,
                       apr_getopt_t *os,
                       int num_args,
                       apr_pool_t *pool)
{
  int i;
  apr_array_header_t *args
    = apr_array_make(pool, DEFAULT_ARRAY_SIZE, sizeof(const char *));

  /* loop for num_args and add each arg to the args array */
  for (i = 0; i < num_args; i++)
    {
      if (os->ind >= os->argc)
        {
          return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);
        }
      array_push_str(args, os->argv[os->ind++], pool);
    }

  *args_p = args;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_opt_parse_all_args(apr_array_header_t **args_p,
                       apr_getopt_t *os,
                       apr_pool_t *pool)
{
  apr_array_header_t *args
    = apr_array_make(pool, DEFAULT_ARRAY_SIZE, sizeof(const char *));

  if (os->ind > os->argc)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);
    }
  while (os->ind < os->argc)
    {
      array_push_str(args, os->argv[os->ind++], pool);
    }

  *args_p = args;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_opt_parse_path(svn_opt_revision_t *rev,
                   const char **truepath,
                   const char *path /* UTF-8! */,
                   apr_pool_t *pool)
{
  const char *peg_rev;

  SVN_ERR(svn_opt__split_arg_at_peg_revision(truepath, &peg_rev, path, pool));

  /* Parse the peg revision, if one was found */
  if (strlen(peg_rev))
    {
      int ret;
      svn_opt_revision_t start_revision, end_revision;

      end_revision.kind = svn_opt_revision_unspecified;

      if (peg_rev[1] == '\0')  /* looking at empty peg revision */
        {
          ret = 0;
          start_revision.kind = svn_opt_revision_unspecified;
          start_revision.value.number = 0;
        }
      else  /* looking at non-empty peg revision */
        {
          const char *rev_str = &peg_rev[1];

          /* URLs get treated differently from wc paths. */
          if (svn_path_is_url(path))
            {
              /* URLs are URI-encoded, so we look for dates with
                 URI-encoded delimiters.  */
              size_t rev_len = strlen(rev_str);
              if (rev_len > 6
                  && rev_str[0] == '%'
                  && rev_str[1] == '7'
                  && (rev_str[2] == 'B'
                      || rev_str[2] == 'b')
                  && rev_str[rev_len-3] == '%'
                  && rev_str[rev_len-2] == '7'
                  && (rev_str[rev_len-1] == 'D'
                      || rev_str[rev_len-1] == 'd'))
                {
                  rev_str = svn_path_uri_decode(rev_str, pool);
                }
            }
          ret = svn_opt_parse_revision(&start_revision,
                                       &end_revision,
                                       rev_str, pool);
        }

      if (ret || end_revision.kind != svn_opt_revision_unspecified)
        {
          /* If an svn+ssh URL was used and it contains only one @,
           * provide an error message that presents a possible solution
           * to the parsing error (issue #2349). */
          if (strncmp(path, "svn+ssh://", 10) == 0)
            {
              const char *at;

              at = strchr(path, '@');
              if (at && strrchr(path, '@') == at)
                return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                         _("Syntax error parsing peg revision "
                                           "'%s'; did you mean '%s@'?"),
                                       &peg_rev[1], path);
            }

          return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                   _("Syntax error parsing peg revision '%s'"),
                                   &peg_rev[1]);
        }
      rev->kind = start_revision.kind;
      rev->value = start_revision.value;
    }
  else
    {
      /* Didn't find a peg revision. */
      rev->kind = svn_opt_revision_unspecified;
    }

  return SVN_NO_ERROR;
}

/* Note: This is substantially copied into svn_client_args_to_target_array() in
 * order to move to libsvn_client while maintaining backward compatibility. */
svn_error_t *
svn_opt__process_target_array(apr_array_header_t **targets_p,
                              apr_array_header_t *input_targets,
                              const apr_array_header_t *known_targets,
                              apr_pool_t *pool)
{
  int i;
  svn_error_t *err = SVN_NO_ERROR;
  apr_array_header_t *output_targets =
    apr_array_make(pool, DEFAULT_ARRAY_SIZE, sizeof(const char *));

  /* Step 1:  create a master array of targets, and come from concatenating
     the targets left by apr_getopt, plus any extra targets (e.g., from the
     --targets switch.) */

  if (known_targets)
    {
      for (i = 0; i < known_targets->nelts; i++)
        {
          /* The --targets array have already been converted to UTF-8,
             because we needed to split up the list with svn_cstring_split. */
          const char *utf8_target = APR_ARRAY_IDX(known_targets,
                                                  i, const char *);
          APR_ARRAY_PUSH(input_targets, const char *) = utf8_target;
        }
    }

  /* Step 2:  process each target.  */

  for (i = 0; i < input_targets->nelts; i++)
    {
      const char *utf8_target = APR_ARRAY_IDX(input_targets, i, const char *);
      const char *true_target;
      const char *target;      /* after all processing is finished */
      const char *peg_rev;

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
          SVN_ERR(svn_opt__arg_canonicalize_url(&true_target, true_target,
                                                 pool));
        }
      else  /* not a url, so treat as a path */
        {
          const char *base_name;

          SVN_ERR(svn_opt__arg_canonicalize_path(&true_target, true_target,
                                                 pool));

          /* If the target has the same name as a Subversion
             working copy administrative dir, skip it. */
          base_name = svn_dirent_basename(true_target, pool);

          /* FIXME:
             The canonical list of administrative directory names is
             maintained in libsvn_wc/adm_files.c:svn_wc_set_adm_dir().
             That list can't be used here, because that use would
             create a circular dependency between libsvn_wc and
             libsvn_subr.  Make sure changes to the lists are always
             synchronized! */
          if (0 == strcmp(base_name, ".svn")
              || 0 == strcmp(base_name, "_svn"))
            {
              err = svn_error_createf(SVN_ERR_RESERVED_FILENAME_SPECIFIED,
                                      err, _("'%s' ends in a reserved name"),
                                      utf8_target);
              continue;
            }
        }

      target = apr_pstrcat(pool, true_target, peg_rev, SVN_VA_NULL);

      APR_ARRAY_PUSH(output_targets, const char *) = target;
    }


  /* kff todo: need to remove redundancies from targets before
     passing it to the cmd_func. */

  *targets_p = output_targets;

  return err;
}

svn_error_t *
svn_opt_parse_revprop(apr_hash_t **revprop_table_p, const char *revprop_spec,
                      apr_pool_t *pool)
{
  const char *sep, *propname;
  svn_string_t *propval;

  if (! *revprop_spec)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Revision property pair is empty"));

  if (! *revprop_table_p)
    *revprop_table_p = apr_hash_make(pool);

  sep = strchr(revprop_spec, '=');
  if (sep)
    {
      propname = apr_pstrndup(pool, revprop_spec, sep - revprop_spec);
      SVN_ERR(svn_utf_cstring_to_utf8(&propname, propname, pool));
      propval = svn_string_create(sep + 1, pool);
    }
  else
    {
      SVN_ERR(svn_utf_cstring_to_utf8(&propname, revprop_spec, pool));
      propval = svn_string_create_empty(pool);
    }

  if (!svn_prop_name_is_valid(propname))
    return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                             _("'%s' is not a valid Subversion property name"),
                             propname);

  svn_hash_sets(*revprop_table_p, propname, propval);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_opt__split_arg_at_peg_revision(const char **true_target,
                                   const char **peg_revision,
                                   const char *utf8_target,
                                   apr_pool_t *pool)
{
  const char *peg_start = NULL; /* pointer to the peg revision, if any */
  const char *ptr;

  for (ptr = (utf8_target + strlen(utf8_target) - 1); ptr >= utf8_target;
        --ptr)
    {
      /* If we hit a path separator, stop looking.  This is OK
          only because our revision specifiers can't contain '/'. */
      if (*ptr == '/')
        break;

      if (*ptr == '@')
        {
          peg_start = ptr;
          break;
        }
    }

  if (peg_start)
    {
      *true_target = apr_pstrmemdup(pool, utf8_target, ptr - utf8_target);
      if (peg_revision)
        *peg_revision = apr_pstrdup(pool, peg_start);
    }
  else
    {
      *true_target = utf8_target;
      if (peg_revision)
        *peg_revision = "";
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_opt__arg_canonicalize_url(const char **url_out, const char *url_in,
                              apr_pool_t *pool)
{
  const char *target;

  /* Convert to URI. */
  target = svn_path_uri_from_iri(url_in, pool);
  /* Auto-escape some ASCII characters. */
  target = svn_path_uri_autoescape(target, pool);

#if '/' != SVN_PATH_LOCAL_SEPARATOR
  /* Allow using file:///C:\users\me/repos on Windows, like we did in 1.6 */
  if (strchr(target, SVN_PATH_LOCAL_SEPARATOR))
    {
      char *p = apr_pstrdup(pool, target);
      target = p;

      /* Convert all local-style separators to the canonical ones. */
      for (; *p != '\0'; ++p)
        if (*p == SVN_PATH_LOCAL_SEPARATOR)
          *p = '/';
    }
#endif

  /* Verify that no backpaths are present in the URL. */
  if (svn_path_is_backpath_present(target))
    return svn_error_createf(SVN_ERR_BAD_URL, 0,
                             _("URL '%s' contains a '..' element"),
                             target);

  /* Strip any trailing '/' and collapse other redundant elements. */
  target = svn_uri_canonicalize(target, pool);

  *url_out = target;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_opt__arg_canonicalize_path(const char **path_out, const char *path_in,
                               apr_pool_t *pool)
{
  const char *apr_target;
  char *truenamed_target; /* APR-encoded */
  apr_status_t apr_err;

  /* canonicalize case, and change all separators to '/'. */
  SVN_ERR(svn_path_cstring_from_utf8(&apr_target, path_in, pool));
  apr_err = apr_filepath_merge(&truenamed_target, "", apr_target,
                               APR_FILEPATH_TRUENAME, pool);

  if (!apr_err)
    /* We have a canonicalized APR-encoded target now. */
    apr_target = truenamed_target;
  else if (APR_STATUS_IS_ENOENT(apr_err))
    /* It's okay for the file to not exist, that just means we
       have to accept the case given to the client. We'll use
       the original APR-encoded target. */
    ;
  else
    return svn_error_createf(apr_err, NULL,
                             _("Error resolving case of '%s'"),
                             svn_dirent_local_style(path_in, pool));

  /* convert back to UTF-8. */
  SVN_ERR(svn_path_cstring_to_utf8(path_out, apr_target, pool));
  *path_out = svn_dirent_canonicalize(*path_out, pool);

  return SVN_NO_ERROR;
}
