/*
 * diff.c: comparing
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

/* ==================================================================== */



/*** Includes. ***/

#include <apr_strings.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include "svn_types.h"
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_delta.h"
#include "svn_diff.h"
#include "svn_mergeinfo.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_utf.h"
#include "svn_pools.h"
#include "svn_config.h"
#include "svn_props.h"
#include "svn_time.h"
#include "svn_sorts.h"
#include "svn_subst.h"
#include "client.h"

#include "private/svn_wc_private.h"

#include "svn_private_config.h"


/*
 * Constant separator strings
 */
static const char equal_string[] =
  "===================================================================";
static const char under_string[] =
  "___________________________________________________________________";


/*-----------------------------------------------------------------*/

/* Utilities */


/* A helper function for display_prop_diffs.  Output the differences between
   the mergeinfo stored in ORIG_MERGEINFO_VAL and NEW_MERGEINFO_VAL in a
   human-readable form to OUTSTREAM, using ENCODING.  Use POOL for temporary
   allocations. */
static svn_error_t *
display_mergeinfo_diff(const char *old_mergeinfo_val,
                       const char *new_mergeinfo_val,
                       const char *encoding,
                       svn_stream_t *outstream,
                       apr_pool_t *pool)
{
  apr_hash_t *old_mergeinfo_hash, *new_mergeinfo_hash, *added, *deleted;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_hash_index_t *hi;

  if (old_mergeinfo_val)
    SVN_ERR(svn_mergeinfo_parse(&old_mergeinfo_hash, old_mergeinfo_val, pool));
  else
    old_mergeinfo_hash = NULL;

  if (new_mergeinfo_val)
    SVN_ERR(svn_mergeinfo_parse(&new_mergeinfo_hash, new_mergeinfo_val, pool));
  else
    new_mergeinfo_hash = NULL;

  SVN_ERR(svn_mergeinfo_diff2(&deleted, &added, old_mergeinfo_hash,
                              new_mergeinfo_hash,
                              TRUE, pool, pool));

  for (hi = apr_hash_first(pool, deleted);
       hi; hi = apr_hash_next(hi))
    {
      const char *from_path = svn__apr_hash_index_key(hi);
      svn_rangelist_t *merge_revarray = svn__apr_hash_index_val(hi);
      svn_string_t *merge_revstr;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_rangelist_to_string(&merge_revstr, merge_revarray,
                                      iterpool));

      SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, iterpool,
                                          _("   Reverse-merged %s:r%s%s"),
                                          from_path, merge_revstr->data,
                                          APR_EOL_STR));
    }

  for (hi = apr_hash_first(pool, added);
       hi; hi = apr_hash_next(hi))
    {
      const char *from_path = svn__apr_hash_index_key(hi);
      svn_rangelist_t *merge_revarray = svn__apr_hash_index_val(hi);
      svn_string_t *merge_revstr;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_rangelist_to_string(&merge_revstr, merge_revarray,
                                      iterpool));

      SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, iterpool,
                                          _("   Merged %s:r%s%s"),
                                          from_path, merge_revstr->data,
                                          APR_EOL_STR));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

#define MAKE_ERR_BAD_RELATIVE_PATH(path, relative_to_dir) \
        svn_error_createf(SVN_ERR_BAD_RELATIVE_PATH, NULL, \
                          _("Path '%s' must be an immediate child of " \
                            "the directory '%s'"), path, relative_to_dir)

/* A helper function used by display_prop_diffs.
   TOKEN is a string holding a property value.
   If TOKEN is empty, or is already terminated by an EOL marker,
   return TOKEN unmodified. Else, return a new string consisting
   of the concatenation of TOKEN and the system's default EOL marker.
   The new string is allocated from POOL.
   If HAD_EOL is not NULL, indicate in *HAD_EOL if the token had a EOL. */
static const svn_string_t *
maybe_append_eol(const svn_string_t *token, svn_boolean_t *had_eol,
                 apr_pool_t *pool)
{
  const char *curp;

  if (had_eol)
    *had_eol = FALSE;

  if (token->len == 0)
    return token;

  curp = token->data + token->len - 1;
  if (*curp == '\r')
    {
      if (had_eol)
        *had_eol = TRUE;
      return token;
    }
  else if (*curp != '\n')
    {
      return svn_string_createf(pool, "%s%s", token->data, APR_EOL_STR);
    }
  else
    {
      if (had_eol)
        *had_eol = TRUE;
      return token;
    }
}

/* Adjust PATH to be relative to the repository root beneath ORIG_TARGET,
 * using RA_SESSION and WC_CTX, and return the result in *ADJUSTED_PATH.
 * ORIG_TARGET is one of the original targets passed to the diff command,
 * and may be used to derive leading path components missing from PATH.
 * WC_ROOT_ABSPATH is the absolute path to the root directory of a working
 * copy involved in a repos-wc diff, and may be NULL.
 * Do all allocations in POOL. */
static svn_error_t *
adjust_relative_to_repos_root(const char **adjusted_path,
                              const char *path,
                              const char *orig_target,
                              svn_ra_session_t *ra_session,
                              svn_wc_context_t *wc_ctx,
                              const char *wc_root_abspath,
                              apr_pool_t *pool)
{
  const char *local_abspath;
  const char *orig_relpath;
  const char *child_relpath;

  if (! ra_session)
    {
      /* We're doing a WC-WC diff, so we can retrieve all information we
       * need from the working copy. */
      SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
      SVN_ERR(svn_wc__node_get_repos_relpath(adjusted_path, wc_ctx,
                                             local_abspath, pool, pool));
      return SVN_NO_ERROR;
    }

  /* Now deal with the repos-repos and repos-wc diff cases.
   * We need to make PATH appear as a child of ORIG_TARGET.
   * ORIG_TARGET is either a URL or a path to a working copy. First,
   * find out what ORIG_TARGET looks like relative to the repository root.*/
  if (svn_path_is_url(orig_target))
    SVN_ERR(svn_ra_get_path_relative_to_root(ra_session,
                                             &orig_relpath,
                                             orig_target, pool));
  else
    {
      const char *orig_abspath;

      SVN_ERR(svn_dirent_get_absolute(&orig_abspath, orig_target, pool));
      SVN_ERR(svn_wc__node_get_repos_relpath(&orig_relpath, wc_ctx,
                                             orig_abspath, pool, pool));
    }

  /* PATH is either a child of the working copy involved in the diff (in
   * the repos-wc diff case), or it's a relative path we can readily use
   * (in either of the repos-repos and repos-wc diff cases). */
  child_relpath = NULL;
  if (wc_root_abspath)
    {
      SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
      child_relpath = svn_dirent_is_child(wc_root_abspath, local_abspath, pool);
    }
  if (child_relpath == NULL)
    child_relpath = path;

  *adjusted_path = svn_relpath_join(orig_relpath, child_relpath, pool);

  return SVN_NO_ERROR;
}

/* Adjust *PATH, *ORIG_PATH_1 and *ORIG_PATH_2, representing the changed file
 * and the two original targets passed to the diff command, to handle the
 * case when we're dealing with different anchors. RELATIVE_TO_DIR is the
 * directory the diff target should be considered relative to. All
 * allocations are done in POOL. */
static svn_error_t *
adjust_paths_for_diff_labels(const char **path,
                             const char **orig_path_1,
                             const char **orig_path_2,
                             const char *relative_to_dir,
                             apr_pool_t *pool)
{
  apr_size_t len;
  const char *new_path = *path;
  const char *new_path1 = *orig_path_1;
  const char *new_path2 = *orig_path_2;

  /* ### Holy cow.  Due to anchor/target weirdness, we can't
     simply join diff_cmd_baton->orig_path_1 with path, ditto for
     orig_path_2.  That will work when they're directory URLs, but
     not for file URLs.  Nor can we just use anchor1 and anchor2
     from do_diff(), at least not without some more logic here.
     What a nightmare.

     For now, to distinguish the two paths, we'll just put the
     unique portions of the original targets in parentheses after
     the received path, with ellipses for handwaving.  This makes
     the labels a bit clumsy, but at least distinctive.  Better
     solutions are possible, they'll just take more thought. */

  len = strlen(svn_dirent_get_longest_ancestor(new_path1, new_path2, pool));
  new_path1 = new_path1 + len;
  new_path2 = new_path2 + len;

  /* ### Should diff labels print paths in local style?  Is there
     already a standard for this?  In any case, this code depends on
     a particular style, so not calling svn_dirent_local_style() on the
     paths below.*/
  if (new_path1[0] == '\0')
    new_path1 = apr_psprintf(pool, "%s", new_path);
  else if (new_path1[0] == '/')
    new_path1 = apr_psprintf(pool, "%s\t(...%s)", new_path, new_path1);
  else
    new_path1 = apr_psprintf(pool, "%s\t(.../%s)", new_path, new_path1);

  if (new_path2[0] == '\0')
    new_path2 = apr_psprintf(pool, "%s", new_path);
  else if (new_path2[0] == '/')
    new_path2 = apr_psprintf(pool, "%s\t(...%s)", new_path, new_path2);
  else
    new_path2 = apr_psprintf(pool, "%s\t(.../%s)", new_path, new_path2);

  if (relative_to_dir)
    {
      /* Possibly adjust the paths shown in the output (see issue #2723). */
      const char *child_path = svn_dirent_is_child(relative_to_dir, new_path,
                                                   pool);

      if (child_path)
        new_path = child_path;
      else if (!svn_path_compare_paths(relative_to_dir, new_path))
        new_path = ".";
      else
        return MAKE_ERR_BAD_RELATIVE_PATH(new_path, relative_to_dir);

      child_path = svn_dirent_is_child(relative_to_dir, new_path1, pool);

      if (child_path)
        new_path1 = child_path;
      else if (!svn_path_compare_paths(relative_to_dir, new_path1))
        new_path1 = ".";
      else
        return MAKE_ERR_BAD_RELATIVE_PATH(new_path1, relative_to_dir);

      child_path = svn_dirent_is_child(relative_to_dir, new_path2, pool);

      if (child_path)
        new_path2 = child_path;
      else if (!svn_path_compare_paths(relative_to_dir, new_path2))
        new_path2 = ".";
      else
        return MAKE_ERR_BAD_RELATIVE_PATH(new_path2, relative_to_dir);
    }
  *path = new_path;
  *orig_path_1 = new_path1;
  *orig_path_2 = new_path2;

  return SVN_NO_ERROR;
}


/* Generate a label for the diff output for file PATH at revision REVNUM.
   If REVNUM is invalid then it is assumed to be the current working
   copy.  Assumes the paths are already in the desired style (local
   vs internal).  Allocate the label in POOL. */
static const char *
diff_label(const char *path,
           svn_revnum_t revnum,
           apr_pool_t *pool)
{
  const char *label;
  if (revnum != SVN_INVALID_REVNUM)
    label = apr_psprintf(pool, _("%s\t(revision %ld)"), path, revnum);
  else
    label = apr_psprintf(pool, _("%s\t(working copy)"), path);

  return label;
}

/* Print a git diff header for an addition within a diff between PATH1 and
 * PATH2 to the stream OS using HEADER_ENCODING.
 * All allocations are done in RESULT_POOL. */
static svn_error_t *
print_git_diff_header_added(svn_stream_t *os, const char *header_encoding,
                            const char *path1, const char *path2,
                            apr_pool_t *result_pool)
{
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, result_pool,
                                      "diff --git a/%s b/%s%s",
                                      path1, path2, APR_EOL_STR));
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, result_pool,
                                      "new file mode 10644" APR_EOL_STR));
  return SVN_NO_ERROR;
}

/* Print a git diff header for a deletion within a diff between PATH1 and
 * PATH2 to the stream OS using HEADER_ENCODING.
 * All allocations are done in RESULT_POOL. */
static svn_error_t *
print_git_diff_header_deleted(svn_stream_t *os, const char *header_encoding,
                              const char *path1, const char *path2,
                              apr_pool_t *result_pool)
{
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, result_pool,
                                      "diff --git a/%s b/%s%s",
                                      path1, path2, APR_EOL_STR));
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, result_pool,
                                      "deleted file mode 10644"
                                      APR_EOL_STR));
  return SVN_NO_ERROR;
}

/* Print a git diff header for a copy from COPYFROM_PATH to PATH to the stream
 * OS using HEADER_ENCODING. All allocations are done in RESULT_POOL. */
static svn_error_t *
print_git_diff_header_copied(svn_stream_t *os, const char *header_encoding,
                             const char *copyfrom_path,
                             svn_revnum_t copyfrom_rev,
                             const char *path,
                             apr_pool_t *result_pool)
{
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, result_pool,
                                      "diff --git a/%s b/%s%s",
                                      copyfrom_path, path, APR_EOL_STR));
  if (copyfrom_rev != SVN_INVALID_REVNUM)
    SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, result_pool,
                                        "copy from %s@%ld%s", copyfrom_path,
                                        copyfrom_rev, APR_EOL_STR));
  else
    SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, result_pool,
                                        "copy from %s%s", copyfrom_path,
                                        APR_EOL_STR));
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, result_pool,
                                      "copy to %s%s", path, APR_EOL_STR));
  return SVN_NO_ERROR;
}

/* Print a git diff header for a rename from COPYFROM_PATH to PATH to the
 * stream OS using HEADER_ENCODING. All allocations are done in RESULT_POOL. */
static svn_error_t *
print_git_diff_header_renamed(svn_stream_t *os, const char *header_encoding,
                              const char *copyfrom_path, const char *path,
                              apr_pool_t *result_pool)
{
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, result_pool,
                                      "diff --git a/%s b/%s%s",
                                      copyfrom_path, path, APR_EOL_STR));
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, result_pool,
                                      "rename from %s%s", copyfrom_path,
                                      APR_EOL_STR));
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, result_pool,
                                      "rename to %s%s", path, APR_EOL_STR));
  return SVN_NO_ERROR;
}

/* Print a git diff header for a modification within a diff between PATH1 and
 * PATH2 to the stream OS using HEADER_ENCODING.
 * All allocations are done in RESULT_POOL. */
static svn_error_t *
print_git_diff_header_modified(svn_stream_t *os, const char *header_encoding,
                               const char *path1, const char *path2,
                               apr_pool_t *result_pool)
{
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, result_pool,
                                      "diff --git a/%s b/%s%s",
                                      path1, path2, APR_EOL_STR));
  return SVN_NO_ERROR;
}

/* Print a git diff header showing the OPERATION to the stream OS using
 * HEADER_ENCODING. Return suitable diff labels for the git diff in *LABEL1
 * and *LABEL2. REPOS_RELPATH1 and REPOS_RELPATH2 are relative to reposroot.
 * are the paths passed to the original diff command. REV1 and REV2 are
 * revisions being diffed. COPYFROM_PATH and COPYFROM_REV indicate where the
 * diffed item was copied from.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
print_git_diff_header(svn_stream_t *os,
                      const char **label1, const char **label2,
                      svn_diff_operation_kind_t operation,
                      const char *repos_relpath1,
                      const char *repos_relpath2,
                      svn_revnum_t rev1,
                      svn_revnum_t rev2,
                      const char *copyfrom_path,
                      svn_revnum_t copyfrom_rev,
                      const char *header_encoding,
                      apr_pool_t *scratch_pool)
{
  if (operation == svn_diff_op_deleted)
    {
      SVN_ERR(print_git_diff_header_deleted(os, header_encoding,
                                            repos_relpath1, repos_relpath2,
                                            scratch_pool));
      *label1 = diff_label(apr_psprintf(scratch_pool, "a/%s", repos_relpath1),
                           rev1, scratch_pool);
      *label2 = diff_label("/dev/null", rev2, scratch_pool);

    }
  else if (operation == svn_diff_op_copied)
    {
      SVN_ERR(print_git_diff_header_copied(os, header_encoding,
                                           copyfrom_path, copyfrom_rev,
                                           repos_relpath2,
                                           scratch_pool));
      *label1 = diff_label(apr_psprintf(scratch_pool, "a/%s", copyfrom_path),
                           rev1, scratch_pool);
      *label2 = diff_label(apr_psprintf(scratch_pool, "b/%s", repos_relpath2),
                           rev2, scratch_pool);
    }
  else if (operation == svn_diff_op_added)
    {
      SVN_ERR(print_git_diff_header_added(os, header_encoding,
                                          repos_relpath1, repos_relpath2,
                                          scratch_pool));
      *label1 = diff_label("/dev/null", rev1, scratch_pool);
      *label2 = diff_label(apr_psprintf(scratch_pool, "b/%s", repos_relpath2),
                           rev2, scratch_pool);
    }
  else if (operation == svn_diff_op_modified)
    {
      SVN_ERR(print_git_diff_header_modified(os, header_encoding,
                                             repos_relpath1, repos_relpath2,
                                             scratch_pool));
      *label1 = diff_label(apr_psprintf(scratch_pool, "a/%s", repos_relpath1),
                           rev1, scratch_pool);
      *label2 = diff_label(apr_psprintf(scratch_pool, "b/%s", repos_relpath2),
                           rev2, scratch_pool);
    }
  else if (operation == svn_diff_op_moved)
    {
      SVN_ERR(print_git_diff_header_renamed(os, header_encoding,
                                            copyfrom_path, repos_relpath2,
                                            scratch_pool));
      *label1 = diff_label(apr_psprintf(scratch_pool, "a/%s", copyfrom_path),
                           rev1, scratch_pool);
      *label2 = diff_label(apr_psprintf(scratch_pool, "b/%s", repos_relpath2),
                           rev2, scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* A helper func that writes out verbal descriptions of property diffs
   to OUTSTREAM.   Of course, OUTSTREAM will probably be whatever was
   passed to svn_client_diff6(), which is probably stdout.

   ### FIXME needs proper docstring

   If USE_GIT_DIFF_FORMAT is TRUE, pring git diff headers, which always
   show paths relative to the repository root. RA_SESSION and WC_CTX are
   needed to normalize paths relative the repository root, and are ignored
   if USE_GIT_DIFF_FORMAT is FALSE.

   WC_ROOT_ABSPATH is the absolute path to the root directory of a working
   copy involved in a repos-wc diff, and may be NULL. */
