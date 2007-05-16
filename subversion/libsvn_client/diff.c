/*
 * diff.c: comparing and merging
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
#include "svn_wc.h"
#include "svn_delta.h"
#include "svn_diff.h"
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
#include "client.h"
#include <assert.h>

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


/* A helper func that writes out verbal descriptions of property diffs
   to FILE.   Of course, the apr_file_t will probably be the 'outfile'
   passed to svn_client_diff3, which is probably stdout. */
static svn_error_t *
display_prop_diffs(const apr_array_header_t *propchanges,
                   apr_hash_t *original_props,
                   const char *path,
                   const char *encoding,
                   apr_file_t *file,
                   apr_pool_t *pool)
{
  int i;

  SVN_ERR(file_printf_from_utf8(file, encoding,
                                _("%sProperty changes on: %s%s"),
                                APR_EOL_STR,
                                svn_path_local_style(path, pool),
                                APR_EOL_STR));

  SVN_ERR(file_printf_from_utf8(file, encoding, "%s" APR_EOL_STR,
                                under_string));

  for (i = 0; i < propchanges->nelts; i++)
    {
      const svn_prop_t *propchange
        = &APR_ARRAY_IDX(propchanges, i, svn_prop_t);

      const svn_string_t *original_value;

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
      
      SVN_ERR(file_printf_from_utf8(file, encoding, _("Name: %s%s"),
                                    propchange->name, APR_EOL_STR));

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


/* Return SVN_ERR_UNSUPPORTED_FEATURE if URL's scheme does not
   match the scheme of the url for ADM_ACCESS's path; return
   SVN_ERR_BAD_URL if no scheme can be found for one or both urls;
   otherwise return SVN_NO_ERROR.  Use ADM_ACCESS's pool for
   temporary allocation. */
static svn_error_t *
check_scheme_match(svn_wc_adm_access_t *adm_access, const char *url)
{
  const char *path = svn_wc_adm_access_path(adm_access);
  apr_pool_t *pool = svn_wc_adm_access_pool(adm_access);
  const svn_wc_entry_t *ent;
  const char *idx1, *idx2;
  
  SVN_ERR(svn_wc_entry(&ent, path, adm_access, TRUE, pool));
  
  idx1 = strchr(url, ':');
  idx2 = strchr(ent->url, ':');

  if ((idx1 == NULL) && (idx2 == NULL))
    {
      return svn_error_createf
        (SVN_ERR_BAD_URL, NULL,
         _("URLs have no scheme ('%s' and '%s')"), url, ent->url);
    }
  else if (idx1 == NULL)
    {
      return svn_error_createf
        (SVN_ERR_BAD_URL, NULL,
         _("URL has no scheme: '%s'"), url);
    }
  else if (idx2 == NULL)
    {
      return svn_error_createf
        (SVN_ERR_BAD_URL, NULL,
         _("URL has no scheme: '%s'"), ent->url);
    }
  else if (((idx1 - url) != (idx2 - ent->url))
           || (strncmp(url, ent->url, idx1 - url) != 0))
    {
      return svn_error_createf
        (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
         _("Access scheme mixtures not yet supported ('%s' and '%s')"),
         url, ent->url);
    }

  /* else */

  return SVN_NO_ERROR;
}


/*-----------------------------------------------------------------*/

/*** Callbacks for 'svn diff', invoked by the repos-diff editor. ***/


struct diff_cmd_baton {
  const apr_array_header_t *options;
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
     svn_client_diff3, either may be SVN_INVALID_REVNUM.  We need these
     because some of the svn_wc_diff_callbacks2_t don't get revision
     arguments.

     ### Perhaps we should change the callback signatures and eliminate
     ### these?
  */
  svn_revnum_t revnum1;
  svn_revnum_t revnum2;

  /* Client config hash (may be NULL). */
  apr_hash_t *config;

  /* Set this if you want diff output even for binary files. */
  svn_boolean_t force_binary;

  /* Set this flag if you want diff_file_changed to output diffs
     unconditionally, even if the diffs are empty. */
  svn_boolean_t force_empty;

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

/* A svn_wc_diff_callbacks2_t function.  Used for both file and directory
   property diffs. */
static svn_error_t *
diff_props_changed(svn_wc_adm_access_t *adm_access,
                   svn_wc_notify_state_t *state,
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
                               diff_cmd_baton->outfile, subpool));

  if (state)
    *state = svn_wc_notify_state_unknown;

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Show differences between TMPFILE1 and TMPFILE2. PATH, REV1, and REV2 are
   used in the headers to indicate the file and revisions.  If either
   MIMETYPE1 or MIMETYPE2 indicate binary content, don't show a diff,
   but instread print a warning message. */
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
  const char *diff_cmd = NULL;
  const char **args = NULL;
  int nargs, exitcode;
  apr_pool_t *subpool = svn_pool_create(diff_cmd_baton->pool);
  svn_stream_t *os;
  apr_file_t *errfile = diff_cmd_baton->errfile;
  const char *label1, *label2;
  svn_boolean_t mt1_binary = FALSE, mt2_binary = FALSE;
  const char *path1, *path2;
  int i;

  /* Get a stream from our output file. */
  os = svn_stream_from_aprfile(diff_cmd_baton->outfile, subpool); 

  /* Assemble any option args. */
  nargs = diff_cmd_baton->options->nelts;
  if (nargs)
    {
      args = apr_palloc(subpool, nargs * sizeof(char *));
      for (i = 0; i < diff_cmd_baton->options->nelts; i++)
        {
          args[i] = 
            ((const char **)(diff_cmd_baton->options->elts))[i];
        }
      assert(i == nargs);
    }

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
  
  for (i = 0; path1[i] && path2[i] && (path1[i] == path2[i]); i++)
    ;
  
  /* Make sure the prefix is made of whole components. (Issue #1771) */
  if (path1[i] || path2[i])
    {
      for ( ; (i > 0) && (path1[i] != '/'); i--)
        ;
    }
  
  path1 = path1 + i;
  path2 = path2 + i;
  
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


  /* Find out if we need to run an external diff */
  if (diff_cmd_baton->config)
    {
      svn_config_t *cfg = apr_hash_get(diff_cmd_baton->config,
                                       SVN_CONFIG_CATEGORY_CONFIG,
                                       APR_HASH_KEY_STRING);
      svn_config_get(cfg, &diff_cmd, SVN_CONFIG_SECTION_HELPERS,
                     SVN_CONFIG_OPTION_DIFF_CMD, NULL);
    }

  if (diff_cmd)
    {
      /* Print out the diff header. */
      SVN_ERR(svn_stream_printf_from_utf8
              (os, diff_cmd_baton->header_encoding, subpool,
               "Index: %s" APR_EOL_STR "%s" APR_EOL_STR, path, equal_string));
      /* Close the stream (flush) */
      SVN_ERR(svn_stream_close(os));

      SVN_ERR(svn_io_run_diff(".", args, nargs, label1, label2,
                              tmpfile1, tmpfile2, 
                              &exitcode, diff_cmd_baton->outfile, errfile,
                              diff_cmd, subpool));
    }
  else   /* use libsvn_diff to generate the diff  */
    {
      svn_diff_t *diff;
      svn_diff_file_options_t *opts = svn_diff_file_options_create(subpool);

      if (diff_cmd_baton->options)
        SVN_ERR(svn_diff_file_options_parse(opts, diff_cmd_baton->options,
                                            subpool));

      SVN_ERR(svn_diff_file_diff_2(&diff, tmpfile1, tmpfile2, opts,
                                   subpool));

      if (svn_diff_contains_diffs(diff) || diff_cmd_baton->force_empty)
        {
          /* Print out the diff header. */
          SVN_ERR(svn_stream_printf_from_utf8
                  (os, diff_cmd_baton->header_encoding, subpool,
                   "Index: %s" APR_EOL_STR "%s" APR_EOL_STR,
                   path, equal_string));

          /* Output the actual diff */
          SVN_ERR(svn_diff_file_output_unified2
                  (os, diff, tmpfile1, tmpfile2, label1, label2,
                   diff_cmd_baton->header_encoding, subpool));
        }
    }

  /* ### todo: someday we'll need to worry about whether we're going
     to need to write a diff plug-in mechanism that makes use of the
     two paths, instead of just blindly running SVN_CLIENT_DIFF.  */

  /* Destroy the subpool. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* A svn_wc_diff_callbacks2_t function. */
static svn_error_t *
diff_file_changed(svn_wc_adm_access_t *adm_access,
                  svn_wc_notify_state_t *content_state,
                  svn_wc_notify_state_t *prop_state,
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
    SVN_ERR(diff_props_changed(adm_access, prop_state, path, prop_changes,
                               original_props, diff_baton));
  if (content_state)
    *content_state = svn_wc_notify_state_unknown;
  if (prop_state)
    *prop_state = svn_wc_notify_state_unknown;
  return SVN_NO_ERROR;
}

/* Because the repos-diff editor passes at least one empty file to
   each of these next two functions, they can be dumb wrappers around
   the main workhorse routine. */

/* A svn_wc_diff_callbacks2_t function. */
static svn_error_t *
diff_file_added(svn_wc_adm_access_t *adm_access,
                svn_wc_notify_state_t *content_state,
                svn_wc_notify_state_t *prop_state,
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

  SVN_ERR(diff_file_changed(adm_access, content_state, prop_state, path,
                            tmpfile1, tmpfile2, 
                            rev1, rev2,
                            mimetype1, mimetype2,
                            prop_changes, original_props, diff_baton));
  
  diff_cmd_baton->force_empty = FALSE;

  return SVN_NO_ERROR;
}

/* A svn_wc_diff_callbacks2_t function. */
static svn_error_t *
diff_file_deleted_with_diff(svn_wc_adm_access_t *adm_access,
                            svn_wc_notify_state_t *state,
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
  return diff_file_changed(adm_access, state, NULL, path,
                           tmpfile1, tmpfile2, 
                           diff_cmd_baton->revnum1, diff_cmd_baton->revnum2,
                           mimetype1, mimetype2,
                           apr_array_make(diff_cmd_baton->pool, 1,
                                          sizeof(svn_prop_t)),
                           apr_hash_make(diff_cmd_baton->pool), diff_baton);
}

/* A svn_wc_diff_callbacks2_t function. */
static svn_error_t *
diff_file_deleted_no_diff(svn_wc_adm_access_t *adm_access,
                          svn_wc_notify_state_t *state,
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

  SVN_ERR(file_printf_from_utf8
          (diff_cmd_baton->outfile,
           diff_cmd_baton->header_encoding,
           "Index: %s (deleted)" APR_EOL_STR "%s" APR_EOL_STR, 
           path, equal_string));

  return SVN_NO_ERROR;
}

/* A svn_wc_diff_callbacks2_t function.
   For now, let's have 'svn diff' send feedback to the top-level
   application, so that something reasonable about directories and
   propsets gets printed to stdout. */
static svn_error_t *
diff_dir_added(svn_wc_adm_access_t *adm_access,
               svn_wc_notify_state_t *state,
               const char *path,
               svn_revnum_t rev,
               void *diff_baton)
{
  if (state)
    *state = svn_wc_notify_state_unknown;

  /* ### todo:  send feedback to app */
  return SVN_NO_ERROR;
}

/* A svn_wc_diff_callbacks2_t function. */
static svn_error_t *
diff_dir_deleted(svn_wc_adm_access_t *adm_access,
                 svn_wc_notify_state_t *state,
                 const char *path,
                 void *diff_baton)
{
  if (state)
    *state = svn_wc_notify_state_unknown;

  return SVN_NO_ERROR;
}
  

/*-----------------------------------------------------------------*/

/*** Callbacks for 'svn merge', invoked by the repos-diff editor. ***/


struct merge_cmd_baton {
  svn_boolean_t force;
  svn_boolean_t dry_run;
  const char *added_path;             /* Set to the dir path whenever the
                                         dir is added as a child of a
                                         versioned dir (dry-run only) */
  const char *target;                 /* Working copy target of merge */
  const char *url;                    /* The second URL in the merge */
  const char *path;                   /* The wc path of the second target, this
                                         can be NULL if we don't have one. */
  const svn_opt_revision_t *revision; /* Revision of second URL in the merge */
  svn_client_ctx_t *ctx;

  /* Whether invocation of the merge_file_added() callback required
     delegation to the merge_file_changed() function for the file
     currently being merged.  This info is used to detect whether a
     file on the left side of a 3-way merge actually exists (important
     because it's created as an empty temp file on disk regardless).*/
  svn_boolean_t add_necessitated_merge;

  /* The list of paths for entries we've deleted, used only when in
     dry_run mode. */
  apr_hash_t *dry_run_deletions;

  /* The diff3_cmd in ctx->config, if any, else null.  We could just
     extract this as needed, but since more than one caller uses it,
     we just set it up when this baton is created. */
  const char *diff3_cmd;
  const apr_array_header_t *merge_options;

  apr_pool_t *pool;
};

apr_hash_t *
svn_client__dry_run_deletions(void *merge_cmd_baton)
{
  struct merge_cmd_baton *merge_b = merge_cmd_baton;
  return merge_b->dry_run_deletions;
}

/* Used to avoid spurious notifications (e.g. conflicts) from a merge
   attempt into an existing target which would have been deleted if we
   weren't in dry_run mode (issue #2584).  Assumes that WCPATH is
   still versioned (e.g. has an associated entry). */
static APR_INLINE svn_boolean_t
dry_run_deleted_p(struct merge_cmd_baton *merge_b, const char *wcpath)
{
    return (merge_b->dry_run &&
            apr_hash_get(merge_b->dry_run_deletions, wcpath,
                         APR_HASH_KEY_STRING) != NULL);
}


/* A svn_wc_diff_callbacks2_t function.  Used for both file and directory
   property merges. */
static svn_error_t *
merge_props_changed(svn_wc_adm_access_t *adm_access,
                    svn_wc_notify_state_t *state,
                    const char *path,
                    const apr_array_header_t *propchanges,
                    apr_hash_t *original_props,
                    void *baton)
{
  apr_array_header_t *props;
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create(merge_b->pool);
  svn_error_t *err;

  SVN_ERR(svn_categorize_props(propchanges, NULL, NULL, &props, subpool));

  /* We only want to merge "regular" version properties:  by
     definition, 'svn merge' shouldn't touch any data within .svn/  */
  if (props->nelts)
    {
      err = svn_wc_merge_props(state, path, adm_access, original_props, props,
                               FALSE, merge_b->dry_run, subpool);
      if (err && (err->apr_err == SVN_ERR_ENTRY_NOT_FOUND
                  || err->apr_err == SVN_ERR_UNVERSIONED_RESOURCE))
        {
          /* if the entry doesn't exist in the wc, just 'skip' over
             this part of the tree-delta. */
          if (state)
            *state = svn_wc_notify_state_missing;
          svn_error_clear(err);
          return SVN_NO_ERROR;        
        }
      else if (err)
        return err;
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* A svn_wc_diff_callbacks2_t function. */
static svn_error_t *
merge_file_changed(svn_wc_adm_access_t *adm_access,
                   svn_wc_notify_state_t *content_state,
                   svn_wc_notify_state_t *prop_state,
                   const char *mine,
                   const char *older,
                   const char *yours,
                   svn_revnum_t older_rev,
                   svn_revnum_t yours_rev,
                   const char *mimetype1,
                   const char *mimetype2,
                   const apr_array_header_t *prop_changes,
                   apr_hash_t *original_props,
                   void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create(merge_b->pool);
  /* xgettext: the '.working', '.merge-left.r%ld' and '.merge-right.r%ld'
     strings are used to tag onto a filename in case of a merge conflict */
  const char *target_label = _(".working");
  const char *left_label = apr_psprintf(subpool,
                                        _(".merge-left.r%ld"),
                                        older_rev);
  const char *right_label = apr_psprintf(subpool,
                                         _(".merge-right.r%ld"),
                                         yours_rev);
  svn_boolean_t has_local_mods;
  svn_boolean_t merge_required = TRUE;
  enum svn_wc_merge_outcome_t merge_outcome;

  /* Easy out:  no access baton means there ain't no merge target */
  if (adm_access == NULL)
    {
      if (content_state)
        *content_state = svn_wc_notify_state_missing;
      if (prop_state)
        *prop_state = svn_wc_notify_state_missing;
      return SVN_NO_ERROR;
    }
  
  /* Other easy outs:  if the merge target isn't under version
     control, or is just missing from disk, fogettaboutit.  There's no
     way svn_wc_merge2() can do the merge. */
  {
    const svn_wc_entry_t *entry;
    svn_node_kind_t kind;

    SVN_ERR(svn_wc_entry(&entry, mine, adm_access, FALSE, subpool));
    SVN_ERR(svn_io_check_path(mine, &kind, subpool));

    /* ### a future thought:  if the file is under version control,
       but the working file is missing, maybe we can 'restore' the
       working file from the text-base, and then allow the merge to run?  */

    if ((! entry) || (kind != svn_node_file))
      {
        if (content_state)
          *content_state = svn_wc_notify_state_missing;
        if (prop_state)
          *prop_state = svn_wc_notify_state_missing;
        return SVN_NO_ERROR;
      }
  }

  /* This callback is essentially no more than a wrapper around
     svn_wc_merge2().  Thank goodness that all the
     diff-editor-mechanisms are doing the hard work of getting the
     fulltexts! */

  /* Do property merge before text merge so that keyword expansion takes
     into account the new property values. */
  if (prop_changes->nelts > 0)
    SVN_ERR(merge_props_changed(adm_access, prop_state, mine, prop_changes,
                                original_props, baton));
  else
    if (prop_state)
      *prop_state = svn_wc_notify_state_unchanged;

  if (older)
    {
      SVN_ERR(svn_wc_text_modified_p(&has_local_mods, mine, FALSE,
                                     adm_access, subpool));

      /* Special case:  if a binary file isn't locally modified, and is
         exactly identical to the 'left' side of the merge, then don't
         allow svn_wc_merge to produce a conflict.  Instead, just
         overwrite the working file with the 'right' side of the merge.

         Alternately, if the 'left' side of the merge doesn't exist in
         the repository, and the 'right' side of the merge is
         identical to the WC, pretend we did the merge (a no-op). */
      if ((! has_local_mods)
          && ((mimetype1 && svn_mime_type_is_binary(mimetype1))
              || (mimetype2 && svn_mime_type_is_binary(mimetype2))))
        {
          /* For adds, the 'left' side of the merge doesn't exist. */
          svn_boolean_t older_revision_exists =
              !merge_b->add_necessitated_merge;
          svn_boolean_t same_contents;
          SVN_ERR(svn_io_files_contents_same_p(&same_contents,
                                               (older_revision_exists ?
                                                older : yours),
                                               mine, subpool));
          if (same_contents)
            {
              if (older_revision_exists && !merge_b->dry_run)
                SVN_ERR(svn_io_file_rename(yours, mine, subpool));
              merge_outcome = svn_wc_merge_merged;
              merge_required = FALSE;
            }
        }  

      if (merge_required)
        {
          SVN_ERR(svn_wc_merge2(&merge_outcome,
                                older, yours, mine, adm_access,
                                left_label, right_label, target_label,
                                merge_b->dry_run, merge_b->diff3_cmd,
                                merge_b->merge_options, subpool));
        }

      if (content_state)
        {
          if (merge_outcome == svn_wc_merge_conflict)
            *content_state = svn_wc_notify_state_conflicted;
          else if (has_local_mods
                   && merge_outcome != svn_wc_merge_unchanged)
            *content_state = svn_wc_notify_state_merged;
          else if (merge_outcome == svn_wc_merge_merged)
            *content_state = svn_wc_notify_state_changed;
          else if (merge_outcome == svn_wc_merge_no_merge)
            *content_state = svn_wc_notify_state_missing;
          else /* merge_outcome == svn_wc_merge_unchanged */
            *content_state = svn_wc_notify_state_unchanged;
        }
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* A svn_wc_diff_callbacks2_t function. */
static svn_error_t *
merge_file_added(svn_wc_adm_access_t *adm_access,
                 svn_wc_notify_state_t *content_state,
                 svn_wc_notify_state_t *prop_state,
                 const char *mine,
                 const char *older,
                 const char *yours,
                 svn_revnum_t rev1,
                 svn_revnum_t rev2,
                 const char *mimetype1,
                 const char *mimetype2,
                 const apr_array_header_t *prop_changes,
                 apr_hash_t *original_props,
                 void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create(merge_b->pool);
  svn_node_kind_t kind;
  const char *copyfrom_url;
  const char *child;
  int i;
  apr_hash_t *new_props;

  /* In most cases, we just leave prop_state as unknown, and let the
     content_state what happened, so we set prop_state here to avoid that
     below. */
  if (prop_state)
    *prop_state = svn_wc_notify_state_unknown;

  /* Apply the prop changes to a new hash table. */
  new_props = apr_hash_copy(subpool, original_props);
  for (i = 0; i < prop_changes->nelts; ++i)
    {
      const svn_prop_t *prop = &APR_ARRAY_IDX(prop_changes, i, svn_prop_t);
      apr_hash_set(new_props, prop->name, APR_HASH_KEY_STRING, prop->value);
    }

  /* Easy out:  if we have no adm_access for the parent directory,
     then this portion of the tree-delta "patch" must be inapplicable.
     Send a 'missing' state back;  the repos-diff editor should then
     send a 'skip' notification. */
  if (! adm_access)
    {
      if (merge_b->dry_run && merge_b->added_path
          && svn_path_is_child(merge_b->added_path, mine, subpool))
        {
          if (content_state)
            *content_state = svn_wc_notify_state_changed;
          if (prop_state && apr_hash_count(new_props))
            *prop_state = svn_wc_notify_state_changed;
        }
      else
        *content_state = svn_wc_notify_state_missing;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_io_check_path(mine, &kind, subpool));
  switch (kind)
    {
    case svn_node_none:
      {
        const svn_wc_entry_t *entry;
        SVN_ERR(svn_wc_entry(&entry, mine, adm_access, FALSE, subpool));
        if (entry && entry->schedule != svn_wc_schedule_delete)
          {
            /* It's versioned but missing. */
            if (content_state)
              *content_state = svn_wc_notify_state_obstructed;
            return SVN_NO_ERROR;
          }
        if (! merge_b->dry_run)
          {
            child = svn_path_is_child(merge_b->target, mine, subpool);
            assert(child != NULL);
            copyfrom_url = svn_path_url_add_component(merge_b->url, child,
                                                      subpool);
            SVN_ERR(check_scheme_match(adm_access, copyfrom_url));

            /* Since 'mine' doesn't exist, and this is
               'merge_file_added', I hope it's safe to assume that
               'older' is empty, and 'yours' is the full file.  Merely
               copying 'yours' to 'mine', isn't enough; we need to get
               the whole text-base and props installed too, just as if
               we had called 'svn cp wc wc'. */

            SVN_ERR(svn_wc_add_repos_file2(mine, adm_access,
                                           yours, NULL,
                                           new_props, NULL,
                                           copyfrom_url,
                                           rev2,
                                           subpool));
          }
        if (content_state)
          *content_state = svn_wc_notify_state_changed;
        if (prop_state && apr_hash_count(new_props))
          *prop_state = svn_wc_notify_state_changed;
      }
      break;
    case svn_node_dir:
      if (content_state)
        {
          /* directory already exists, is it under version control? */
          const svn_wc_entry_t *entry;
          SVN_ERR(svn_wc_entry(&entry, mine, adm_access, FALSE, subpool));

          if (entry && dry_run_deleted_p(merge_b, mine))
            *content_state = svn_wc_notify_state_changed;
          else
            /* this will make the repos_editor send a 'skipped' message */
            *content_state = svn_wc_notify_state_obstructed;
        }
      break;
    case svn_node_file:
      {
        /* file already exists, is it under version control? */
        const svn_wc_entry_t *entry;
        SVN_ERR(svn_wc_entry(&entry, mine, adm_access, FALSE, subpool));

        /* If it's an unversioned file, don't touch it.  If its scheduled
           for deletion, then rm removed it from the working copy and the
           user must have recreated it, don't touch it */
        if (!entry || entry->schedule == svn_wc_schedule_delete)
          {
            /* this will make the repos_editor send a 'skipped' message */
            if (content_state)
              *content_state = svn_wc_notify_state_obstructed;
          }
        else
          {
            if (dry_run_deleted_p(merge_b, mine))
              {
                if (content_state)
                  *content_state = svn_wc_notify_state_changed;
              }
            else
              {
                /* Indicate that we merge because of an add to handle a
                   special case for binary files with no local mods. */
                  merge_b->add_necessitated_merge = TRUE;

                  SVN_ERR(merge_file_changed(adm_access, content_state,
                                             prop_state, mine, older, yours,
                                             rev1, rev2,
                                             mimetype1, mimetype2,
                                             prop_changes, original_props,
                                             baton));

                /* Reset the state so that the baton can safely be reused
                   in subsequent ops occurring during this merge. */
                  merge_b->add_necessitated_merge = FALSE;
              }
          }
        break;      
      }
    default:
      if (content_state)
        *content_state = svn_wc_notify_state_unknown;
      break;
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* A svn_wc_diff_callbacks2_t function. */
static svn_error_t *
merge_file_deleted(svn_wc_adm_access_t *adm_access,
                   svn_wc_notify_state_t *state,
                   const char *mine,
                   const char *older,
                   const char *yours,
                   const char *mimetype1,
                   const char *mimetype2,
                   apr_hash_t *original_props,
                   void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create(merge_b->pool);
  svn_node_kind_t kind;
  svn_wc_adm_access_t *parent_access;
  const char *parent_path;
  svn_error_t *err;

  /* Easy out:  if we have no adm_access for the parent directory,
     then this portion of the tree-delta "patch" must be inapplicable.
     Send a 'missing' state back;  the repos-diff editor should then
     send a 'skip' notification. */
  if (! adm_access)
    {
      if (state)
        *state = svn_wc_notify_state_missing;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_io_check_path(mine, &kind, subpool));
  switch (kind)
    {
    case svn_node_file:
      svn_path_split(mine, &parent_path, NULL, subpool);
      SVN_ERR(svn_wc_adm_retrieve(&parent_access, adm_access, parent_path,
                                  subpool));
      {
        /* Passing NULL for the notify_func and notify_baton because
         * repos_diff.c:delete_item will do it for us. */
        err = svn_client__wc_delete(mine, parent_access, merge_b->force,
                                    merge_b->dry_run, 
                                    NULL, NULL,
                                    merge_b->ctx, subpool);
      }
      if (err && state)
        {
          *state = svn_wc_notify_state_obstructed;
          svn_error_clear(err);
        }
      else if (state)
        {
          *state = svn_wc_notify_state_changed;
        }
      break;
    case svn_node_dir:
      if (state)
        *state = svn_wc_notify_state_obstructed;
      break;
    case svn_node_none:
      /* file is already non-existent, this is a no-op. */
      if (state)
        *state = svn_wc_notify_state_missing;
      break;
    default:
      if (state)
        *state = svn_wc_notify_state_unknown;
      break;
    }
    
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* A svn_wc_diff_callbacks2_t function. */
static svn_error_t *
merge_dir_added(svn_wc_adm_access_t *adm_access,
                svn_wc_notify_state_t *state,
                const char *path,
                svn_revnum_t rev,
                void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create(merge_b->pool);
  svn_node_kind_t kind;
  const svn_wc_entry_t *entry;
  const char *copyfrom_url, *child;

  /* Easy out:  if we have no adm_access for the parent directory,
     then this portion of the tree-delta "patch" must be inapplicable.
     Send a 'missing' state back;  the repos-diff editor should then
     send a 'skip' notification. */
  if (! adm_access)
    {
      if (state)
        {
          if (merge_b->dry_run && merge_b->added_path
              && svn_path_is_child(merge_b->added_path, path, subpool))
            *state = svn_wc_notify_state_changed;
          else
            *state = svn_wc_notify_state_missing;
        }
      return SVN_NO_ERROR;
    }

  child = svn_path_is_child(merge_b->target, path, subpool);
  assert(child != NULL);
  copyfrom_url = svn_path_url_add_component(merge_b->url, child, subpool);
  SVN_ERR(check_scheme_match(adm_access, copyfrom_url));

  SVN_ERR(svn_io_check_path(path, &kind, subpool));
  switch (kind)
    {
    case svn_node_none:
      SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, subpool));
      if (entry && entry->schedule != svn_wc_schedule_delete)
        {
          /* Versioned but missing */
          if (state)
            *state = svn_wc_notify_state_obstructed;
          return SVN_NO_ERROR;
        }
      if (! merge_b->dry_run)
        {
          SVN_ERR(svn_io_make_dir_recursively(path, subpool));
          SVN_ERR(svn_wc_add2(path, adm_access,
                              copyfrom_url, rev,
                              merge_b->ctx->cancel_func,
                              merge_b->ctx->cancel_baton,
                              NULL, NULL, /* don't pass notification func! */
                              subpool));

        }
      if (merge_b->dry_run)
        merge_b->added_path = apr_pstrdup(merge_b->pool, path);
      if (state)
        *state = svn_wc_notify_state_changed;
      break;
    case svn_node_dir:
      /* Adding an unversioned directory doesn't destroy data */
      SVN_ERR(svn_wc_entry(&entry, path, adm_access, TRUE, subpool));
      if (! entry || (entry && entry->schedule == svn_wc_schedule_delete))
        {
          if (!merge_b->dry_run)
            SVN_ERR(svn_wc_add2(path, adm_access,
                                copyfrom_url, rev,
                                merge_b->ctx->cancel_func,
                                merge_b->ctx->cancel_baton,
                                NULL, NULL, /* no notification func! */
                                subpool));
          if (merge_b->dry_run)
            merge_b->added_path = apr_pstrdup(merge_b->pool, path);
          if (state)
            *state = svn_wc_notify_state_changed;
        }
      else if (state)
        {
          if (dry_run_deleted_p(merge_b, path))
            *state = svn_wc_notify_state_changed;
          else
            *state = svn_wc_notify_state_obstructed;
        }
      break;
    case svn_node_file:
      if (merge_b->dry_run)
        merge_b->added_path = NULL;

      if (state)
        {
          SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, subpool));

          if (entry && dry_run_deleted_p(merge_b, path))
            /* ### TODO: Retain record of this dir being added to
               ### avoid problems from subsequent edits which try to
               ### add children. */
            *state = svn_wc_notify_state_changed;
          else
            *state = svn_wc_notify_state_obstructed;
        }
      break;
    default:
      if (merge_b->dry_run)
        merge_b->added_path = NULL;
      if (state)
        *state = svn_wc_notify_state_unknown;
      break;
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Struct used for as the baton for calling merge_delete_notify_func(). */
typedef struct merge_delete_notify_baton_t
{
  svn_client_ctx_t *ctx;

  /* path to skip */
  const char *path_skip;
} merge_delete_notify_baton_t;

/* Notify callback function that wraps the normal callback
 * function to remove a notification that will be sent twice
 * and set the proper action. */
static void
merge_delete_notify_func(void *baton,
                         const svn_wc_notify_t *notify,
                         apr_pool_t *pool)
{
  merge_delete_notify_baton_t *mdb = baton;
  svn_wc_notify_t *new_notify;

  /* Skip the notification for the path we called svn_client__wc_delete() with,
   * because it will be outputed by repos_diff.c:delete_item */  
  if (strcmp(notify->path, mdb->path_skip) == 0)
    return;
  
  /* svn_client__wc_delete() is written primarily for scheduling operations not
   * update operations.  Since merges are update operations we need to alter
   * the delete notification to show as an update not a schedule so alter 
   * the action. */
  if (notify->action == svn_wc_notify_delete)
    {
      /* We need to copy it since notify is const. */
      new_notify = svn_wc_dup_notify(notify, pool);
      new_notify->action = svn_wc_notify_update_delete;
      notify = new_notify;
    }

  if (mdb->ctx->notify_func2)
    (*mdb->ctx->notify_func2)(mdb->ctx->notify_baton2, notify, pool);
}

/* A svn_wc_diff_callbacks2_t function. */
static svn_error_t *
merge_dir_deleted(svn_wc_adm_access_t *adm_access,
                  svn_wc_notify_state_t *state,
                  const char *path,
                  void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create(merge_b->pool);
  svn_node_kind_t kind;
  svn_wc_adm_access_t *parent_access;
  const char *parent_path;
  svn_error_t *err;

  /* Easy out:  if we have no adm_access for the parent directory,
     then this portion of the tree-delta "patch" must be inapplicable.
     Send a 'missing' state back;  the repos-diff editor should then
     send a 'skip' notification. */
  if (! adm_access)
    {
      if (state)
        *state = svn_wc_notify_state_missing;
      return SVN_NO_ERROR;
    }
  
  SVN_ERR(svn_io_check_path(path, &kind, subpool));
  switch (kind)
    {
    case svn_node_dir:
      {
        merge_delete_notify_baton_t mdb;

        mdb.ctx = merge_b->ctx;
        mdb.path_skip = path;

        svn_path_split(path, &parent_path, NULL, subpool);
        SVN_ERR(svn_wc_adm_retrieve(&parent_access, adm_access, parent_path,
                                    subpool));
        err = svn_client__wc_delete(path, parent_access, merge_b->force,
                                    merge_b->dry_run,
                                    merge_delete_notify_func, &mdb,
                                    merge_b->ctx, subpool);
        if (err && state)
          {
            *state = svn_wc_notify_state_obstructed;
            svn_error_clear(err);
          }
        else if (state)
          {
            *state = svn_wc_notify_state_changed;
          }
      }
      break;
    case svn_node_file:
      if (state)
        *state = svn_wc_notify_state_obstructed;
      break;
    case svn_node_none:
      /* dir is already non-existent, this is a no-op. */
      if (state)
        *state = svn_wc_notify_state_missing;
      break;
    default:
      if (state)
        *state = svn_wc_notify_state_unknown;
      break;
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}
  
/* The main callback table for 'svn merge'.  */
static const svn_wc_diff_callbacks2_t
merge_callbacks =
  {
    merge_file_changed,
    merge_file_added,
    merge_file_deleted,
    merge_dir_added,
    merge_dir_deleted,
    merge_props_changed
  };


/*-----------------------------------------------------------------------*/

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
  SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, pool));
  SVN_ERR(svn_wc_adm_close(adm_access));
  if (! entry)
    return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                             _("'%s' is not under version control"),
                             svn_path_local_style(path, pool));

  if (entry->url)  
    *url = apr_pstrdup(pool, entry->url);
  else
    *url = apr_pstrdup(pool, entry->copyfrom_url);
  return SVN_NO_ERROR;
}

/** Helper structure: for passing around the diff parameters */
struct diff_parameters
{
  /* Additional parameters for diff tool */
  const apr_array_header_t *options;

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

  /* Recurse */
  svn_boolean_t recurse;

  /* Ignore acestry */
  svn_boolean_t ignore_ancestry;

  /* Ignore deleted */
  svn_boolean_t no_diff_deleted;
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
          (&drr->rev2, ra_session, params->revision2,
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
          (&drr->rev1, ra_session, params->revision1, 
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

/* URL1/PATH1, URL2/PATH2, and TARGET_WCPATH all better be
   directories.  For the single file case, the caller does the merging
   manually.  PATH1 and PATH2 can be NULL.

   If PEG_REVISION is specified, then INITIAL_PATH2 is the path to peg
   off of, unless it is NULL, in which case INITIAL_URL2 is the peg
   path.  The actual two paths to compare are then found by tracing
   copy history from this peg path to INITIAL_REVISION2 and
   INITIAL_REVISION1.
*/
static svn_error_t *
do_merge(const char *initial_URL1,
         const char *initial_path1,
         const svn_opt_revision_t *initial_revision1,
         const char *initial_URL2,
         const char *initial_path2,
         const svn_opt_revision_t *initial_revision2,
         const svn_opt_revision_t *peg_revision,
         const char *target_wcpath,
         svn_wc_adm_access_t *adm_access,
         svn_boolean_t recurse,
         svn_boolean_t ignore_ancestry,
         svn_boolean_t dry_run,
         const svn_wc_diff_callbacks2_t *callbacks,
         void *callback_baton,
         svn_client_ctx_t *ctx,
         apr_pool_t *pool)
{
  svn_revnum_t start_revnum, end_revnum;
  svn_ra_session_t *ra_session, *ra_session2;
  const svn_ra_reporter2_t *reporter;
  void *report_baton;
  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;
  struct merge_cmd_baton *merge_b = callback_baton;
  const char *URL1, *URL2, *path1, *path2;
  svn_opt_revision_t *revision1, *revision2;

  /* Sanity check -- ensure that we have valid revisions to look at. */
  if ((initial_revision1->kind == svn_opt_revision_unspecified)
      || (initial_revision2->kind == svn_opt_revision_unspecified))
    {
      return svn_error_create
        (SVN_ERR_CLIENT_BAD_REVISION, NULL,
         _("Not all required revisions are specified"));
    }

  /* If we are performing a pegged merge, we need to find out what our
     actual URLs will be. */
  if (peg_revision->kind != svn_opt_revision_unspecified)
    {
      SVN_ERR(svn_client__repos_locations(&URL1, &revision1,
                                          &URL2, &revision2,
                                          NULL,
                                          initial_path2 ? initial_path2
                                          : initial_URL2,
                                          peg_revision,
                                          initial_revision1,
                                          initial_revision2,
                                          ctx, pool));

      merge_b->url = URL2;
      path1 = NULL;
      path2 = NULL;
      merge_b->path = NULL;
    }
  else
    {
      URL1 = initial_URL1;
      URL2 = initial_URL2;
      path1 = initial_path1;
      path2 = initial_path2;
      revision1 = apr_pcalloc(pool, sizeof(*revision1));
      *revision1 = *initial_revision1;
      revision2 = apr_pcalloc(pool, sizeof(*revision2));
      *revision2 = *initial_revision2;
    }
  
  /* Establish first RA session to URL1. */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, URL1, NULL,
                                               NULL, NULL, FALSE, TRUE, 
                                               ctx, pool));
  /* Resolve the revision numbers. */
  SVN_ERR(svn_client__get_revision_number
          (&start_revnum, ra_session, revision1, path1, pool));
  SVN_ERR(svn_client__get_revision_number
          (&end_revnum, ra_session, revision2, path2, pool));

  /* Open a second session used to request individual file
     contents. Although a session can be used for multiple requests, it
     appears that they must be sequential. Since the first request, for
     the diff, is still being processed the first session cannot be
     reused. This applies to ra_dav, ra_local does not appears to have
     this limitation. */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session2, URL1, NULL,
                                               NULL, NULL, FALSE, TRUE,
                                               ctx, pool));
 
  SVN_ERR(svn_client__get_diff_editor(target_wcpath,
                                      adm_access,
                                      callbacks,
                                      callback_baton,
                                      recurse,
                                      dry_run,
                                      ra_session2,
                                      start_revnum,
                                      ctx->notify_func2,
                                      ctx->notify_baton2,
                                      ctx->cancel_func,
                                      ctx->cancel_baton,
                                      &diff_editor,
                                      &diff_edit_baton,
                                      pool));

  SVN_ERR(svn_ra_do_diff2(ra_session,
                          &reporter, &report_baton,
                          end_revnum,
                          "",
                          recurse,
                          ignore_ancestry,
                          TRUE,  /* text_deltas */
                          URL2,
                          diff_editor, diff_edit_baton, pool));

  SVN_ERR(reporter->set_path(report_baton, "", start_revnum, FALSE, NULL,
                             pool));
  
  SVN_ERR(reporter->finish_report(report_baton, pool));
  
  /* Sleep to ensure timestamp integrity. */
  svn_sleep_for_timestamps();

  return SVN_NO_ERROR;
}


/* Get REVISION of the file at URL.  SOURCE is a path that refers to that 
   file's entry in the working copy, or NULL if we don't have one.  Return in 
   *FILENAME the name of a file containing the file contents, in *PROPS a hash 
   containing the properties and in *REV the revision.  All allocation occurs 
   in POOL. */
static svn_error_t *
single_file_merge_get_file(const char **filename,
                           apr_hash_t **props,
                           svn_revnum_t *rev,
                           const char *url,
                           const char *path,
                           const svn_opt_revision_t *revision,
                           struct merge_cmd_baton *merge_b,
                           apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  apr_file_t *fp;
  svn_stream_t *stream;

  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, url, NULL,
                                               NULL, NULL, FALSE, TRUE, 
                                               merge_b->ctx, pool));
  SVN_ERR(svn_client__get_revision_number(rev, ra_session, revision,
                                          path, pool));
  SVN_ERR(svn_io_open_unique_file2(&fp, filename,
                                   merge_b->target, ".tmp",
                                   svn_io_file_del_none, pool));
  stream = svn_stream_from_aprfile2(fp, FALSE, pool);
  SVN_ERR(svn_ra_get_file(ra_session, "", *rev,
                          stream, NULL, props, pool));
  SVN_ERR(svn_stream_close(stream));

  return SVN_NO_ERROR;
}
                            

/* The single-file, simplified version of do_merge. */
static svn_error_t *
do_single_file_merge(const char *initial_URL1,
                     const char *initial_path1,
                     const svn_opt_revision_t *initial_revision1,
                     const char *initial_URL2,
                     const char *initial_path2,
                     const svn_opt_revision_t *initial_revision2,
                     const svn_opt_revision_t *peg_revision,
                     const char *target_wcpath,
                     svn_wc_adm_access_t *adm_access,
                     struct merge_cmd_baton *merge_b,
                     apr_pool_t *pool)
{
  apr_hash_t *props1, *props2;
  const char *tmpfile1, *tmpfile2;
  svn_revnum_t rev1, rev2;
  const char *mimetype1, *mimetype2;
  svn_string_t *pval;
  apr_array_header_t *propchanges;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_unknown;
  svn_wc_notify_state_t text_state = svn_wc_notify_state_unknown;
  const char *URL1, *path1, *URL2, *path2;
  svn_opt_revision_t *revision1, *revision2;
  svn_error_t *err;

  /* If we are performing a pegged merge, we need to find out what our
     actual URLs will be. */
  if (peg_revision->kind != svn_opt_revision_unspecified)
    {
      SVN_ERR(svn_client__repos_locations(&URL1, &revision1,
                                          &URL2, &revision2,
                                          NULL,
                                          initial_path2 ? initial_path2
                                          : initial_URL2,
                                          peg_revision,
                                          initial_revision1,
                                          initial_revision2,
                                          merge_b->ctx, pool));

      merge_b->url = URL2;
      merge_b->path = NULL;
      path1 = NULL;
      path2 = NULL;
    }
  else
    {
      URL1 = initial_URL1;
      URL2 = initial_URL2;
      path1 = initial_path1;
      path2 = initial_path2;
      revision1 = apr_pcalloc(pool, sizeof(*revision1));
      *revision1 = *initial_revision1;
      revision2 = apr_pcalloc(pool, sizeof(*revision2));
      *revision2 = *initial_revision2;
    }
  
  /* ### heh, funny.  we could be fetching two fulltexts from two
     *totally* different repositories here.  :-) */
  SVN_ERR(single_file_merge_get_file(&tmpfile1, &props1, &rev1,
                                     URL1, path1, revision1,
                                     merge_b, pool));

  SVN_ERR(single_file_merge_get_file(&tmpfile2, &props2, &rev2,
                                     URL2, path2, revision2, 
                                     merge_b, pool));

  /* Discover any svn:mime-type values in the proplists */
  pval = apr_hash_get(props1, SVN_PROP_MIME_TYPE, strlen(SVN_PROP_MIME_TYPE));
  mimetype1 = pval ? pval->data : NULL;

  pval = apr_hash_get(props2, SVN_PROP_MIME_TYPE, strlen(SVN_PROP_MIME_TYPE));
  mimetype2 = pval ? pval->data : NULL;

  /* Deduce property diffs. */
  SVN_ERR(svn_prop_diffs(&propchanges, props2, props1, pool));

  SVN_ERR(merge_file_changed(adm_access,
                             &text_state, &prop_state,
                             merge_b->target,
                             tmpfile1,
                             tmpfile2,
                             rev1,
                             rev2,
                             mimetype1, mimetype2,
                             propchanges, props1,
                             merge_b));

  /* Ignore if temporary file not found. It may have been renamed. */
  err = svn_io_remove_file(tmpfile1, pool);
  if (err && ! APR_STATUS_IS_ENOENT(err->apr_err))
    return err;
  svn_error_clear(err);
  err = svn_io_remove_file(tmpfile2, pool);
  if (err && ! APR_STATUS_IS_ENOENT(err->apr_err))
    return err;
  svn_error_clear(err);
  
  if (merge_b->ctx->notify_func2)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(merge_b->target, svn_wc_notify_update_update,
                               pool);
      notify->kind = svn_node_file;
      notify->content_state = text_state;
      notify->prop_state = prop_state;
      (*merge_b->ctx->notify_func2)(merge_b->ctx->notify_baton2, notify,
                                    pool);
    }

  /* Sleep to ensure timestamp integrity. */
  svn_sleep_for_timestamps();

  return SVN_NO_ERROR;
}


