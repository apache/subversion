/*
 * diff.c: comparing
 *
 * ====================================================================
 * Copyright (c) 2000-2007, 2009 CollabNet.  All rights reserved.
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
#include "svn_path.h"
#include "svn_io.h"
#include "svn_utf.h"
#include "svn_pools.h"
#include "svn_config.h"
#include "svn_props.h"
#include "svn_time.h"
#include "svn_sorts.h"
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

/* Wrapper for apr_file_printf(), which see.  FORMAT is a utf8-encoded
   string after it is formatted, so this function can convert it to
   ENCODING before printing. */
static svn_error_t *
file_printf_from_utf8(apr_file_t *fptr, const char *encoding,
                      const char *format, ...)
  __attribute__ ((format(printf, 3, 4)));
static svn_error_t *
file_printf_from_utf8(apr_file_t *fptr, const char *encoding,
                      const char *format, ...)
{
  va_list ap;
  const char *buf, *buf_apr;

  va_start(ap, format);
  buf = apr_pvsprintf(apr_file_pool_get(fptr), format, ap);
  va_end(ap);

  SVN_ERR(svn_utf_cstring_from_utf8_ex2(&buf_apr, buf, encoding,
                                        apr_file_pool_get(fptr)));

  return svn_io_file_write_full(fptr, buf_apr, strlen(buf_apr),
                                NULL, apr_file_pool_get(fptr));
}


/* A helper function for display_prop_diffs.  Output the differences between
   the mergeinfo stored in ORIG_MERGEINFO_VAL and NEW_MERGEINFO_VAL in a
   human-readable form to FILE, using ENCODING.  Use POOL for temporary
   allocations. */
static svn_error_t *
display_mergeinfo_diff(const char *old_mergeinfo_val,
                       const char *new_mergeinfo_val,
                       const char *encoding,
                       apr_file_t *file,
                       apr_pool_t *pool)
{
  apr_hash_t *old_mergeinfo_hash, *new_mergeinfo_hash, *added, *deleted;
  apr_hash_index_t *hi;
  const char *from_path;
  apr_array_header_t *merge_revarray;

  if (old_mergeinfo_val)
    SVN_ERR(svn_mergeinfo_parse(&old_mergeinfo_hash, old_mergeinfo_val, pool));
  else
    old_mergeinfo_hash = NULL;

  if (new_mergeinfo_val)
    SVN_ERR(svn_mergeinfo_parse(&new_mergeinfo_hash, new_mergeinfo_val, pool));
  else
    new_mergeinfo_hash = NULL;

  SVN_ERR(svn_mergeinfo_diff(&deleted, &added, old_mergeinfo_hash,
                             new_mergeinfo_hash,
                             TRUE, pool));

  for (hi = apr_hash_first(pool, deleted);
       hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      svn_string_t *merge_revstr;

      apr_hash_this(hi, &key, NULL, &val);
      from_path = key;
      merge_revarray = val;

      SVN_ERR(svn_rangelist_to_string(&merge_revstr, merge_revarray, pool));

      SVN_ERR(file_printf_from_utf8(file, encoding,
                                    _("   Reverse-merged %s:r%s%s"),
                                    from_path, merge_revstr->data,
                                    APR_EOL_STR));
    }

  for (hi = apr_hash_first(pool, added);
       hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      svn_string_t *merge_revstr;

      apr_hash_this(hi, &key, NULL, &val);
      from_path = key;
      merge_revarray = val;

      SVN_ERR(svn_rangelist_to_string(&merge_revstr, merge_revarray, pool));

      SVN_ERR(file_printf_from_utf8(file, encoding,
                                    _("   Merged %s:r%s%s"),
                                    from_path, merge_revstr->data,
                                    APR_EOL_STR));
    }

  return SVN_NO_ERROR;
}

#define MAKE_ERR_BAD_RELATIVE_PATH(path, relative_to_dir) \
        svn_error_createf(SVN_ERR_BAD_RELATIVE_PATH, NULL, \
                          _("Path '%s' must be an immediate child of " \
                            "the directory '%s'"), path, relative_to_dir)

/* A helper func that writes out verbal descriptions of property diffs
   to FILE.   Of course, the apr_file_t will probably be the 'outfile'
   passed to svn_client_diff4, which is probably stdout. */