static svn_error_t *
display_prop_diffs(const apr_array_header_t *propchanges,
                   apr_hash_t *original_props,
                   const char *path,
                   const char *orig_path1,
                   const char *orig_path2,
                   svn_revnum_t rev1,
                   svn_revnum_t rev2,
                   const char *encoding,
                   svn_stream_t *outstream,
                   const char *relative_to_dir,
                   svn_boolean_t show_diff_header,
                   svn_boolean_t use_git_diff_format,
                   svn_ra_session_t *ra_session,
                   svn_wc_context_t *wc_ctx,
                   const char *wc_root_abspath,
                   apr_pool_t *pool)
{
  int i;
  const char *path1 = orig_path1;
  const char *path2 = orig_path2;
  apr_pool_t *iterpool;

  if (use_git_diff_format)
    {
      SVN_ERR(adjust_relative_to_repos_root(&path1, path, orig_path1,
                                            ra_session, wc_ctx,
                                            wc_root_abspath,
                                            pool));
      SVN_ERR(adjust_relative_to_repos_root(&path2, path, orig_path2,
                                            ra_session, wc_ctx,
                                            wc_root_abspath,
                                            pool));
    }

  /* If we're creating a diff on the wc root, path would be empty. */
  if (path[0] == '\0')
    path = apr_psprintf(pool, ".");

  if (show_diff_header)
    {
      const char *label1;
      const char *label2;
      const char *adjusted_path1 = path1;
      const char *adjusted_path2 = path2;

      SVN_ERR(adjust_paths_for_diff_labels(&path, &adjusted_path1,
                                           &adjusted_path2,
                                           relative_to_dir, pool));

      label1 = diff_label(adjusted_path1, rev1, pool);
      label2 = diff_label(adjusted_path2, rev2, pool);

      /* ### Should we show the paths in platform specific format,
       * ### diff_content_changed() does not! */

      SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, pool,
                                          "Index: %s" APR_EOL_STR
                                          "%s" APR_EOL_STR,
                                          path, equal_string));

      if (use_git_diff_format)
        SVN_ERR(print_git_diff_header(outstream, &label1, &label2,
                                      svn_diff_op_modified,
                                      path1, path2, rev1, rev2, NULL,
                                      SVN_INVALID_REVNUM,
                                      encoding, pool));

      SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, pool,
                                          "--- %s" APR_EOL_STR
                                          "+++ %s" APR_EOL_STR,
                                          label1,
                                          label2));
    }

  SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, pool,
                                      _("%sProperty changes on: %s%s"),
                                      APR_EOL_STR,
                                      use_git_diff_format ? path1 : path,
                                      APR_EOL_STR));

  SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, pool,
                                      "%s" APR_EOL_STR, under_string));

  iterpool = svn_pool_create(pool);
  for (i = 0; i < propchanges->nelts; i++)
    {
      const char *action;
      const svn_string_t *original_value;
      const svn_prop_t *propchange =
        &APR_ARRAY_IDX(propchanges, i, svn_prop_t);

      if (original_props)
        original_value = apr_hash_get(original_props,
                                      propchange->name, APR_HASH_KEY_STRING);
      else
        original_value = NULL;

      /* If the property doesn't exist on either side, or if it exists
         with the same value, skip it.  */
      if ((! (original_value || propchange->value))
          || (original_value && propchange->value
              && svn_string_compare(original_value, propchange->value)))
        continue;

      svn_pool_clear(iterpool);

      if (! original_value)
        action = "Added";
      else if (! propchange->value)
        action = "Deleted";
      else
        action = "Modified";
      SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, iterpool,
                                          "%s: %s%s", action,
                                          propchange->name, APR_EOL_STR));

      if (strcmp(propchange->name, SVN_PROP_MERGEINFO) == 0)
        {
          const char *orig = original_value ? original_value->data : NULL;
          const char *val = propchange->value ? propchange->value->data : NULL;
          svn_error_t *err = display_mergeinfo_diff(orig, val, encoding,
                                                    outstream, iterpool);

          /* Issue #3896: If we can't pretty-print mergeinfo differences
             because invalid mergeinfo is present, then don't let the diff
             fail, just print the diff as any other property. */
          if (err && err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
            {
              svn_error_clear(err);
            }
          else
            {
              SVN_ERR(err);
              continue;
            }
        }

      {
        svn_diff_t *diff;
        svn_diff_file_options_t options = { 0 };
        const svn_string_t *tmp;
        const svn_string_t *orig;
        const svn_string_t *val;
        svn_boolean_t val_has_eol;

        /* The last character in a property is often not a newline.
           An eol character is appended to prevent the diff API to add a
           '\ No newline at end of file' line. We add
           '\ No newline at end of property' manually if needed. */
        tmp = original_value ? original_value
                             : svn_string_create_empty(iterpool);
        orig = maybe_append_eol(tmp, NULL, iterpool);

        tmp = propchange->value ? propchange->value :
                                  svn_string_create_empty(iterpool);
        val = maybe_append_eol(tmp, &val_has_eol, iterpool);

        SVN_ERR(svn_diff_mem_string_diff(&diff, orig, val, &options,
                                         iterpool));

        /* UNIX patch will try to apply a diff even if the diff header
         * is missing. It tries to be helpful by asking the user for a
         * target filename when it can't determine the target filename
         * from the diff header. But there usually are no files which
         * UNIX patch could apply the property diff to, so we use "##"
         * instead of "@@" as the default hunk delimiter for property diffs.
         * We also supress the diff header. */
        SVN_ERR(svn_diff_mem_string_output_unified2(outstream, diff, FALSE,
                                                    "##",
                                           svn_dirent_local_style(path,
                                                                  iterpool),
                                           svn_dirent_local_style(path,
                                                                  iterpool),
                                           encoding, orig, val, iterpool));
        if (!val_has_eol)
          {
            const char *s = "\\ No newline at end of property" APR_EOL_STR;
            SVN_ERR(svn_stream_puts(outstream, s));
          }
      }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/*-----------------------------------------------------------------*/

/*** Callbacks for 'svn diff', invoked by the repos-diff editor. ***/


struct diff_cmd_baton {

  /* If non-null, the external diff command to invoke. */
  const char *diff_cmd;

  /* This is allocated in this struct's pool or a higher-up pool. */
  union {
    /* If 'diff_cmd' is null, then this is the parsed options to
       pass to the internal libsvn_diff implementation. */
    svn_diff_file_options_t *for_internal;
    /* Else if 'diff_cmd' is non-null, then... */
    struct {
      /* ...this is an argument array for the external command, and */
      const char **argv;
      /* ...this is the length of argv. */
      int argc;
    } for_external;
  } options;

  apr_pool_t *pool;
  svn_stream_t *outstream;
  svn_stream_t *errstream;

  const char *header_encoding;

  /* The original targets passed to the diff command.  We may need
     these to construct distinctive diff labels when comparing the
     same relative path in the same revision, under different anchors
     (for example, when comparing a trunk against a branch). */
  const char *orig_path_1;
  const char *orig_path_2;

  /* These are the numeric representations of the revisions passed to
     svn_client_diff6(), either may be SVN_INVALID_REVNUM.  We need these
     because some of the svn_wc_diff_callbacks4_t don't get revision
     arguments.

     ### Perhaps we should change the callback signatures and eliminate
     ### these?
  */
  svn_revnum_t revnum1;
  svn_revnum_t revnum2;

  /* Set this if you want diff output even for binary files. */
  svn_boolean_t force_binary;

  /* Set this flag if you want diff_file_changed to output diffs
     unconditionally, even if the diffs are empty. */
  svn_boolean_t force_empty;

  /* The directory that diff target paths should be considered as
     relative to for output generation (see issue #2723). */
  const char *relative_to_dir;

  /* Whether property differences are ignored. */
  svn_boolean_t ignore_properties;

  /* Whether to show only property changes. */
  svn_boolean_t properties_only;

  /* Whether we're producing a git-style diff. */
  svn_boolean_t use_git_diff_format;

  /* Whether deletion of a file is summarized versus showing a full diff. */
  svn_boolean_t no_diff_deleted;

  svn_wc_context_t *wc_ctx;

  /* The RA session used during diffs involving the repository. */
  svn_ra_session_t *ra_session;

  /* During a repos-wc diff, this is the absolute path to the root
   * directory of the working copy involved in the diff. */
  const char *wc_root_abspath;

  /* The anchor to prefix before wc paths */
  const char *anchor;

  /* Whether the local diff target of a repos->wc diff is a copy. */
  svn_boolean_t repos_wc_diff_target_is_copy;

  /* A hashtable using the visited paths as keys.
   * ### This is needed for us to know if we need to print a diff header for
   * ### a path that has property changes. */
  apr_hash_t *visited_paths;
};


/* A helper function that marks a path as visited. It copies PATH
 * into the correct pool before referencing it from the hash table. */
static void
mark_path_as_visited(struct diff_cmd_baton *diff_cmd_baton, const char *path)
{
  const char *p;

  p = apr_pstrdup(apr_hash_pool_get(diff_cmd_baton->visited_paths), path);
  apr_hash_set(diff_cmd_baton->visited_paths, p, APR_HASH_KEY_STRING, p);
}

/* An helper for diff_dir_props_changed, diff_file_changed and diff_file_added
 */
static svn_error_t *
diff_props_changed(svn_wc_notify_state_t *state,
                   svn_boolean_t *tree_conflicted,
                   const char *path,
                   svn_boolean_t dir_was_added,
                   const apr_array_header_t *propchanges,
                   apr_hash_t *original_props,
                   struct diff_cmd_baton *diff_cmd_baton,
                   apr_pool_t *scratch_pool)
{
  apr_array_header_t *props;
  svn_boolean_t show_diff_header;

  /* If property differences are ignored, there's nothing to do. */
  if (diff_cmd_baton->ignore_properties)
    return SVN_NO_ERROR;

  SVN_ERR(svn_categorize_props(propchanges, NULL, NULL, &props,
                               scratch_pool));

  if (apr_hash_get(diff_cmd_baton->visited_paths, path, APR_HASH_KEY_STRING))
    show_diff_header = FALSE;
  else
    show_diff_header = TRUE;

  if (props->nelts > 0)
    {
      /* We're using the revnums from the diff_cmd_baton since there's
       * no revision argument to the svn_wc_diff_callback_t
       * dir_props_changed(). */
      SVN_ERR(display_prop_diffs(props, original_props, path,
                                 diff_cmd_baton->orig_path_1,
                                 diff_cmd_baton->orig_path_2,
                                 diff_cmd_baton->revnum1,
                                 diff_cmd_baton->revnum2,
                                 diff_cmd_baton->header_encoding,
                                 diff_cmd_baton->outstream,
                                 diff_cmd_baton->relative_to_dir,
                                 show_diff_header,
                                 diff_cmd_baton->use_git_diff_format,
                                 diff_cmd_baton->ra_session,
                                 diff_cmd_baton->wc_ctx,
                                 diff_cmd_baton->wc_root_abspath,
                                 scratch_pool));

      /* We've printed the diff header so now we can mark the path as
       * visited. */
      if (show_diff_header)
        mark_path_as_visited(diff_cmd_baton, path);
    }

  if (state)
    *state = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks4_t function. */
static svn_error_t *
diff_dir_props_changed(svn_wc_notify_state_t *state,
                       svn_boolean_t *tree_conflicted,
                       const char *path,
                       svn_boolean_t dir_was_added,
                       const apr_array_header_t *propchanges,
                       apr_hash_t *original_props,
                       void *diff_baton,
                       apr_pool_t *scratch_pool)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;

  if (diff_cmd_baton->anchor)
    path = svn_dirent_join(diff_cmd_baton->anchor, path, scratch_pool);

  return svn_error_trace(diff_props_changed(state,
                                            tree_conflicted, path,
                                            dir_was_added,
                                            propchanges,
                                            original_props,
                                            diff_cmd_baton,
                                            scratch_pool));
}


/* Show differences between TMPFILE1 and TMPFILE2. PATH, REV1, and REV2 are
   used in the headers to indicate the file and revisions.  If either
   MIMETYPE1 or MIMETYPE2 indicate binary content, don't show a diff,
   but instead print a warning message. */
static svn_error_t *
diff_content_changed(const char *path,
                     const char *tmpfile1,
                     const char *tmpfile2,
                     svn_revnum_t rev1,
                     svn_revnum_t rev2,
                     const char *mimetype1,
                     const char *mimetype2,
                     svn_diff_operation_kind_t operation,
                     const char *copyfrom_path,
                     svn_revnum_t copyfrom_rev,
                     struct diff_cmd_baton *diff_cmd_baton)
{
  int exitcode;
  apr_pool_t *subpool = svn_pool_create(diff_cmd_baton->pool);
  const char *rel_to_dir = diff_cmd_baton->relative_to_dir;
  svn_stream_t *errstream = diff_cmd_baton->errstream;
  svn_stream_t *outstream = diff_cmd_baton->outstream;
  const char *label1, *label2;
  svn_boolean_t mt1_binary = FALSE, mt2_binary = FALSE;
  const char *path1 = diff_cmd_baton->orig_path_1;
  const char *path2 = diff_cmd_baton->orig_path_2;

  /* If only property differences are shown, there's nothing to do. */
  if (diff_cmd_baton->properties_only)
    return SVN_NO_ERROR;

  /* Generate the diff headers. */
  SVN_ERR(adjust_paths_for_diff_labels(&path, &path1, &path2,
                                       rel_to_dir, subpool));

  label1 = diff_label(path1, rev1, subpool);
  label2 = diff_label(path2, rev2, subpool);

  /* Possible easy-out: if either mime-type is binary and force was not
     specified, don't attempt to generate a viewable diff at all.
     Print a warning and exit. */
  if (mimetype1)
    mt1_binary = svn_mime_type_is_binary(mimetype1);
  if (mimetype2)
    mt2_binary = svn_mime_type_is_binary(mimetype2);

  if (! diff_cmd_baton->force_binary && (mt1_binary || mt2_binary))
    {
      /* Print out the diff header. */
      SVN_ERR(svn_stream_printf_from_utf8(outstream,
               diff_cmd_baton->header_encoding, subpool,
               "Index: %s" APR_EOL_STR "%s" APR_EOL_STR, path, equal_string));

      /* ### Print git diff headers. */

      SVN_ERR(svn_stream_printf_from_utf8(outstream,
               diff_cmd_baton->header_encoding, subpool,
               _("Cannot display: file marked as a binary type.%s"),
               APR_EOL_STR));

      if (mt1_binary && !mt2_binary)
        SVN_ERR(svn_stream_printf_from_utf8(outstream,
                 diff_cmd_baton->header_encoding, subpool,
                 "svn:mime-type = %s" APR_EOL_STR, mimetype1));
      else if (mt2_binary && !mt1_binary)
        SVN_ERR(svn_stream_printf_from_utf8(outstream,
                 diff_cmd_baton->header_encoding, subpool,
                 "svn:mime-type = %s" APR_EOL_STR, mimetype2));
      else if (mt1_binary && mt2_binary)
        {
          if (strcmp(mimetype1, mimetype2) == 0)
            SVN_ERR(svn_stream_printf_from_utf8(outstream,
                     diff_cmd_baton->header_encoding, subpool,
                     "svn:mime-type = %s" APR_EOL_STR,
                     mimetype1));
          else
            SVN_ERR(svn_stream_printf_from_utf8(outstream,
                     diff_cmd_baton->header_encoding, subpool,
                     "svn:mime-type = (%s, %s)" APR_EOL_STR,
                     mimetype1, mimetype2));
        }

      /* Exit early. */
      svn_pool_destroy(subpool);
      return SVN_NO_ERROR;
    }


  if (diff_cmd_baton->diff_cmd)
    {
      apr_file_t *outfile;
      apr_file_t *errfile;
      const char *outfilename;
      const char *errfilename;
      svn_stream_t *stream;

      /* Print out the diff header. */
      SVN_ERR(svn_stream_printf_from_utf8(outstream,
               diff_cmd_baton->header_encoding, subpool,
               "Index: %s" APR_EOL_STR "%s" APR_EOL_STR, path, equal_string));

      /* ### Do we want to add git diff headers here too? I'd say no. The
       * ### 'Index' and '===' line is something subversion has added. The rest
       * ### is up to the external diff application. We may be dealing with
       * ### a non-git compatible diff application.*/

      /* We deal in streams, but svn_io_run_diff2() deals in file handles,
         unfortunately, so we need to make these temporary files, and then
         copy the contents to our stream. */
      SVN_ERR(svn_io_open_unique_file3(&outfile, &outfilename, NULL,
                                       svn_io_file_del_on_pool_cleanup,
                                       subpool, subpool));
      SVN_ERR(svn_io_open_unique_file3(&errfile, &errfilename, NULL,
                                       svn_io_file_del_on_pool_cleanup,
                                       subpool, subpool));

      SVN_ERR(svn_io_run_diff2(".",
                               diff_cmd_baton->options.for_external.argv,
                               diff_cmd_baton->options.for_external.argc,
                               label1, label2,
                               tmpfile1, tmpfile2,
                               &exitcode, outfile, errfile,
                               diff_cmd_baton->diff_cmd, subpool));

      SVN_ERR(svn_io_file_close(outfile, subpool));
      SVN_ERR(svn_io_file_close(errfile, subpool));

      /* Now, open and copy our files to our output streams. */
      SVN_ERR(svn_stream_open_readonly(&stream, outfilename,
                                       subpool, subpool));
      SVN_ERR(svn_stream_copy3(stream, svn_stream_disown(outstream, subpool),
                               NULL, NULL, subpool));
      SVN_ERR(svn_stream_open_readonly(&stream, errfilename,
                                       subpool, subpool));
      SVN_ERR(svn_stream_copy3(stream, svn_stream_disown(errstream, subpool),
                               NULL, NULL, subpool));

      /* We have a printed a diff for this path, mark it as visited. */
      mark_path_as_visited(diff_cmd_baton, path);
    }
  else   /* use libsvn_diff to generate the diff  */
    {
      svn_diff_t *diff;

      SVN_ERR(svn_diff_file_diff_2(&diff, tmpfile1, tmpfile2,
                                   diff_cmd_baton->options.for_internal,
                                   subpool));

      if (svn_diff_contains_diffs(diff) || diff_cmd_baton->force_empty ||
          diff_cmd_baton->use_git_diff_format)
        {
          /* Print out the diff header. */
          SVN_ERR(svn_stream_printf_from_utf8(outstream,
                   diff_cmd_baton->header_encoding, subpool,
                   "Index: %s" APR_EOL_STR "%s" APR_EOL_STR,
                   path, equal_string));

          if (diff_cmd_baton->use_git_diff_format)
            {
              const char *tmp_path1, *tmp_path2;
              SVN_ERR(adjust_relative_to_repos_root(
                         &tmp_path1, path, diff_cmd_baton->orig_path_1,
                         diff_cmd_baton->ra_session, diff_cmd_baton->wc_ctx,
                         diff_cmd_baton->wc_root_abspath, subpool));
              SVN_ERR(adjust_relative_to_repos_root(
                         &tmp_path2, path, diff_cmd_baton->orig_path_2,
                         diff_cmd_baton->ra_session, diff_cmd_baton->wc_ctx,
                         diff_cmd_baton->wc_root_abspath, subpool));
              SVN_ERR(print_git_diff_header(outstream, &label1, &label2,
                                            operation,
                                            tmp_path1, tmp_path2, rev1, rev2,
                                            copyfrom_path,
                                            copyfrom_rev,
                                            diff_cmd_baton->header_encoding,
                                            subpool));
            }

          /* Output the actual diff */
          if (svn_diff_contains_diffs(diff) || diff_cmd_baton->force_empty)
            SVN_ERR(svn_diff_file_output_unified3(outstream, diff,
                     tmpfile1, tmpfile2, label1, label2,
                     diff_cmd_baton->header_encoding, rel_to_dir,
                     diff_cmd_baton->options.for_internal->show_c_function,
                     subpool));

          /* We have a printed a diff for this path, mark it as visited. */
          mark_path_as_visited(diff_cmd_baton, path);
        }
    }

  /* ### todo: someday we'll need to worry about whether we're going
     to need to write a diff plug-in mechanism that makes use of the
     two paths, instead of just blindly running SVN_CLIENT_DIFF.  */

  /* Destroy the subpool. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
diff_file_opened(svn_boolean_t *tree_conflicted,
                 svn_boolean_t *skip,
                 const char *path,
                 svn_revnum_t rev,
                 void *diff_baton,
                 apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks4_t function. */
static svn_error_t *
diff_file_changed(svn_wc_notify_state_t *content_state,
                  svn_wc_notify_state_t *prop_state,
                  svn_boolean_t *tree_conflicted,
                  const char *path,
                  const char *tmpfile1,
                  const char *tmpfile2,
                  svn_revnum_t rev1,
                  svn_revnum_t rev2,
                  const char *mimetype1,
                  const char *mimetype2,
                  const apr_array_header_t *prop_changes,
                  apr_hash_t *original_props,
                  void *diff_baton,
                  apr_pool_t *scratch_pool)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;

  /* During repos->wc diff of a copy revision numbers obtained
   * from the working copy are always SVN_INVALID_REVNUM. */
  if (diff_cmd_baton->repos_wc_diff_target_is_copy)
    {
      if (rev1 == SVN_INVALID_REVNUM &&
          diff_cmd_baton->revnum1 != SVN_INVALID_REVNUM)
        rev1 = diff_cmd_baton->revnum1;

      if (rev2 == SVN_INVALID_REVNUM &&
          diff_cmd_baton->revnum2 != SVN_INVALID_REVNUM)
        rev2 = diff_cmd_baton->revnum2;
    }

  if (diff_cmd_baton->anchor)
    path = svn_dirent_join(diff_cmd_baton->anchor, path, scratch_pool);
  if (tmpfile1)
    SVN_ERR(diff_content_changed(path,
                                 tmpfile1, tmpfile2, rev1, rev2,
                                 mimetype1, mimetype2,
                                 svn_diff_op_modified, NULL,
                                 SVN_INVALID_REVNUM, diff_cmd_baton));
  if (prop_changes->nelts > 0)
    SVN_ERR(diff_props_changed(prop_state, tree_conflicted,
                               path, FALSE, prop_changes,
                               original_props, diff_cmd_baton, scratch_pool));
  if (content_state)
    *content_state = svn_wc_notify_state_unknown;
  if (prop_state)
    *prop_state = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;
  return SVN_NO_ERROR;
}

/* Because the repos-diff editor passes at least one empty file to
   each of these next two functions, they can be dumb wrappers around
   the main workhorse routine. */

/* An svn_wc_diff_callbacks4_t function. */
static svn_error_t *
diff_file_added(svn_wc_notify_state_t *content_state,
                svn_wc_notify_state_t *prop_state,
                svn_boolean_t *tree_conflicted,
                const char *path,
                const char *tmpfile1,
                const char *tmpfile2,
                svn_revnum_t rev1,
                svn_revnum_t rev2,
                const char *mimetype1,
                const char *mimetype2,
                const char *copyfrom_path,
                svn_revnum_t copyfrom_revision,
                const apr_array_header_t *prop_changes,
                apr_hash_t *original_props,
                void *diff_baton,
                apr_pool_t *scratch_pool)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;

  /* During repos->wc diff of a copy revision numbers obtained
   * from the working copy are always SVN_INVALID_REVNUM. */
  if (diff_cmd_baton->repos_wc_diff_target_is_copy)
    {
      if (rev1 == SVN_INVALID_REVNUM &&
          diff_cmd_baton->revnum1 != SVN_INVALID_REVNUM)
        rev1 = diff_cmd_baton->revnum1;

      if (rev2 == SVN_INVALID_REVNUM &&
          diff_cmd_baton->revnum2 != SVN_INVALID_REVNUM)
        rev2 = diff_cmd_baton->revnum2;
    }

  if (diff_cmd_baton->anchor)
    path = svn_dirent_join(diff_cmd_baton->anchor, path, scratch_pool);

  /* We want diff_file_changed to unconditionally show diffs, even if
     the diff is empty (as would be the case if an empty file were
     added.)  It's important, because 'patch' would still see an empty
     diff and create an empty file.  It's also important to let the
     user see that *something* happened. */
  diff_cmd_baton->force_empty = TRUE;

  if (tmpfile1 && copyfrom_path)
    SVN_ERR(diff_content_changed(path,
                                 tmpfile1, tmpfile2, rev1, rev2,
                                 mimetype1, mimetype2,
                                 svn_diff_op_copied, copyfrom_path,
                                 copyfrom_revision, diff_cmd_baton));
  else if (tmpfile1)
    SVN_ERR(diff_content_changed(path,
                                 tmpfile1, tmpfile2, rev1, rev2,
                                 mimetype1, mimetype2,
                                 svn_diff_op_added, NULL, SVN_INVALID_REVNUM,
                                 diff_cmd_baton));
  if (prop_changes->nelts > 0)
    SVN_ERR(diff_props_changed(prop_state, tree_conflicted,
                               path, FALSE, prop_changes,
                               original_props, diff_cmd_baton, scratch_pool));
  if (content_state)
    *content_state = svn_wc_notify_state_unknown;
  if (prop_state)
    *prop_state = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;

  diff_cmd_baton->force_empty = FALSE;

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks4_t function. */
static svn_error_t *
diff_file_deleted(svn_wc_notify_state_t *state,
                  svn_boolean_t *tree_conflicted,
                  const char *path,
                  const char *tmpfile1,
                  const char *tmpfile2,
                  const char *mimetype1,
                  const char *mimetype2,
                  apr_hash_t *original_props,
                  void *diff_baton,
                  apr_pool_t *scratch_pool)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;

  if (diff_cmd_baton->anchor)
    path = svn_dirent_join(diff_cmd_baton->anchor, path, scratch_pool);

  if (diff_cmd_baton->no_diff_deleted)
    {
      SVN_ERR(svn_stream_printf_from_utf8(diff_cmd_baton->outstream,
                diff_cmd_baton->header_encoding, scratch_pool,
                "Index: %s (deleted)" APR_EOL_STR "%s" APR_EOL_STR,
                path, equal_string));
    }
  else
    {
      if (tmpfile1)
        SVN_ERR(diff_content_changed(path,
                                     tmpfile1, tmpfile2,
                                     diff_cmd_baton->revnum1,
                                     diff_cmd_baton->revnum2,
                                     mimetype1, mimetype2,
                                     svn_diff_op_deleted, NULL,
                                     SVN_INVALID_REVNUM, diff_cmd_baton));
    }

  /* We don't list all the deleted properties. */

  if (state)
    *state = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks4_t function. */
static svn_error_t *
diff_dir_added(svn_wc_notify_state_t *state,
               svn_boolean_t *tree_conflicted,
               svn_boolean_t *skip,
               svn_boolean_t *skip_children,
               const char *path,
               svn_revnum_t rev,
               const char *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               void *diff_baton,
               apr_pool_t *scratch_pool)
{
  /*struct diff_cmd_baton *diff_cmd_baton = diff_baton;
  if (diff_cmd_baton->anchor)
    path = svn_dirent_join(diff_cmd_baton->anchor, path, scratch_pool);*/

  /* Do nothing. */

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks4_t function. */
static svn_error_t *
diff_dir_deleted(svn_wc_notify_state_t *state,
                 svn_boolean_t *tree_conflicted,
                 const char *path,
                 void *diff_baton,
                 apr_pool_t *scratch_pool)
{
  /*struct diff_cmd_baton *diff_cmd_baton = diff_baton;
  if (diff_cmd_baton->anchor)
    path = svn_dirent_join(diff_cmd_baton->anchor, path, scratch_pool);*/

  /* Do nothing. */

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks4_t function. */
static svn_error_t *
diff_dir_opened(svn_boolean_t *tree_conflicted,
                svn_boolean_t *skip,
                svn_boolean_t *skip_children,
                const char *path,
                svn_revnum_t rev,
                void *diff_baton,
                apr_pool_t *scratch_pool)
{
  /*struct diff_cmd_baton *diff_cmd_baton = diff_baton;
  if (diff_cmd_baton->anchor)
    path = svn_dirent_join(diff_cmd_baton->anchor, path, scratch_pool);*/

  /* Do nothing. */

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks4_t function. */
static svn_error_t *
diff_dir_closed(svn_wc_notify_state_t *contentstate,
                svn_wc_notify_state_t *propstate,
                svn_boolean_t *tree_conflicted,
                const char *path,
                svn_boolean_t dir_was_added,
                void *diff_baton,
                apr_pool_t *scratch_pool)
{
  /*struct diff_cmd_baton *diff_cmd_baton = diff_baton;
  if (diff_cmd_baton->anchor)
    path = svn_dirent_join(diff_cmd_baton->anchor, path, scratch_pool);*/

  /* Do nothing. */

  return SVN_NO_ERROR;
}

static const svn_wc_diff_callbacks4_t diff_callbacks =
{
  diff_file_opened,
  diff_file_changed,
  diff_file_added,
  diff_file_deleted,
  diff_dir_deleted,
  diff_dir_opened,
  diff_dir_added,
  diff_dir_props_changed,
  diff_dir_closed
};

/*-----------------------------------------------------------------*/

/** The logic behind 'svn diff' and 'svn merge'.  */


/* Hi!  This is a comment left behind by Karl, and Ben is too afraid
   to erase it at this time, because he's not fully confident that all
   this knowledge has been grokked yet.

   There are five cases:
      1. path is not a URL and start_revision != end_revision
      2. path is not a URL and start_revision == end_revision
      3. path is a URL and start_revision != end_revision
      4. path is a URL and start_revision == end_revision
      5. path is not a URL and no revisions given

   With only one distinct revision the working copy provides the
   other.  When path is a URL there is no working copy. Thus

     1: compare repository versions for URL coresponding to working copy
     2: compare working copy against repository version
     3: compare repository versions for URL
     4: nothing to do.
     5: compare working copy against text-base

   Case 4 is not as stupid as it looks, for example it may occur if
   the user specifies two dates that resolve to the same revision.  */




/* Helper function: given a working-copy ABSPATH_OR_URL, return its
   associated url in *URL, allocated in RESULT_POOL.  If ABSPATH_OR_URL is
   *already* a URL, that's fine, return ABSPATH_OR_URL allocated in
   RESULT_POOL.

   Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
convert_to_url(const char **url,
               svn_wc_context_t *wc_ctx,
               const char *abspath_or_url,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  if (svn_path_is_url(abspath_or_url))
    {
      *url = apr_pstrdup(result_pool, abspath_or_url);
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_wc__node_get_url(url, wc_ctx, abspath_or_url,
                               result_pool, scratch_pool));
  if (! *url)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                             _("Path '%s' has no URL"),
                             svn_dirent_local_style(abspath_or_url,
                                                    scratch_pool));
  return SVN_NO_ERROR;
}

/** Check if paths PATH_OR_URL1 and PATH_OR_URL2 are urls and if the
 * revisions REVISION1 and REVISION2 are local. If PEG_REVISION is not
 * unspecified, ensure that at least one of the two revisions is not
 * BASE or WORKING.
 * If PATH_OR_URL1 can only be found in the repository, set *IS_REPOS1
 * to TRUE. If PATH_OR_URL2 can only be found in the repository, set
 * *IS_REPOS2 to TRUE. */
static svn_error_t *
check_paths(svn_boolean_t *is_repos1,
            svn_boolean_t *is_repos2,
            const char *path_or_url1,
            const char *path_or_url2,
            const svn_opt_revision_t *revision1,
            const svn_opt_revision_t *revision2,
            const svn_opt_revision_t *peg_revision)
{
  svn_boolean_t is_local_rev1, is_local_rev2;

  /* Verify our revision arguments in light of the paths. */
  if ((revision1->kind == svn_opt_revision_unspecified)
      || (revision2->kind == svn_opt_revision_unspecified))
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                            _("Not all required revisions are specified"));

  /* Revisions can be said to be local or remote.
   * BASE and WORKING are local revisions.  */
  is_local_rev1 =
    ((revision1->kind == svn_opt_revision_base)
     || (revision1->kind == svn_opt_revision_working));
  is_local_rev2 =
    ((revision2->kind == svn_opt_revision_base)
     || (revision2->kind == svn_opt_revision_working));

  if (peg_revision->kind != svn_opt_revision_unspecified &&
      is_local_rev1 && is_local_rev2)
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                            _("At least one revision must be something other "
                              "than BASE or WORKING when diffing a URL"));

  /* Working copy paths with non-local revisions get turned into
     URLs.  We don't do that here, though.  We simply record that it
     needs to be done, which is information that helps us choose our
     diff helper function.  */
  *is_repos1 = ! is_local_rev1 || svn_path_is_url(path_or_url1);
  *is_repos2 = ! is_local_rev2 || svn_path_is_url(path_or_url2);

  return SVN_NO_ERROR;
}

/* Raise an error if the diff target URL does not exist at REVISION.
 * If REVISION does not equal OTHER_REVISION, mention both revisions in
 * the error message. Use RA_SESSION to contact the repository.
 * Use POOL for temporary allocations. */
static svn_error_t *
check_diff_target_exists(const char *url,
                         svn_revnum_t revision,
                         svn_revnum_t other_revision,
                         svn_ra_session_t *ra_session,
                         apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const char *session_url;

  SVN_ERR(svn_ra_get_session_url(ra_session, &session_url, pool));

  if (strcmp(url, session_url) != 0)
    SVN_ERR(svn_ra_reparent(ra_session, url, pool));

  SVN_ERR(svn_ra_check_path(ra_session, "", revision, &kind, pool));
  if (kind == svn_node_none)
    {
      if (revision == other_revision)
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("Diff target '%s' was not found in the "
                                   "repository at revision '%ld'"),
                                 url, revision);
      else
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("Diff target '%s' was not found in the "
                                   "repository at revision '%ld' or '%ld'"),
                                 url, revision, other_revision);
     }

  if (strcmp(url, session_url) != 0)
    SVN_ERR(svn_ra_reparent(ra_session, session_url, pool));

  return SVN_NO_ERROR;
}