/* A Theoretical Note From Ben, regarding do_diff().

   This function is really svn_client_diff3().  If you read the public
   API description for svn_client_diff, it sounds quite Grand.  It
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

   Perhaps someday a brave soul will truly make svn_client_diff3
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
                          _("Sorry, svn_client_diff3 was called in a way "
                            "that is not yet supported"));
}


/* Perform a diff between two working-copy paths.  
   
   PATH1 and PATH2 are both working copy paths.  REVISION1 and
   REVISION2 are their respective revisions.

   All other options are the same as those passed to svn_client_diff3(). */
static svn_error_t *
diff_wc_wc(const apr_array_header_t *options,
           const char *path1,
           const svn_opt_revision_t *revision1,
           const char *path2,
           const svn_opt_revision_t *revision2,
           svn_boolean_t recurse,
           svn_boolean_t ignore_ancestry,
           const svn_wc_diff_callbacks2_t *callbacks,
           struct diff_cmd_baton *callback_baton,
           svn_client_ctx_t *ctx,
           apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access, *target_access;
  const char *target;

  /* Assert that we have valid input. */
  assert(! svn_path_is_url(path1));
  assert(! svn_path_is_url(path2));

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
                                 path1, FALSE, recurse ? -1 : 0,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 pool));

  /* Resolve named revisions to real numbers. */
  SVN_ERR(svn_client__get_revision_number
          (&callback_baton->revnum1, NULL, revision1, path1, pool));
  callback_baton->revnum2 = SVN_INVALID_REVNUM;  /* WC */

  SVN_ERR(svn_wc_diff3(adm_access, target, callbacks, callback_baton,
                       recurse, ignore_ancestry, pool));
  SVN_ERR(svn_wc_adm_close(adm_access));
  return SVN_NO_ERROR;
}