static svn_error_t *
display_prop_diffs(const apr_array_header_t *propchanges,
                   apr_hash_t *original_props,
                   const char *path,
                   const char *encoding,
                   apr_file_t *file,
                   const char *relative_to_dir,
                   apr_pool_t *pool)
{
  int i;

  if (relative_to_dir)
    {
      /* Possibly adjust the path shown in the output (see issue #2723). */
      const char *child_path = svn_path_is_child(relative_to_dir, path, pool);

      if (child_path)
        path = child_path;
      else if (!svn_path_compare_paths(relative_to_dir, path))
        path = ".";
      else
        return MAKE_ERR_BAD_RELATIVE_PATH(path, relative_to_dir);
    }

  SVN_ERR(file_printf_from_utf8(file, encoding,
                                _("%sProperty changes on: %s%s"),
                                APR_EOL_STR,
                                svn_path_local_style(path, pool),
                                APR_EOL_STR));

  SVN_ERR(file_printf_from_utf8(file, encoding, "%s" APR_EOL_STR,
                                under_string));

  for (i = 0; i < propchanges->nelts; i++)
    {
      const char *header_fmt;
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

      if (! original_value)
        header_fmt = _("Added: %s%s");
      else if (! propchange->value)
        header_fmt = _("Deleted: %s%s");
      else
        header_fmt = _("Modified: %s%s");
      SVN_ERR(file_printf_from_utf8(file, encoding, header_fmt,
                                    propchange->name, APR_EOL_STR));

      if (strcmp(propchange->name, SVN_PROP_MERGEINFO) == 0)
        {
          const char *orig = original_value ? original_value->data : NULL;
          const char *val = propchange->value ? propchange->value->data : NULL;

          SVN_ERR(display_mergeinfo_diff(orig, val, encoding, file, pool));

          continue;
        }

      /* For now, we have a rather simple heuristic: if this is an
         "svn:" property, then assume the value is UTF-8 and must
         therefore be converted before printing.  Otherwise, just
         print whatever's there and hope for the best. */
      {
        svn_boolean_t val_is_utf8 = svn_prop_is_svn_prop(propchange->name);

        if (original_value != NULL)
          {
            if (val_is_utf8)
              {
                SVN_ERR(file_printf_from_utf8
                        (file, encoding,
                         "   - %s" APR_EOL_STR, original_value->data));
              }
            else
              {
                /* ### todo: check for error? */
                apr_file_printf
                  (file, "   - %s" APR_EOL_STR, original_value->data);
              }
          }

        if (propchange->value != NULL)
          {
            if (val_is_utf8)
              {
                SVN_ERR(file_printf_from_utf8
                        (file, encoding, "   + %s" APR_EOL_STR,
                         propchange->value->data));
              }
            else
              {
                /* ### todo: check for error? */
                apr_file_printf(file, "   + %s" APR_EOL_STR,
                                propchange->value->data);
              }
          }
      }
    }

  /* ### todo [issue #1533]: Use file_printf_from_utf8() to convert this
     to native encoding, at least conditionally?  Or is it better to
     have under_string always output the same eol, so programs can
     find it consistently?  Also, what about checking for error? */
  apr_file_printf(file, APR_EOL_STR);

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
  apr_file_t *outfile;
  apr_file_t *errfile;

  const char *header_encoding;

  /* The original targets passed to the diff command.  We may need
     these to construct distinctive diff labels when comparing the
     same relative path in the same revision, under different anchors
     (for example, when comparing a trunk against a branch). */
  const char *orig_path_1;
  const char *orig_path_2;

  /* These are the numeric representations of the revisions passed to
     svn_client_diff4, either may be SVN_INVALID_REVNUM.  We need these
     because some of the svn_wc_diff_callbacks3_t don't get revision
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
};


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

/* An svn_wc_diff_callbacks3_t function.  Used for both file and directory
   property diffs. */
static svn_error_t *
diff_props_changed(svn_wc_adm_access_t *adm_access,
                   svn_wc_notify_state_t *state,
                   svn_boolean_t *tree_conflicted,
                   const char *path,
                   const apr_array_header_t *propchanges,
                   apr_hash_t *original_props,
                   void *diff_baton)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;
  apr_array_header_t *props;
  apr_pool_t *subpool = svn_pool_create(diff_cmd_baton->pool);

  SVN_ERR(svn_categorize_props(propchanges, NULL, NULL, &props, subpool));

  if (props->nelts > 0)
    SVN_ERR(display_prop_diffs(props, original_props, path,
                               diff_cmd_baton->header_encoding,
                               diff_cmd_baton->outfile,
                               diff_cmd_baton->relative_to_dir,
                               subpool));

  if (state)
    *state = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
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
                     void *diff_baton)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;
  int exitcode;
  apr_pool_t *subpool = svn_pool_create(diff_cmd_baton->pool);
  svn_stream_t *os;
  const char *rel_to_dir = diff_cmd_baton->relative_to_dir;
  apr_file_t *errfile = diff_cmd_baton->errfile;
  const char *label1, *label2;
  svn_boolean_t mt1_binary = FALSE, mt2_binary = FALSE;
  const char *path1, *path2;
  apr_size_t len;

  /* Get a stream from our output file. */
  os = svn_stream_from_aprfile2(diff_cmd_baton->outfile, TRUE, subpool);

  /* Generate the diff headers. */

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

  path1 = diff_cmd_baton->orig_path_1;
  path2 = diff_cmd_baton->orig_path_2;
  len = strlen(svn_path_get_longest_ancestor(path1, path2, subpool));
  path1 = path1 + len;
  path2 = path2 + len;

  /* ### Should diff labels print paths in local style?  Is there
     already a standard for this?  In any case, this code depends on
     a particular style, so not calling svn_path_local_style() on the
     paths below.*/
  if (path1[0] == '\0')
    path1 = apr_psprintf(subpool, "%s", path);
  else if (path1[0] == '/')
    path1 = apr_psprintf(subpool, "%s\t(...%s)", path, path1);
  else
    path1 = apr_psprintf(subpool, "%s\t(.../%s)", path, path1);

  if (path2[0] == '\0')
    path2 = apr_psprintf(subpool, "%s", path);
  else if (path2[0] == '/')
    path2 = apr_psprintf(subpool, "%s\t(...%s)", path, path2);
  else
    path2 = apr_psprintf(subpool, "%s\t(.../%s)", path, path2);

  if (diff_cmd_baton->relative_to_dir)
    {
      /* Possibly adjust the paths shown in the output (see issue #2723). */
      const char *child_path = svn_path_is_child(rel_to_dir, path, subpool);

      if (child_path)
        path = child_path;
      else if (!svn_path_compare_paths(rel_to_dir, path))
        path = ".";
      else
        return MAKE_ERR_BAD_RELATIVE_PATH(path, rel_to_dir);

      child_path = svn_path_is_child(rel_to_dir, path1, subpool);

      if (child_path)
        path1 = child_path;
      else if (!svn_path_compare_paths(rel_to_dir, path1))
        path1 = ".";
      else
        return MAKE_ERR_BAD_RELATIVE_PATH(path1, rel_to_dir);

      child_path = svn_path_is_child(rel_to_dir, path2, subpool);

      if (child_path)
        path2 = child_path;
      else if (!svn_path_compare_paths(rel_to_dir, path2))
        path2 = ".";
      else
        return MAKE_ERR_BAD_RELATIVE_PATH(path2, rel_to_dir);
    }

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
      SVN_ERR(svn_stream_printf_from_utf8
              (os, diff_cmd_baton->header_encoding, subpool,
               "Index: %s" APR_EOL_STR "%s" APR_EOL_STR, path, equal_string));

      SVN_ERR(svn_stream_printf_from_utf8
              (os, diff_cmd_baton->header_encoding, subpool,
               _("Cannot display: file marked as a binary type.%s"),
               APR_EOL_STR));

      if (mt1_binary && !mt2_binary)
        SVN_ERR(svn_stream_printf_from_utf8
                (os, diff_cmd_baton->header_encoding, subpool,
                 "svn:mime-type = %s" APR_EOL_STR, mimetype1));
      else if (mt2_binary && !mt1_binary)
        SVN_ERR(svn_stream_printf_from_utf8
                (os, diff_cmd_baton->header_encoding, subpool,
                 "svn:mime-type = %s" APR_EOL_STR, mimetype2));
      else if (mt1_binary && mt2_binary)
        {
          if (strcmp(mimetype1, mimetype2) == 0)
            SVN_ERR(svn_stream_printf_from_utf8
                    (os, diff_cmd_baton->header_encoding, subpool,
                     "svn:mime-type = %s" APR_EOL_STR,
                     mimetype1));
          else
            SVN_ERR(svn_stream_printf_from_utf8
                    (os, diff_cmd_baton->header_encoding, subpool,
                     "svn:mime-type = (%s, %s)" APR_EOL_STR,
                     mimetype1, mimetype2));
        }

      /* Exit early. */
      svn_pool_destroy(subpool);
      return SVN_NO_ERROR;
    }


  if (diff_cmd_baton->diff_cmd)
    {
      /* Print out the diff header. */
      SVN_ERR(svn_stream_printf_from_utf8
              (os, diff_cmd_baton->header_encoding, subpool,
               "Index: %s" APR_EOL_STR "%s" APR_EOL_STR, path, equal_string));
      /* Close the stream (flush) */
      SVN_ERR(svn_stream_close(os));

      SVN_ERR(svn_io_run_diff(".",
                              diff_cmd_baton->options.for_external.argv,
                              diff_cmd_baton->options.for_external.argc,
                              label1, label2,
                              tmpfile1, tmpfile2,
                              &exitcode, diff_cmd_baton->outfile, errfile,
                              diff_cmd_baton->diff_cmd, subpool));
    }
  else   /* use libsvn_diff to generate the diff  */
    {
      svn_diff_t *diff;

      SVN_ERR(svn_diff_file_diff_2(&diff, tmpfile1, tmpfile2,
                                   diff_cmd_baton->options.for_internal,
                                   subpool));

      if (svn_diff_contains_diffs(diff) || diff_cmd_baton->force_empty)
        {
          /* Print out the diff header. */
          SVN_ERR(svn_stream_printf_from_utf8
                  (os, diff_cmd_baton->header_encoding, subpool,
                   "Index: %s" APR_EOL_STR "%s" APR_EOL_STR,
                   path, equal_string));
          /* Output the actual diff */
          SVN_ERR(svn_diff_file_output_unified3
                  (os, diff, tmpfile1, tmpfile2, label1, label2,
                   diff_cmd_baton->header_encoding, rel_to_dir,
                   diff_cmd_baton->options.for_internal->show_c_function,
                   subpool));
        }
    }

  /* ### todo: someday we'll need to worry about whether we're going
     to need to write a diff plug-in mechanism that makes use of the
     two paths, instead of just blindly running SVN_CLIENT_DIFF.  */

  /* Destroy the subpool. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function. */