/* Return in *RESOLVED_URL the URL which PATH_OR_URL@PEG_REVISION has in
 * REVISION. If the object has no location in REVISION, set *RESOLVED_URL
 * to NULL. */
static svn_error_t *
resolve_pegged_diff_target_url(const char **resolved_url,
                               svn_ra_session_t *ra_session,
                               const char *path_or_url,
                               const svn_opt_revision_t *peg_revision,
                               const svn_opt_revision_t *revision,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *scratch_pool)
{
  svn_error_t *err;

  /* Check if the PATH_OR_URL exists at REVISION. */
  err = svn_client__repos_locations(resolved_url, NULL,
                                    NULL, NULL,
                                    ra_session,
                                    path_or_url,
                                    peg_revision,
                                    revision,
                                    NULL,
                                    ctx, scratch_pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_CLIENT_UNRELATED_RESOURCES ||
          err->apr_err == SVN_ERR_FS_NOT_FOUND)
        {
          svn_error_clear(err);
          *resolved_url = NULL;
        }
      else
        return svn_error_trace(err);
    }

  return SVN_NO_ERROR;
}

/** Prepare a repos repos diff between PATH_OR_URL1 and
 * PATH_OR_URL2@PEG_REVISION, in the revision range REVISION1:REVISION2.
 * Return URLs and peg revisions in *URL1, *REV1 and in *URL2, *REV2.
 * Return suitable anchors in *ANCHOR1 and *ANCHOR2, and targets in
 * *TARGET1 and *TARGET2, based on *URL1 and *URL2.
 * Indicate the corresponding node kinds in *KIND1 and *KIND2, and verify
 * that at least one of the diff targets exists.
 * Set *BASE_PATH corresponding to the URL opened in the new *RA_SESSION
 * which is pointing at *ANCHOR1.
 * Use client context CTX. Do all allocations in POOL. */
