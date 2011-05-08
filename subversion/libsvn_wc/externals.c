/*
 * externals.c :  routines dealing with (file) externals in the working copy
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



#include <stdlib.h>
#include <string.h>

#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include <apr_general.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_props.h"
#include "svn_io.h"
#include "svn_hash.h"
#include "svn_wc.h"

#include "private/svn_mergeinfo_private.h"
#include "private/svn_skel.h"

#include "wc.h"
#include "props.h"
#include "translate.h"
#include "workqueue.h"
#include "conflicts.h"

#include "svn_private_config.h"

/** Externals **/

/*
 * Look for either
 *
 *   -r N
 *   -rN
 *
 * in the LINE_PARTS array and update the revision field in ITEM with
 * the revision if the revision is found.  Set REV_IDX to the index in
 * LINE_PARTS where the revision specification starts.  Remove from
 * LINE_PARTS the element(s) that specify the revision.
 * PARENT_DIRECTORY_DISPLAY and LINE are given to return a nice error
 * string.
 *
 * If this function returns successfully, then LINE_PARTS will have
 * only two elements in it.
 */
static svn_error_t *
find_and_remove_externals_revision(int *rev_idx,
                                   const char **line_parts,
                                   int num_line_parts,
                                   svn_wc_external_item2_t *item,
                                   const char *parent_directory_display,
                                   const char *line,
                                   apr_pool_t *pool)
{
  int i;

  for (i = 0; i < 2; ++i)
    {
      const char *token = line_parts[i];

      if (token[0] == '-' && token[1] == 'r')
        {
          svn_opt_revision_t end_revision = { svn_opt_revision_unspecified };
          const char *digits_ptr;
          int shift_count;
          int j;

          *rev_idx = i;

          if (token[2] == '\0')
            {
              /* There must be a total of four elements in the line if
                 -r N is used. */
              if (num_line_parts != 4)
                goto parse_error;

              shift_count = 2;
              digits_ptr = line_parts[i+1];
            }
          else
            {
              /* There must be a total of three elements in the line
                 if -rN is used. */
              if (num_line_parts != 3)
                goto parse_error;

              shift_count = 1;
              digits_ptr = token+2;
            }

          if (svn_opt_parse_revision(&item->revision,
                                     &end_revision,
                                     digits_ptr, pool) != 0)
            goto parse_error;
          /* We want a single revision, not a range. */
          if (end_revision.kind != svn_opt_revision_unspecified)
            goto parse_error;
          /* Allow only numbers and dates, not keywords. */
          if (item->revision.kind != svn_opt_revision_number
              && item->revision.kind != svn_opt_revision_date)
            goto parse_error;

          /* Shift any line elements past the revision specification
             down over the revision specification. */
          for (j = i; j < num_line_parts-shift_count; ++j)
            line_parts[j] = line_parts[j+shift_count];
          line_parts[num_line_parts-shift_count] = NULL;

          /* Found the revision, so leave the function immediately, do
           * not continue looking for additional revisions. */
          return SVN_NO_ERROR;
        }
    }

  /* No revision was found, so there must be exactly two items in the
     line array. */
  if (num_line_parts == 2)
    return SVN_NO_ERROR;

 parse_error:
  return svn_error_createf
    (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
     _("Error parsing %s property on '%s': '%s'"),
     SVN_PROP_EXTERNALS,
     parent_directory_display,
     line);
}