static svn_error_t *
diff_file_changed(svn_wc_adm_access_t *adm_access,
                  svn_wc_notify_state_t *content_state,
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
                  void *diff_baton)
{
  if (tmpfile1)
    SVN_ERR(diff_content_changed(path,
                                 tmpfile1, tmpfile2, rev1, rev2,
                                 mimetype1, mimetype2, diff_baton));
  if (prop_changes->nelts > 0)
    SVN_ERR(diff_props_changed(adm_access, prop_state, tree_conflicted,
                               path, prop_changes,
                               original_props, diff_baton));
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

/* An svn_wc_diff_callbacks3_t function. */
static svn_error_t *
diff_file_added(svn_wc_adm_access_t *adm_access,
                svn_wc_notify_state_t *content_state,
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
                void *diff_baton)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;

  /* We want diff_file_changed to unconditionally show diffs, even if
     the diff is empty (as would be the case if an empty file were
     added.)  It's important, because 'patch' would still see an empty
     diff and create an empty file.  It's also important to let the
     user see that *something* happened. */
  diff_cmd_baton->force_empty = TRUE;

  SVN_ERR(diff_file_changed(adm_access, content_state, prop_state,
                            tree_conflicted, path,
                            tmpfile1, tmpfile2,
                            rev1, rev2,
                            mimetype1, mimetype2,
                            prop_changes, original_props, diff_baton));

  diff_cmd_baton->force_empty = FALSE;

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function. */
static svn_error_t *
diff_file_deleted_with_diff(svn_wc_adm_access_t *adm_access,
                            svn_wc_notify_state_t *state,
                            svn_boolean_t *tree_conflicted,
                            const char *path,
                            const char *tmpfile1,
                            const char *tmpfile2,
                            const char *mimetype1,
                            const char *mimetype2,
                            apr_hash_t *original_props,
                            void *diff_baton)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;

  /* We don't list all the deleted properties. */
  return diff_file_changed(adm_access, state, NULL, tree_conflicted, path,
                           tmpfile1, tmpfile2,
                           diff_cmd_baton->revnum1, diff_cmd_baton->revnum2,
                           mimetype1, mimetype2,
                           apr_array_make(diff_cmd_baton->pool, 1,
                                          sizeof(svn_prop_t)),
                           apr_hash_make(diff_cmd_baton->pool), diff_baton);
}

/* An svn_wc_diff_callbacks3_t function. */
static svn_error_t *
diff_file_deleted_no_diff(svn_wc_adm_access_t *adm_access,
                          svn_wc_notify_state_t *state,
                          svn_boolean_t *tree_conflicted,
                          const char *path,
                          const char *tmpfile1,
                          const char *tmpfile2,
                          const char *mimetype1,
                          const char *mimetype2,
                          apr_hash_t *original_props,
                          void *diff_baton)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;

  if (state)
    *state = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return file_printf_from_utf8
          (diff_cmd_baton->outfile,
           diff_cmd_baton->header_encoding,
           "Index: %s (deleted)" APR_EOL_STR "%s" APR_EOL_STR,
           path, equal_string);
}