static svn_error_t *
diff_prepare_repos_repos(const char **url1,
                         const char **url2,
                         const char **base_path,
                         svn_revnum_t *rev1,
                         svn_revnum_t *rev2,
                         const char **anchor1,
                         const char **anchor2,
                         const char **target1,
                         const char **target2,
                         svn_node_kind_t *kind1,
                         svn_node_kind_t *kind2,
                         svn_ra_session_t **ra_session,
                         svn_client_ctx_t *ctx,
                         const char *path_or_url1,
                         const char *path_or_url2,
                         const svn_opt_revision_t *revision1,
                         const svn_opt_revision_t *revision2,
                         const svn_opt_revision_t *peg_revision,
                         apr_pool_t *pool)
{
  const char *abspath_or_url2;
  const char *abspath_or_url1;

  if (!svn_path_is_url(path_or_url2))
    SVN_ERR(svn_dirent_get_absolute(&abspath_or_url2, path_or_url2,
                                    pool));
  else
    abspath_or_url2 = path_or_url2;

  if (!svn_path_is_url(path_or_url1))
    SVN_ERR(svn_dirent_get_absolute(&abspath_or_url1, path_or_url1,
                                    pool));
  else
    abspath_or_url1 = path_or_url1;

  /* Figure out URL1 and URL2. */
  SVN_ERR(convert_to_url(url1, ctx->wc_ctx, abspath_or_url1,
                         pool, pool));
  SVN_ERR(convert_to_url(url2, ctx->wc_ctx, abspath_or_url2,
                         pool, pool));

  /* We need exactly one BASE_PATH, so we'll let the BASE_PATH
     calculated for PATH_OR_URL2 override the one for PATH_OR_URL1
     (since the diff will be "applied" to URL2 anyway). */
  *base_path = NULL;
  if (strcmp(*url1, path_or_url1) != 0)
    *base_path = path_or_url1;
  if (strcmp(*url2, path_or_url2) != 0)
    *base_path = path_or_url2;

  SVN_ERR(svn_client__open_ra_session_internal(ra_session, NULL, *url2,
                                               NULL, NULL, FALSE,
                                               TRUE, ctx, pool));

  /* If we are performing a pegged diff, we need to find out what our
     actual URLs will be. */
  if (peg_revision->kind != svn_opt_revision_unspecified)
    {
      const char *resolved_url1;
      const char *resolved_url2;

      SVN_ERR(resolve_pegged_diff_target_url(&resolved_url2, *ra_session,
                                             path_or_url2, peg_revision,
                                             revision2, ctx, pool));

      SVN_ERR(svn_ra_reparent(*ra_session, *url1, pool));
      SVN_ERR(resolve_pegged_diff_target_url(&resolved_url1, *ra_session,
                                             path_or_url1, peg_revision,
                                             revision1, ctx, pool));

      /* Either or both URLs might have changed as a result of resolving
       * the PATH_OR_URL@PEG_REVISION's history. If only one of the URLs
       * could be resolved, use the same URL for URL1 and URL2, so we can
       * show a diff that adds or removes the object (see issue #4153). */
      if (resolved_url2)
        {
          *url2 = resolved_url2;
          if (!resolved_url1)
            *url1 = resolved_url2;
        }
      if (resolved_url1)
        {
          *url1 = resolved_url1;
          if (!resolved_url2)
            *url2 = resolved_url1;
        }

      /* Reparent the session, since *URL2 might have changed as a result
         the above call. */
      SVN_ERR(svn_ra_reparent(*ra_session, *url2, pool));
    }

  /* Resolve revision and get path kind for the second target. */
  SVN_ERR(svn_client__get_revision_number(rev2, NULL, ctx->wc_ctx,
           (path_or_url2 == *url2) ? NULL : abspath_or_url2,
           *ra_session, revision2, pool));
  SVN_ERR(svn_ra_check_path(*ra_session, "", *rev2, kind2, pool));

  /* Do the same for the first target. */
  SVN_ERR(svn_ra_reparent(*ra_session, *url1, pool));
  SVN_ERR(svn_client__get_revision_number(rev1, NULL, ctx->wc_ctx,
           (strcmp(path_or_url1, *url1) == 0) ? NULL : abspath_or_url1,
           *ra_session, revision1, pool));
  SVN_ERR(svn_ra_check_path(*ra_session, "", *rev1, kind1, pool));

  /* Either both URLs must exist at their respective revisions,
   * or one of them may be missing from one side of the diff. */
  if (*kind1 == svn_node_none && *kind2 == svn_node_none)
    {
      if (strcmp(*url1, *url2) == 0)
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("Diff target '%s' was not found in the "
                                   "repository at revisions '%ld' and '%ld'"),
                                 *url1, *rev1, *rev2);
      else
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("Diff targets '%s' and '%s' were not found "
                                   "in the repository at revisions '%ld' and "
                                   "'%ld'"),
                                 *url1, *url2, *rev1, *rev2);
    }
  else if (*kind1 == svn_node_none)
    SVN_ERR(check_diff_target_exists(*url1, *rev2, *rev1, *ra_session, pool));
  else if (*kind2 == svn_node_none)
    SVN_ERR(check_diff_target_exists(*url2, *rev1, *rev2, *ra_session, pool));

  /* Choose useful anchors and targets for our two URLs. */
  *anchor1 = *url1;
  *anchor2 = *url2;
  *target1 = "";
  *target2 = "";

  /* If one of the targets is a file, use the parent directory as anchor. */
  if (*kind1 == svn_node_file || *kind2 == svn_node_file)
    {
      svn_uri_split(anchor1, target1, *url1, pool);
      svn_uri_split(anchor2, target2, *url2, pool);
      if (*base_path)
        *base_path = svn_dirent_dirname(*base_path, pool);
      SVN_ERR(svn_ra_reparent(*ra_session, *anchor1, pool));
    }

  return SVN_NO_ERROR;
}

/* A Theoretical Note From Ben, regarding do_diff().

   This function is really svn_client_diff6().  If you read the public
   API description for svn_client_diff6(), it sounds quite Grand.  It
   sounds really generalized and abstract and beautiful: that it will
   diff any two paths, be they working-copy paths or URLs, at any two
   revisions.

   Now, the *reality* is that we have exactly three 'tools' for doing
   diffing, and thus this routine is built around the use of the three
   tools.  Here they are, for clarity:

     - svn_wc_diff:  assumes both paths are the same wcpath.
                     compares wcpath@BASE vs. wcpath@WORKING

     - svn_wc_get_diff_editor:  compares some URL@REV vs. wcpath@WORKING

     - svn_client__get_diff_editor:  compares some URL1@REV1 vs. URL2@REV2

   So the truth of the matter is, if the caller's arguments can't be
   pigeonholed into one of these three use-cases, we currently bail
   with a friendly apology.

   Perhaps someday a brave soul will truly make svn_client_diff6()
   perfectly general.  For now, we live with the 90% case.  Certainly,
   the commandline client only calls this function in legal ways.
   When there are other users of svn_client.h, maybe this will become
   a more pressing issue.
 */

/* Return a "you can't do that" error, optionally wrapping another
   error CHILD_ERR. */
static svn_error_t *
unsupported_diff_error(svn_error_t *child_err)
{
  return svn_error_create(SVN_ERR_INCORRECT_PARAMS, child_err,
                          _("Sorry, svn_client_diff6 was called in a way "
                            "that is not yet supported"));
}

/* Try to get properties for LOCAL_ABSPATH and return them in the property
 * hash *PROPS. If there are no properties because LOCAL_ABSPATH is not
 * versioned, return an empty property hash. */
static svn_error_t *
get_props(apr_hash_t **props,
          const char *local_abspath,
          svn_wc_context_t *wc_ctx,
          apr_pool_t *result_pool,
          apr_pool_t *scratch_pool)
{
  svn_error_t *err;

  err = svn_wc_prop_list2(props, wc_ctx, local_abspath, result_pool,
                          scratch_pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND ||
          err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY ||
          err->apr_err == SVN_ERR_WC_UPGRADE_REQUIRED)
        {
          svn_error_clear(err);
          *props = apr_hash_make(result_pool);
        }
      else
        return svn_error_trace(err);
    }

  return SVN_NO_ERROR;
}

/* Produce a diff between two arbitrary files at LOCAL_ABSPATH1 and
 * LOCAL_ABSPATH2, using the diff callbacks from CALLBACKS.
 * Use PATH as the name passed to diff callbacks.
 * FILE1_IS_EMPTY and FILE2_IS_EMPTY are used as hints which diff callback
 * function to use to compare the files (added/deleted/changed).
 *
 * If ORIGINAL_PROPS_OVERRIDE is not NULL, use it as original properties
 * instead of reading properties from LOCAL_ABSPATH1. This is required when
 * a file replaces a directory, where LOCAL_ABSPATH1 is an empty file that
 * file content must be diffed against, but properties to diff against come
 * from the replaced directory. */
static svn_error_t *
do_arbitrary_files_diff(const char *local_abspath1,
                        const char *local_abspath2,
                        const char *path,
                        svn_boolean_t file1_is_empty,
                        svn_boolean_t file2_is_empty,
                        apr_hash_t *original_props_override,
                        const svn_wc_diff_callbacks4_t *callbacks,
                        struct diff_cmd_baton *diff_cmd_baton,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *scratch_pool)
{
  apr_hash_t *original_props;
  apr_hash_t *modified_props;
  apr_array_header_t *prop_changes;
  svn_string_t *original_mime_type = NULL;
  svn_string_t *modified_mime_type = NULL;

  if (ctx->cancel_func)
    SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

  if (diff_cmd_baton->ignore_properties)
    {
      original_props = apr_hash_make(scratch_pool);
      modified_props = apr_hash_make(scratch_pool);
    }
  else
    {
      /* Try to get properties from either file. It's OK if the files do not
       * have properties, or if they are unversioned. */
      if (original_props_override)
        original_props = original_props_override;
      else
        SVN_ERR(get_props(&original_props, local_abspath1, ctx->wc_ctx,
                          scratch_pool, scratch_pool));
      SVN_ERR(get_props(&modified_props, local_abspath2, ctx->wc_ctx,
                        scratch_pool, scratch_pool));
    }

  SVN_ERR(svn_prop_diffs(&prop_changes, modified_props, original_props,
                         scratch_pool));

  if (!diff_cmd_baton->force_binary)
    {
      /* Try to determine the mime-type of each file. */
      original_mime_type = apr_hash_get(original_props, SVN_PROP_MIME_TYPE,
                                        APR_HASH_KEY_STRING);
      if (!file1_is_empty && !original_mime_type)
        {
          const char *mime_type;
          SVN_ERR(svn_io_detect_mimetype2(&mime_type, local_abspath1,
                                          ctx->mimetypes_map, scratch_pool));

          if (mime_type)
            original_mime_type = svn_string_create(mime_type, scratch_pool);
        }

      modified_mime_type = apr_hash_get(modified_props, SVN_PROP_MIME_TYPE,
                                        APR_HASH_KEY_STRING);
      if (!file2_is_empty && !modified_mime_type)
        {
          const char *mime_type;
          SVN_ERR(svn_io_detect_mimetype2(&mime_type, local_abspath1,
                                          ctx->mimetypes_map, scratch_pool));

          if (mime_type)
            modified_mime_type = svn_string_create(mime_type, scratch_pool);
        }
    }

  /* Produce the diff. */
  if (file1_is_empty && !file2_is_empty)
    SVN_ERR(callbacks->file_added(NULL, NULL, NULL, path,
                                  local_abspath1, local_abspath2,
                                  /* ### TODO get real revision info
                                   * for versioned files? */
                                  SVN_INVALID_REVNUM, SVN_INVALID_REVNUM,
                                  original_mime_type ?
                                    original_mime_type->data : NULL,
                                  modified_mime_type ?
                                    modified_mime_type->data : NULL,
                                  /* ### TODO get copyfrom? */
                                  NULL, SVN_INVALID_REVNUM,
                                  prop_changes, original_props,
                                  diff_cmd_baton, scratch_pool));
  else if (!file1_is_empty && file2_is_empty)
    SVN_ERR(callbacks->file_deleted(NULL, NULL, path,
                                    local_abspath1, local_abspath2,
                                    original_mime_type ?
                                      original_mime_type->data : NULL,
                                    modified_mime_type ?
                                      modified_mime_type->data : NULL,
                                    original_props,
                                    diff_cmd_baton, scratch_pool));
  else
    SVN_ERR(callbacks->file_changed(NULL, NULL, NULL, path,
                                    local_abspath1, local_abspath2,
                                    /* ### TODO get real revision info
                                     * for versioned files? */
                                    SVN_INVALID_REVNUM, SVN_INVALID_REVNUM,
                                    original_mime_type ?
                                      original_mime_type->data : NULL,
                                    modified_mime_type ?
                                      modified_mime_type->data : NULL,
                                    prop_changes, original_props,
                                    diff_cmd_baton, scratch_pool));

