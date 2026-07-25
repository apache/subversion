/*
 * diff_patch.c: writer for unidiff files from diff_processor.
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
#include "svn_diff.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_utf.h"
#include "svn_props.h"
#include "client.h"

#include "private/svn_client_private.h"
#include "private/svn_wc_private.h"
#include "private/svn_diff_private.h"
#include "private/svn_io_private.h"

#include "svn_private_config.h"


/* Utilities */

#define DIFF_REVNUM_NONEXISTENT ((svn_revnum_t) -100)

#define MAKE_ERR_BAD_RELATIVE_PATH(path, relative_to_dir) \
        svn_error_createf(SVN_ERR_BAD_RELATIVE_PATH, NULL, \
                          _("Path '%s' must be an immediate child of " \
                            "the directory '%s'"), path, relative_to_dir)


/* Calculate the repository relative path of DIFF_RELPATH, using
 * SESSION_RELPATH and WC_CTX, and return the result in *REPOS_RELPATH.
 * ORIG_TARGET is the related original target passed to the diff command,
 * and may be used to derive leading path components missing from PATH.
 * ANCHOR is the local path where the diff editor is anchored.
 * Do all allocations in POOL. */
static svn_error_t *
make_repos_relpath(const char **repos_relpath,
                   const char *diff_relpath,
                   const char *orig_target,
                   const char *session_relpath,
                   svn_wc_context_t *wc_ctx,
                   const char *anchor,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  const char *local_abspath;

  if (! session_relpath
      || (anchor && !svn_path_is_url(orig_target)))
    {
      svn_error_t *err;
      /* We're doing a WC-WC diff, so we can retrieve all information we
       * need from the working copy. */
      SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                      svn_dirent_join(anchor, diff_relpath,
                                                      scratch_pool),
                                      scratch_pool));

      err = svn_wc__node_get_repos_info(NULL, repos_relpath, NULL, NULL,
                                        wc_ctx, local_abspath,
                                        result_pool, scratch_pool);

      if (!session_relpath
          || ! err
          || (err && err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND))
        {
           return svn_error_trace(err);
        }

      /* The path represents a local working copy path, but does not
         exist. Fall through to calculate an in-repository location
         based on the ra session */

      /* ### Maybe we should use the nearest existing ancestor instead? */
      svn_error_clear(err);
    }

  *repos_relpath = svn_relpath_join(session_relpath, diff_relpath,
                                    result_pool);

  return SVN_NO_ERROR;
}

/* Adjust paths to handle the case when we're dealing with different anchors.
 *
 * Set *INDEX_PATH to the new relative path. Set *LABEL_PATH1 and
 * *LABEL_PATH2 to that path annotated with the unique parts of ORIG_PATH_1
 * and ORIG_PATH_2 respectively, like this:
 *
 *   INDEX_PATH:  "path"
 *   LABEL_PATH1: "path\t(.../branches/branch1)"
 *   LABEL_PATH2: "path\t(.../trunk)"
 *
 * Make the output paths relative to RELATIVE_TO_DIR (if not null) by
 * removing it from the beginning of (ANCHOR + RELPATH).
 *
 * ANCHOR (if not null) is the local path where the diff editor is anchored.
 * RELPATH is the path to the changed node within the diff editor, so
 * relative to ANCHOR.
 *
 * RELATIVE_TO_DIR and ANCHOR are of the same form -- either absolute local
 * paths or relative paths relative to the same base.
 *
 * ORIG_PATH_1 and ORIG_PATH_2 represent the two original target paths or
 * URLs passed to the diff command.
 *
 * Allocate results in RESULT_POOL (or as a pointer to RELPATH) and
 * temporary data in SCRATCH_POOL.
 */
static svn_error_t *
adjust_paths_for_diff_labels(const char **index_path,
                             const char **label_path1,
                             const char **label_path2,
                             const char *relative_to_dir,
                             const char *anchor,
                             const char *relpath,
                             const char *orig_path_1,
                             const char *orig_path_2,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  const char *new_path = relpath;
  const char *new_path1 = orig_path_1;
  const char *new_path2 = orig_path_2;

  if (anchor)
    new_path = svn_dirent_join(anchor, new_path, result_pool);

  if (relative_to_dir)
    {
      /* Possibly adjust the paths shown in the output (see issue #2723). */
      const char *child_path = svn_dirent_is_child(relative_to_dir, new_path,
                                                   result_pool);

      if (child_path)
        new_path = child_path;
      else if (! strcmp(relative_to_dir, new_path))
        new_path = ".";
      else
        return MAKE_ERR_BAD_RELATIVE_PATH(
                 svn_dirent_local_style(new_path, scratch_pool),
                 svn_dirent_local_style(relative_to_dir, scratch_pool));
    }

  {
    apr_size_t len;
    svn_boolean_t is_url1;
    svn_boolean_t is_url2;
    /* ### Holy cow.  Due to anchor/target weirdness, we can't
       simply join dwi->orig_path_1 with path, ditto for
       orig_path_2.  That will work when they're directory URLs, but
       not for file URLs.  Nor can we just use anchor1 and anchor2
       from do_diff(), at least not without some more logic here.
       What a nightmare.

       For now, to distinguish the two paths, we'll just put the
       unique portions of the original targets in parentheses after
       the received path, with ellipses for handwaving.  This makes
       the labels a bit clumsy, but at least distinctive.  Better
       solutions are possible, they'll just take more thought. */

    /* ### BH: We can now just construct the repos_relpath, etc. as the
           anchor is available. See also make_repos_relpath() */

    /* Remove the common prefix of NEW_PATH1 and NEW_PATH2. */
    is_url1 = svn_path_is_url(new_path1);
    is_url2 = svn_path_is_url(new_path2);

    if (is_url1 && is_url2)
      len = strlen(svn_uri_get_longest_ancestor(new_path1, new_path2,
                                                scratch_pool));
    else if (!is_url1 && !is_url2)
      len = strlen(svn_dirent_get_longest_ancestor(new_path1, new_path2,
                                                   scratch_pool));
    else
      len = 0; /* Path and URL */

    new_path1 += len;
    new_path2 += len;
  }

  /* ### Should diff labels print paths in local style?  Is there
     already a standard for this?  In any case, this code depends on
     a particular style, so not calling svn_dirent_local_style() on the
     paths below.*/

  if (new_path[0] == '\0')
    new_path = ".";

  if (new_path1[0] == '\0')
    new_path1 = new_path;
  else if (svn_path_is_url(new_path1))
    new_path1 = apr_psprintf(result_pool, "%s\t(%s)", new_path, new_path1);
  else if (new_path1[0] == '/')
    new_path1 = apr_psprintf(result_pool, "%s\t(...%s)", new_path, new_path1);
  else
    new_path1 = apr_psprintf(result_pool, "%s\t(.../%s)", new_path, new_path1);

  if (new_path2[0] == '\0')
    new_path2 = new_path;
  else if (svn_path_is_url(new_path2))
    new_path2 = apr_psprintf(result_pool, "%s\t(%s)", new_path, new_path2);
  else if (new_path2[0] == '/')
    new_path2 = apr_psprintf(result_pool, "%s\t(...%s)", new_path, new_path2);
  else
    new_path2 = apr_psprintf(result_pool, "%s\t(.../%s)", new_path, new_path2);

  *index_path = new_path;
  *label_path1 = new_path1;
  *label_path2 = new_path2;

  return SVN_NO_ERROR;
}