/* Perform a diff between two repository paths.  
   
   DIFF_PARAM.PATH1 and DIFF_PARAM.PATH2 may be either URLs or the working
   copy paths. DIFF_PARAM.REVISION1 and DIFF_PARAM.REVISION2 are their
   respective revisions. If DIFF_PARAM.PEG_REVISION is specified, 
   DIFF_PARAM.PATH2 is the path at the peg revision, and the actual two
   paths compared are determined by following copy history from PATH2.

   All other options are the same as those passed to svn_client_diff3(). */
static svn_error_t *
diff_repos_repos(const struct diff_parameters *diff_param,
                 const svn_wc_diff_callbacks2_t *callbacks,
                 struct diff_cmd_baton *callback_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_ra_session_t *extra_ra_session;

  const svn_ra_reporter2_t *reporter;
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
           NULL, callbacks, callback_baton, diff_param->recurse,
           FALSE /* doesn't matter for diff */, extra_ra_session, drr.rev1, 
           NULL /* no notify_func */, NULL /* no notify_baton */,
           ctx->cancel_func, ctx->cancel_baton,
           &diff_editor, &diff_edit_baton, pool));
  
  /* We want to switch our txn into URL2 */
  SVN_ERR(svn_ra_do_diff2
          (drr.ra_session, &reporter, &report_baton, drr.rev2, drr.target1,
           diff_param->recurse, diff_param->ignore_ancestry, TRUE,
           drr.url2, diff_editor, diff_edit_baton, pool));

  /* Drive the reporter; do the diff. */
  SVN_ERR(reporter->set_path(report_baton, "", drr.rev1, FALSE, NULL,
                             pool));
  SVN_ERR(reporter->finish_report(report_baton, pool));

  return SVN_NO_ERROR;
}