  return SVN_NO_ERROR;
}

struct arbitrary_diff_walker_baton {
  /* The root directories of the trees being compared. */
  const char *root1_abspath;
  const char *root2_abspath;

  /* TRUE if recursing within an added subtree of root2_abspath that
   * does not exist in root1_abspath. */
  svn_boolean_t recursing_within_added_subtree;

  /* TRUE if recursing within an administrative (.i.e. .svn) directory. */
  svn_boolean_t recursing_within_adm_dir;

  /* The absolute path of the adm dir if RECURSING_WITHIN_ADM_DIR is TRUE.
   * Else this is NULL.*/
  const char *adm_dir_abspath;

  /* A path to an empty file used for diffs that add/delete files. */
  const char *empty_file_abspath;

  const svn_wc_diff_callbacks4_t *callbacks;
  struct diff_cmd_baton *callback_baton;
  svn_client_ctx_t *ctx;
  apr_pool_t *pool;
} arbitrary_diff_walker_baton;

/* Forward declaration needed because this function has a cyclic
 * dependency with do_arbitrary_dirs_diff(). */
static svn_error_t *
arbitrary_diff_walker(void *baton, const char *local_abspath,
                      const apr_finfo_t *finfo,
                      apr_pool_t *scratch_pool);

/* Produce a diff between two arbitrary directories at LOCAL_ABSPATH1 and
 * LOCAL_ABSPATH2, using the provided diff callbacks to show file changes
 * and, for versioned nodes, property changes.
 *
 * If ROOT_ABSPATH1 and ROOT_ABSPATH2 are not NULL, show paths in diffs
 * relative to these roots, rather than relative to LOCAL_ABSPATH1 and
 * LOCAL_ABSPATH2. This is needed when crawling a subtree that exists
 * only within LOCAL_ABSPATH2. */
static svn_error_t *
do_arbitrary_dirs_diff(const char *local_abspath1,
                       const char *local_abspath2,
                       const char *root_abspath1,
                       const char *root_abspath2,
                       const svn_wc_diff_callbacks4_t *callbacks,
                       struct diff_cmd_baton *callback_baton,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *scratch_pool)
{
  apr_file_t *empty_file;
  svn_node_kind_t kind1;

  struct arbitrary_diff_walker_baton b;

  /* If LOCAL_ABSPATH1 is not a directory, crawl LOCAL_ABSPATH2 instead
   * and compare it to LOCAL_ABSPATH1, showing only additions.
   * This case can only happen during recursion from arbitrary_diff_walker(),
   * because do_arbitrary_nodes_diff() prevents this from happening at
   * the root of the comparison. */
  SVN_ERR(svn_io_check_resolved_path(local_abspath1, &kind1, scratch_pool));
  b.recursing_within_added_subtree = (kind1 != svn_node_dir);

  b.root1_abspath = root_abspath1 ? root_abspath1 : local_abspath1;
  b.root2_abspath = root_abspath2 ? root_abspath2 : local_abspath2;
  b.recursing_within_adm_dir = FALSE;
  b.adm_dir_abspath = NULL;
  b.callbacks = callbacks;
  b.callback_baton = callback_baton;
  b.ctx = ctx;
  b.pool = scratch_pool;

  SVN_ERR(svn_io_open_unique_file3(&empty_file, &b.empty_file_abspath,
                                   NULL, svn_io_file_del_on_pool_cleanup,
                                   scratch_pool, scratch_pool));

  SVN_ERR(svn_io_dir_walk2(b.recursing_within_added_subtree ? local_abspath2
                                                            : local_abspath1,
                           0, arbitrary_diff_walker, &b, scratch_pool));

  return SVN_NO_ERROR;
}

/* An implementation of svn_io_walk_func_t.
 * Note: LOCAL_ABSPATH is the path being crawled and can be on either side
 * of the diff depending on baton->recursing_within_added_subtree. */