svn_error_t *
svn_wc_parse_externals_description3(apr_array_header_t **externals_p,
                                    const char *parent_directory,
                                    const char *desc,
                                    svn_boolean_t canonicalize_url,
                                    apr_pool_t *pool)
{
  apr_array_header_t *lines = svn_cstring_split(desc, "\n\r", TRUE, pool);
  int i;
  const char *parent_directory_display = svn_path_is_url(parent_directory) ?
    parent_directory : svn_dirent_local_style(parent_directory, pool);

  if (externals_p)
    *externals_p = apr_array_make(pool, 1, sizeof(svn_wc_external_item2_t *));

  for (i = 0; i < lines->nelts; i++)
    {
      const char *line = APR_ARRAY_IDX(lines, i, const char *);
      apr_status_t status;
      char **line_parts;
      int num_line_parts;
      svn_wc_external_item2_t *item;
      const char *token0;
      const char *token1;
      svn_boolean_t token0_is_url;
      svn_boolean_t token1_is_url;

      /* Index into line_parts where the revision specification
         started. */
      int rev_idx = -1;

      if ((! line) || (line[0] == '#'))
        continue;

      /* else proceed */

      status = apr_tokenize_to_argv(line, &line_parts, pool);
      if (status)
        return svn_error_wrap_apr(status,
                                  _("Can't split line into components: '%s'"),
                                  line);
      /* Count the number of tokens. */
      for (num_line_parts = 0; line_parts[num_line_parts]; num_line_parts++)
        ;

      SVN_ERR(svn_wc_external_item_create
              ((const svn_wc_external_item2_t **) &item, pool));
      item->revision.kind = svn_opt_revision_unspecified;
      item->peg_revision.kind = svn_opt_revision_unspecified;

      /*
       * There are six different formats of externals:
       *
       * 1) DIR URL
       * 2) DIR -r N URL
       * 3) DIR -rN  URL
       * 4) URL DIR
       * 5) -r N URL DIR
       * 6) -rN URL DIR
       *
       * The last three allow peg revisions in the URL.
       *
       * With relative URLs and no '-rN' or '-r N', there is no way to
       * distinguish between 'DIR URL' and 'URL DIR' when URL is a
       * relative URL like /svn/repos/trunk, so this case is taken as
       * case 4).
       */
      if (num_line_parts < 2 || num_line_parts > 4)
        return svn_error_createf
          (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
           _("Error parsing %s property on '%s': '%s'"),
           SVN_PROP_EXTERNALS,
           parent_directory_display,
           line);

      /* To make it easy to check for the forms, find and remove -r N
         or -rN from the line item array.  If it is found, rev_idx
         contains the index into line_parts where '-r' was found and
         set item->revision to the parsed revision. */
      /* ### ugh. stupid cast. */
      SVN_ERR(find_and_remove_externals_revision(&rev_idx,
                                                 (const char **)line_parts,
                                                 num_line_parts, item,
                                                 parent_directory_display,
                                                 line, pool));

      token0 = line_parts[0];
      token1 = line_parts[1];

      token0_is_url = svn_path_is_url(token0);
      token1_is_url = svn_path_is_url(token1);

      if (token0_is_url && token1_is_url)
        return svn_error_createf
          (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
           _("Invalid %s property on '%s': "
             "cannot use two absolute URLs ('%s' and '%s') in an external; "
             "one must be a path where an absolute or relative URL is "
             "checked out to"),
           SVN_PROP_EXTERNALS, parent_directory_display, token0, token1);

      if (0 == rev_idx && token1_is_url)
        return svn_error_createf
          (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
           _("Invalid %s property on '%s': "
             "cannot use a URL '%s' as the target directory for an external "
             "definition"),
           SVN_PROP_EXTERNALS, parent_directory_display, token1);

      if (1 == rev_idx && token0_is_url)
        return svn_error_createf
          (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
           _("Invalid %s property on '%s': "
             "cannot use a URL '%s' as the target directory for an external "
             "definition"),
           SVN_PROP_EXTERNALS, parent_directory_display, token0);

      /* The appearence of -r N or -rN forces the type of external.
         If -r is at the beginning of the line or the first token is
         an absolute URL or if the second token is not an absolute
         URL, then the URL supports peg revisions. */
      if (0 == rev_idx ||
          (-1 == rev_idx && (token0_is_url || ! token1_is_url)))
        {
          /* The URL is passed to svn_opt_parse_path in
             uncanonicalized form so that the scheme relative URL
             //hostname/foo is not collapsed to a server root relative
             URL /hostname/foo. */
          SVN_ERR(svn_opt_parse_path(&item->peg_revision, &item->url,
                                     token0, pool));
          item->target_dir = token1;
        }
      else
        {
          item->target_dir = token0;
          item->url = token1;
          item->peg_revision = item->revision;
        }

      SVN_ERR(svn_opt_resolve_revisions(&item->peg_revision,
                                        &item->revision, TRUE, FALSE,
                                        pool));

      item->target_dir = svn_dirent_internal_style(item->target_dir, pool);

      if (item->target_dir[0] == '\0' || item->target_dir[0] == '/'
          || svn_path_is_backpath_present(item->target_dir))
        return svn_error_createf
          (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
           _("Invalid %s property on '%s': "
             "target '%s' is an absolute path or involves '..'"),
           SVN_PROP_EXTERNALS,
           parent_directory_display,
           item->target_dir);

      if (canonicalize_url)
        {
          /* Uh... this is stupid.  But it's consistent with what our
             code did before we split up the relpath/dirent/uri APIs.
             Still, given this, it's no wonder that our own libraries
             don't ask this function to canonicalize the results.  */
          if (svn_path_is_url(item->url))
            item->url = svn_uri_canonicalize(item->url, pool);
          else
            item->url = svn_dirent_canonicalize(item->url, pool);
        }

      if (externals_p)
        APR_ARRAY_PUSH(*externals_p, svn_wc_external_item2_t *) = item;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__set_file_external_location(svn_wc_context_t *wc_ctx,
                                   const char *local_abspath,
                                   const char *url,
                                   const svn_opt_revision_t *peg_rev,
                                   const svn_opt_revision_t *rev,
                                   const char *repos_root_url,
                                   apr_pool_t *scratch_pool)
{
  const char *external_repos_relpath;
  const svn_opt_revision_t unspecified_rev = { svn_opt_revision_unspecified };

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(!url || svn_uri_is_canonical(url, scratch_pool));

  if (url)
    {
      external_repos_relpath = svn_uri_is_child(repos_root_url, url,
                                                scratch_pool);

      if (external_repos_relpath == NULL)
          return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                   _("Can't add a file external to '%s' as it"
                                     " is not a file in repository '%s'."),
                                   url, repos_root_url);

      SVN_ERR_ASSERT(peg_rev != NULL);
      SVN_ERR_ASSERT(rev != NULL);
    }
  else
    {
      external_repos_relpath = NULL;
      peg_rev = &unspecified_rev;
      rev = &unspecified_rev;
    }

  SVN_ERR(svn_wc__db_temp_op_set_file_external(wc_ctx->db, local_abspath,
                                               external_repos_relpath, peg_rev,
                                               rev, scratch_pool));

  return SVN_NO_ERROR;
}

struct edit_baton
{
  apr_pool_t *pool;
  svn_wc__db_t *db;

  const char *local_abspath;
  const char *name;

  svn_revnum_t *target_revision;
  svn_revnum_t base_revision;
};

/* svn_delta_editor_t function for svn_wc__get_file_external_editor */
static svn_error_t *
set_target_revision(void *edit_baton,
                     svn_revnum_t target_revision,
                     apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  *eb->target_revision = target_revision;

  return SVN_NO_ERROR;
}

/* svn_delta_editor_t function for svn_wc__get_file_external_editor */
static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *dir_pool,
          void **root_baton)
{
  *root_baton = edit_baton;
  return SVN_NO_ERROR;
}