/* Perform a diff between a repository path and a working-copy path.
   
   PATH1 may be either a URL or a working copy path.  PATH2 is a
   working copy path.  REVISION1 and REVISION2 are their respective
   revisions.  If REVERSE is TRUE, the diff will be done in reverse.
   If PEG_REVISION is specified, then PATH1 is the path in the peg
   revision, and the actual repository path to be compared is
   determined by following copy history.

   All other options are the same as those passed to svn_client_diff3(). */
static svn_error_t *
diff_repos_wc(const apr_array_header_t *options,
              const char *path1,
              const svn_opt_revision_t *revision1,
              const svn_opt_revision_t *peg_revision,
              const char *path2,
              const svn_opt_revision_t *revision2,
              svn_boolean_t reverse,
              svn_boolean_t recurse,
              svn_boolean_t ignore_ancestry,
              const svn_wc_diff_callbacks2_t *callbacks,
              struct diff_cmd_baton *callback_baton,
              svn_client_ctx_t *ctx,
              apr_pool_t *pool)
{
  const char *url1, *anchor, *anchor_url, *target;
  svn_wc_adm_access_t *adm_access, *dir_access;
  const svn_wc_entry_t *entry;
  svn_revnum_t rev;
  svn_ra_session_t *ra_session;
  const svn_ra_reporter2_t *reporter;
  void *report_baton;
  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;
  svn_boolean_t rev2_is_base = (revision2->kind == svn_opt_revision_base);

  /* Assert that we have valid input. */
  assert(! svn_path_is_url(path2));

  /* Convert path1 to a URL to feed to do_diff. */
  SVN_ERR(convert_to_url(&url1, path1, pool));

  SVN_ERR(svn_wc_adm_open_anchor(&adm_access, &dir_access, &target,
                                 path2, FALSE, recurse ? -1 : 0,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 pool));
  anchor = svn_wc_adm_access_path(adm_access);

  /* Fetch the URL of the anchor directory. */
  SVN_ERR(svn_wc_entry(&entry, anchor, adm_access, FALSE, pool));
  if (! entry)
    return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                             _("'%s' is not under version control"),
                             svn_path_local_style(anchor, pool));
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
      
  SVN_ERR(svn_wc_get_diff_editor3(adm_access, target,
                                  callbacks, callback_baton,
                                  recurse,
                                  ignore_ancestry,
                                  rev2_is_base,
                                  reverse,
                                  ctx->cancel_func, ctx->cancel_baton,
                                  &diff_editor, &diff_edit_baton,
                                  pool));

  /* Tell the RA layer we want a delta to change our txn to URL1 */
  SVN_ERR(svn_client__get_revision_number
          (&rev, ra_session, revision1, 
           (path1 == url1) ? NULL : path1, pool));

  if (!reverse)
    callback_baton->revnum1 = rev;
  else
    callback_baton->revnum2 = rev;

  SVN_ERR(svn_ra_do_diff2(ra_session,
                          &reporter, &report_baton,
                          rev,
                          target ? svn_path_uri_decode(target, pool) : NULL,
                          recurse,
                          ignore_ancestry,
                          TRUE,  /* text_deltas */
                          url1,
                          diff_editor, diff_edit_baton, pool));

  /* Create a txn mirror of path2;  the diff editor will print
     diffs in reverse.  :-)  */
  SVN_ERR(svn_wc_crawl_revisions2(path2, dir_access,
                                  reporter, report_baton,
                                  FALSE, recurse, FALSE,
                                  NULL, NULL, /* notification is N/A */
                                  NULL, pool));

  SVN_ERR(svn_wc_adm_close(adm_access));
  return SVN_NO_ERROR;
}