static svn_error_t *
arbitrary_diff_walker(void *baton, const char *local_abspath,
                      const apr_finfo_t *finfo,
                      apr_pool_t *scratch_pool)
{
  struct arbitrary_diff_walker_baton *b = baton;
  const char *local_abspath1;
  const char *local_abspath2;
  svn_node_kind_t kind1;
  svn_node_kind_t kind2;
  const char *child_relpath;
  apr_hash_t *dirents1;
  apr_hash_t *dirents2;
  apr_hash_t *merged_dirents;
  apr_array_header_t *sorted_dirents;
  int i;
  apr_pool_t *iterpool;

  if (b->ctx->cancel_func)
    SVN_ERR(b->ctx->cancel_func(b->ctx->cancel_baton));

  if (finfo->filetype != APR_DIR)
    return SVN_NO_ERROR;

  if (b->recursing_within_adm_dir)
    {
      if (svn_dirent_skip_ancestor(b->adm_dir_abspath, local_abspath))
        return SVN_NO_ERROR;
      else
        {
          b->recursing_within_adm_dir = FALSE;
          b->adm_dir_abspath = NULL;
        }
    }
  else if (strcmp(svn_dirent_basename(local_abspath, scratch_pool),
                  SVN_WC_ADM_DIR_NAME) == 0)
    {
      b->recursing_within_adm_dir = TRUE;
      b->adm_dir_abspath = apr_pstrdup(b->pool, local_abspath);
      return SVN_NO_ERROR;
    }

  if (b->recursing_within_added_subtree)
    child_relpath = svn_dirent_skip_ancestor(b->root2_abspath, local_abspath);
  else
    child_relpath = svn_dirent_skip_ancestor(b->root1_abspath, local_abspath);
  if (!child_relpath)
    return SVN_NO_ERROR;

  local_abspath1 = svn_dirent_join(b->root1_abspath, child_relpath,
                                   scratch_pool);
  SVN_ERR(svn_io_check_resolved_path(local_abspath1, &kind1, scratch_pool));

  local_abspath2 = svn_dirent_join(b->root2_abspath, child_relpath,
                                   scratch_pool);
  SVN_ERR(svn_io_check_resolved_path(local_abspath2, &kind2, scratch_pool));

  if (kind1 == svn_node_dir)
    SVN_ERR(svn_io_get_dirents3(&dirents1, local_abspath1,
                                TRUE, /* only_check_type */
                                scratch_pool, scratch_pool));
  else
    dirents1 = apr_hash_make(scratch_pool);

  if (kind2 == svn_node_dir)
    {
      apr_hash_t *original_props;
      apr_hash_t *modified_props;
      apr_array_header_t *prop_changes;

      /* Show any property changes for this directory. */
      SVN_ERR(get_props(&original_props, local_abspath1, b->ctx->wc_ctx,
                        scratch_pool, scratch_pool));
      SVN_ERR(get_props(&modified_props, local_abspath2, b->ctx->wc_ctx,
                        scratch_pool, scratch_pool));
      SVN_ERR(svn_prop_diffs(&prop_changes, modified_props, original_props,
                             scratch_pool));
      if (prop_changes->nelts > 0)
        SVN_ERR(diff_props_changed(NULL, NULL, child_relpath,
                                   b->recursing_within_added_subtree,
                                   prop_changes, original_props,
                                   b->callback_baton, scratch_pool));

      /* Read directory entries. */
      SVN_ERR(svn_io_get_dirents3(&dirents2, local_abspath2,
                                  TRUE, /* only_check_type */
                                  scratch_pool, scratch_pool));
    }
  else
    dirents2 = apr_hash_make(scratch_pool);

  /* Compare dirents1 to dirents2 and show added/deleted/changed files. */
  merged_dirents = apr_hash_merge(scratch_pool, dirents1, dirents2,
                                  NULL, NULL);
  sorted_dirents = svn_sort__hash(merged_dirents,
                                  svn_sort_compare_items_as_paths,
                                  scratch_pool);
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < sorted_dirents->nelts; i++)
    {
      svn_sort__item_t elt = APR_ARRAY_IDX(sorted_dirents, i, svn_sort__item_t);
      const char *name = elt.key;
      svn_io_dirent2_t *dirent1;
      svn_io_dirent2_t *dirent2;
      const char *child1_abspath;
      const char *child2_abspath;

      svn_pool_clear(iterpool);

      if (b->ctx->cancel_func)
        SVN_ERR(b->ctx->cancel_func(b->ctx->cancel_baton));

      if (strcmp(name, SVN_WC_ADM_DIR_NAME) == 0)
        continue;

      dirent1 = apr_hash_get(dirents1, name, APR_HASH_KEY_STRING);
      if (!dirent1)
        {
          dirent1 = svn_io_dirent2_create(iterpool);
          dirent1->kind = svn_node_none;
        }
      dirent2 = apr_hash_get(dirents2, name, APR_HASH_KEY_STRING);
      if (!dirent2)
        {
          dirent2 = svn_io_dirent2_create(iterpool);
          dirent2->kind = svn_node_none;
        }

      child1_abspath = svn_dirent_join(local_abspath1, name, iterpool);
      child2_abspath = svn_dirent_join(local_abspath2, name, iterpool);

      if (dirent1->special)
        SVN_ERR(svn_io_check_resolved_path(child1_abspath, &dirent1->kind,
                                           iterpool));
      if (dirent2->special)
        SVN_ERR(svn_io_check_resolved_path(child1_abspath, &dirent2->kind,
                                           iterpool));

      if (dirent1->kind == svn_node_dir &&
          dirent2->kind == svn_node_dir)
        continue;

      /* Files that exist only in dirents1. */
      if (dirent1->kind == svn_node_file &&
          (dirent2->kind == svn_node_dir || dirent2->kind == svn_node_none))
        SVN_ERR(do_arbitrary_files_diff(child1_abspath, b->empty_file_abspath,
                                        svn_relpath_join(child_relpath, name,
                                                         iterpool),
                                        FALSE, TRUE, NULL,
                                        b->callbacks, b->callback_baton,
                                        b->ctx, iterpool));

      /* Files that exist only in dirents2. */
      if (dirent2->kind == svn_node_file &&
          (dirent1->kind == svn_node_dir || dirent1->kind == svn_node_none))
        {
          apr_hash_t *original_props;

          SVN_ERR(get_props(&original_props, child1_abspath, b->ctx->wc_ctx,
                            scratch_pool, scratch_pool));
          SVN_ERR(do_arbitrary_files_diff(b->empty_file_abspath, child2_abspath,
                                          svn_relpath_join(child_relpath, name,
                                                           iterpool),
                                          TRUE, FALSE, original_props,
                                          b->callbacks, b->callback_baton,
                                          b->ctx, iterpool));
        }

      /* Files that exist in dirents1 and dirents2. */
      if (dirent1->kind == svn_node_file && dirent2->kind == svn_node_file)
        SVN_ERR(do_arbitrary_files_diff(child1_abspath, child2_abspath,
                                        svn_relpath_join(child_relpath, name,
                                                         iterpool),
                                        FALSE, FALSE, NULL,
                                        b->callbacks, b->callback_baton,
                                        b->ctx, scratch_pool));

      /* Directories that only exist in dirents2. These aren't crawled
       * by this walker so we have to crawl them separately. */
      if (dirent2->kind == svn_node_dir &&
          (dirent1->kind == svn_node_file || dirent1->kind == svn_node_none))
        SVN_ERR(do_arbitrary_dirs_diff(child1_abspath, child2_abspath,
                                       b->root1_abspath, b->root2_abspath,
                                       b->callbacks, b->callback_baton,
                                       b->ctx, iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Produce a diff between two files or two directories at LOCAL_ABSPATH1
 * and LOCAL_ABSPATH2, using the provided diff callbacks to show changes
 * in files. The files and directories involved may be part of a working
 * copy or they may be unversioned. For versioned files, show property
 * changes, too. */
static svn_error_t *
do_arbitrary_nodes_diff(const char *local_abspath1,
                        const char *local_abspath2,
                        const svn_wc_diff_callbacks4_t *callbacks,
                        struct diff_cmd_baton *callback_baton,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind1;
  svn_node_kind_t kind2;

  SVN_ERR(svn_io_check_resolved_path(local_abspath1, &kind1, scratch_pool));
  SVN_ERR(svn_io_check_resolved_path(local_abspath2, &kind2, scratch_pool));
  if (kind1 != kind2)
    return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                             _("'%s' is not the same node kind as '%s'"),
                             local_abspath1, local_abspath2);

  if (kind1 == svn_node_file)
    SVN_ERR(do_arbitrary_files_diff(local_abspath1, local_abspath2,
                                    svn_dirent_basename(local_abspath2,
                                                        scratch_pool),
                                    FALSE, FALSE, NULL,
                                    callbacks, callback_baton,
                                    ctx, scratch_pool));
  else if (kind1 == svn_node_dir)
    SVN_ERR(do_arbitrary_dirs_diff(local_abspath1, local_abspath2,
                                   NULL, NULL,
                                   callbacks, callback_baton,
                                   ctx, scratch_pool));
  else
    return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                             _("'%s' is not a file or directory"),
                             kind1 == svn_node_none ?
                              local_abspath1 : local_abspath2);
  return SVN_NO_ERROR;
}


/* Perform a diff between two working-copy paths.

   PATH1 and PATH2 are both working copy paths.  REVISION1 and
   REVISION2 are their respective revisions.

   All other options are the same as those passed to svn_client_diff6(). */
static svn_error_t *
diff_wc_wc(const char *path1,
           const svn_opt_revision_t *revision1,
           const char *path2,
           const svn_opt_revision_t *revision2,
           svn_depth_t depth,
           svn_boolean_t ignore_ancestry,
           svn_boolean_t show_copies_as_adds,
           svn_boolean_t use_git_diff_format,
           const apr_array_header_t *changelists,
           const svn_wc_diff_callbacks4_t *callbacks,
           struct diff_cmd_baton *callback_baton,
           svn_client_ctx_t *ctx,
           apr_pool_t *pool)
{
  const char *abspath1;
  svn_error_t *err;
  svn_node_kind_t kind;

  SVN_ERR_ASSERT(! svn_path_is_url(path1));
  SVN_ERR_ASSERT(! svn_path_is_url(path2));

  SVN_ERR(svn_dirent_get_absolute(&abspath1, path1, pool));

  if ((strcmp(path1, path2) != 0)
      || (! ((revision1->kind == svn_opt_revision_base)
             && (revision2->kind == svn_opt_revision_working))))
    {
      const char *abspath2;

      SVN_ERR(svn_dirent_get_absolute(&abspath2, path2, pool));
      return svn_error_trace(do_arbitrary_nodes_diff(abspath1, abspath2,
                                                     callbacks,
                                                     callback_baton,
                                                     ctx, pool));
    }

  /* Resolve named revisions to real numbers. */
  err = svn_client__get_revision_number(&callback_baton->revnum1, NULL,
                                        ctx->wc_ctx, abspath1, NULL,
                                        revision1, pool);

  /* In case of an added node, we have no base rev, and we show a revision
   * number of 0. Note that this code is currently always asking for
   * svn_opt_revision_base.
   * ### TODO: get rid of this 0 for added nodes. */
  if (err && (err->apr_err == SVN_ERR_CLIENT_BAD_REVISION))
    {
      svn_error_clear(err);
      callback_baton->revnum1 = 0;
    }
  else
    SVN_ERR(err);

  callback_baton->revnum2 = SVN_INVALID_REVNUM;  /* WC */

  SVN_ERR(svn_wc_read_kind(&kind, ctx->wc_ctx, abspath1, FALSE, pool));

  if (kind != svn_node_dir)
    callback_baton->anchor = svn_dirent_dirname(path1, pool);
  else
    callback_baton->anchor = path1;

  SVN_ERR(svn_wc_diff6(ctx->wc_ctx,
                       abspath1,
                       callbacks, callback_baton,
                       depth,
                       ignore_ancestry, show_copies_as_adds,
                       use_git_diff_format, changelists,
                       ctx->cancel_func, ctx->cancel_baton,
                       pool));
  return SVN_NO_ERROR;
}

/* Create an array of regular properties in PROP_HASH, filtering entry-props
 * and wc-props. Allocate the returned array in RESULT_POOL.
 * Use SCRATCH_POOL for temporary allocations. */
static apr_array_header_t *
make_regular_props_array(apr_hash_t *prop_hash,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  apr_array_header_t *regular_props;
  apr_hash_index_t *hi;

  regular_props = apr_array_make(result_pool, 0, sizeof(svn_prop_t));
  for (hi = apr_hash_first(scratch_pool, prop_hash); hi;
       hi = apr_hash_next(hi))
    {
      const char *name = svn__apr_hash_index_key(hi);
      svn_string_t *value = svn__apr_hash_index_val(hi);
      svn_prop_kind_t prop_kind = svn_property_kind2(name);

      if (prop_kind == svn_prop_regular_kind)
        {
          svn_prop_t *prop = apr_palloc(scratch_pool, sizeof(svn_prop_t));

          prop->name = name;
          prop->value = value;
          APR_ARRAY_PUSH(regular_props, svn_prop_t) = *prop;
        }
    }

  return regular_props;
}

/* Create a hash of regular properties from PROP_HASH, filtering entry-props
 * and wc-props. Allocate the returned hash in RESULT_POOL.
 * Use SCRATCH_POOL for temporary allocations. */
static apr_hash_t *
make_regular_props_hash(apr_hash_t *prop_hash,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  apr_hash_t *regular_props;
  apr_hash_index_t *hi;

  regular_props = apr_hash_make(result_pool);
  for (hi = apr_hash_first(scratch_pool, prop_hash); hi;
       hi = apr_hash_next(hi))
    {
      const char *name = svn__apr_hash_index_key(hi);
      svn_string_t *value = svn__apr_hash_index_val(hi);
      svn_prop_kind_t prop_kind = svn_property_kind2(name);

      if (prop_kind == svn_prop_regular_kind)
        apr_hash_set(regular_props, name, APR_HASH_KEY_STRING, value);
    }

  return regular_props;
}

/* Handle an added or deleted diff target file for a repos<->repos diff.
 *
 * Using the provided diff CALLBACKS and the CALLBACK_BATON, show the file
 * TARGET@PEG_REVISION as added or deleted, depending on SHOW_DELETION.
 * TARGET is a path relative to RA_SESSION's URL.
 * REV1 and REV2 are the revisions being compared.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
diff_repos_repos_added_or_deleted_file(const char *target,
                                       svn_revnum_t peg_revision,
                                       svn_revnum_t rev1,
                                       svn_revnum_t rev2,
                                       svn_boolean_t show_deletion,
                                      const char *empty_file,
                                       const svn_wc_diff_callbacks4_t
                                         *callbacks,
                                       struct diff_cmd_baton *callback_baton,
                                       svn_ra_session_t *ra_session,
                                       apr_pool_t *scratch_pool)
{
  const char *file_abspath;
  svn_stream_t *content;
  apr_hash_t *prop_hash;

  SVN_ERR(svn_stream_open_unique(&content, &file_abspath, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 scratch_pool, scratch_pool));
  SVN_ERR(svn_ra_get_file(ra_session, target, peg_revision, content, NULL,
                          &prop_hash, scratch_pool));
  SVN_ERR(svn_stream_close(content));

  if (show_deletion)
    {
      SVN_ERR(callbacks->file_deleted(NULL, NULL,
                                      target, file_abspath, empty_file,
                                      apr_hash_get(prop_hash,
                                                   SVN_PROP_MIME_TYPE,
                                                   APR_HASH_KEY_STRING),
                                      NULL,
                                      make_regular_props_hash(
                                        prop_hash, scratch_pool, scratch_pool),
                                      callback_baton, scratch_pool));
    }
  else
    {
      SVN_ERR(callbacks->file_added(NULL, NULL, NULL,
                                    target, empty_file, file_abspath,
                                    rev1, rev2, NULL,
                                    apr_hash_get(prop_hash, SVN_PROP_MIME_TYPE,
                                                 APR_HASH_KEY_STRING),
                                    NULL, SVN_INVALID_REVNUM,
                                    make_regular_props_array(prop_hash,
                                                             scratch_pool,
                                                             scratch_pool),
                                    NULL, callback_baton, scratch_pool));
    }
    
  return SVN_NO_ERROR;
}

/* Handle an added or deleted diff target directory for a repos<->repos diff.
 *
 * Using the provided diff CALLBACKS and the CALLBACK_BATON, show the
 * directory TARGET@PEG_REVISION, and all of its children, as added or deleted,
 * depending on SHOW_DELETION. TARGET is a path relative to RA_SESSION's URL.
 * REV1 and REV2 are the revisions being compared.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
diff_repos_repos_added_or_deleted_dir(const char *target,
                                      svn_revnum_t revision,
                                      svn_revnum_t rev1,
                                      svn_revnum_t rev2,
                                      svn_boolean_t show_deletion,
                                      const char *empty_file,
                                      const svn_wc_diff_callbacks4_t
                                        *callbacks,
                                      struct diff_cmd_baton *callback_baton,
                                      svn_ra_session_t *ra_session,
                                      apr_pool_t *scratch_pool)
{
  apr_hash_t *dirents;
  apr_hash_t *props;
  apr_pool_t *iterpool;
  apr_hash_index_t *hi;

  SVN_ERR(svn_ra_get_dir2(ra_session, &dirents, NULL, &props,
                          target, revision, SVN_DIRENT_KIND,
                          scratch_pool));

  if (show_deletion)
    SVN_ERR(callbacks->dir_deleted(NULL, NULL, target, callback_baton,
                                   scratch_pool));
  else
    SVN_ERR(callbacks->dir_added(NULL, NULL, NULL, NULL,
                                 target, revision,
                                 NULL, SVN_INVALID_REVNUM,
                                 callback_baton, scratch_pool));
  if (props)
    {
      if (show_deletion)
        SVN_ERR(callbacks->dir_props_changed(NULL, NULL, target, FALSE,
                                             apr_array_make(scratch_pool, 0,
                                                            sizeof(svn_prop_t)),
                                             make_regular_props_hash(
                                               props, scratch_pool,
                                               scratch_pool),
                                             callback_baton, scratch_pool));
      else
        SVN_ERR(callbacks->dir_props_changed(NULL, NULL, target, TRUE,
                                             make_regular_props_array(
                                               props, scratch_pool,
                                               scratch_pool),
                                             NULL,
                                             callback_baton, scratch_pool));
    }

  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, dirents); hi; hi = apr_hash_next(hi))
    {
      const char *name = svn__apr_hash_index_key(hi);
      svn_dirent_t *dirent = svn__apr_hash_index_val(hi);
      const char *child_target;

      svn_pool_clear(iterpool);

      child_target = svn_relpath_join(target, name, iterpool);

      if (dirent->kind == svn_node_dir)
        SVN_ERR(diff_repos_repos_added_or_deleted_dir(child_target,
                                                      revision, rev1, rev2,
                                                      show_deletion,
                                                      empty_file,
                                                      callbacks,
                                                      callback_baton,
                                                      ra_session,
                                                      iterpool));
      else if (dirent->kind == svn_node_file)
        SVN_ERR(diff_repos_repos_added_or_deleted_file(child_target,
                                                       revision, rev1, rev2,
                                                       show_deletion,
                                                       empty_file,
                                                       callbacks,
                                                       callback_baton,
                                                       ra_session,
                                                       iterpool));
    }
  svn_pool_destroy(iterpool);

  if (!show_deletion)
    SVN_ERR(callbacks->dir_closed(NULL, NULL, NULL, target, TRUE,
                                  callback_baton, scratch_pool));

  return SVN_NO_ERROR;
}


/* Handle an added or deleted diff target for a repos<->repos diff.
 *
 * Using the provided diff CALLBACKS and the CALLBACK_BATON, show
 * TARGET@PEG_REVISION, and all of its children, if any, as added or deleted.
 * TARGET is a path relative to RA_SESSION's URL.
 * REV1 and REV2 are the revisions being compared.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
diff_repos_repos_added_or_deleted_target(const char *target1,
                                         const char *target2,
                                         svn_revnum_t rev1,
                                         svn_revnum_t rev2,
                                         svn_node_kind_t kind1,
                                         svn_node_kind_t kind2,
                                         const svn_wc_diff_callbacks4_t
                                           *callbacks,
                                         struct diff_cmd_baton *callback_baton,
                                         svn_ra_session_t *ra_session,
                                         apr_pool_t *scratch_pool)
{
  const char *existing_target;
  svn_revnum_t existing_rev;
  svn_node_kind_t existing_kind;
  svn_boolean_t show_deletion;
  const char *empty_file;

  SVN_ERR_ASSERT(kind1 == svn_node_none || kind2 == svn_node_none);

  /* Are we showing an addition or deletion? */
  show_deletion = (kind2 == svn_node_none);

  /* Which target is being added/deleted? Is it a file or a directory? */
  if (show_deletion)
    {
      existing_target = target1;
      existing_rev = rev1;
      existing_kind = kind1;
    }
  else
    {
      existing_target = target2;
      existing_rev = rev2;
      existing_kind = kind2;
    }

  /* All file content will be diffed against the empty file. */
  SVN_ERR(svn_io_open_unique_file3(NULL, &empty_file, NULL,
                                   svn_io_file_del_on_pool_cleanup,
                                   scratch_pool, scratch_pool));

  if (existing_kind == svn_node_file)
    {
      /* Get file content and show a diff against the empty file. */
      SVN_ERR(diff_repos_repos_added_or_deleted_file(existing_target,
                                                     existing_rev,
                                                     rev1, rev2,
                                                     show_deletion,
                                                     empty_file,
                                                     callbacks,
                                                     callback_baton,
                                                     ra_session,
                                                     scratch_pool));
    }
  else
    {
      /* Walk the added/deleted tree and show a diff for each child. */
      SVN_ERR(diff_repos_repos_added_or_deleted_dir(existing_target,
                                                    existing_rev,
                                                    rev1, rev2,
                                                    show_deletion,
                                                    empty_file,
                                                    callbacks,
                                                    callback_baton,
                                                    ra_session,
                                                    scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Perform a diff between two repository paths.

   PATH_OR_URL1 and PATH_OR_URL2 may be either URLs or the working copy paths.
   REVISION1 and REVISION2 are their respective revisions.
   If PEG_REVISION is specified, PATH_OR_URL2 is the path at the peg revision,
   and the actual two paths compared are determined by following copy
   history from PATH_OR_URL2.

   All other options are the same as those passed to svn_client_diff6(). */
static svn_error_t *
diff_repos_repos(const svn_wc_diff_callbacks4_t *callbacks,
                 struct diff_cmd_baton *callback_baton,
                 svn_client_ctx_t *ctx,
                 const char *path_or_url1,
                 const char *path_or_url2,
                 const svn_opt_revision_t *revision1,
                 const svn_opt_revision_t *revision2,
                 const svn_opt_revision_t *peg_revision,
                 svn_depth_t depth,
                 svn_boolean_t ignore_ancestry,
                 apr_pool_t *pool)
{
  svn_ra_session_t *extra_ra_session;

  const svn_ra_reporter3_t *reporter;
  void *reporter_baton;

  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;

  const char *url1;
  const char *url2;
  const char *base_path;
  svn_revnum_t rev1;
  svn_revnum_t rev2;
  svn_node_kind_t kind1;
  svn_node_kind_t kind2;
  const char *anchor1;
  const char *anchor2;
  const char *target1;
  const char *target2;
  svn_ra_session_t *ra_session;

  /* Prepare info for the repos repos diff. */
  SVN_ERR(diff_prepare_repos_repos(&url1, &url2, &base_path, &rev1, &rev2,
                                   &anchor1, &anchor2, &target1, &target2,
                                   &kind1, &kind2, &ra_session,
                                   ctx, path_or_url1, path_or_url2,
                                   revision1, revision2, peg_revision,
                                   pool));

  /* Get actual URLs. */
  callback_baton->orig_path_1 = url1;
  callback_baton->orig_path_2 = url2;

  /* Get numeric revisions. */
  callback_baton->revnum1 = rev1;
  callback_baton->revnum2 = rev2;

  callback_baton->ra_session = ra_session;
  callback_baton->anchor = base_path;

  if (kind1 == svn_node_none || kind2 == svn_node_none)
    {
      /* One side of the diff does not exist.
       * Walk the tree that does exist, showing a series of additions
       * or deletions. */
      SVN_ERR(diff_repos_repos_added_or_deleted_target(target1, target2,
                                                       rev1, rev2,
                                                       kind1, kind2,
                                                       callbacks,
                                                       callback_baton,
                                                       ra_session,
                                                       pool));
      return SVN_NO_ERROR;
    }

  /* Now, we open an extra RA session to the correct anchor
     location for URL1.  This is used during the editor calls to fetch file
     contents.  */
  SVN_ERR(svn_client__open_ra_session_internal(&extra_ra_session, NULL,
                                               anchor1, NULL, NULL, FALSE,
                                               TRUE, ctx, pool));

  /* Set up the repos_diff editor on BASE_PATH, if available.
     Otherwise, we just use "". */
  SVN_ERR(svn_client__get_diff_editor(
                &diff_editor, &diff_edit_baton,
                depth,
                extra_ra_session, rev1, TRUE /* walk_deleted_dirs */,
                TRUE /* text_deltas */,
                callbacks, callback_baton,
                ctx->cancel_func, ctx->cancel_baton,
                NULL /* no notify_func */, NULL /* no notify_baton */,
                pool));

  /* We want to switch our txn into URL2 */
  SVN_ERR(svn_ra_do_diff3
          (ra_session, &reporter, &reporter_baton, rev2, target1,
           depth, ignore_ancestry, TRUE /* text_deltas */,
           url2, diff_editor, diff_edit_baton, pool));

  /* Drive the reporter; do the diff. */
  SVN_ERR(reporter->set_path(reporter_baton, "", rev1,
                             svn_depth_infinity,
                             FALSE, NULL,
                             pool));
  return reporter->finish_report(reporter_baton, pool);
}


/* Using CALLBACKS, show a REPOS->WC diff for a file TARGET, which in the
 * working copy is at FILE2_ABSPATH. KIND1 is the node kind of the repository
 * target (either svn_node_file or svn_node_none). REV is the revision the
 * working file is diffed against. RA_SESSION points at the URL of the file
 * in the repository and is used to get the file's repository-version content,
 * if necessary. The other parameters are as in diff_repos_wc(). */
static svn_error_t *
diff_repos_wc_file_target(const char *target,
                          const char *file2_abspath,
                          svn_node_kind_t kind1,
                          svn_revnum_t rev,
                          svn_boolean_t reverse,
                          svn_boolean_t show_copies_as_adds,
                          const svn_wc_diff_callbacks4_t *callbacks,
                          void *callback_baton,
                          svn_ra_session_t *ra_session,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *scratch_pool)
{
  const char *file1_abspath;
  svn_stream_t *file1_content;
  svn_stream_t *file2_content;
  apr_hash_t *file1_props = NULL;
  apr_hash_t *file2_props;
  svn_boolean_t is_copy = FALSE;
  apr_hash_t *keywords = NULL;
  svn_string_t *keywords_prop;
  svn_subst_eol_style_t eol_style;
  const char *eol_str;

  /* Get content and props of file 1 (the remote file). */
  SVN_ERR(svn_stream_open_unique(&file1_content, &file1_abspath, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 scratch_pool, scratch_pool));
  if (kind1 == svn_node_file)
    {
      if (show_copies_as_adds)
        SVN_ERR(svn_wc__node_get_origin(&is_copy, 
                                        NULL, NULL, NULL, NULL, NULL,
                                        ctx->wc_ctx, file2_abspath,
                                        FALSE, scratch_pool, scratch_pool));
      /* If showing copies as adds, diff against the empty file. */
      if (!(show_copies_as_adds && is_copy))
        SVN_ERR(svn_ra_get_file(ra_session, "", rev, file1_content,
                                NULL, &file1_props, scratch_pool));
    }

  SVN_ERR(svn_stream_close(file1_content));

  SVN_ERR(svn_wc_prop_list2(&file2_props, ctx->wc_ctx, file2_abspath,
                            scratch_pool, scratch_pool));

  /* We might have to create a normalised version of the working file. */
  svn_subst_eol_style_from_value(&eol_style, &eol_str,
                                 apr_hash_get(file2_props,
                                              SVN_PROP_EOL_STYLE,
                                              APR_HASH_KEY_STRING));
  keywords_prop = apr_hash_get(file2_props, SVN_PROP_KEYWORDS,
                               APR_HASH_KEY_STRING);
  if (keywords_prop)
    SVN_ERR(svn_subst_build_keywords2(&keywords, keywords_prop->data,
                                      NULL, NULL, 0, NULL,
                                      scratch_pool));
  if (svn_subst_translation_required(eol_style, SVN_SUBST_NATIVE_EOL_STR,
                                     keywords, FALSE, TRUE))
    {
      svn_stream_t *working_content;
      svn_stream_t *normalized_content;

      SVN_ERR(svn_stream_open_readonly(&working_content, file2_abspath,
                                       scratch_pool, scratch_pool));

      /* Create a temporary file and copy normalised data into it. */
      SVN_ERR(svn_stream_open_unique(&file2_content, &file2_abspath, NULL,
                                     svn_io_file_del_on_pool_cleanup,
                                     scratch_pool, scratch_pool));
      normalized_content = svn_subst_stream_translated(
                             file2_content, SVN_SUBST_NATIVE_EOL_STR,
                             TRUE, keywords, FALSE, scratch_pool);
      SVN_ERR(svn_stream_copy3(working_content, normalized_content,
                               ctx->cancel_func, ctx->cancel_baton,
                               scratch_pool));
    }

  if (kind1 == svn_node_file && !(show_copies_as_adds && is_copy))
    {
      SVN_ERR(callbacks->file_opened(NULL, NULL, target,
                                     reverse ? SVN_INVALID_REVNUM : rev,
                                     callback_baton, scratch_pool));

      if (reverse)
        SVN_ERR(callbacks->file_changed(NULL, NULL, NULL, target,
                                        file2_abspath, file1_abspath,
                                        SVN_INVALID_REVNUM, rev,
                                        apr_hash_get(file2_props,
                                                     SVN_PROP_MIME_TYPE,
                                                     APR_HASH_KEY_STRING),
                                        apr_hash_get(file1_props,
                                                     SVN_PROP_MIME_TYPE,
                                                     APR_HASH_KEY_STRING),
                                        make_regular_props_array(
                                          file1_props, scratch_pool,
                                          scratch_pool),
                                        file2_props,
                                        callback_baton, scratch_pool));
      else
        SVN_ERR(callbacks->file_changed(NULL, NULL, NULL, target,
                                        file1_abspath, file2_abspath,
                                        rev, SVN_INVALID_REVNUM,
                                        apr_hash_get(file1_props,
                                                     SVN_PROP_MIME_TYPE,
                                                     APR_HASH_KEY_STRING),
                                        apr_hash_get(file2_props,
                                                     SVN_PROP_MIME_TYPE,
                                                     APR_HASH_KEY_STRING),
                                        make_regular_props_array(
                                          file2_props, scratch_pool,
                                          scratch_pool),
                                        file1_props,
                                        callback_baton, scratch_pool));
    }
  else
    {
      if (reverse)
        {
          SVN_ERR(callbacks->file_deleted(NULL, NULL,
                                          target, file2_abspath, file1_abspath,
                                          apr_hash_get(file2_props,
                                                       SVN_PROP_MIME_TYPE,
                                                       APR_HASH_KEY_STRING),
                                          NULL,
                                          make_regular_props_hash(
                                            file2_props, scratch_pool,
                                            scratch_pool),
                                          callback_baton, scratch_pool));
        }
      else
        {
          SVN_ERR(callbacks->file_added(NULL, NULL, NULL, target,
                                        file1_abspath, file2_abspath,
                                        rev, SVN_INVALID_REVNUM,
                                        NULL,
                                        apr_hash_get(file2_props,
                                                     SVN_PROP_MIME_TYPE,
                                                     APR_HASH_KEY_STRING),
                                        NULL, SVN_INVALID_REVNUM,
                                        make_regular_props_array(
                                          file2_props, scratch_pool,
                                          scratch_pool),
                                        NULL,
                                        callback_baton, scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}

/* Perform a diff between a repository path and a working-copy path.

   PATH_OR_URL1 may be either a URL or a working copy path.  PATH2 is a
   working copy path.  REVISION1 and REVISION2 are their respective
   revisions.  If REVERSE is TRUE, the diff will be done in reverse.
   If PEG_REVISION is specified, then PATH_OR_URL1 is the path in the peg
   revision, and the actual repository path to be compared is
   determined by following copy history.

   All other options are the same as those passed to svn_client_diff6(). */
static svn_error_t *
diff_repos_wc(const char *path_or_url1,
              const svn_opt_revision_t *revision1,
              const svn_opt_revision_t *peg_revision,
              const char *path2,
              const svn_opt_revision_t *revision2,
              svn_boolean_t reverse,
              svn_depth_t depth,
              svn_boolean_t ignore_ancestry,
              svn_boolean_t show_copies_as_adds,
              svn_boolean_t use_git_diff_format,
              const apr_array_header_t *changelists,
              const svn_wc_diff_callbacks4_t *callbacks,
              struct diff_cmd_baton *callback_baton,
              svn_client_ctx_t *ctx,
              apr_pool_t *pool)
{
  const char *url1, *anchor, *anchor_url, *target;
  svn_revnum_t rev;
  svn_ra_session_t *ra_session;
  svn_depth_t diff_depth;
  const svn_ra_reporter3_t *reporter;
  void *reporter_baton;
  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;
  svn_boolean_t rev2_is_base = (revision2->kind == svn_opt_revision_base);
  svn_boolean_t server_supports_depth;
  const char *abspath_or_url1;
  const char *abspath2;
  const char *anchor_abspath;
  svn_node_kind_t kind1;
  svn_node_kind_t kind2;
  svn_boolean_t is_copy;
  svn_revnum_t copyfrom_rev;
  const char *copy_source_repos_relpath;
  const char *copy_source_repos_root_url;

  SVN_ERR_ASSERT(! svn_path_is_url(path2));

  if (!svn_path_is_url(path_or_url1))
    SVN_ERR(svn_dirent_get_absolute(&abspath_or_url1, path_or_url1, pool));
  else
    abspath_or_url1 = path_or_url1;

  SVN_ERR(svn_dirent_get_absolute(&abspath2, path2, pool));

  /* Convert path_or_url1 to a URL to feed to do_diff. */
  SVN_ERR(convert_to_url(&url1, ctx->wc_ctx, abspath_or_url1, pool, pool));

  SVN_ERR(svn_wc_get_actual_target2(&anchor, &target,
                                    ctx->wc_ctx, path2,
                                    pool, pool));

  /* Fetch the URL of the anchor directory. */
  SVN_ERR(svn_dirent_get_absolute(&anchor_abspath, anchor, pool));
  SVN_ERR(svn_wc__node_get_url(&anchor_url, ctx->wc_ctx, anchor_abspath,
                               pool, pool));
  if (! anchor_url)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                             _("Directory '%s' has no URL"),
                             svn_dirent_local_style(anchor, pool));

  /* If we are performing a pegged diff, we need to find out what our
     actual URLs will be. */
  if (peg_revision->kind != svn_opt_revision_unspecified)
    {
      SVN_ERR(svn_client__repos_locations(&url1, NULL, NULL, NULL,
                                          NULL,
                                          path_or_url1,
                                          peg_revision,
                                          revision1, NULL,
                                          ctx, pool));
      if (!reverse)
        {
          callback_baton->orig_path_1 = url1;
          callback_baton->orig_path_2 =
            svn_path_url_add_component2(anchor_url, target, pool);
        }
      else
        {
          callback_baton->orig_path_1 =
            svn_path_url_add_component2(anchor_url, target, pool);
          callback_baton->orig_path_2 = url1;
        }
    }

  if (use_git_diff_format)
    {
      SVN_ERR(svn_wc__get_wc_root(&callback_baton->wc_root_abspath,
                                  ctx->wc_ctx, anchor_abspath,
                                  pool, pool));
    }

  /* Open an RA session to URL1 to figure out its node kind. */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, NULL, url1,
                                               NULL, NULL, FALSE, TRUE,
                                               ctx, pool));
  /* Resolve the revision to use for URL1. */
  SVN_ERR(svn_client__get_revision_number(&rev, NULL, ctx->wc_ctx,
                                          (strcmp(path_or_url1, url1) == 0)
                                                    ? NULL : abspath_or_url1,
                                          ra_session, revision1, pool));
  SVN_ERR(svn_ra_check_path(ra_session, "", rev, &kind1, pool));

  /* Figure out the node kind of the local target. */
  SVN_ERR(svn_io_check_resolved_path(abspath2, &kind2, pool));

  callback_baton->ra_session = ra_session;
  callback_baton->anchor = anchor;

  if (!reverse)
    callback_baton->revnum1 = rev;
  else
    callback_baton->revnum2 = rev;

  /* Check if our diff target is a copied node. */
  SVN_ERR(svn_wc__node_get_origin(&is_copy, 
                                  &copyfrom_rev,
                                  &copy_source_repos_relpath,
                                  &copy_source_repos_root_url,
                                  NULL, NULL,
                                  ctx->wc_ctx, abspath2,
                                  FALSE, pool, pool));

  /* If both diff targets can be diffed as files, fetch the appropriate
   * file content from the repository and generate a diff against the
   * local version of the file.
   * However, if comparing the repository version of the file to the BASE
   * tree version we can use the diff editor to transmit a delta instead
   * of potentially huge file content. */
  if ((!rev2_is_base || is_copy) &&
      (kind1 == svn_node_file || kind1 == svn_node_none)
       && kind2 == svn_node_file)
    {
      SVN_ERR(diff_repos_wc_file_target(target, abspath2, kind1, rev,
                                        reverse, show_copies_as_adds,
                                        callbacks, callback_baton,
                                        ra_session, ctx, pool));

      return SVN_NO_ERROR;
    }

  /* Use the diff editor to generate the diff. */
  SVN_ERR(svn_ra_has_capability(ra_session, &server_supports_depth,
                                SVN_RA_CAPABILITY_DEPTH, pool));
  SVN_ERR(svn_wc__get_diff_editor(&diff_editor, &diff_edit_baton,
                                  ctx->wc_ctx,
                                  anchor_abspath,
                                  target,
                                  depth,
                                  ignore_ancestry,
                                  show_copies_as_adds,
                                  use_git_diff_format,
                                  rev2_is_base,
                                  reverse,
                                  server_supports_depth,
                                  changelists,
                                  callbacks, callback_baton,
                                  ctx->cancel_func, ctx->cancel_baton,
                                  pool, pool));
  SVN_ERR(svn_ra_reparent(ra_session, anchor_url, pool));

  if (depth != svn_depth_infinity)
    diff_depth = depth;
  else
    diff_depth = svn_depth_unknown;

  if (is_copy)
    {
      const char *copyfrom_url;
      const char *copyfrom_parent_url;
      const char *copyfrom_basename;
      svn_depth_t copy_depth;

      callback_baton->repos_wc_diff_target_is_copy = TRUE;
      
      /* We're diffing a locally copied/moved directory.
       * Describe the copy source to the reporter instead of the copy itself.
       * Doing the latter would generate a single add_directory() call to the
       * diff editor which results in an unexpected diff (the copy would
       * be shown as deleted). */

      copyfrom_url = apr_pstrcat(pool, copy_source_repos_root_url, "/",
                                 copy_source_repos_relpath, (char *)NULL);
      svn_uri_split(&copyfrom_parent_url, &copyfrom_basename,
                    copyfrom_url, pool);
      SVN_ERR(svn_ra_reparent(ra_session, copyfrom_parent_url, pool));

      /* Tell the RA layer we want a delta to change our txn to URL1 */ 
      SVN_ERR(svn_ra_do_diff3(ra_session,
                              &reporter, &reporter_baton,
                              rev,
                              copyfrom_basename,
                              diff_depth,
                              ignore_ancestry,
                              TRUE,  /* text_deltas */
                              url1,
                              diff_editor, diff_edit_baton, pool));

      /* Report the copy source. */
      SVN_ERR(svn_wc__node_get_depth(&copy_depth, ctx->wc_ctx, abspath2,
                                     pool));
      SVN_ERR(reporter->set_path(reporter_baton, "", copyfrom_rev,
                                 copy_depth, FALSE, NULL, pool));
      
      /* Finish the report to generate the diff. */
      SVN_ERR(reporter->finish_report(reporter_baton, pool));
    }
  else
    {
      /* Tell the RA layer we want a delta to change our txn to URL1 */ 
      SVN_ERR(svn_ra_do_diff3(ra_session,
                              &reporter, &reporter_baton,
                              rev,
                              target,
                              diff_depth,
                              ignore_ancestry,
                              TRUE,  /* text_deltas */
                              url1,
                              diff_editor, diff_edit_baton, pool));

      /* Create a txn mirror of path2;  the diff editor will print
         diffs in reverse.  :-)  */
      SVN_ERR(svn_wc_crawl_revisions5(ctx->wc_ctx, abspath2,
                                      reporter, reporter_baton,
                                      FALSE, depth, TRUE,
                                      (! server_supports_depth),
                                      FALSE,
                                      ctx->cancel_func, ctx->cancel_baton,
                                      NULL, NULL, /* notification is N/A */
                                      pool));
    }

  return SVN_NO_ERROR;
}


/* This is basically just the guts of svn_client_diff[_peg]6(). */
static svn_error_t *
do_diff(const svn_wc_diff_callbacks4_t *callbacks,
        struct diff_cmd_baton *callback_baton,
        svn_client_ctx_t *ctx,
        const char *path_or_url1,
        const char *path_or_url2,
        const svn_opt_revision_t *revision1,
        const svn_opt_revision_t *revision2,
        const svn_opt_revision_t *peg_revision,
        svn_depth_t depth,
        svn_boolean_t ignore_ancestry,
        svn_boolean_t show_copies_as_adds,
        svn_boolean_t use_git_diff_format,
        const apr_array_header_t *changelists,
        apr_pool_t *pool)
{
  svn_boolean_t is_repos1;
  svn_boolean_t is_repos2;

  /* Check if paths/revisions are urls/local. */
  SVN_ERR(check_paths(&is_repos1, &is_repos2, path_or_url1, path_or_url2,
                      revision1, revision2, peg_revision));

  if (is_repos1)
    {
      if (is_repos2)
        {
          /* ### Ignores 'show_copies_as_adds'. */
          SVN_ERR(diff_repos_repos(callbacks, callback_baton, ctx,
                                   path_or_url1, path_or_url2,
                                   revision1, revision2,
                                   peg_revision, depth, ignore_ancestry,
                                   pool));
        }
      else /* path_or_url2 is a working copy path */
        {
          SVN_ERR(diff_repos_wc(path_or_url1, revision1, peg_revision,
                                path_or_url2, revision2, FALSE, depth,
                                ignore_ancestry, show_copies_as_adds,
                                use_git_diff_format, changelists,
                                callbacks, callback_baton, ctx, pool));
        }
    }
  else /* path_or_url1 is a working copy path */
    {
      if (is_repos2)
        {
          SVN_ERR(diff_repos_wc(path_or_url2, revision2, peg_revision,
                                path_or_url1, revision1, TRUE, depth,
                                ignore_ancestry, show_copies_as_adds,
                                use_git_diff_format, changelists,
                                callbacks, callback_baton, ctx, pool));
        }
      else /* path_or_url2 is a working copy path */
        {
          SVN_ERR(diff_wc_wc(path_or_url1, revision1, path_or_url2, revision2,
                             depth, ignore_ancestry, show_copies_as_adds,
                             use_git_diff_format, changelists,
                             callbacks, callback_baton, ctx, pool));
        }
    }

  return SVN_NO_ERROR;
}

/* Perform a summary diff between two working-copy paths.

   PATH1 and PATH2 are both working copy paths.  REVISION1 and
   REVISION2 are their respective revisions.

   All other options are the same as those passed to svn_client_diff6(). */
static svn_error_t *
diff_summarize_wc_wc(svn_client_diff_summarize_func_t summarize_func,
                     void *summarize_baton,
                     const char *path1,
                     const svn_opt_revision_t *revision1,
                     const char *path2,
                     const svn_opt_revision_t *revision2,
                     svn_depth_t depth,
                     svn_boolean_t ignore_ancestry,
                     const apr_array_header_t *changelists,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  svn_wc_diff_callbacks4_t *callbacks;
  void *callback_baton;
  const char *abspath1, *target1;
  svn_node_kind_t kind;

  SVN_ERR_ASSERT(! svn_path_is_url(path1));
  SVN_ERR_ASSERT(! svn_path_is_url(path2));

  /* Currently we support only the case where path1 and path2 are the
     same path. */
  if ((strcmp(path1, path2) != 0)
      || (! ((revision1->kind == svn_opt_revision_base)
             && (revision2->kind == svn_opt_revision_working))))
    return unsupported_diff_error
      (svn_error_create
       (SVN_ERR_INCORRECT_PARAMS, NULL,
        _("Summarized diffs are only supported between a path's text-base "
          "and its working files at this time")));

  /* Find the node kind of PATH1 so that we know whether the diff drive will
     be anchored at PATH1 or its parent dir. */
  SVN_ERR(svn_dirent_get_absolute(&abspath1, path1, pool));
  SVN_ERR(svn_wc_read_kind(&kind, ctx->wc_ctx, abspath1, FALSE, pool));
  target1 = (kind == svn_node_dir) ? "" : svn_dirent_basename(path1, pool);
  SVN_ERR(svn_client__get_diff_summarize_callbacks(
            &callbacks, &callback_baton, target1,
            summarize_func, summarize_baton, pool));

  SVN_ERR(svn_wc_diff6(ctx->wc_ctx,
                       abspath1,
                       callbacks, callback_baton,
                       depth,
                       ignore_ancestry, FALSE /* show_copies_as_adds */,
                       FALSE /* use_git_diff_format */, changelists,
                       ctx->cancel_func, ctx->cancel_baton,
                       pool));
  return SVN_NO_ERROR;
}

/* Perform a diff summary between two repository paths. */
static svn_error_t *
diff_summarize_repos_repos(svn_client_diff_summarize_func_t summarize_func,
                           void *summarize_baton,
                           svn_client_ctx_t *ctx,
                           const char *path_or_url1,
                           const char *path_or_url2,
                           const svn_opt_revision_t *revision1,
                           const svn_opt_revision_t *revision2,
                           const svn_opt_revision_t *peg_revision,
                           svn_depth_t depth,
                           svn_boolean_t ignore_ancestry,
                           apr_pool_t *pool)
{
  svn_ra_session_t *extra_ra_session;

  const svn_ra_reporter3_t *reporter;
  void *reporter_baton;

  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;

  const char *url1;
  const char *url2;
  const char *base_path;
  svn_revnum_t rev1;
  svn_revnum_t rev2;
  svn_node_kind_t kind1;
  svn_node_kind_t kind2;
  const char *anchor1;
  const char *anchor2;
  const char *target1;
  const char *target2;
  svn_ra_session_t *ra_session;
  svn_wc_diff_callbacks4_t *callbacks;
  void *callback_baton;

  /* Prepare info for the repos repos diff. */
  SVN_ERR(diff_prepare_repos_repos(&url1, &url2, &base_path, &rev1, &rev2,
                                   &anchor1, &anchor2, &target1, &target2,
                                   &kind1, &kind2, &ra_session,
                                   ctx, path_or_url1, path_or_url2,
                                   revision1, revision2,
                                   peg_revision, pool));

  if (kind1 == svn_node_none || kind2 == svn_node_none)
    {
      /* One side of the diff does not exist.
       * Walk the tree that does exist, showing a series of additions
       * or deletions. */
      SVN_ERR(svn_client__get_diff_summarize_callbacks(
                &callbacks, &callback_baton, target1,
                summarize_func, summarize_baton, pool));
      SVN_ERR(diff_repos_repos_added_or_deleted_target(target1, target2,
                                                       rev1, rev2,
                                                       kind1, kind2,
                                                       callbacks,
                                                       callback_baton,
                                                       ra_session,
                                                       pool));
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_client__get_diff_summarize_callbacks(
            &callbacks, &callback_baton,
            target1, summarize_func, summarize_baton, pool));

  /* Now, we open an extra RA session to the correct anchor
     location for URL1.  This is used to get the kind of deleted paths.  */
  SVN_ERR(svn_client__open_ra_session_internal(&extra_ra_session, NULL,
                                               anchor1, NULL, NULL, FALSE,
                                               TRUE, ctx, pool));

  /* Set up the repos_diff editor. */
  SVN_ERR(svn_client__get_diff_editor(&diff_editor, &diff_edit_baton,
            depth,
            extra_ra_session, rev1, TRUE /* walk_deleted_dirs */,
            FALSE /* text_deltas */,
            callbacks, callback_baton,
            ctx->cancel_func, ctx->cancel_baton,
            NULL /* notify_func */, NULL /* notify_baton */, pool));

  /* We want to switch our txn into URL2 */
  SVN_ERR(svn_ra_do_diff3
          (ra_session, &reporter, &reporter_baton, rev2, target1,
           depth, ignore_ancestry,
           FALSE /* do not create text delta */, url2, diff_editor,
           diff_edit_baton, pool));

  /* Drive the reporter; do the diff. */
  SVN_ERR(reporter->set_path(reporter_baton, "", rev1,
                             svn_depth_infinity,
                             FALSE, NULL, pool));
  return reporter->finish_report(reporter_baton, pool);
}

/* This is basically just the guts of svn_client_diff_summarize[_peg]2(). */
static svn_error_t *
do_diff_summarize(svn_client_diff_summarize_func_t summarize_func,
                  void *summarize_baton,
                  svn_client_ctx_t *ctx,
                  const char *path_or_url1,
                  const char *path_or_url2,
                  const svn_opt_revision_t *revision1,
                  const svn_opt_revision_t *revision2,
                  const svn_opt_revision_t *peg_revision,
                  svn_depth_t depth,
                  svn_boolean_t ignore_ancestry,
                  const apr_array_header_t *changelists,
                  apr_pool_t *pool)
{
  svn_boolean_t is_repos1;
  svn_boolean_t is_repos2;

  /* Check if paths/revisions are urls/local. */
  SVN_ERR(check_paths(&is_repos1, &is_repos2, path_or_url1, path_or_url2,
                      revision1, revision2, peg_revision));

  if (is_repos1 && is_repos2)
    return diff_summarize_repos_repos(summarize_func, summarize_baton, ctx,
                                      path_or_url1, path_or_url2,
                                      revision1, revision2,
                                      peg_revision, depth, ignore_ancestry,
                                      pool);
  else if (! is_repos1 && ! is_repos2)
    return diff_summarize_wc_wc(summarize_func, summarize_baton,
                                path_or_url1, revision1,
                                path_or_url2, revision2,
                                depth, ignore_ancestry,
                                changelists, ctx, pool);
  else
   return unsupported_diff_error(
            svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                             _("Summarizing diff cannot compare repository "
                               "to WC")));
}


/* Initialize DIFF_CMD_BATON.diff_cmd and DIFF_CMD_BATON.options,
 * according to OPTIONS and CONFIG.  CONFIG and OPTIONS may be null.
 * Allocate the fields in POOL, which should be at least as long-lived
 * as the pool DIFF_CMD_BATON itself is allocated in.
 */
static svn_error_t *
set_up_diff_cmd_and_options(struct diff_cmd_baton *diff_cmd_baton,
                            const apr_array_header_t *options,
                            apr_hash_t *config, apr_pool_t *pool)
{
  const char *diff_cmd = NULL;

  /* See if there is a diff command and/or diff arguments. */
  if (config)
    {
      svn_config_t *cfg = apr_hash_get(config, SVN_CONFIG_CATEGORY_CONFIG,
                                       APR_HASH_KEY_STRING);
      svn_config_get(cfg, &diff_cmd, SVN_CONFIG_SECTION_HELPERS,
                     SVN_CONFIG_OPTION_DIFF_CMD, NULL);
      if (options == NULL)
        {
          const char *diff_extensions;
          svn_config_get(cfg, &diff_extensions, SVN_CONFIG_SECTION_HELPERS,
                         SVN_CONFIG_OPTION_DIFF_EXTENSIONS, NULL);
          if (diff_extensions)
            options = svn_cstring_split(diff_extensions, " \t\n\r", TRUE, pool);
        }
    }

  if (options == NULL)
    options = apr_array_make(pool, 0, sizeof(const char *));

  if (diff_cmd)
    SVN_ERR(svn_path_cstring_to_utf8(&diff_cmd_baton->diff_cmd, diff_cmd,
                                     pool));
  else
    diff_cmd_baton->diff_cmd = NULL;

  /* If there was a command, arrange options to pass to it. */
  if (diff_cmd_baton->diff_cmd)
    {
      const char **argv = NULL;
      int argc = options->nelts;
      if (argc)
        {
          int i;
          argv = apr_palloc(pool, argc * sizeof(char *));
          for (i = 0; i < argc; i++)
            SVN_ERR(svn_utf_cstring_to_utf8(&argv[i],
                      APR_ARRAY_IDX(options, i, const char *), pool));
        }
      diff_cmd_baton->options.for_external.argv = argv;
      diff_cmd_baton->options.for_external.argc = argc;
    }
  else  /* No command, so arrange options for internal invocation instead. */
    {
      diff_cmd_baton->options.for_internal
        = svn_diff_file_options_create(pool);
      SVN_ERR(svn_diff_file_options_parse
              (diff_cmd_baton->options.for_internal, options, pool));
    }

  return SVN_NO_ERROR;
}

/*----------------------------------------------------------------------- */

/*** Public Interfaces. ***/

/* Display context diffs between two PATH/REVISION pairs.  Each of
   these inputs will be one of the following:

   - a repository URL at a given revision.
   - a working copy path, ignoring local mods.
   - a working copy path, including local mods.

   We can establish a matrix that shows the nine possible types of
   diffs we expect to support.


      ` .     DST ||  URL:rev   | WC:base    | WC:working |
          ` .     ||            |            |            |
      SRC     ` . ||            |            |            |
      ============++============+============+============+
       URL:rev    || (*)        | (*)        | (*)        |
                  ||            |            |            |
                  ||            |            |            |
                  ||            |            |            |
      ------------++------------+------------+------------+
       WC:base    || (*)        |                         |
                  ||            | New svn_wc_diff which   |
                  ||            | is smart enough to      |
                  ||            | handle two WC paths     |
      ------------++------------+ and their related       +
       WC:working || (*)        | text-bases and working  |
                  ||            | files.  This operation  |
                  ||            | is entirely local.      |
                  ||            |                         |
      ------------++------------+------------+------------+
      * These cases require server communication.
*/
svn_error_t *
svn_client_diff6(const apr_array_header_t *options,
                 const char *path_or_url1,
                 const svn_opt_revision_t *revision1,
                 const char *path_or_url2,
                 const svn_opt_revision_t *revision2,
                 const char *relative_to_dir,
                 svn_depth_t depth,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t no_diff_deleted,
                 svn_boolean_t show_copies_as_adds,
                 svn_boolean_t ignore_content_type,
                 svn_boolean_t ignore_properties,
                 svn_boolean_t properties_only,
                 svn_boolean_t use_git_diff_format,
                 const char *header_encoding,
                 svn_stream_t *outstream,
                 svn_stream_t *errstream,
                 const apr_array_header_t *changelists,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  struct diff_cmd_baton diff_cmd_baton = { 0 };
  svn_opt_revision_t peg_revision;

  if (ignore_properties && properties_only)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("Cannot ignore properties and show only "
                              "properties at the same time"));

  /* We will never do a pegged diff from here. */
  peg_revision.kind = svn_opt_revision_unspecified;

  /* setup callback and baton */
  diff_cmd_baton.orig_path_1 = path_or_url1;
  diff_cmd_baton.orig_path_2 = path_or_url2;

  SVN_ERR(set_up_diff_cmd_and_options(&diff_cmd_baton, options,
                                      ctx->config, pool));
  diff_cmd_baton.pool = pool;
  diff_cmd_baton.outstream = outstream;
  diff_cmd_baton.errstream = errstream;
  diff_cmd_baton.header_encoding = header_encoding;
  diff_cmd_baton.revnum1 = SVN_INVALID_REVNUM;
  diff_cmd_baton.revnum2 = SVN_INVALID_REVNUM;

  diff_cmd_baton.force_empty = FALSE;
  diff_cmd_baton.force_binary = ignore_content_type;
  diff_cmd_baton.ignore_properties = ignore_properties;
  diff_cmd_baton.properties_only = properties_only;
  diff_cmd_baton.relative_to_dir = relative_to_dir;
  diff_cmd_baton.use_git_diff_format = use_git_diff_format;
  diff_cmd_baton.no_diff_deleted = no_diff_deleted;
  diff_cmd_baton.wc_ctx = ctx->wc_ctx;
  diff_cmd_baton.visited_paths = apr_hash_make(pool);
  diff_cmd_baton.ra_session = NULL;
  diff_cmd_baton.wc_root_abspath = NULL;
  diff_cmd_baton.anchor = NULL;

  return do_diff(&diff_callbacks, &diff_cmd_baton, ctx,
                 path_or_url1, path_or_url2, revision1, revision2,
                 &peg_revision,
                 depth, ignore_ancestry, show_copies_as_adds,
                 use_git_diff_format, changelists, pool);
}

svn_error_t *
svn_client_diff_peg6(const apr_array_header_t *options,
                     const char *path_or_url,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *start_revision,
                     const svn_opt_revision_t *end_revision,
                     const char *relative_to_dir,
                     svn_depth_t depth,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t no_diff_deleted,
                     svn_boolean_t show_copies_as_adds,
                     svn_boolean_t ignore_content_type,
                     svn_boolean_t ignore_properties,
                     svn_boolean_t properties_only,
                     svn_boolean_t use_git_diff_format,
                     const char *header_encoding,
                     svn_stream_t *outstream,
                     svn_stream_t *errstream,
                     const apr_array_header_t *changelists,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  struct diff_cmd_baton diff_cmd_baton = { 0 };

  if (ignore_properties && properties_only)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("Cannot ignore properties and show only "
                              "properties at the same time"));

  /* setup callback and baton */
  diff_cmd_baton.orig_path_1 = path_or_url;
  diff_cmd_baton.orig_path_2 = path_or_url;

  SVN_ERR(set_up_diff_cmd_and_options(&diff_cmd_baton, options,
                                      ctx->config, pool));
  diff_cmd_baton.pool = pool;
  diff_cmd_baton.outstream = outstream;
  diff_cmd_baton.errstream = errstream;
  diff_cmd_baton.header_encoding = header_encoding;
  diff_cmd_baton.revnum1 = SVN_INVALID_REVNUM;
  diff_cmd_baton.revnum2 = SVN_INVALID_REVNUM;

  diff_cmd_baton.force_empty = FALSE;
  diff_cmd_baton.force_binary = ignore_content_type;
  diff_cmd_baton.ignore_properties = ignore_properties;
  diff_cmd_baton.properties_only = properties_only;
  diff_cmd_baton.relative_to_dir = relative_to_dir;
  diff_cmd_baton.use_git_diff_format = use_git_diff_format;
  diff_cmd_baton.no_diff_deleted = no_diff_deleted;
  diff_cmd_baton.wc_ctx = ctx->wc_ctx;
  diff_cmd_baton.visited_paths = apr_hash_make(pool);
  diff_cmd_baton.ra_session = NULL;
  diff_cmd_baton.wc_root_abspath = NULL;
  diff_cmd_baton.anchor = NULL;

  return do_diff(&diff_callbacks, &diff_cmd_baton, ctx,
                 path_or_url, path_or_url, start_revision, end_revision,
                 peg_revision,
                 depth, ignore_ancestry, show_copies_as_adds,
                 use_git_diff_format, changelists, pool);
}

svn_error_t *
svn_client_diff_summarize2(const char *path_or_url1,
                           const svn_opt_revision_t *revision1,
                           const char *path_or_url2,
                           const svn_opt_revision_t *revision2,
                           svn_depth_t depth,
                           svn_boolean_t ignore_ancestry,
                           const apr_array_header_t *changelists,
                           svn_client_diff_summarize_func_t summarize_func,
                           void *summarize_baton,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool)
{
  /* We will never do a pegged diff from here. */
  svn_opt_revision_t peg_revision;
  peg_revision.kind = svn_opt_revision_unspecified;

  return do_diff_summarize(summarize_func, summarize_baton, ctx,
                           path_or_url1, path_or_url2, revision1, revision2,
                           &peg_revision,
                           depth, ignore_ancestry, changelists, pool);
}

svn_error_t *
svn_client_diff_summarize_peg2(const char *path_or_url,
                               const svn_opt_revision_t *peg_revision,
                               const svn_opt_revision_t *start_revision,
                               const svn_opt_revision_t *end_revision,
                               svn_depth_t depth,
                               svn_boolean_t ignore_ancestry,
                               const apr_array_header_t *changelists,
                               svn_client_diff_summarize_func_t summarize_func,
                               void *summarize_baton,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *pool)
{
  return do_diff_summarize(summarize_func, summarize_baton, ctx,
                           path_or_url, path_or_url,
                           start_revision, end_revision, peg_revision,
                           depth, ignore_ancestry, changelists, pool);
}

svn_client_diff_summarize_t *
svn_client_diff_summarize_dup(const svn_client_diff_summarize_t *diff,
                              apr_pool_t *pool)
{
  svn_client_diff_summarize_t *dup_diff = apr_palloc(pool, sizeof(*dup_diff));

  *dup_diff = *diff;

  if (diff->path)
    dup_diff->path = apr_pstrdup(pool, diff->path);

  return dup_diff;
}