/* An svn_wc_diff_callbacks3_t function. */
static svn_error_t *
diff_dir_added(svn_wc_adm_access_t *adm_access,
               svn_wc_notify_state_t *state,
               svn_boolean_t *tree_conflicted,
               const char *path,
               svn_revnum_t rev,
               void *diff_baton)
{
  if (state)
    *state = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;

  /* Do nothing. */

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function. */
static svn_error_t *
diff_dir_deleted(svn_wc_adm_access_t *adm_access,
                 svn_wc_notify_state_t *state,
                 svn_boolean_t *tree_conflicted,
                 const char *path,
                 void *diff_baton)
{
  if (state)
    *state = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;

  /* Do nothing. */

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function. */
static svn_error_t *
diff_dir_opened(svn_wc_adm_access_t *adm_access,
                svn_boolean_t *tree_conflicted,
                const char *path,
                svn_revnum_t rev,
                void *diff_baton)
{
  if (tree_conflicted)
    *tree_conflicted = FALSE;

  /* Do nothing. */

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function. */
static svn_error_t *
diff_dir_closed(svn_wc_adm_access_t *adm_access,
                svn_wc_notify_state_t *contentstate,
                svn_wc_notify_state_t *propstate,
                svn_boolean_t *tree_conflicted,
                const char *path,
                void *diff_baton)
{
  if (contentstate)
    *contentstate = svn_wc_notify_state_unknown;
  if (propstate)
    *propstate = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;

  /* Do nothing. */

  return SVN_NO_ERROR;
}


/*-----------------------------------------------------------------*/

/** The logic behind 'svn diff' and 'svn merge'.  */


/* Hi!  This is a comment left behind by Karl, and Ben is too afraid
   to erase it at this time, because he's not fully confident that all
   this knowledge has been grokked yet.

   There are five cases:
      1. path is not an URL and start_revision != end_revision
      2. path is not an URL and start_revision == end_revision
      3. path is an URL and start_revision != end_revision
      4. path is an URL and start_revision == end_revision
      5. path is not an URL and no revisions given

   With only one distinct revision the working copy provides the
   other.  When path is an URL there is no working copy. Thus

     1: compare repository versions for URL coresponding to working copy
     2: compare working copy against repository version
     3: compare repository versions for URL
     4: nothing to do.
     5: compare working copy against text-base

   Case 4 is not as stupid as it looks, for example it may occur if
   the user specifies two dates that resolve to the same revision.  */




/* Helper function: given a working-copy PATH, return its associated
   url in *URL, allocated in POOL.  If PATH is *already* a URL, that's
   fine, just set *URL = PATH. */
static svn_error_t *
convert_to_url(const char **url,
               const char *path,
               apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;  /* ### FIXME local */
  const svn_wc_entry_t *entry;

  if (svn_path_is_url(path))
    {
      *url = path;
      return SVN_NO_ERROR;
    }

  /* ### This may not be a good idea, see issue 880 */
  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path, FALSE,
                                 0, NULL, NULL, pool));
  SVN_ERR(svn_wc__entry_versioned(&entry, path, adm_access, FALSE, pool));
  SVN_ERR(svn_wc_adm_close2(adm_access, pool));

  if (entry->url)
    *url = apr_pstrdup(pool, entry->url);
  else
    *url = apr_pstrdup(pool, entry->copyfrom_url);
  return SVN_NO_ERROR;
}

/** Helper structure: for passing around the diff parameters */
struct diff_parameters
{
  /* First input path */
  const char *path1;

  /* Revision of first input path */
  const svn_opt_revision_t *revision1;

  /* Second input path */
  const char *path2;

  /* Revision of second input path */
  const svn_opt_revision_t *revision2;

  /* Peg revision */
  const svn_opt_revision_t *peg_revision;

  /* Desired depth */
  svn_depth_t depth;

  /* Ignore ancestry */
  svn_boolean_t ignore_ancestry;

  /* Ignore deleted */
  svn_boolean_t no_diff_deleted;

  /* Changelists of interest */
  const apr_array_header_t *changelists;
};

/** Helper structure: filled by check_paths() */
struct diff_paths
{
  /* path1 can only be found in the repository? */
  svn_boolean_t is_repos1;

  /* path2 can only be found in the repository? */
  svn_boolean_t is_repos2;
};


/** Check if paths are urls and if the revisions are local, and, for
    pegged revisions, ensure that at least one revision is non-local.
    Fills the PATHS structure. */
static svn_error_t *
check_paths(const struct diff_parameters *params,
            struct diff_paths *paths)
{
  svn_boolean_t is_local_rev1, is_local_rev2;

  /* Verify our revision arguments in light of the paths. */
  if ((params->revision1->kind == svn_opt_revision_unspecified)
      || (params->revision2->kind == svn_opt_revision_unspecified))
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                            _("Not all required revisions are specified"));

  /* Revisions can be said to be local or remote.  BASE and WORKING,
     for example, are local.  */
  is_local_rev1 =
    ((params->revision1->kind == svn_opt_revision_base)
     || (params->revision1->kind == svn_opt_revision_working));
  is_local_rev2 =
    ((params->revision2->kind == svn_opt_revision_base)
     || (params->revision2->kind == svn_opt_revision_working));

  if (params->peg_revision->kind != svn_opt_revision_unspecified)
    {
      if (is_local_rev1 && is_local_rev2)
        return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                                _("At least one revision must be non-local "
                                  "for a pegged diff"));

      paths->is_repos1 = ! is_local_rev1;
      paths->is_repos2 = ! is_local_rev2;
    }
  else
    {
      /* Working copy paths with non-local revisions get turned into
         URLs.  We don't do that here, though.  We simply record that it
         needs to be done, which is information that helps us choose our
         diff helper function.  */
      paths->is_repos1 = ! is_local_rev1 || svn_path_is_url(params->path1);
      paths->is_repos2 = ! is_local_rev2 || svn_path_is_url(params->path2);
    }

  return SVN_NO_ERROR;
}

/** Helper structure filled by diff_prepare_repos_repos */
struct diff_repos_repos_t
{
  /* URL created from path1 */
  const char *url1;

  /* URL created from path2 */
  const char *url2;

  /* The BASE_PATH for the diff */
  const char *base_path;

  /* url1 and url2 are the same */
  svn_boolean_t same_urls;

  /* Revision of url1 */
  svn_revnum_t rev1;

  /* Revision of url2 */
  svn_revnum_t rev2;

  /* Anchor based on url1 */
  const char *anchor1;

  /* Anchor based on url2 */
  const char *anchor2;

  /* Target based on url1 */
  const char *target1;

  /* Target based on url2 */
  const char *target2;

  /* RA session pointing at anchor1. */
  svn_ra_session_t *ra_session;
};

/** Helper function: prepare a repos repos diff. Fills DRR
 * structure. */