/* This is basically just the guts of svn_client_diff[_peg]3(). */
static svn_error_t *
do_diff(const struct diff_parameters *diff_param,
        const svn_wc_diff_callbacks2_t *callbacks,
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
          SVN_ERR(diff_repos_repos(diff_param, callbacks, callback_baton,
                                   ctx, pool));
        }
      else /* path2 is a working copy path */
        {
          SVN_ERR(diff_repos_wc(diff_param->options,
                                diff_param->path1, diff_param->revision1,
                                diff_param->peg_revision,
                                diff_param->path2, diff_param->revision2,
                                FALSE, diff_param->recurse,
                                diff_param->ignore_ancestry,
                                callbacks, callback_baton, ctx, pool));
        }
    }
  else /* path1 is a working copy path */
    {
      if (diff_paths.is_repos2)
        {
          SVN_ERR(diff_repos_wc(diff_param->options,
                                diff_param->path2, diff_param->revision2,
                                diff_param->peg_revision,
                                diff_param->path1, diff_param->revision1,
                                TRUE, diff_param->recurse,
                                diff_param->ignore_ancestry,
                                callbacks, callback_baton, ctx, pool));
        }
      else /* path2 is a working copy path */
        {
          SVN_ERR(diff_wc_wc(diff_param->options,
                             diff_param->path1, diff_param->revision1,
                             diff_param->path2, diff_param->revision2,
                             diff_param->recurse,
                             diff_param->ignore_ancestry,
                             callbacks, callback_baton, ctx, pool));
        }
    }

  return SVN_NO_ERROR;
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

  const svn_ra_reporter2_t *reporter;
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

  /* Set up the repos_diff editor on BASE_PATH, if available.
     Otherwise, we just use "". */
  SVN_ERR(svn_client__get_diff_summarize_editor
          (drr.base_path ? drr.base_path : "", summarize_func,
           summarize_baton, extra_ra_session, drr.rev1, ctx->cancel_func,
           ctx->cancel_baton, &diff_editor, &diff_edit_baton, pool));
  
  /* We want to switch our txn into URL2 */
  SVN_ERR(svn_ra_do_diff2
          (drr.ra_session, &reporter, &report_baton, drr.rev2, drr.target1,
           diff_param->recurse, diff_param->ignore_ancestry,
           FALSE /* do not create text delta */, drr.url2, diff_editor,
           diff_edit_baton, pool));

  /* Drive the reporter; do the diff. */
  SVN_ERR(reporter->set_path(report_baton, "", drr.rev1, FALSE, NULL,
                             pool));
  SVN_ERR(reporter->finish_report(report_baton, pool));

  return SVN_NO_ERROR;
}