/* Generate a label for the diff output for file PATH at revision REVNUM.
   If REVNUM is invalid then it is assumed to be the current working
   copy.  Assumes the paths are already in the desired style (local
   vs internal).  Allocate the label in RESULT-POOL. */
static const char *
diff_label(const char *path,
           svn_revnum_t revnum,
           apr_pool_t *result_pool)
{
  const char *label;
  if (revnum >= 0)
    label = apr_psprintf(result_pool, _("%s\t(revision %ld)"), path, revnum);
  else if (revnum == DIFF_REVNUM_NONEXISTENT)
    label = apr_psprintf(result_pool, _("%s\t(nonexistent)"), path);
  else /* SVN_INVALID_REVNUM */
    label = apr_psprintf(result_pool, _("%s\t(working copy)"), path);

  return label;
}

/* Standard modes produced in git style diffs */
static const int exec_mode =                 0755;
static const int noexec_mode =               0644;
static const int kind_file_mode =         0100000;
/*static const kind_dir_mode =            0040000;*/
static const int kind_symlink_mode =      0120000;

/* Print a git diff header for an addition within a diff between PATH1 and
 * PATH2 to the stream OS using HEADER_ENCODING. */
static svn_error_t *
print_git_diff_header_added(svn_stream_t *os, const char *header_encoding,
                            const char *path1, const char *path2,
                            svn_boolean_t exec_bit,
                            svn_boolean_t symlink_bit,
                            apr_pool_t *scratch_pool)
{
  int new_mode = (exec_bit ? exec_mode : noexec_mode)
                 | (symlink_bit ? kind_symlink_mode : kind_file_mode);

  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "diff --git a/%s b/%s%s",
                                      path1, path2, APR_EOL_STR));
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "new file mode %06o" APR_EOL_STR,
                                      new_mode));
  return SVN_NO_ERROR;
}

/* Print a git diff header for a deletion within a diff between PATH1 and
 * PATH2 to the stream OS using HEADER_ENCODING. */
static svn_error_t *
print_git_diff_header_deleted(svn_stream_t *os, const char *header_encoding,
                              const char *path1, const char *path2,
                              svn_boolean_t exec_bit,
                              svn_boolean_t symlink_bit,
                              apr_pool_t *scratch_pool)
{
  int old_mode = (exec_bit ? exec_mode : noexec_mode)
                 | (symlink_bit ? kind_symlink_mode : kind_file_mode);
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "diff --git a/%s b/%s%s",
                                      path1, path2, APR_EOL_STR));
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "deleted file mode %06o" APR_EOL_STR,
                                      old_mode));
  return SVN_NO_ERROR;
}

/* Print a git diff header for a copy from COPYFROM_PATH to PATH to the stream
 * OS using HEADER_ENCODING. */
static svn_error_t *
print_git_diff_header_copied(svn_stream_t *os, const char *header_encoding,
                             const char *copyfrom_path,
                             svn_revnum_t copyfrom_rev,
                             const char *path,
                             apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "diff --git a/%s b/%s%s",
                                      copyfrom_path, path, APR_EOL_STR));
  if (copyfrom_rev != SVN_INVALID_REVNUM)
    SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                        "copy from %s@%ld%s", copyfrom_path,
                                        copyfrom_rev, APR_EOL_STR));
  else
    SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                        "copy from %s%s", copyfrom_path,
                                        APR_EOL_STR));
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "copy to %s%s", path, APR_EOL_STR));
  return SVN_NO_ERROR;
}

/* Print a git diff header for a rename from COPYFROM_PATH to PATH to the
 * stream OS using HEADER_ENCODING. */
static svn_error_t *
print_git_diff_header_renamed(svn_stream_t *os, const char *header_encoding,
                              const char *copyfrom_path, const char *path,
                              apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "diff --git a/%s b/%s%s",
                                      copyfrom_path, path, APR_EOL_STR));
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "rename from %s%s", copyfrom_path,
                                      APR_EOL_STR));
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "rename to %s%s", path, APR_EOL_STR));
  return SVN_NO_ERROR;
}

/* Print a git diff header for a modification within a diff between PATH1 and
 * PATH2 to the stream OS using HEADER_ENCODING. */
static svn_error_t *
print_git_diff_header_modified(svn_stream_t *os, const char *header_encoding,
                               const char *path1, const char *path2,
                               apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "diff --git a/%s b/%s%s",
                                      path1, path2, APR_EOL_STR));
  return SVN_NO_ERROR;
}

/* Helper function for print_git_diff_header */
static svn_error_t *
maybe_print_mode_change(svn_stream_t *os,
                        const char *header_encoding,
                        svn_boolean_t exec_bit1,
                        svn_boolean_t exec_bit2,
                        svn_boolean_t symlink_bit1,
                        svn_boolean_t symlink_bit2,
                        const char *git_index_shas,
                        apr_pool_t *scratch_pool)
{
  int old_mode = (exec_bit1 ? exec_mode : noexec_mode)
                 | (symlink_bit1 ? kind_symlink_mode : kind_file_mode);
  int new_mode = (exec_bit2 ? exec_mode : noexec_mode)
                 | (symlink_bit2 ? kind_symlink_mode : kind_file_mode);
  if (old_mode == new_mode)
    {
      if (git_index_shas)
        SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                            "index %s %06o" APR_EOL_STR,
                                            git_index_shas, old_mode));
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "old mode %06o" APR_EOL_STR, old_mode));
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "new mode %06o" APR_EOL_STR, new_mode));
  return SVN_NO_ERROR;
}