static svn_error_t *
diff_prepare_repos_repos(const struct diff_parameters *params,
                         struct diff_repos_repos_t *drr,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  svn_node_kind_t kind1, kind2;

  /* Figure out URL1 and URL2. */
  SVN_ERR(convert_to_url(&drr->url1, params->path1, pool));
  SVN_ERR(convert_to_url(&drr->url2, params->path2, pool));
  drr->same_urls = (strcmp(drr->url1, drr->url2) == 0);

  /* We need exactly one BASE_PATH, so we'll let the BASE_PATH
     calculated for PATH2 override the one for PATH1 (since the diff
     will be "applied" to URL2 anyway). */
  drr->base_path = NULL;
  if (drr->url1 != params->path1)
    drr->base_path = params->path1;
  if (drr->url2 != params->path2)
    drr->base_path = params->path2;

  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, drr->url2,
                                               NULL, NULL, NULL, FALSE,
                                               TRUE, ctx, pool));

  /* If we are performing a pegged diff, we need to find out what our
     actual URLs will be. */
  if (params->peg_revision->kind != svn_opt_revision_unspecified)
    {
      svn_opt_revision_t *start_ignore, *end_ignore;

      SVN_ERR(svn_client__repos_locations(&drr->url1, &start_ignore,
                                          &drr->url2, &end_ignore,
                                          ra_session,
                                          params->path2,
                                          params->peg_revision,
                                          params->revision1,
                                          params->revision2,
                                          ctx, pool));
      /* Reparent the session, since drr->url2 might have changed as a result
         the above call. */
      SVN_ERR(svn_ra_reparent(ra_session, drr->url2, pool));
    }

  /* Resolve revision and get path kind for the second target. */
  SVN_ERR(svn_client__get_revision_number
          (&drr->rev2, NULL, ra_session, params->revision2,
           (params->path2 == drr->url2) ? NULL : params->path2, pool));
  SVN_ERR(svn_ra_check_path(ra_session, "", drr->rev2, &kind2, pool));
  if (kind2 == svn_node_none)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FOUND, NULL,
       _("'%s' was not found in the repository at revision %ld"),
       drr->url2, drr->rev2);

  /* Do the same for the first target. */
  SVN_ERR(svn_ra_reparent(ra_session, drr->url1, pool));
  SVN_ERR(svn_client__get_revision_number
          (&drr->rev1, NULL, ra_session, params->revision1,
           (params->path1 == drr->url1) ? NULL : params->path1, pool));
  SVN_ERR(svn_ra_check_path(ra_session, "", drr->rev1, &kind1, pool));
  if (kind1 == svn_node_none)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FOUND, NULL,
       _("'%s' was not found in the repository at revision %ld"),
       drr->url1, drr->rev1);

  /* Choose useful anchors and targets for our two URLs. */
  drr->anchor1 = drr->url1;
  drr->anchor2 = drr->url2;
  drr->target1 = "";
  drr->target2 = "";
  if ((kind1 == svn_node_file) || (kind2 == svn_node_file))
    {
      svn_path_split(drr->url1, &drr->anchor1, &drr->target1, pool);
      drr->target1 = svn_path_uri_decode(drr->target1, pool);
      svn_path_split(drr->url2, &drr->anchor2, &drr->target2, pool);
      drr->target2 = svn_path_uri_decode(drr->target2, pool);
      if (drr->base_path)
        drr->base_path = svn_path_dirname(drr->base_path, pool);
      SVN_ERR(svn_ra_reparent(ra_session, drr->anchor1, pool));
    }

  drr->ra_session = ra_session;
  return SVN_NO_ERROR;
}

/* A Theoretical Note From Ben, regarding do_diff().

   This function is really svn_client_diff4().  If you read the public
   API description for svn_client_diff4(), it sounds quite Grand.  It
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

   Perhaps someday a brave soul will truly make svn_client_diff4
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
                          _("Sorry, svn_client_diff4 was called in a way "
                            "that is not yet supported"));
}


/* Perform a diff between two working-copy paths.

   PATH1 and PATH2 are both working copy paths.  REVISION1 and
   REVISION2 are their respective revisions.

   All other options are the same as those passed to svn_client_diff4(). */
static svn_error_t *
diff_wc_wc(const char *path1,
           const svn_opt_revision_t *revision1,
           const char *path2,
           const svn_opt_revision_t *revision2,
           svn_depth_t depth,
           svn_boolean_t ignore_ancestry,
           const apr_array_header_t *changelists,
           const svn_wc_diff_callbacks3_t *callbacks,
           struct diff_cmd_baton *callback_baton,
           svn_client_ctx_t *ctx,
           apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access, *target_access;
  const char *target;
  int levels_to_lock = SVN_WC__LEVELS_TO_LOCK_FROM_DEPTH(depth);

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
        _("Only diffs between a path's text-base "
          "and its working files are supported at this time")));

  SVN_ERR(svn_wc_adm_open_anchor(&adm_access, &target_access, &target,
                                 path1, FALSE, levels_to_lock,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 pool));

  /* Resolve named revisions to real numbers. */
  SVN_ERR(svn_client__get_revision_number
          (&callback_baton->revnum1, NULL, NULL, revision1, path1, pool));
  callback_baton->revnum2 = SVN_INVALID_REVNUM;  /* WC */

  SVN_ERR(svn_wc_diff5(adm_access, target, callbacks, callback_baton,
                       depth, ignore_ancestry, changelists, pool));
  return svn_wc_adm_close2(adm_access, pool);
}


/* Perform a diff between two repository paths.

   DIFF_PARAM.PATH1 and DIFF_PARAM.PATH2 may be either URLs or the working
   copy paths. DIFF_PARAM.REVISION1 and DIFF_PARAM.REVISION2 are their
   respective revisions. If DIFF_PARAM.PEG_REVISION is specified,
   DIFF_PARAM.PATH2 is the path at the peg revision, and the actual two
   paths compared are determined by following copy history from PATH2.

   All other options are the same as those passed to svn_client_diff4(). */