/* This is basically just the guts of svn_client_diff_summarize[_peg](). */
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
    {
      SVN_ERR(diff_summarize_repos_repos(diff_param, summarize_func,
                                         summarize_baton, ctx, pool));
    }
  else
    return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                            _("Summarizing diff can only compare repository "
                              "to repository"));

  return SVN_NO_ERROR;
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
svn_client_diff3(const apr_array_header_t *options,
                 const char *path1,
                 const svn_opt_revision_t *revision1,
                 const char *path2,
                 const svn_opt_revision_t *revision2,
                 svn_boolean_t recurse,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t no_diff_deleted,
                 svn_boolean_t ignore_content_type,
                 const char *header_encoding,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  struct diff_parameters diff_params;

  struct diff_cmd_baton diff_cmd_baton;
  svn_wc_diff_callbacks2_t diff_callbacks;

  /* We will never do a pegged diff from here. */
  svn_opt_revision_t peg_revision;
  peg_revision.kind = svn_opt_revision_unspecified;

  /* fill diff_param */
  diff_params.options = options;
  diff_params.path1 = path1;
  diff_params.revision1 = revision1;
  diff_params.path2 = path2;
  diff_params.revision2 = revision2;
  diff_params.peg_revision = &peg_revision;
  diff_params.recurse = recurse;
  diff_params.ignore_ancestry = ignore_ancestry;
  diff_params.no_diff_deleted = no_diff_deleted;

  /* setup callback and baton */
  diff_callbacks.file_changed = diff_file_changed;
  diff_callbacks.file_added = diff_file_added;
  diff_callbacks.file_deleted = no_diff_deleted ? diff_file_deleted_no_diff :
                                                  diff_file_deleted_with_diff;
  diff_callbacks.dir_added =  diff_dir_added;
  diff_callbacks.dir_deleted = diff_dir_deleted;
  diff_callbacks.dir_props_changed = diff_props_changed;
    
  diff_cmd_baton.orig_path_1 = path1;
  diff_cmd_baton.orig_path_2 = path2;

  diff_cmd_baton.options = options;
  diff_cmd_baton.pool = pool;
  diff_cmd_baton.outfile = outfile;
  diff_cmd_baton.errfile = errfile;
  diff_cmd_baton.header_encoding = header_encoding;
  diff_cmd_baton.revnum1 = SVN_INVALID_REVNUM;
  diff_cmd_baton.revnum2 = SVN_INVALID_REVNUM;

  diff_cmd_baton.config = ctx->config;
  diff_cmd_baton.force_empty = FALSE;
  diff_cmd_baton.force_binary = ignore_content_type;

  return do_diff(&diff_params, &diff_callbacks, &diff_cmd_baton, ctx, pool);
}