/* Print a git diff header showing the OPERATION to the stream OS using
 * HEADER_ENCODING.
 *
 * Return suitable diff labels for the git diff in *LABEL1 and *LABEL2.
 *
 * REV1 and REV2 are the revisions being diffed.
 * COPYFROM_PATH and COPYFROM_REV indicate where the
 * diffed item was copied from.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
print_git_diff_header(svn_stream_t *os,
                      const char **label1, const char **label2,
                      svn_diff_operation_kind_t operation,
                      svn_revnum_t rev1,
                      svn_revnum_t rev2,
                      const char *diff_relpath,
                      const char *copyfrom_path,
                      svn_revnum_t copyfrom_rev,
                      apr_hash_t *left_props,
                      apr_hash_t *right_props,
                      const char *git_index_shas,
                      const char *header_encoding,
                      const svn_client__diff_driver_info_t *ddi,
                      apr_pool_t *scratch_pool)
{
  const char *repos_relpath1;
  const char *repos_relpath2;
  const char *copyfrom_repos_relpath = NULL;
  svn_boolean_t exec_bit1 = (svn_prop_get_value(left_props,
                                                SVN_PROP_EXECUTABLE) != NULL);
  svn_boolean_t exec_bit2 = (svn_prop_get_value(right_props,
                                                SVN_PROP_EXECUTABLE) != NULL);
  svn_boolean_t symlink_bit1 = (svn_prop_get_value(left_props,
                                                   SVN_PROP_SPECIAL) != NULL);
  svn_boolean_t symlink_bit2 = (svn_prop_get_value(right_props,
                                                   SVN_PROP_SPECIAL) != NULL);

  SVN_ERR(make_repos_relpath(&repos_relpath1, diff_relpath,
                             ddi->orig_path_1,
                             ddi->session_relpath,
                             ddi->wc_ctx,
                             ddi->anchor,
                             scratch_pool, scratch_pool));
  SVN_ERR(make_repos_relpath(&repos_relpath2, diff_relpath,
                             ddi->orig_path_2,
                             ddi->session_relpath,
                             ddi->wc_ctx,
                             ddi->anchor,
                             scratch_pool, scratch_pool));
  if (copyfrom_path)
    SVN_ERR(make_repos_relpath(&copyfrom_repos_relpath, copyfrom_path,
                               ddi->orig_path_2,
                               ddi->session_relpath,
                               ddi->wc_ctx,
                               ddi->anchor,
                               scratch_pool, scratch_pool));

  if (operation == svn_diff_op_deleted)
    {
      SVN_ERR(print_git_diff_header_deleted(os, header_encoding,
                                            repos_relpath1, repos_relpath2,
                                            exec_bit1, symlink_bit1,
                                            scratch_pool));
      *label1 = diff_label(apr_psprintf(scratch_pool, "a/%s", repos_relpath1),
                           rev1, scratch_pool);
      *label2 = diff_label(apr_psprintf(scratch_pool, "b/%s", repos_relpath2),
                           rev2, scratch_pool);

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
      SVN_ERR(maybe_print_mode_change(os, header_encoding,
                                      exec_bit1, exec_bit2,
                                      symlink_bit1, symlink_bit2,
                                      git_index_shas,
                                      scratch_pool));
    }
  else if (operation == svn_diff_op_added)
    {
      SVN_ERR(print_git_diff_header_added(os, header_encoding,
                                          repos_relpath1, repos_relpath2,
                                          exec_bit2, symlink_bit2,
                                          scratch_pool));
      *label1 = diff_label(apr_psprintf(scratch_pool, "a/%s", repos_relpath1),
                           rev1, scratch_pool);
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
      SVN_ERR(maybe_print_mode_change(os, header_encoding,
                                      exec_bit1, exec_bit2,
                                      symlink_bit1, symlink_bit2,
                                      git_index_shas,
                                      scratch_pool));
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
      SVN_ERR(maybe_print_mode_change(os, header_encoding,
                                      exec_bit1, exec_bit2,
                                      symlink_bit1, symlink_bit2,
                                      git_index_shas,
                                      scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Print the "Index:" and "=====" lines.
 * Show the paths in platform-independent format ('/' separators)
 */
static svn_error_t *
print_diff_index_header(svn_stream_t *outstream,
                        const char *header_encoding,
                        const char *index_path,
                        const char *suffix,
                        apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_stream_printf_from_utf8(outstream,
                                      header_encoding, scratch_pool,
                                      "Index: %s%s" APR_EOL_STR
                                      SVN_DIFF__EQUAL_STRING APR_EOL_STR,
                                      index_path, suffix));
  return SVN_NO_ERROR;
}

/* A helper func that writes out verbal descriptions of property diffs
   to OUTSTREAM.   Of course, OUTSTREAM will probably be whatever was
   passed to svn_client_diff7(), which is probably stdout.

   ### FIXME needs proper docstring

   If USE_GIT_DIFF_FORMAT is TRUE, print git diff headers, which always
   show paths relative to the repository root. DDI->session_relpath and
   DDI->wc_ctx are needed to normalize paths relative the repository root,
   and are ignored if USE_GIT_DIFF_FORMAT is FALSE.

   If @a pretty_print_mergeinfo is true, then describe 'svn:mergeinfo'
   property changes in a human-readable form that says what changes were
   merged or reverse merged; otherwise (or if the mergeinfo property values
   don't parse correctly) display them just like any other property.
 */