static svn_error_t *
diff_repos_repos(const struct diff_parameters *diff_param,
                 const svn_wc_diff_callbacks3_t *callbacks,
                 struct diff_cmd_baton *callback_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_ra_session_t *extra_ra_session;

  const svn_ra_reporter3_t *reporter;
  void *report_baton;

  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;

  struct diff_repos_repos_t drr;

  /* Prepare info for the repos repos diff. */
  SVN_ERR(diff_prepare_repos_repos(diff_param, &drr, ctx, pool));

  /* Get actual URLs. */
  callback_baton->orig_path_1 = drr.url1;
  callback_baton->orig_path_2 = drr.url2;

  /* Get numeric revisions. */
  callback_baton->revnum1 = drr.rev1;
  callback_baton->revnum2 = drr.rev2;

  /* Now, we open an extra RA session to the correct anchor
     location for URL1.  This is used during the editor calls to fetch file
     contents.  */
  SVN_ERR(svn_client__open_ra_session_internal
          (&extra_ra_session, drr.anchor1, NULL, NULL, NULL, FALSE, TRUE, ctx,
           pool));

  /* Set up the repos_diff editor on BASE_PATH, if available.
     Otherwise, we just use "". */
  SVN_ERR(svn_client__get_diff_editor
          (drr.base_path ? drr.base_path : "",
           NULL, callbacks, callback_baton, diff_param->depth,
           FALSE /* doesn't matter for diff */, extra_ra_session, drr.rev1,
           NULL /* no notify_func */, NULL /* no notify_baton */,
           ctx->cancel_func, ctx->cancel_baton,
           &diff_editor, &diff_edit_baton, pool));

  /* We want to switch our txn into URL2 */
  SVN_ERR(svn_ra_do_diff3
          (drr.ra_session, &reporter, &report_baton, drr.rev2, drr.target1,
           diff_param->depth, diff_param->ignore_ancestry, TRUE,
           drr.url2, diff_editor, diff_edit_baton, pool));

  /* Drive the reporter; do the diff. */
  SVN_ERR(reporter->set_path(report_baton, "", drr.rev1,
                             svn_depth_infinity,
                             FALSE, NULL,
                             pool));
  return reporter->finish_report(report_baton, pool);
}


/* Perform a diff between a repository path and a working-copy path.

   PATH1 may be either a URL or a working copy path.  PATH2 is a
   working copy path.  REVISION1 and REVISION2 are their respective
   revisions.  If REVERSE is TRUE, the diff will be done in reverse.
   If PEG_REVISION is specified, then PATH1 is the path in the peg
   revision, and the actual repository path to be compared is
   determined by following copy history.

   All other options are the same as those passed to svn_client_diff4(). */
static svn_error_t *
diff_repos_wc(const char *path1,
              const svn_opt_revision_t *revision1,
              const svn_opt_revision_t *peg_revision,
              const char *path2,
              const svn_opt_revision_t *revision2,
              svn_boolean_t reverse,
              svn_depth_t depth,
              svn_boolean_t ignore_ancestry,
              const apr_array_header_t *changelists,
              const svn_wc_diff_callbacks3_t *callbacks,
              struct diff_cmd_baton *callback_baton,
              svn_client_ctx_t *ctx,
              apr_pool_t *pool)
{
  const char *url1, *anchor, *anchor_url, *target;
  svn_wc_adm_access_t *adm_access, *dir_access;
  const svn_wc_entry_t *entry;
  svn_revnum_t rev;
  svn_ra_session_t *ra_session;
  const svn_ra_reporter3_t *reporter;
  void *report_baton;
  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;
  svn_boolean_t rev2_is_base = (revision2->kind == svn_opt_revision_base);
  int levels_to_lock = SVN_WC__LEVELS_TO_LOCK_FROM_DEPTH(depth);
  svn_boolean_t server_supports_depth;

  SVN_ERR_ASSERT(! svn_path_is_url(path2));

  /* Convert path1 to a URL to feed to do_diff. */
  SVN_ERR(convert_to_url(&url1, path1, pool));

  SVN_ERR(svn_wc_adm_open_anchor(&adm_access, &dir_access, &target,
                                 path2, FALSE, levels_to_lock,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 pool));
  anchor = svn_wc_adm_access_path(adm_access);

  /* Fetch the URL of the anchor directory. */
  SVN_ERR(svn_wc__entry_versioned(&entry, anchor, adm_access, FALSE, pool));
  if (! entry->url)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                             _("Directory '%s' has no URL"),
                             svn_path_local_style(anchor, pool));
  anchor_url = apr_pstrdup(pool, entry->url);

  /* If we are performing a pegged diff, we need to find out what our
     actual URLs will be. */
  if (peg_revision->kind != svn_opt_revision_unspecified)
    {
      svn_opt_revision_t *start_ignore, *end_ignore, end;
      const char *url_ignore;

      end.kind = svn_opt_revision_unspecified;

      SVN_ERR(svn_client__repos_locations(&url1, &start_ignore,
                                          &url_ignore, &end_ignore,
                                          NULL,
                                          path1,
                                          peg_revision,
                                          revision1, &end,
                                          ctx, pool));

      if (!reverse)
        {
          callback_baton->orig_path_1 = url1;
          callback_baton->orig_path_2 = svn_path_join(anchor_url, target, pool);
        }
      else
        {
          callback_baton->orig_path_1 = svn_path_join(anchor_url, target, pool);
          callback_baton->orig_path_2 = url1;
        }
    }

  /* Establish RA session to path2's anchor */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, anchor_url,
                                               NULL, NULL, NULL, FALSE, TRUE,
                                               ctx, pool));

  SVN_ERR(svn_wc_get_diff_editor5(adm_access, target,
                                  callbacks, callback_baton,
                                  depth,
                                  ignore_ancestry,
                                  rev2_is_base,
                                  reverse,
                                  ctx->cancel_func, ctx->cancel_baton,
                                  changelists,
                                  &diff_editor, &diff_edit_baton,
                                  pool));

  /* Tell the RA layer we want a delta to change our txn to URL1 */
  SVN_ERR(svn_client__get_revision_number
          (&rev, NULL, ra_session, revision1,
           (path1 == url1) ? NULL : path1, pool));

  if (!reverse)
    callback_baton->revnum1 = rev;
  else
    callback_baton->revnum2 = rev;

  SVN_ERR(svn_ra_do_diff3(ra_session,
                          &reporter, &report_baton,
                          rev,
                          target ? svn_path_uri_decode(target, pool) : NULL,
                          depth,
                          ignore_ancestry,
                          TRUE,  /* text_deltas */
                          url1,
                          diff_editor, diff_edit_baton, pool));

  SVN_ERR(svn_ra_has_capability(ra_session, &server_supports_depth,
                                SVN_RA_CAPABILITY_DEPTH, pool));

  /* Create a txn mirror of path2;  the diff editor will print
     diffs in reverse.  :-)  */
  SVN_ERR(svn_wc_crawl_revisions4(path2, dir_access,
                                  reporter, report_baton,
                                  FALSE, depth, TRUE, (! server_supports_depth),
                                  FALSE, NULL, NULL, /* notification is N/A */
                                  NULL, pool));

  return svn_wc_adm_close2(adm_access, pool);
}