/* svn_delta_editor_t function for svn_wc__get_file_external_editor */
static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *file_pool,
         void **file_baton)
{
  struct edit_baton *eb = parent_baton;
  if (strcmp(path, eb->name))
      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                               _("This editor can only update '%s'"),
                               svn_dirent_local_style(eb->local_abspath,
                                                      file_pool));

  eb->base_revision = SVN_INVALID_REVNUM;

  return SVN_NO_ERROR;
}

/* svn_delta_editor_t function for svn_wc__get_file_external_editor */
static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *file_pool,
          void **file_baton)
{
  struct edit_baton *eb = parent_baton;
  if (strcmp(path, eb->name))
      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                               _("This editor can only update '%s'"),
                               svn_dirent_local_style(eb->local_abspath,
                                                      file_pool));

  eb->base_revision = base_revision;

  return SVN_NO_ERROR;
}

/* svn_delta_editor_t function for svn_wc__get_file_external_editor */
static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{

  return SVN_NO_ERROR;
}

/* svn_delta_editor_t function for svn_wc__get_file_external_editor */
static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

/* svn_delta_editor_t function for svn_wc__get_file_external_editor */
static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__get_file_external_editor(const svn_delta_editor_t **editor,
                                 void **edit_baton,
                                 const char *switch_url,
                                 svn_revnum_t *target_revision,
                                 svn_wc_context_t *wc_ctx,
                                 const char *local_abspath,
                                 svn_boolean_t use_commit_times,
                                 const char *diff3_cmd,
                                 svn_wc_conflict_resolver_func2_t conflict_func,
                                 void *conflict_baton,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 svn_wc_notify_func2_t notify_func,
                                 void *notify_baton,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = wc_ctx->db;
  apr_pool_t *edit_pool = result_pool;
  struct edit_baton *eb = apr_pcalloc(edit_pool, sizeof(*eb));
  svn_delta_editor_t *tree_editor = svn_delta_default_editor(edit_pool);

  eb->pool = edit_pool;
  eb->db = db;
  eb->local_abspath = apr_pstrdup(edit_pool, local_abspath);
  eb->name = svn_dirent_basename(eb->local_abspath, NULL);
  eb->target_revision = target_revision;

  tree_editor->open_root = open_root;
  tree_editor->set_target_revision = set_target_revision;
  tree_editor->add_file = add_file;
  tree_editor->open_file = open_file;
  tree_editor->apply_textdelta = apply_textdelta;
  tree_editor->change_file_prop = change_file_prop;
  tree_editor->close_file = close_file;

  return svn_delta_get_cancellation_editor(cancel_func, cancel_baton,
                                           tree_editor, eb,
                                           editor, edit_baton,
                                           result_pool);
}