static svn_error_t *
display_prop_diffs(const apr_array_header_t *propchanges,
                   apr_hash_t *left_props,
                   apr_hash_t *right_props,
                   const char *diff_relpath,
                   svn_revnum_t rev1,
                   svn_revnum_t rev2,
                   const char *encoding,
                   svn_stream_t *outstream,
                   const char *relative_to_dir,
                   svn_boolean_t show_diff_header,
                   svn_boolean_t use_git_diff_format,
                   svn_boolean_t pretty_print_mergeinfo,
                   const svn_client__diff_driver_info_t *ddi,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *scratch_pool)
{
  const char *repos_relpath1 = NULL;
  const char *index_path;
  const char *label_path1, *label_path2;

  if (use_git_diff_format)
    {
      SVN_ERR(make_repos_relpath(&repos_relpath1, diff_relpath, ddi->orig_path_1,
                                 ddi->session_relpath, ddi->wc_ctx, ddi->anchor,
                                 scratch_pool, scratch_pool));
    }

  /* If we're creating a diff on the wc root, path would be empty. */
  SVN_ERR(adjust_paths_for_diff_labels(&index_path,
                                       &label_path1, &label_path2,
                                       relative_to_dir, ddi->anchor,
                                       diff_relpath,
                                       ddi->orig_path_1, ddi->orig_path_2,
                                       scratch_pool, scratch_pool));

  if (show_diff_header)
    {
      const char *label1;
      const char *label2;

      label1 = diff_label(label_path1, rev1, scratch_pool);
      label2 = diff_label(label_path2, rev2, scratch_pool);

      SVN_ERR(print_diff_index_header(outstream, encoding,
                                      index_path, "", scratch_pool));

      if (use_git_diff_format)
        SVN_ERR(print_git_diff_header(outstream, &label1, &label2,
                                      svn_diff_op_modified,
                                      rev1, rev2,
                                      diff_relpath,
                                      NULL, SVN_INVALID_REVNUM,
                                      left_props, right_props,
                                      NULL,
                                      encoding, ddi, scratch_pool));

      /* --- label1
       * +++ label2 */
      SVN_ERR(svn_diff__unidiff_write_header(
        outstream, encoding, label1, label2, scratch_pool));
    }

  SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, scratch_pool,
                                      APR_EOL_STR
                                      "Property changes on: %s"
                                      APR_EOL_STR,
                                      use_git_diff_format
                                            ? repos_relpath1
                                            : index_path));

  SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, scratch_pool,
                                      SVN_DIFF__UNDER_STRING APR_EOL_STR));

  SVN_ERR(svn_diff__display_prop_diffs(
            outstream, encoding, propchanges, left_props,
            pretty_print_mergeinfo,
            -1 /* context_size */,
            cancel_func, cancel_baton, scratch_pool));

  return SVN_NO_ERROR;
}

/*-----------------------------------------------------------------*/

/*** Callbacks for 'svn diff', invoked by the repos-diff editor. ***/

/* Diff writer state */
typedef struct diff_writer_info_t
{
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

  /* Set this if you want diff output even for binary files. */
  svn_boolean_t force_binary;

  /* The directory that diff target paths should be considered as
     relative to for output generation (see issue #2723). */
  const char *relative_to_dir;

  /* Whether property differences are ignored. */
  svn_boolean_t ignore_properties;

  /* Whether to show only property changes. */
  svn_boolean_t properties_only;

  /* Whether we're producing a git-style diff. */
  svn_boolean_t use_git_diff_format;

  /* Whether addition of a file is summarized versus showing a full diff. */
  svn_boolean_t no_diff_added;

  /* Whether deletion of a file is summarized versus showing a full diff. */
  svn_boolean_t no_diff_deleted;

  /* Whether to ignore copyfrom information when showing adds */
  svn_boolean_t show_copies_as_adds;

  /* Whether to show mergeinfo prop changes in human-readable form */
  svn_boolean_t pretty_print_mergeinfo;

  /* Empty files for creating diffs or NULL if not used yet */
  const char *empty_file;

  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  svn_client__diff_driver_info_t ddi;
} diff_writer_info_t;

/* An helper for diff_dir_props_changed, diff_file_changed and diff_file_added
 */