svn_error_t *
svn_client_diff2(const apr_array_header_t *options,
                 const char *path1,
                 const svn_opt_revision_t *revision1,
                 const char *path2,
                 const svn_opt_revision_t *revision2,
                 svn_boolean_t recurse,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t no_diff_deleted,
                 svn_boolean_t ignore_content_type,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  return svn_client_diff3(options, path1, revision1, path2, revision2,
                          recurse, ignore_ancestry, no_diff_deleted,
                          ignore_content_type, SVN_APR_LOCALE_CHARSET,
                          outfile, errfile, ctx, pool);
}

svn_error_t *
svn_client_diff(const apr_array_header_t *options,
                const char *path1,
                const svn_opt_revision_t *revision1,
                const char *path2,
                const svn_opt_revision_t *revision2,
                svn_boolean_t recurse,
                svn_boolean_t ignore_ancestry,
                svn_boolean_t no_diff_deleted,
                apr_file_t *outfile,
                apr_file_t *errfile,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  return svn_client_diff2(options, path1, revision1, path2, revision2,
                          recurse, ignore_ancestry, no_diff_deleted, FALSE,
                          outfile, errfile, ctx, pool);
}

svn_error_t *
svn_client_diff_peg3(const apr_array_header_t *options,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *start_revision,
                     const svn_opt_revision_t *end_revision,
                     svn_boolean_t recurse,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t no_diff_deleted,
                     svn_boolean_t ignore_content_type,
                     const char *header_encoding,
                     apr_file_t *outfile,
                     apr_file_t *errfile,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  struct diff_parameters diff_params;

  struct diff_cmd_baton diff_cmd_baton;
  svn_wc_diff_callbacks2_t diff_callbacks;

  /* fill diff_param */
  diff_params.options = options;
  diff_params.path1 = path;
  diff_params.revision1 = start_revision;
  diff_params.path2 = path;
  diff_params.revision2 = end_revision;
  diff_params.peg_revision = peg_revision;
  diff_params.recurse = recurse;
  diff_params.ignore_ancestry = ignore_ancestry;
  diff_params.no_diff_deleted = no_diff_deleted;

  /* setup callback and baton */
  diff_callbacks.file_changed = diff_file_changed;
  diff_callbacks.file_added = diff_file_added;
  diff_callbacks.file_deleted = no_diff_deleted ? diff_file_deleted_no_diff :
                                                  diff_file_deleted_with_diff;
  diff_callbacks.dir_added =  diff_dir_added;
  diff_callbacks.dir_deleted = diff_dir_deleted;
  diff_callbacks.dir_props_changed = diff_props_changed;
    
  diff_cmd_baton.orig_path_1 = path;
  diff_cmd_baton.orig_path_2 = path;

  diff_cmd_baton.options = options;
  diff_cmd_baton.pool = pool;
  diff_cmd_baton.outfile = outfile;
  diff_cmd_baton.errfile = errfile;
  diff_cmd_baton.header_encoding = header_encoding;
  diff_cmd_baton.revnum1 = SVN_INVALID_REVNUM;
  diff_cmd_baton.revnum2 = SVN_INVALID_REVNUM;

  diff_cmd_baton.config = ctx->config;
  diff_cmd_baton.force_empty = FALSE;
  diff_cmd_baton.force_binary = ignore_content_type;

  return do_diff(&diff_params, &diff_callbacks, &diff_cmd_baton, ctx, pool);
}

svn_error_t *
svn_client_diff_peg2(const apr_array_header_t *options,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *start_revision,
                     const svn_opt_revision_t *end_revision,
                     svn_boolean_t recurse,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t no_diff_deleted,
                     svn_boolean_t ignore_content_type,
                     apr_file_t *outfile,
                     apr_file_t *errfile,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  return svn_client_diff_peg3(options, path, peg_revision, start_revision,
                              end_revision, recurse, ignore_ancestry,
                              no_diff_deleted, ignore_content_type,
                              SVN_APR_LOCALE_CHARSET, outfile, errfile,
                              ctx, pool);
}

svn_error_t *
svn_client_diff_peg(const apr_array_header_t *options,
                    const char *path,
                    const svn_opt_revision_t *peg_revision,
                    const svn_opt_revision_t *start_revision,
                    const svn_opt_revision_t *end_revision,
                    svn_boolean_t recurse,
                    svn_boolean_t ignore_ancestry,
                    svn_boolean_t no_diff_deleted,
                    apr_file_t *outfile,
                    apr_file_t *errfile,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  return svn_client_diff_peg2(options, path, peg_revision,
                              start_revision, end_revision, recurse,
                              ignore_ancestry, no_diff_deleted, FALSE,
                              outfile, errfile, ctx, pool);
}

svn_error_t *
svn_client_diff_summarize(const char *path1,
                          const svn_opt_revision_t *revision1,
                          const char *path2,
                          const svn_opt_revision_t *revision2,
                          svn_boolean_t recurse,
                          svn_boolean_t ignore_ancestry,
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
  diff_params.options = NULL;
  diff_params.path1 = path1;
  diff_params.revision1 = revision1;
  diff_params.path2 = path2;
  diff_params.revision2 = revision2;
  diff_params.peg_revision = &peg_revision;
  diff_params.recurse = recurse;
  diff_params.ignore_ancestry = ignore_ancestry;
  diff_params.no_diff_deleted = FALSE;

  return do_diff_summarize(&diff_params, summarize_func, summarize_baton,
                           ctx, pool);
}