svn_error_t *
svn_wc__crawl_file_external(svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            const svn_ra_reporter3_t *reporter,
                            void *report_baton,
                            svn_boolean_t restore_files,
                            svn_boolean_t use_commit_times,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            svn_wc_notify_func2_t notify_func,
                            void *notify_baton,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = wc_ctx->db;
  svn_error_t *err;
  svn_wc__db_status_t status;
  svn_wc__db_kind_t kind;
  svn_wc__db_lock_t *lock;
  svn_revnum_t revision;
  const char *repos_root_url;
  const char *repos_relpath;

  err = svn_wc__db_base_get_info(&status, &kind, &revision,
                                 &repos_relpath, &repos_root_url, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, &lock,
                                 NULL, NULL, NULL,
                                 db, local_abspath,
                                 scratch_pool, scratch_pool);

  if (err
      || status != svn_wc__db_status_normal
      || kind == svn_wc__db_kind_dir)
    {
      if (err && err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_return(err);

      svn_error_clear(err);

      /* We don't know about this node, so all we have to do is tell
         the reporter that we don't know this node.

         But first we have to start the report by sending some basic
         information for the root. */

      SVN_ERR(reporter->set_path(report_baton, "", 0, svn_depth_infinity,
                                 FALSE, NULL, scratch_pool));
      SVN_ERR(reporter->delete_path(report_baton, "", scratch_pool));

      /* Finish the report, which causes the update editor to be
         driven. */
      SVN_ERR(reporter->finish_report(report_baton, scratch_pool));

      return SVN_NO_ERROR;
    }
  else
    {
      /* Report that we know the path */
      SVN_ERR(reporter->set_path(report_baton, "", revision,
                                 svn_depth_infinity, FALSE, NULL,
                                 scratch_pool));

      /* For compatibility with the normal update editor report we report
         the target as switched.

         ### We can probably report a parent url and unswitched later */
      SVN_ERR(reporter->link_path(report_baton, "", 
                                  svn_path_url_add_component2(repos_root_url,
                                                              repos_relpath,
                                                              scratch_pool),
                                  revision,
                                  svn_depth_infinity,
                                  FALSE /* start_empty*/,
                                  lock ? lock->token : NULL,
                                  scratch_pool));
    }

  return svn_error_return(reporter->finish_report(report_baton, scratch_pool));
}