/* This is basically just the guts of svn_client_diff[_peg]3(). */
static svn_error_t *
do_diff(const struct diff_parameters *diff_param,
        const svn_wc_diff_callbacks3_t *callbacks,
        struct diff_cmd_baton *callback_baton,
        svn_client_ctx_t *ctx,
        apr_pool_t *pool)
{
  struct diff_paths diff_paths;

  /* Check if paths/revisions are urls/local. */
  SVN_ERR(check_paths(diff_param, &diff_paths));

  if (diff_paths.is_repos1)
    {
      if (diff_paths.is_repos2)
        {
          return diff_repos_repos(diff_param, callbacks, callback_baton,
                                  ctx, pool);
        }
      else /* path2 is a working copy path */
        {
          return diff_repos_wc(diff_param->path1, diff_param->revision1,
                               diff_param->peg_revision,
                               diff_param->path2, diff_param->revision2,
                               FALSE, diff_param->depth,
                               diff_param->ignore_ancestry,
                               diff_param->changelists,
                               callbacks, callback_baton, ctx, pool);
        }
    }
  else /* path1 is a working copy path */
    {
      if (diff_paths.is_repos2)
        {
          return diff_repos_wc(diff_param->path2, diff_param->revision2,
                               diff_param->peg_revision,
                               diff_param->path1, diff_param->revision1,
                               TRUE, diff_param->depth,
                               diff_param->ignore_ancestry,
                               diff_param->changelists,
                               callbacks, callback_baton, ctx, pool);
        }
      else /* path2 is a working copy path */
        {
          return diff_wc_wc(diff_param->path1, diff_param->revision1,
                            diff_param->path2, diff_param->revision2,
                            diff_param->depth,
                            diff_param->ignore_ancestry,
                            diff_param->changelists,
                            callbacks, callback_baton, ctx, pool);
        }
    }
}

/* Perform a diff summary between two repository paths. */
static svn_error_t *
diff_summarize_repos_repos(const struct diff_parameters *diff_param,
                           svn_client_diff_summarize_func_t summarize_func,
                           void *summarize_baton,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool)
{
  svn_ra_session_t *extra_ra_session;

  const svn_ra_reporter3_t *reporter;
  void *report_baton;

  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;

  struct diff_repos_repos_t drr;

  /* Prepare info for the repos repos diff. */
  SVN_ERR(diff_prepare_repos_repos(diff_param, &drr, ctx, pool));

  /* Now, we open an extra RA session to the correct anchor
     location for URL1.  This is used to get the kind of deleted paths.  */
  SVN_ERR(svn_client__open_ra_session_internal
          (&extra_ra_session, drr.anchor1, NULL, NULL, NULL, FALSE, TRUE,
           ctx, pool));

  /* Set up the repos_diff editor. */
  SVN_ERR(svn_client__get_diff_summarize_editor
          (drr.target2, summarize_func,
           summarize_baton, extra_ra_session, drr.rev1, ctx->cancel_func,
           ctx->cancel_baton, &diff_editor, &diff_edit_baton, pool));

  /* We want to switch our txn into URL2 */
  SVN_ERR(svn_ra_do_diff3
          (drr.ra_session, &reporter, &report_baton, drr.rev2, drr.target1,
           diff_param->depth, diff_param->ignore_ancestry,
           FALSE /* do not create text delta */, drr.url2, diff_editor,
           diff_edit_baton, pool));

  /* Drive the reporter; do the diff. */
  SVN_ERR(reporter->set_path(report_baton, "", drr.rev1,
                             svn_depth_infinity,
                             FALSE, NULL, pool));
  return reporter->finish_report(report_baton, pool);
}

/* This is basically just the guts of svn_client_diff_summarize[_peg]2(). */
static svn_error_t *
do_diff_summarize(const struct diff_parameters *diff_param,
                  svn_client_diff_summarize_func_t summarize_func,
                  void *summarize_baton,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  struct diff_paths diff_paths;

  /* Check if paths/revisions are urls/local. */
  SVN_ERR(check_paths(diff_param, &diff_paths));

  if (diff_paths.is_repos1 && diff_paths.is_repos2)
    return diff_summarize_repos_repos(diff_param, summarize_func,
                                      summarize_baton, ctx, pool);
  else
    return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                            _("Summarizing diff can only compare repository "
                              "to repository"));
}


/* Initialize DIFF_CMD_BATON.diff_cmd and DIFF_CMD_BATON.options,
 * according to OPTIONS and CONFIG.  CONFIG may be null.
 * Allocate the fields in POOL, which should be at least as long-lived
 * as the pool DIFF_CMD_BATON itself is allocated in.
 */