svn_error_t *
svn_client_diff_summarize_peg(const char *path,
                              const svn_opt_revision_t *peg_revision,
                              const svn_opt_revision_t *start_revision,
                              const svn_opt_revision_t *end_revision,
                              svn_boolean_t recurse,
                              svn_boolean_t ignore_ancestry,
                              svn_client_diff_summarize_func_t summarize_func,
                              void *summarize_baton,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *pool)
{
  struct diff_parameters diff_params;

  /* fill diff_param */
  diff_params.options = NULL;
  diff_params.path1 = path;
  diff_params.revision1 = start_revision;
  diff_params.path2 = path;
  diff_params.revision2 = end_revision;
  diff_params.peg_revision = peg_revision;
  diff_params.recurse = recurse;
  diff_params.ignore_ancestry = ignore_ancestry;
  diff_params.no_diff_deleted = FALSE;

  return do_diff_summarize(&diff_params, summarize_func, summarize_baton,
                           ctx, pool);
}

svn_error_t *
svn_client_merge2(const char *source1,
                  const svn_opt_revision_t *revision1,
                  const char *source2,
                  const svn_opt_revision_t *revision2,
                  const char *target_wcpath,
                  svn_boolean_t recurse,
                  svn_boolean_t ignore_ancestry,
                  svn_boolean_t force,
                  svn_boolean_t dry_run,
                  const apr_array_header_t *merge_options,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  struct merge_cmd_baton merge_cmd_baton;
  const char *URL1, *URL2;
  const char *path1, *path2;
  svn_opt_revision_t peg_revision;

  /* This is not a pegged merge. */
  peg_revision.kind = svn_opt_revision_unspecified;

  /* if source1 or source2 are paths, we need to get the underlying url
   * from the wc and save the initial path we were passed so we can use it as 
   * a path parameter (either in the baton or not).  otherwise, the path 
   * will just be NULL, which means we won't be able to figure out some kind 
   * of revision specifications, but in that case it won't matter, because 
   * those ways of specifying a revision are meaningless for a url.
   */
  SVN_ERR(svn_client_url_from_path(&URL1, source1, pool));
  if (! URL1)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                             _("'%s' has no URL"),
                             svn_path_local_style(source1, pool));

  SVN_ERR(svn_client_url_from_path(&URL2, source2, pool));
  if (! URL2)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL, 
                             _("'%s' has no URL"),
                             svn_path_local_style(source2, pool));

  if (URL1 == source1)
    path1 = NULL;
  else
    path1 = source1;

  if (URL2 == source2)
    path2 = NULL;
  else
    path2 = source2;

  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, target_wcpath,
                                 ! dry_run, recurse ? -1 : 0,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 pool));

  SVN_ERR(svn_wc_entry(&entry, target_wcpath, adm_access, FALSE, pool));
  if (entry == NULL)
    return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                             _("'%s' is not under version control"), 
                             svn_path_local_style(target_wcpath, pool));

  merge_cmd_baton.force = force;
  merge_cmd_baton.dry_run = dry_run;
  merge_cmd_baton.merge_options = merge_options;
  merge_cmd_baton.target = target_wcpath;
  merge_cmd_baton.url = URL2;
  merge_cmd_baton.revision = revision2;
  merge_cmd_baton.path = path2;
  merge_cmd_baton.added_path = NULL;
  merge_cmd_baton.add_necessitated_merge = FALSE;
  merge_cmd_baton.dry_run_deletions = (dry_run ? apr_hash_make(pool) : NULL);
  merge_cmd_baton.ctx = ctx;
  merge_cmd_baton.pool = pool;

  /* Set up the diff3 command, so various callers don't have to. */
  {
    svn_config_t *cfg =
      ctx->config ? apr_hash_get(ctx->config, SVN_CONFIG_CATEGORY_CONFIG,
                                 APR_HASH_KEY_STRING) : NULL;

    svn_config_get(cfg, &(merge_cmd_baton.diff3_cmd),
                   SVN_CONFIG_SECTION_HELPERS,
                   SVN_CONFIG_OPTION_DIFF3_CMD, NULL);
  }

  /* If our target_wcpath is a single file, assume that PATH1 and
     PATH2 are files as well, and do a single-file merge. */
  if (entry->kind == svn_node_file)
    {
      SVN_ERR(do_single_file_merge(URL1, path1, revision1,
                                   URL2, path2, revision2,
                                   &peg_revision,
                                   target_wcpath,
                                   adm_access,
                                   &merge_cmd_baton,
                                   pool));
    }

  /* Otherwise, this must be a directory merge.  Do the fancy
     recursive diff-editor thing. */
  else if (entry->kind == svn_node_dir)
    {
      SVN_ERR(do_merge(URL1,
                       path1,
                       revision1,
                       URL2,
                       merge_cmd_baton.path,
                       revision2,
                       &peg_revision,
                       target_wcpath,
                       adm_access,
                       recurse,
                       ignore_ancestry,
                       dry_run,
                       &merge_callbacks,
                       &merge_cmd_baton,
                       ctx,
                       pool));
    }

  SVN_ERR(svn_wc_adm_close(adm_access));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_merge(const char *source1,
                 const svn_opt_revision_t *revision1,
                 const char *source2,
                 const svn_opt_revision_t *revision2,
                 const char *target_wcpath,
                 svn_boolean_t recurse,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t force,
                 svn_boolean_t dry_run,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  return svn_client_merge2(source1, revision1, source2, revision2,
                           target_wcpath, recurse, ignore_ancestry, force,
                           dry_run, NULL, ctx, pool);
}

svn_error_t *
svn_client_merge_peg2(const char *source,
                      const svn_opt_revision_t *revision1,
                      const svn_opt_revision_t *revision2,
                      const svn_opt_revision_t *peg_revision,
                      const char *target_wcpath,
                      svn_boolean_t recurse,
                      svn_boolean_t ignore_ancestry,
                      svn_boolean_t force,
                      svn_boolean_t dry_run,
                      const apr_array_header_t *merge_options,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  struct merge_cmd_baton merge_cmd_baton;
  const char *URL;
  const char *path;

  /* if source1 or source2 are paths, we need to get the underlying url
   * from the wc and save the initial path we were passed so we can use it as 
   * a path parameter (either in the baton or not).  otherwise, the path 
   * will just be NULL, which means we won't be able to figure out some kind 
   * of revision specifications, but in that case it won't matter, because 
   * those ways of specifying a revision are meaningless for a url.
   */
  SVN_ERR(svn_client_url_from_path(&URL, source, pool));
  if (! URL)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                             _("'%s' has no URL"),
                             svn_path_local_style(source, pool));
  if (URL == source)
    path = NULL;
  else
    path = source;

  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, target_wcpath,
                                 ! dry_run, recurse ? -1 : 0,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 pool));

  SVN_ERR(svn_wc_entry(&entry, target_wcpath, adm_access, FALSE, pool));
  if (entry == NULL)
    return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                             _("'%s' is not under version control"), 
                             svn_path_local_style(target_wcpath, pool));

  merge_cmd_baton.force = force;
  merge_cmd_baton.dry_run = dry_run;
  merge_cmd_baton.merge_options = merge_options;
  merge_cmd_baton.target = target_wcpath;
  merge_cmd_baton.url = URL;
  merge_cmd_baton.revision = revision2;
  merge_cmd_baton.path = path;
  merge_cmd_baton.added_path = NULL;
  merge_cmd_baton.add_necessitated_merge = FALSE;
  merge_cmd_baton.dry_run_deletions = (dry_run ? apr_hash_make(pool) : NULL);
  merge_cmd_baton.ctx = ctx;
  merge_cmd_baton.pool = pool;

  /* Set up the diff3 command, so various callers don't have to. */
  {
    svn_config_t *cfg =
      ctx->config ? apr_hash_get(ctx->config, SVN_CONFIG_CATEGORY_CONFIG,
                                 APR_HASH_KEY_STRING) : NULL;

    svn_config_get(cfg, &(merge_cmd_baton.diff3_cmd),
                   SVN_CONFIG_SECTION_HELPERS,
                   SVN_CONFIG_OPTION_DIFF3_CMD, NULL);
  }

  /* If our target_wcpath is a single file, assume that PATH1 and
     PATH2 are files as well, and do a single-file merge. */
  if (entry->kind == svn_node_file)
    {
      SVN_ERR(do_single_file_merge(URL, path, revision1,
                                   URL, path, revision2,
                                   peg_revision,
                                   target_wcpath,
                                   adm_access,
                                   &merge_cmd_baton,
                                   pool));
    }

  /* Otherwise, this must be a directory merge.  Do the fancy
     recursive diff-editor thing. */
  else if (entry->kind == svn_node_dir)
    {
      SVN_ERR(do_merge(URL,
                       path,
                       revision1,
                       URL,
                       path,
                       revision2,
                       peg_revision,
                       target_wcpath,
                       adm_access,
                       recurse,
                       ignore_ancestry,
                       dry_run,
                       &merge_callbacks,
                       &merge_cmd_baton,
                       ctx,
                       pool));
    }

  SVN_ERR(svn_wc_adm_close(adm_access));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_merge_peg(const char *source,
                     const svn_opt_revision_t *revision1,
                     const svn_opt_revision_t *revision2,
                     const svn_opt_revision_t *peg_revision,
                     const char *target_wcpath,
                     svn_boolean_t recurse,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t force,
                     svn_boolean_t dry_run,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  return svn_client_merge_peg2(source, revision1, revision2, peg_revision,
                               target_wcpath, recurse, ignore_ancestry, force,
                               dry_run, NULL, ctx, pool);
}