static svn_error_t *
diff_props_changed(const char *diff_relpath,
                   svn_revnum_t rev1,
                   svn_revnum_t rev2,
                   const apr_array_header_t *propchanges,
                   apr_hash_t *left_props,
                   apr_hash_t *right_props,
                   svn_boolean_t show_diff_header,
                   diff_writer_info_t *dwi,
                   apr_pool_t *scratch_pool)
{
  apr_array_header_t *props;

  /* If property differences are ignored, there's nothing to do. */
  if (dwi->ignore_properties)
    return SVN_NO_ERROR;

  SVN_ERR(svn_categorize_props(propchanges, NULL, NULL, &props,
                               scratch_pool));

  if (props->nelts > 0)
    {
      /* We're using the revnums from the dwi since there's
       * no revision argument to the svn_wc_diff_callback_t
       * dir_props_changed(). */
      SVN_ERR(display_prop_diffs(props, left_props, right_props,
                                 diff_relpath,
                                 rev1,
                                 rev2,
                                 dwi->header_encoding,
                                 dwi->outstream,
                                 dwi->relative_to_dir,
                                 show_diff_header,
                                 dwi->use_git_diff_format,
                                 dwi->pretty_print_mergeinfo,
                                 &dwi->ddi,
                                 dwi->cancel_func,
                                 dwi->cancel_baton,
                                 scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Given a file ORIG_TMPFILE, return a path to a temporary file that lives at
 * least as long as RESULT_POOL, containing the git-like represention of
 * ORIG_TMPFILE */
static svn_error_t *
transform_link_to_git(const char **new_tmpfile,
                      const char **git_sha1,
                      const char *orig_tmpfile,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  apr_file_t *orig;
  apr_file_t *gitlike;
  svn_stringbuf_t *line;

  *git_sha1 = NULL;

  SVN_ERR(svn_io_file_open(&orig, orig_tmpfile, APR_READ, APR_OS_DEFAULT,
                           scratch_pool));
  SVN_ERR(svn_io_open_unique_file3(&gitlike, new_tmpfile, NULL,
                                   svn_io_file_del_on_pool_cleanup,
                                   result_pool, scratch_pool));

  SVN_ERR(svn_io_file_readline(orig, &line, NULL, NULL, 2 * APR_PATH_MAX + 2,
                               scratch_pool, scratch_pool));

  if (line->len > 5 && !strncmp(line->data, "link ", 5))
    {
      const char *sz_str;
      svn_checksum_t *checksum;

      svn_stringbuf_remove(line, 0, 5);

      SVN_ERR(svn_io_file_write_full(gitlike, line->data, line->len,
                                     NULL, scratch_pool));

      /* git calculates the sha over "blob X\0" + the actual data,
         where X is the decimal size of the blob. */
      sz_str = apr_psprintf(scratch_pool, "blob %u", (unsigned int)line->len);
      svn_stringbuf_insert(line, 0, sz_str, strlen(sz_str) + 1);

      SVN_ERR(svn_checksum(&checksum, svn_checksum_sha1,
                           line->data, line->len, scratch_pool));

      *git_sha1 = svn_checksum_to_cstring(checksum, result_pool);
    }
  else
    {
      /* Not a link... so can't convert */
      *new_tmpfile = apr_pstrdup(result_pool, orig_tmpfile);
    }

  SVN_ERR(svn_io_file_close(orig, scratch_pool));
  SVN_ERR(svn_io_file_close(gitlike, scratch_pool));
  return SVN_NO_ERROR;
}

/* Show differences between TMPFILE1 and TMPFILE2. DIFF_RELPATH, REV1, and
   REV2 are used in the headers to indicate the file and revisions.

   If either side has an svn:mime-type property that indicates 'binary'
   content, then if DWI->force_binary is set, attempt to produce the
   diff in the usual way, otherwise produce a 'GIT binary diff' in git mode
   or print a warning message in non-git mode.

   If FORCE_DIFF is TRUE, always write a diff, even for empty diffs.

   Set *WROTE_HEADER to TRUE if a diff header was written */
static svn_error_t *
diff_content_changed(svn_boolean_t *wrote_header,
                     const char *diff_relpath,
                     const char *tmpfile1,
                     const char *tmpfile2,
                     svn_revnum_t rev1,
                     svn_revnum_t rev2,
                     apr_hash_t *left_props,
                     apr_hash_t *right_props,
                     svn_diff_operation_kind_t operation,
                     svn_boolean_t force_diff,
                     const char *copyfrom_path,
                     svn_revnum_t copyfrom_rev,
                     diff_writer_info_t *dwi,
                     apr_pool_t *scratch_pool)
{
  const char *rel_to_dir = dwi->relative_to_dir;
  svn_stream_t *outstream = dwi->outstream;
  const char *label1, *label2;
  svn_boolean_t mt1_binary = FALSE, mt2_binary = FALSE;
  const char *index_path;
  const char *label_path1, *label_path2;
  const char *mimetype1 = svn_prop_get_value(left_props, SVN_PROP_MIME_TYPE);
  const char *mimetype2 = svn_prop_get_value(right_props, SVN_PROP_MIME_TYPE);
  const char *index_shas = NULL;

  /* If only property differences are shown, there's nothing to do. */
  if (dwi->properties_only)
    return SVN_NO_ERROR;

  /* Generate the diff headers. */
  SVN_ERR(adjust_paths_for_diff_labels(&index_path,
                                       &label_path1, &label_path2,
                                       rel_to_dir, dwi->ddi.anchor,
                                       diff_relpath,
                                       dwi->ddi.orig_path_1, dwi->ddi.orig_path_2,
                                       scratch_pool, scratch_pool));

  label1 = diff_label(label_path1, rev1, scratch_pool);
  label2 = diff_label(label_path2, rev2, scratch_pool);

  /* Possible easy-out: if either mime-type is binary and force was not
     specified, don't attempt to generate a viewable diff at all.
     Print a warning and exit. */
  if (mimetype1)
    mt1_binary = svn_mime_type_is_binary(mimetype1);
  if (mimetype2)
    mt2_binary = svn_mime_type_is_binary(mimetype2);

  if (dwi->use_git_diff_format)
    {
      const char *l_hash = NULL;
      const char *r_hash = NULL;

      /* Change symlinks to their 'git like' plain format */
      if (svn_prop_get_value(left_props, SVN_PROP_SPECIAL))
        SVN_ERR(transform_link_to_git(&tmpfile1, &l_hash, tmpfile1,
                                      scratch_pool, scratch_pool));
      if (svn_prop_get_value(right_props, SVN_PROP_SPECIAL))
        SVN_ERR(transform_link_to_git(&tmpfile2, &r_hash, tmpfile2,
                                      scratch_pool, scratch_pool));

      if (l_hash && r_hash)
        {
          /* The symlink has changed. But we can't tell the user of the
             diff whether we are writing git diffs or svn diffs of the
             symlink... except when we add a git-like index line */

          l_hash = apr_pstrndup(scratch_pool, l_hash, 8);
          r_hash = apr_pstrndup(scratch_pool, r_hash, 8);

          index_shas = apr_psprintf(scratch_pool, "%8s..%8s",
                                    l_hash, r_hash);
        }
    }

  if (! dwi->force_binary && (mt1_binary || mt2_binary))
    {
      /* Print out the diff header. */
      SVN_ERR(print_diff_index_header(outstream, dwi->header_encoding,
                                      index_path, "", scratch_pool));
      *wrote_header = TRUE;

      /* ### Print git diff headers. */

      if (dwi->use_git_diff_format)
        {
          svn_stream_t *left_stream;
          svn_stream_t *right_stream;

          SVN_ERR(print_git_diff_header(outstream,
                                        &label1, &label2,
                                        operation,
                                        rev1, rev2,
                                        diff_relpath,
                                        copyfrom_path, copyfrom_rev,
                                        left_props, right_props,
                                        index_shas,
                                        dwi->header_encoding,
                                        &dwi->ddi, scratch_pool));

          SVN_ERR(svn_stream_open_readonly(&left_stream, tmpfile1,
                                           scratch_pool, scratch_pool));
          SVN_ERR(svn_stream_open_readonly(&right_stream, tmpfile2,
                                           scratch_pool, scratch_pool));
          SVN_ERR(svn_diff_output_binary(outstream,
                                         left_stream, right_stream,
                                         dwi->cancel_func, dwi->cancel_baton,
                                         scratch_pool));
        }
      else
        {
          SVN_ERR(svn_stream_printf_from_utf8(outstream,
                   dwi->header_encoding, scratch_pool,
                   _("Cannot display: file marked as a binary type.%s"),
                   APR_EOL_STR));

          if (mt1_binary && !mt2_binary)
            SVN_ERR(svn_stream_printf_from_utf8(outstream,
                     dwi->header_encoding, scratch_pool,
                     "svn:mime-type = %s" APR_EOL_STR, mimetype1));
          else if (mt2_binary && !mt1_binary)
            SVN_ERR(svn_stream_printf_from_utf8(outstream,
                     dwi->header_encoding, scratch_pool,
                     "svn:mime-type = %s" APR_EOL_STR, mimetype2));
          else if (mt1_binary && mt2_binary)
            {
              if (strcmp(mimetype1, mimetype2) == 0)
                SVN_ERR(svn_stream_printf_from_utf8(outstream,
                         dwi->header_encoding, scratch_pool,
                         "svn:mime-type = %s" APR_EOL_STR,
                         mimetype1));
              else
                SVN_ERR(svn_stream_printf_from_utf8(outstream,
                         dwi->header_encoding, scratch_pool,
                         "svn:mime-type = (%s, %s)" APR_EOL_STR,
                         mimetype1, mimetype2));
            }
        }

      /* Exit early. */
      return SVN_NO_ERROR;
    }


  if (dwi->diff_cmd)
    {
      svn_stream_t *errstream = dwi->errstream;
      apr_file_t *outfile;
      apr_file_t *errfile;
      const char *outfilename;
      const char *errfilename;
      svn_stream_t *stream;
      int exitcode;

      /* Print out the diff header. */
      SVN_ERR(print_diff_index_header(outstream, dwi->header_encoding,
                                      index_path, "", scratch_pool));
      *wrote_header = TRUE;

      /* ### Do we want to add git diff headers here too? I'd say no. The
       * ### 'Index' and '===' line is something subversion has added. The rest
       * ### is up to the external diff application. We may be dealing with
       * ### a non-git compatible diff application.*/

      /* We deal in streams, but svn_io_run_diff2() deals in file handles,
         so we may need to make temporary files and then copy the contents
         to our stream. */
      outfile = svn_stream__aprfile(outstream);
      if (outfile)
        outfilename = NULL;
      else
        SVN_ERR(svn_io_open_unique_file3(&outfile, &outfilename, NULL,
                                         svn_io_file_del_on_pool_cleanup,
                                         scratch_pool, scratch_pool));

      errfile = svn_stream__aprfile(errstream);
      if (errfile)
        errfilename = NULL;
      else
        SVN_ERR(svn_io_open_unique_file3(&errfile, &errfilename, NULL,
                                         svn_io_file_del_on_pool_cleanup,
                                         scratch_pool, scratch_pool));

      SVN_ERR(svn_io_run_diff2(".",
                               dwi->options.for_external.argv,
                               dwi->options.for_external.argc,
                               label1, label2,
                               tmpfile1, tmpfile2,
                               &exitcode, outfile, errfile,
                               dwi->diff_cmd, scratch_pool));

      /* Now, open and copy our files to our output streams. */
      if (outfilename)
        {
          SVN_ERR(svn_io_file_close(outfile, scratch_pool));
          SVN_ERR(svn_stream_open_readonly(&stream, outfilename,
                                           scratch_pool, scratch_pool));
          SVN_ERR(svn_stream_copy3(stream, svn_stream_disown(outstream,
                                                             scratch_pool),
                                   NULL, NULL, scratch_pool));
        }
      if (errfilename)
        {
          SVN_ERR(svn_io_file_close(errfile, scratch_pool));
          SVN_ERR(svn_stream_open_readonly(&stream, errfilename,
                                           scratch_pool, scratch_pool));
          SVN_ERR(svn_stream_copy3(stream, svn_stream_disown(errstream,
                                                             scratch_pool),
                                   NULL, NULL, scratch_pool));
        }
    }
  else   /* use libsvn_diff to generate the diff  */
    {
      svn_diff_t *diff;

      SVN_ERR(svn_diff_file_diff_2(&diff, tmpfile1, tmpfile2,
                                   dwi->options.for_internal,
                                   scratch_pool));

      if (force_diff
          || dwi->use_git_diff_format
          || svn_diff_contains_diffs(diff))
        {
          /* Print out the diff header. */
          SVN_ERR(print_diff_index_header(outstream, dwi->header_encoding,
                                          index_path, "", scratch_pool));
          *wrote_header = TRUE;

          if (dwi->use_git_diff_format)
            {
              SVN_ERR(print_git_diff_header(outstream,
                                            &label1, &label2,
                                            operation,
                                            rev1, rev2,
                                            diff_relpath,
                                            copyfrom_path, copyfrom_rev,
                                            left_props, right_props,
                                            index_shas,
                                            dwi->header_encoding,
                                            &dwi->ddi, scratch_pool));
            }

          /* Output the actual diff */
          if (force_diff || svn_diff_contains_diffs(diff))
            SVN_ERR(svn_diff_file_output_unified4(outstream, diff,
                     tmpfile1, tmpfile2, label1, label2,
                     dwi->header_encoding, rel_to_dir,
                     dwi->options.for_internal->show_c_function,
                     dwi->options.for_internal->context_size,
                     dwi->cancel_func, dwi->cancel_baton,
                     scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t callback. */
static svn_error_t *
diff_file_changed(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const svn_diff_source_t *right_source,
                  const char *left_file,
                  const char *right_file,
                  /*const*/ apr_hash_t *left_props,
                  /*const*/ apr_hash_t *right_props,
                  svn_boolean_t file_modified,
                  const apr_array_header_t *prop_changes,
                  void *file_baton,
                  const struct svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  diff_writer_info_t *dwi = processor->baton;
  svn_boolean_t wrote_header = FALSE;

  if (file_modified)
    SVN_ERR(diff_content_changed(&wrote_header, relpath,
                                 left_file, right_file,
                                 left_source->revision,
                                 right_source->revision,
                                 left_props, right_props,
                                 svn_diff_op_modified, FALSE,
                                 NULL,
                                 SVN_INVALID_REVNUM, dwi,
                                 scratch_pool));
  if (prop_changes->nelts > 0)
    SVN_ERR(diff_props_changed(relpath,
                               left_source->revision,
                               right_source->revision, prop_changes,
                               left_props, right_props, !wrote_header,
                               dwi, scratch_pool));
  return SVN_NO_ERROR;
}

/* Because the repos-diff editor passes at least one empty file to
   each of these next two functions, they can be dumb wrappers around
   the main workhorse routine. */

/* An svn_diff_tree_processor_t callback. */
static svn_error_t *
diff_file_added(const char *relpath,
                const svn_diff_source_t *copyfrom_source,
                const svn_diff_source_t *right_source,
                const char *copyfrom_file,
                const char *right_file,
                /*const*/ apr_hash_t *copyfrom_props,
                /*const*/ apr_hash_t *right_props,
                void *file_baton,
                const struct svn_diff_tree_processor_t *processor,
                apr_pool_t *scratch_pool)
{
  diff_writer_info_t *dwi = processor->baton;
  svn_boolean_t wrote_header = FALSE;
  const char *left_file;
  apr_hash_t *left_props;
  apr_array_header_t *prop_changes;

  if (dwi->no_diff_added)
    {
      const char *index_path = relpath;

      if (dwi->ddi.anchor)
        index_path = svn_dirent_join(dwi->ddi.anchor, relpath,
                                     scratch_pool);

      SVN_ERR(print_diff_index_header(dwi->outstream, dwi->header_encoding,
                                      index_path, " (added)",
                                      scratch_pool));
      wrote_header = TRUE;
      return SVN_NO_ERROR;
    }

  /* During repos->wc diff of a copy revision numbers obtained
   * from the working copy are always SVN_INVALID_REVNUM. */
  if (copyfrom_source && !dwi->show_copies_as_adds)
    {
      left_file = copyfrom_file;
      left_props = copyfrom_props ? copyfrom_props : apr_hash_make(scratch_pool);
    }
  else
    {
      if (!dwi->empty_file)
        SVN_ERR(svn_io_open_unique_file3(NULL, &dwi->empty_file,
                                         NULL, svn_io_file_del_on_pool_cleanup,
                                         dwi->pool, scratch_pool));

      left_file = dwi->empty_file;
      left_props = apr_hash_make(scratch_pool);

      copyfrom_source = NULL;
      copyfrom_file = NULL;
    }

  SVN_ERR(svn_prop_diffs(&prop_changes, right_props, left_props, scratch_pool));

  if (copyfrom_source && right_file)
    SVN_ERR(diff_content_changed(&wrote_header, relpath,
                                 left_file, right_file,
                                 copyfrom_source->revision,
                                 right_source->revision,
                                 left_props, right_props,
                                 copyfrom_source->moved_from_relpath
                                    ? svn_diff_op_moved
                                    : svn_diff_op_copied,
                                 TRUE /* force diff output */,
                                 copyfrom_source->moved_from_relpath
                                    ? copyfrom_source->moved_from_relpath
                                    : copyfrom_source->repos_relpath,
                                 copyfrom_source->revision,
                                 dwi, scratch_pool));
  else if (right_file)
    SVN_ERR(diff_content_changed(&wrote_header, relpath,
                                 left_file, right_file,
                                 DIFF_REVNUM_NONEXISTENT,
                                 right_source->revision,
                                 left_props, right_props,
                                 svn_diff_op_added,
                                 TRUE /* force diff output */,
                                 NULL, SVN_INVALID_REVNUM,
                                 dwi, scratch_pool));

  if (prop_changes->nelts > 0)
    SVN_ERR(diff_props_changed(relpath,
                               copyfrom_source ? copyfrom_source->revision
                                               : DIFF_REVNUM_NONEXISTENT,
                               right_source->revision,
                               prop_changes,
                               left_props, right_props,
                               ! wrote_header, dwi, scratch_pool));

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t callback. */
static svn_error_t *
diff_file_deleted(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const char *left_file,
                  /*const*/ apr_hash_t *left_props,
                  void *file_baton,
                  const struct svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  diff_writer_info_t *dwi = processor->baton;

  if (dwi->no_diff_deleted)
    {
      const char *index_path = relpath;

      if (dwi->ddi.anchor)
        index_path = svn_dirent_join(dwi->ddi.anchor, relpath,
                                     scratch_pool);

      SVN_ERR(print_diff_index_header(dwi->outstream, dwi->header_encoding,
                                      index_path, " (deleted)",
                                      scratch_pool));
    }
  else
    {
      svn_boolean_t wrote_header = FALSE;

      if (!dwi->empty_file)
        SVN_ERR(svn_io_open_unique_file3(NULL, &dwi->empty_file,
                                         NULL, svn_io_file_del_on_pool_cleanup,
                                         dwi->pool, scratch_pool));

      if (left_file)
        SVN_ERR(diff_content_changed(&wrote_header, relpath,
                                     left_file, dwi->empty_file,
                                     left_source->revision,
                                     DIFF_REVNUM_NONEXISTENT,
                                     left_props,
                                     NULL,
                                     svn_diff_op_deleted, FALSE,
                                     NULL, SVN_INVALID_REVNUM,
                                     dwi,
                                     scratch_pool));

      if (left_props && apr_hash_count(left_props))
        {
          apr_array_header_t *prop_changes;

          SVN_ERR(svn_prop_diffs(&prop_changes, apr_hash_make(scratch_pool),
                                 left_props, scratch_pool));

          SVN_ERR(diff_props_changed(relpath,
                                     left_source->revision,
                                     DIFF_REVNUM_NONEXISTENT,
                                     prop_changes,
                                     left_props, NULL,
                                     ! wrote_header, dwi, scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks4_t function. */
static svn_error_t *
diff_dir_changed(const char *relpath,
                 const svn_diff_source_t *left_source,
                 const svn_diff_source_t *right_source,
                 /*const*/ apr_hash_t *left_props,
                 /*const*/ apr_hash_t *right_props,
                 const apr_array_header_t *prop_changes,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  diff_writer_info_t *dwi = processor->baton;

  SVN_ERR(diff_props_changed(relpath,
                             left_source->revision,
                             right_source->revision,
                             prop_changes,
                             left_props, right_props,
                             TRUE /* show_diff_header */,
                             dwi,
                             scratch_pool));

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t callback. */
static svn_error_t *
diff_dir_added(const char *relpath,
               const svn_diff_source_t *copyfrom_source,
               const svn_diff_source_t *right_source,
               /*const*/ apr_hash_t *copyfrom_props,
               /*const*/ apr_hash_t *right_props,
               void *dir_baton,
               const struct svn_diff_tree_processor_t *processor,
               apr_pool_t *scratch_pool)
{
  diff_writer_info_t *dwi = processor->baton;
  apr_hash_t *left_props;
  apr_array_header_t *prop_changes;

  if (dwi->no_diff_added)
    return SVN_NO_ERROR;

  if (copyfrom_source && !dwi->show_copies_as_adds)
    {
      left_props = copyfrom_props ? copyfrom_props
                                  : apr_hash_make(scratch_pool);
    }
  else
    {
      left_props = apr_hash_make(scratch_pool);
      copyfrom_source = NULL;
    }

  SVN_ERR(svn_prop_diffs(&prop_changes, right_props, left_props,
                         scratch_pool));

  return svn_error_trace(diff_props_changed(relpath,
                                            copyfrom_source ? copyfrom_source->revision
                                                            : DIFF_REVNUM_NONEXISTENT,
                                            right_source->revision,
                                            prop_changes,
                                            left_props, right_props,
                                            TRUE /* show_diff_header */,
                                            dwi,
                                            scratch_pool));
}

/* An svn_diff_tree_processor_t callback. */
static svn_error_t *
diff_dir_deleted(const char *relpath,
                 const svn_diff_source_t *left_source,
                 /*const*/ apr_hash_t *left_props,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  diff_writer_info_t *dwi = processor->baton;
  apr_array_header_t *prop_changes;
  apr_hash_t *right_props;

  if (dwi->no_diff_deleted)
    return SVN_NO_ERROR;

  right_props = apr_hash_make(scratch_pool);
  SVN_ERR(svn_prop_diffs(&prop_changes, right_props,
                         left_props, scratch_pool));

  SVN_ERR(diff_props_changed(relpath,
                             left_source->revision,
                             DIFF_REVNUM_NONEXISTENT,
                             prop_changes,
                             left_props, right_props,
                             TRUE /* show_diff_header */,
                             dwi,
                             scratch_pool));

  return SVN_NO_ERROR;
}

/* Initialize DWI.diff_cmd and DWI.options,
 * according to OPTIONS and CONFIG.  CONFIG and OPTIONS may be null.
 * Allocate the fields in RESULT_POOL, which should be at least as long-lived
 * as the pool DWI itself is allocated in.
 */
static svn_error_t *
create_diff_writer_info(diff_writer_info_t *dwi,
                        const apr_array_header_t *options,
                        apr_hash_t *config, apr_pool_t *result_pool)
{
  const char *diff_cmd = NULL;

  /* See if there is a diff command and/or diff arguments. */
  if (config)
    {
      svn_config_t *cfg = svn_hash_gets(config, SVN_CONFIG_CATEGORY_CONFIG);
      svn_config_get(cfg, &diff_cmd, SVN_CONFIG_SECTION_HELPERS,
                     SVN_CONFIG_OPTION_DIFF_CMD, NULL);
      if (options == NULL)
        {
          const char *diff_extensions;
          svn_config_get(cfg, &diff_extensions, SVN_CONFIG_SECTION_HELPERS,
                         SVN_CONFIG_OPTION_DIFF_EXTENSIONS, NULL);
          if (diff_extensions)
            options = svn_cstring_split(diff_extensions, " \t\n\r", TRUE,
                                        result_pool);
        }
    }

  if (options == NULL)
    options = apr_array_make(result_pool, 0, sizeof(const char *));

  if (diff_cmd)
    SVN_ERR(svn_path_cstring_to_utf8(&dwi->diff_cmd, diff_cmd,
                                     result_pool));
  else
    dwi->diff_cmd = NULL;

  /* If there was a command, arrange options to pass to it. */
  if (dwi->diff_cmd)
    {
      const char **argv = NULL;
      int argc = options->nelts;
      if (argc)
        {
          int i;
          argv = apr_palloc(result_pool, argc * sizeof(char *));
          for (i = 0; i < argc; i++)
            SVN_ERR(svn_utf_cstring_to_utf8(&argv[i],
                      APR_ARRAY_IDX(options, i, const char *), result_pool));
        }
      dwi->options.for_external.argv = argv;
      dwi->options.for_external.argc = argc;
    }
  else  /* No command, so arrange options for internal invocation instead. */
    {
      dwi->options.for_internal = svn_diff_file_options_create(result_pool);
      SVN_ERR(svn_diff_file_options_parse(dwi->options.for_internal,
                                          options, result_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__get_diff_writer_svn(
                svn_diff_tree_processor_t **diff_processor,
                svn_client__diff_driver_info_t **ddi_p,
                const char *anchor,
                const char *orig_path_1,
                const char *orig_path_2,
                const apr_array_header_t *options,
                const char *relative_to_dir,
                svn_boolean_t no_diff_added,
                svn_boolean_t no_diff_deleted,
                svn_boolean_t show_copies_as_adds,
                svn_boolean_t ignore_content_type,
                svn_boolean_t ignore_properties,
                svn_boolean_t properties_only,
                svn_boolean_t use_git_diff_format,
                svn_boolean_t pretty_print_mergeinfo,
                const char *header_encoding,
                svn_stream_t *outstream,
                svn_stream_t *errstream,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  diff_writer_info_t *dwi = apr_pcalloc(pool, sizeof(*dwi));
  svn_diff_tree_processor_t *processor;

  /* setup callback and baton */

  SVN_ERR(create_diff_writer_info(dwi, options,
                                  ctx->config, pool));
  dwi->pool = pool;
  dwi->outstream = outstream;
  dwi->errstream = errstream;
  dwi->header_encoding = header_encoding;

  dwi->force_binary = ignore_content_type;
  dwi->ignore_properties = ignore_properties;
  dwi->properties_only = properties_only;
  dwi->relative_to_dir = relative_to_dir;
  dwi->use_git_diff_format = use_git_diff_format;
  dwi->no_diff_added = no_diff_added;
  dwi->no_diff_deleted = no_diff_deleted;
  dwi->show_copies_as_adds = show_copies_as_adds;
  dwi->pretty_print_mergeinfo = pretty_print_mergeinfo;

  dwi->cancel_func = ctx->cancel_func;
  dwi->cancel_baton = ctx->cancel_baton;

  dwi->ddi.wc_ctx = ctx->wc_ctx;
  dwi->ddi.session_relpath = NULL;
  dwi->ddi.anchor = anchor;
  dwi->ddi.orig_path_1 = orig_path_1;
  dwi->ddi.orig_path_2 = orig_path_2;

  processor = svn_diff__tree_processor_create(dwi, pool);

  processor->dir_added = diff_dir_added;
  processor->dir_changed = diff_dir_changed;
  processor->dir_deleted = diff_dir_deleted;

  processor->file_added = diff_file_added;
  processor->file_changed = diff_file_changed;
  processor->file_deleted = diff_file_deleted;

  *diff_processor = processor;
  *ddi_p = &dwi->ddi;
  return SVN_NO_ERROR;
}