static svn_error_t *
set_up_diff_cmd_and_options(struct diff_cmd_baton *diff_cmd_baton,
                            const apr_array_header_t *options,
                            apr_hash_t *config, apr_pool_t *pool)
{
  const char *diff_cmd = NULL;

  /* See if there is a command. */
  if (config)
    {
      svn_config_t *cfg = apr_hash_get(config, SVN_CONFIG_CATEGORY_CONFIG,
                                       APR_HASH_KEY_STRING);
      svn_config_get(cfg, &diff_cmd, SVN_CONFIG_SECTION_HELPERS,
                     SVN_CONFIG_OPTION_DIFF_CMD, NULL);
    }

  diff_cmd_baton->diff_cmd = diff_cmd;

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
            argv[i] = APR_ARRAY_IDX(options, i, const char *);
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
svn_client_diff4(const apr_array_header_t *options,
                 const char *path1,
                 const svn_opt_revision_t *revision1,
                 const char *path2,
                 const svn_opt_revision_t *revision2,
                 const char *relative_to_dir,
                 svn_depth_t depth,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t no_diff_deleted,
                 svn_boolean_t ignore_content_type,
                 const char *header_encoding,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
                 const apr_array_header_t *changelists,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  struct diff_parameters diff_params;

  struct diff_cmd_baton diff_cmd_baton;
  svn_wc_diff_callbacks3_t diff_callbacks;

  /* We will never do a pegged diff from here. */
  svn_opt_revision_t peg_revision;
  peg_revision.kind = svn_opt_revision_unspecified;

  /* fill diff_param */
  diff_params.path1 = path1;
  diff_params.revision1 = revision1;
  diff_params.path2 = path2;
  diff_params.revision2 = revision2;
  diff_params.peg_revision = &peg_revision;
  diff_params.depth = depth;
  diff_params.ignore_ancestry = ignore_ancestry;
  diff_params.no_diff_deleted = no_diff_deleted;
  diff_params.changelists = changelists;

  /* setup callback and baton */
  diff_callbacks.file_changed = diff_file_changed;
  diff_callbacks.file_added = diff_file_added;
  diff_callbacks.file_deleted = no_diff_deleted ? diff_file_deleted_no_diff :
                                                  diff_file_deleted_with_diff;
  diff_callbacks.dir_added =  diff_dir_added;
  diff_callbacks.dir_deleted = diff_dir_deleted;
  diff_callbacks.dir_props_changed = diff_props_changed;
  diff_callbacks.dir_opened = diff_dir_opened;
  diff_callbacks.dir_closed = diff_dir_closed;

  diff_cmd_baton.orig_path_1 = path1;
  diff_cmd_baton.orig_path_2 = path2;

  SVN_ERR(set_up_diff_cmd_and_options(&diff_cmd_baton, options,
                                      ctx->config, pool));
  diff_cmd_baton.pool = pool;
  diff_cmd_baton.outfile = outfile;
  diff_cmd_baton.errfile = errfile;
  diff_cmd_baton.header_encoding = header_encoding;
  diff_cmd_baton.revnum1 = SVN_INVALID_REVNUM;
  diff_cmd_baton.revnum2 = SVN_INVALID_REVNUM;

  diff_cmd_baton.force_empty = FALSE;
  diff_cmd_baton.force_binary = ignore_content_type;
  diff_cmd_baton.relative_to_dir = relative_to_dir;

  return do_diff(&diff_params, &diff_callbacks, &diff_cmd_baton, ctx, pool);
}

svn_error_t *
svn_client_diff_peg4(const apr_array_header_t *options,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *start_revision,
                     const svn_opt_revision_t *end_revision,
                     const char *relative_to_dir,
                     svn_depth_t depth,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t no_diff_deleted,
                     svn_boolean_t ignore_content_type,
                     const char *header_encoding,
                     apr_file_t *outfile,
                     apr_file_t *errfile,
                     const apr_array_header_t *changelists,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  struct diff_parameters diff_params;

  struct diff_cmd_baton diff_cmd_baton;
  svn_wc_diff_callbacks3_t diff_callbacks;

  if (svn_path_is_url(path) &&
        (start_revision->kind == svn_opt_revision_base
         || end_revision->kind == svn_opt_revision_base) )
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                            _("Revision type requires a working copy "
                              "path, not a URL"));

  /* fill diff_param */
  diff_params.path1 = path;
  diff_params.revision1 = start_revision;
  diff_params.path2 = path;
  diff_params.revision2 = end_revision;
  diff_params.peg_revision = peg_revision;
  diff_params.depth = depth;
  diff_params.ignore_ancestry = ignore_ancestry;
  diff_params.no_diff_deleted = no_diff_deleted;
  diff_params.changelists = changelists;

  /* setup callback and baton */
  diff_callbacks.file_changed = diff_file_changed;
  diff_callbacks.file_added = diff_file_added;
  diff_callbacks.file_deleted = no_diff_deleted ? diff_file_deleted_no_diff :
                                                  diff_file_deleted_with_diff;
  diff_callbacks.dir_added =  diff_dir_added;
  diff_callbacks.dir_deleted = diff_dir_deleted;
  diff_callbacks.dir_props_changed = diff_props_changed;
  diff_callbacks.dir_opened = diff_dir_opened;
  diff_callbacks.dir_closed = diff_dir_closed;

  diff_cmd_baton.orig_path_1 = path;
  diff_cmd_baton.orig_path_2 = path;

  SVN_ERR(set_up_diff_cmd_and_options(&diff_cmd_baton, options,
                                      ctx->config, pool));
  diff_cmd_baton.pool = pool;
  diff_cmd_baton.outfile = outfile;
  diff_cmd_baton.errfile = errfile;
  diff_cmd_baton.header_encoding = header_encoding;
  diff_cmd_baton.revnum1 = SVN_INVALID_REVNUM;
  diff_cmd_baton.revnum2 = SVN_INVALID_REVNUM;

  diff_cmd_baton.force_empty = FALSE;
  diff_cmd_baton.force_binary = ignore_content_type;
  diff_cmd_baton.relative_to_dir = relative_to_dir;

  return do_diff(&diff_params, &diff_callbacks, &diff_cmd_baton, ctx, pool);
}

svn_error_t *
svn_client_diff_summarize2(const char *path1,
                           const svn_opt_revision_t *revision1,
                           const char *path2,
                           const svn_opt_revision_t *revision2,
                           svn_depth_t depth,
                           svn_boolean_t ignore_ancestry,
                           const apr_array_header_t *changelists,
                           svn_client_diff_summarize_func_t summarize_func,
                           void *summarize_baton,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool)
{
  struct diff_parameters diff_params;

  /* We will never do a pegged diff from here. */
  svn_opt_revision_t peg_revision;
  peg_revision.kind = svn_opt_revision_unspecified;

  /* fill diff_param */
  diff_params.path1 = path1;
  diff_params.revision1 = revision1;
  diff_params.path2 = path2;
  diff_params.revision2 = revision2;
  diff_params.peg_revision = &peg_revision;
  diff_params.depth = depth;
  diff_params.ignore_ancestry = ignore_ancestry;
  diff_params.no_diff_deleted = FALSE;
  diff_params.changelists = changelists;

  return do_diff_summarize(&diff_params, summarize_func, summarize_baton,
                           ctx, pool);
}

svn_error_t *
svn_client_diff_summarize_peg2(const char *path,
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
  struct diff_parameters diff_params;

  /* fill diff_param */
  diff_params.path1 = path;
  diff_params.revision1 = start_revision;
  diff_params.path2 = path;
  diff_params.revision2 = end_revision;
  diff_params.peg_revision = peg_revision;
  diff_params.depth = depth;
  diff_params.ignore_ancestry = ignore_ancestry;
  diff_params.no_diff_deleted = FALSE;
  diff_params.changelists = changelists;

  return do_diff_summarize(&diff_params, summarize_func, summarize_baton,
                           ctx, pool);
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
