/*
 * diff.c: comparing and merging
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
#include "svn_test.h"
#include "svn_io.h"
#include "svn_utf.h"
#include "svn_pools.h"
#include "svn_config.h"
#include "client.h"
#include <assert.h>


/*
 * Constant separator strings
 */
static const char equal_string[] = 
  "===================================================================";
static const char under_string[] =
  "___________________________________________________________________";


/*-----------------------------------------------------------------*/

/* Utilities */

/* A helper func that writes out verbal descriptions of property diffs
   to FILE.   Of course, the apr_file_t will probably be the 'outfile'
   passed to svn_client_diff, which is probably stdout. */
static svn_error_t *
display_prop_diffs (const apr_array_header_t *propchanges,
                    apr_hash_t *original_props,
                    const char *path,
                    apr_file_t *file,
                    apr_pool_t *pool)
{
  int i;

  SVN_ERR (svn_io_file_printf (file,
                               APR_EOL_STR "Property changes on: %s"
                               APR_EOL_STR, path));

  /* ### todo [issue #1533]: Use svn_io_file_printf() to convert this
     line of dashes to native encoding, at least conditionally?  Or is
     it better to have under_string always output the same, so
     programs can find it?  Also, what about checking for error? */
  apr_file_printf (file, "%s" APR_EOL_STR, under_string);

  for (i = 0; i < propchanges->nelts; i++)
    {
      const svn_prop_t *propchange
        = &APR_ARRAY_IDX(propchanges, i, svn_prop_t);

      const svn_string_t *original_value;

      if (original_props)
        original_value = apr_hash_get (original_props, 
                                       propchange->name, APR_HASH_KEY_STRING);
      else
        original_value = NULL;
      
      SVN_ERR (svn_io_file_printf (file, "Name: %s" APR_EOL_STR,
                                   propchange->name));

      /* For now, we have a rather simple heuristic: if this is an
         "svn:" property, then assume the value is UTF-8 and must
         therefore be converted before printing.  Otherwise, just
         print whatever's there and hope for the best. */
      {
        svn_boolean_t val_is_utf8 = svn_prop_is_svn_prop (propchange->name);
        
        if (original_value != NULL)
          {
            if (val_is_utf8)
              {
                SVN_ERR (svn_io_file_printf
                         (file, "   - %s" APR_EOL_STR, original_value->data));
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
                SVN_ERR (svn_io_file_printf
                         (file, "   + %s" APR_EOL_STR,
                          propchange->value->data));
              }
            else
              {
                /* ### todo: check for error? */
                apr_file_printf (file, "   + %s" APR_EOL_STR,
                                 propchange->value->data);
              }
          }
      }
    }

  /* ### todo [issue #1533]: Use svn_io_file_printf() to convert this
     to native encoding, at least conditionally?  Or is it better to
     have under_string always output the same eol, so programs can
     find it consistently?  Also, what about checking for error? */
  apr_file_printf (file, APR_EOL_STR);

  return SVN_NO_ERROR;
}


/* Return SVN_ERR_UNSUPPORTED_FEATURE if @a url's schema does not
   match the schema of the url for @a adm_access's path; return
   SVN_ERR_BAD_URL if no schema can be found for one or both urls;
   otherwise return SVN_NO_ERROR.  Use @a adm_access's pool for
   temporary allocation. */
static svn_error_t *
check_schema_match (svn_wc_adm_access_t *adm_access, const char *url)
{
  const char *path = svn_wc_adm_access_path (adm_access);
  apr_pool_t *pool = svn_wc_adm_access_pool (adm_access);
  const svn_wc_entry_t *ent;
  const char *idx1, *idx2;
  
  SVN_ERR (svn_wc_entry (&ent, path, adm_access, TRUE, pool));
  
  idx1 = strchr (url, ':');
  idx2 = strchr (ent->url, ':');

  if ((idx1 == NULL) && (idx2 == NULL))
    {
      return svn_error_createf
        (SVN_ERR_BAD_URL, NULL,
         "URLs have no schemas:\n"
         "   '%s'\n"
         "   '%s'", url, ent->url);
    }
  else if (idx1 == NULL)
    {
      return svn_error_createf
        (SVN_ERR_BAD_URL, NULL,
         "URL has no schema: '%s'\n", url);
    }
  else if (idx2 == NULL)
    {
      return svn_error_createf
        (SVN_ERR_BAD_URL, NULL,
         "URL has no schema: '%s'\n", ent->url);
    }
  else if (((idx1 - url) != (idx2 - ent->url))
           || (strncmp (url, ent->url, idx1 - url) != 0))
    {
      return svn_error_createf
        (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
         "Access method (schema) mixtures not yet supported:\n"
         "   '%s'\n"
         "   '%s'\n"
         "See http://subversion.tigris.org/issues/show_bug.cgi?id=1321 "
         "for details.", url, ent->url);
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

  /* The original targets passed to the diff command.  We may need
     these to construct distinctive diff labels when comparing the
     same relative path in the same revision, under different anchors
     (for example, when comparing a trunk against a branch). */
  const char *orig_path_1;
  const char *orig_path_2;

  /* These are the numeric representations of the revisions passed to
     svn_client_diff, either may be SVN_INVALID_REVNUM.  We need these
     because some of the svn_wc_diff_callbacks_t don't get revision
     arguments.

     ### Perhaps we should change the callback signatures and eliminate
     ### these?
  */
  svn_revnum_t revnum1;
  svn_revnum_t revnum2;

  /* Client config hash (may be NULL). */
  apr_hash_t *config;

  /* Set this flag if you want diff_file_changed to output diffs
     unconditionally, even if the diffs are empty. */
  svn_boolean_t force_diff_output;

};


/* Generate a label for the diff output for file PATH at revision REVNUM.
   If REVNUM is invalid then it is assumed to be the current working
   copy. */
static const char *
diff_label (const char *path,
            svn_revnum_t revnum,
            apr_pool_t *pool)
{
  const char *label;
  if (revnum != SVN_INVALID_REVNUM)
    label = apr_psprintf (pool, "%s\t(revision %" SVN_REVNUM_T_FMT ")",
                          path, revnum);
  else
    label = apr_psprintf (pool, "%s\t(working copy)", path);

  return label;
}

/* The main workhorse, which invokes an external 'diff' program on the
   two temporary files.   The path is the "true" label to use in the
   diff output. */
static svn_error_t *
diff_file_changed (svn_wc_adm_access_t *adm_access,
                   svn_wc_notify_state_t *state,
                   const char *path,
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
  apr_file_t *outfile = diff_cmd_baton->outfile;
  apr_file_t *errfile = diff_cmd_baton->errfile;
  apr_pool_t *subpool = svn_pool_create (diff_cmd_baton->pool);
  const char *label1, *label2;

  /* Execute local diff command on these two paths, print to stdout. */
  nargs = diff_cmd_baton->options->nelts;
  if (nargs)
    {
      int i;
      args = apr_palloc (subpool, nargs * sizeof (char *));
      for (i = 0; i < diff_cmd_baton->options->nelts; i++)
        {
          args[i] = 
            ((const char **)(diff_cmd_baton->options->elts))[i];
        }
      assert (i == nargs);
    }

  if (rev1 == rev2)
    {
      /* ### Holy cow.  Due to anchor/target weirdness, we can't
         simply join diff_cmd_baton->orig_path_1 with path, ditto for
         orig_path_2.  That will work when they're directory URLs, but
         not for file URLs.  Nor can we just use anchor1 and anchor2
         from do_diff(), at least not without some more logic here.
         What a nightmare.

         For now, to distinguish the two paths, we'll just put the
         unique portions of the original targets in parentheses before
         the received path, with ellipses for handwaving.  This makes
         the labels a bit clumsy, but at least distinctive.  Better
         solutions are possible, they'll just take more thought. */
      const char *path1 = diff_cmd_baton->orig_path_1;
      const char *path2 = diff_cmd_baton->orig_path_2;
      int i;

      for (i = 0; path1[i] && path2[i] && (path1[i] == path2[i]); i++)
        ;

      path1 = path1 + i;
      path2 = path2 + i;

      if (path1[0] == '\0')
        path1 = apr_psprintf (subpool, "%s", path);
      else if (path1[0] == '/')
        path1 = apr_psprintf (subpool, "%s\t(...%s)", path, path1);
      else
        path1 = apr_psprintf (subpool, "%s\t(.../%s)", path, path1);

      if (path2[0] == '\0')
        path2 = apr_psprintf (subpool, "%s", path);
      else if (path2[0] == '/')
        path2 = apr_psprintf (subpool, "%s\t(...%s)", path, path2);
      else
        path2 = apr_psprintf (subpool, "%s\t(.../%s)", path, path2);
      
      label1 = diff_label (path1, rev1, subpool);
      label2 = diff_label (path2, rev2, subpool);
    }
  else
    {
      label1 = diff_label (path, rev1, subpool);
      label2 = diff_label (path, rev2, subpool);
    }

  /* Find out if we need to run an external diff */
  if (diff_cmd_baton->config)
    {
      svn_config_t *cfg = apr_hash_get (diff_cmd_baton->config,
                                        SVN_CONFIG_CATEGORY_CONFIG,
                                        APR_HASH_KEY_STRING);
      svn_config_get (cfg, &diff_cmd, SVN_CONFIG_SECTION_HELPERS,
                      SVN_CONFIG_OPTION_DIFF_CMD, NULL);
    }

  if (diff_cmd)
    {
      /* Print out the diff header. */
      SVN_ERR (svn_io_file_printf (outfile, "Index: %s" APR_EOL_STR
                                   "%s" APR_EOL_STR, path, equal_string));

      SVN_ERR (svn_io_run_diff (".", args, nargs, label1, label2,
                                tmpfile1, tmpfile2, 
                                &exitcode, outfile, errfile,
                                diff_cmd, subpool));
    }
  else
    {
      svn_diff_t *diff;

      /* We don't currently support any options (well, other than -u, since we 
         default to unified diff output anyway), so if we received anything 
         other than that it's an error. */
      if (diff_cmd_baton->options)
        {
          int i;

          for (i = 0; i < diff_cmd_baton->options->nelts; ++i)
            {
              const char *arg
                = ((const char **)(diff_cmd_baton->options->elts))[i];

              if (strcmp(arg, "-u") == 0)
                continue;
              else
                return svn_error_createf(SVN_ERR_INVALID_DIFF_OPTION, NULL,
                                         "'%s' is not supported", arg);
            }
        }

      SVN_ERR (svn_diff_file_diff (&diff, tmpfile1, tmpfile2, subpool));
      if (svn_diff_contains_diffs (diff) || diff_cmd_baton->force_diff_output)
        {
          svn_boolean_t mt1_binary = FALSE, mt2_binary = FALSE;

          /* Print out the diff header. */
          SVN_ERR (svn_io_file_printf (outfile, "Index: %s" APR_EOL_STR
                                       "%s" APR_EOL_STR, path, equal_string));

          /* If either file is marked as a known binary type, just
             print a warning. */
          if (mimetype1)
            mt1_binary = svn_mime_type_is_binary (mimetype1);
          if (mimetype2)
            mt2_binary = svn_mime_type_is_binary (mimetype2);

          if (mt1_binary || mt2_binary)
            {
              SVN_ERR (svn_io_file_printf 
                       (outfile,
                        "Cannot display: file marked as a binary type."
                        APR_EOL_STR));
              
              if (mt1_binary && !mt2_binary)
                SVN_ERR (svn_io_file_printf (outfile,
                                             "svn:mime-type = %s" APR_EOL_STR,
                                             mimetype1));
              else if (mt2_binary && !mt1_binary)
                SVN_ERR (svn_io_file_printf (outfile,
                                             "svn:mime-type = %s" APR_EOL_STR,
                                             mimetype2));
              else if (mt1_binary && mt2_binary)
                {
                  if (strcmp (mimetype1, mimetype2) == 0)
                    SVN_ERR (svn_io_file_printf
                             (outfile,
                              "svn:mime-type = %s" APR_EOL_STR,
                              mimetype1));
                  else
                    SVN_ERR (svn_io_file_printf
                             (outfile,
                              "svn:mime-type = (%s, %s)" APR_EOL_STR,
                              mimetype1, mimetype2));
                }
            }
          else
            {
              /* Output the actual diff */
              SVN_ERR (svn_diff_file_output_unified (outfile, diff,
                                                     tmpfile1, tmpfile2,
                                                     label1, label2,
                                                     subpool));
            }
        }
    }

  /* ### todo: someday we'll need to worry about whether we're going
     to need to write a diff plug-in mechanism that makes use of the
     two paths, instead of just blindly running SVN_CLIENT_DIFF.  */

  if (state)
    *state = svn_wc_notify_state_unknown;

  /* Destroy the subpool. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}

/* The because the repos-diff editor passes at least one empty file to
   each of these next two functions, they can be dumb wrappers around
   the main workhorse routine. */
static svn_error_t *
diff_file_added (svn_wc_adm_access_t *adm_access,
                 svn_wc_notify_state_t *state,
                 const char *path,
                 const char *tmpfile1,
                 const char *tmpfile2,
                 svn_revnum_t rev1,
                 svn_revnum_t rev2,
                 const char *mimetype1,
                 const char *mimetype2,
                 void *diff_baton)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;

  /* We want diff_file_changed to unconditionally show diffs, even if
     the diff is empty (as would be the case if an empty file were
     added.)  It's important, because 'patch' would still see an empty
     diff and create an empty file.  It's also important to let the
     user see that *something* happened. */
  diff_cmd_baton->force_diff_output = TRUE;

  SVN_ERR (diff_file_changed (adm_access, state, path, tmpfile1, tmpfile2, 
                              rev1, rev2,
                              mimetype1, mimetype2, diff_baton));
  
  diff_cmd_baton->force_diff_output = FALSE;

  return SVN_NO_ERROR;
}

static svn_error_t *
diff_file_deleted_with_diff (svn_wc_adm_access_t *adm_access,
                             svn_wc_notify_state_t *state,
                             const char *path,
                             const char *tmpfile1,
                             const char *tmpfile2,
                             const char *mimetype1,
                             const char *mimetype2,
                             void *diff_baton)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;

  return diff_file_changed (adm_access, state, path, tmpfile1, tmpfile2, 
                            diff_cmd_baton->revnum1, diff_cmd_baton->revnum2,
                            mimetype1, mimetype2, diff_baton);
}

static svn_error_t *
diff_file_deleted_no_diff (svn_wc_adm_access_t *adm_access,
                           svn_wc_notify_state_t *state,
                           const char *path,
                           const char *tmpfile1,
                           const char *tmpfile2,
                           const char *mimetype1,
                           const char *mimetype2,
                           void *diff_baton)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;

  if (state)
    *state = svn_wc_notify_state_unknown;

  SVN_ERR (svn_io_file_printf
           (diff_cmd_baton->outfile,
            "Index: %s (deleted)" APR_EOL_STR "%s" APR_EOL_STR, 
            path, equal_string));

  return SVN_NO_ERROR;
}

/* For now, let's have 'svn diff' send feedback to the top-level
   application, so that something reasonable about directories and
   propsets gets printed to stdout. */
static svn_error_t *
diff_dir_added (svn_wc_adm_access_t *adm_access,
                svn_wc_notify_state_t *state,
                const char *path,
                svn_revnum_t rev,
                void *diff_baton)
{
  /* ### todo:  send feedback to app */
  return SVN_NO_ERROR;
}

static svn_error_t *
diff_dir_deleted (svn_wc_adm_access_t *adm_access,
                  svn_wc_notify_state_t *state,
                  const char *path,
                  void *diff_baton)
{
  if (state)
    *state = svn_wc_notify_state_unknown;

  return SVN_NO_ERROR;
}
  
static svn_error_t *
diff_props_changed (svn_wc_adm_access_t *adm_access,
                    svn_wc_notify_state_t *state,
                    const char *path,
                    const apr_array_header_t *propchanges,
                    apr_hash_t *original_props,
                    void *diff_baton)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;
  apr_array_header_t *props;
  apr_pool_t *subpool = svn_pool_create (diff_cmd_baton->pool);

  SVN_ERR (svn_categorize_props (propchanges, NULL, NULL, &props, subpool));

  if (props->nelts > 0)
    SVN_ERR (display_prop_diffs (props, original_props, path,
                                 diff_cmd_baton->outfile, subpool));

  if (state)
    *state = svn_wc_notify_state_unknown;

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


/*-----------------------------------------------------------------*/

/*** Callbacks for 'svn merge', invoked by the repos-diff editor. ***/


struct merge_cmd_baton {
  svn_boolean_t force;
  svn_boolean_t dry_run;
  const char *target;                 /* Working copy target of merge */
  const char *url;                    /* The second URL in the merge */
  const char *path;                   /* The wc path of the second target, this
                                         can be NULL if we don't have one. */
  const svn_opt_revision_t *revision; /* Revision of second URL in the merge */
  svn_client_ctx_t *ctx;

  /* The diff3_cmd in ctx->config, if any, else null.  We could just
     extract this as needed, but since more than one caller uses it,
     we just set it up when this baton is created. */
  const char *diff3_cmd;

  apr_pool_t *pool;
};


static svn_error_t *
merge_file_changed (svn_wc_adm_access_t *adm_access,
                    svn_wc_notify_state_t *state,
                    const char *mine,
                    const char *older,
                    const char *yours,
                    svn_revnum_t older_rev,
                    svn_revnum_t yours_rev,
                    const char *mimetype1,
                    const char *mimetype2,
                    void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create (merge_b->pool);
  const char *target_label = ".working";
  const char *left_label = apr_psprintf (subpool,
                                         ".merge-left.r%" SVN_REVNUM_T_FMT,
                                         older_rev);
  const char *right_label = apr_psprintf (subpool,
                                          ".merge-right.r%" SVN_REVNUM_T_FMT,
                                          yours_rev);
  svn_boolean_t has_local_mods;
  svn_boolean_t merge_required = TRUE;
  enum svn_wc_merge_outcome_t merge_outcome;

  /* Easy out:  no access baton means there ain't no merge target */
  if (adm_access == NULL)
    {
      if (state)
        *state = svn_wc_notify_state_missing;
      return SVN_NO_ERROR;
    }
  
  /* Other easy outs:  if the merge target isn't under version
     control, or is just missing from disk, fogettaboutit.  There's no
     way svn_wc_merge() can do the merge. */
  {
    const svn_wc_entry_t *entry;
    svn_node_kind_t kind;

    SVN_ERR (svn_wc_entry (&entry, mine, adm_access, FALSE, subpool));
    SVN_ERR (svn_io_check_path (mine, &kind, subpool));

    /* ### a future thought:  if the file is under version control,
       but the working file is missing, maybe we can 'restore' the
       working file from the text-base, and then allow the merge to run?  */

    if ((! entry) || (kind != svn_node_file))
      {
        if (state)
          *state = svn_wc_notify_state_missing;
        return SVN_NO_ERROR;
      }
  }

  /* This callback is essentially no more than a wrapper around
     svn_wc_merge().  Thank goodness that all the
     diff-editor-mechanisms are doing the hard work of getting the
     fulltexts! */

  SVN_ERR (svn_wc_text_modified_p (&has_local_mods, mine, FALSE,
                                   adm_access, subpool));

  /* Special case:  if a binary file isn't locally modified, and is
     exactly identical to the 'left' side of the merge, then don't
     allow svn_wc_merge to produce a conflict.  Instead, just
     overwrite the working file with the 'right' side of the merge. */
  if ((! has_local_mods)
      && ((mimetype1 && svn_mime_type_is_binary (mimetype1))
          || (mimetype2 && svn_mime_type_is_binary (mimetype1))))
    {
      svn_boolean_t same_contents;
      /* ### someday, we should just be able to compare
         identity-strings here.  */
      SVN_ERR (svn_io_files_contents_same_p (&same_contents,
                                             older, mine, subpool));
      if (same_contents)
        {
          if (! merge_b->dry_run)
            SVN_ERR (svn_io_file_rename (yours, mine, subpool));          
          merge_outcome = svn_wc_merge_merged;
          merge_required = FALSE;
        }
    }  

  if (merge_required)
    {
      SVN_ERR (svn_wc_merge (older, yours, mine, adm_access,
                             left_label, right_label, target_label,
                             merge_b->dry_run, &merge_outcome, 
                             merge_b->diff3_cmd, subpool));
    }

  /* Philip asks "Why?"  Why does the notification depend on whether the
     file had modifications before the merge?  If the merge didn't change
     the file because the local mods already included the change why does
     that result it "merged" notification?  That's information available
     through the status command, while the fact that the merge didn't
     change the file is lost :-( */

  if (state)
    {
      if (merge_outcome == svn_wc_merge_conflict)
        *state = svn_wc_notify_state_conflicted;
      else if (has_local_mods)
        *state = svn_wc_notify_state_merged;
      else if (merge_outcome == svn_wc_merge_merged)
        *state = svn_wc_notify_state_changed;
      else if (merge_outcome == svn_wc_merge_no_merge)
        *state = svn_wc_notify_state_missing;
      else /* merge_outcome == svn_wc_merge_unchanged */
        *state = svn_wc_notify_state_unchanged;
    }

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


static svn_error_t *
merge_file_added (svn_wc_adm_access_t *adm_access,
                  svn_wc_notify_state_t *state,
                  const char *mine,
                  const char *older,
                  const char *yours,
                  svn_revnum_t rev1,
                  svn_revnum_t rev2,
                  const char *mimetype1,
                  const char *mimetype2,
                  void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create (merge_b->pool);
  svn_node_kind_t kind;
  const char *copyfrom_url;
  const char *child;

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

  SVN_ERR (svn_io_check_path (mine, &kind, subpool));
  switch (kind)
    {
    case svn_node_none:
      if (! merge_b->dry_run)
        {
          child = svn_path_is_child(merge_b->target, mine, merge_b->pool);
          assert (child != NULL);
          copyfrom_url = svn_path_join (merge_b->url, child, merge_b->pool);
          SVN_ERR (check_schema_match (adm_access, copyfrom_url));

          {
            /* Since 'mine' doesn't exist, and this is
               'merge_file_added', I hope it's safe to assume that
               'older' is empty, and 'yours' is the full file.  Merely
               copying 'yours' to 'mine', isn't enough; we need to get
               the whole text-base and props installed too, just as if
               we had called 'svn cp wc wc'. */

            SVN_ERR (svn_wc_add_repos_file (mine, adm_access,
                                            yours,
                                            apr_hash_make (merge_b->pool),
                                            copyfrom_url,
                                            rev2,
                                            merge_b->pool));

            if (state)
              *state = svn_wc_notify_state_changed;
          }
        }
      break;
    case svn_node_dir:
      {
        /* this will make the repos_editor send a 'skipped' message */
        if (state)
          *state = svn_wc_notify_state_obstructed;
      }
    case svn_node_file:
      {
        /* file already exists, is it under version control? */
        const svn_wc_entry_t *entry;
        SVN_ERR (svn_wc_entry (&entry, mine, adm_access, FALSE, subpool));

        /* If it's an unversioned file, don't touch it.  If its scheduled
           for deletion, then rm removed it from the working copy and the
           user must have recreated it, don't touch it */
        if (!entry || entry->schedule == svn_wc_schedule_delete)
          {
            /* this will make the repos_editor send a 'skipped' message */
            if (state)
              *state = svn_wc_notify_state_obstructed;
          }
        else
          {
            SVN_ERR (merge_file_changed (adm_access, state,
                                         mine, older, yours,
                                         rev1, rev2,
                                         mimetype1, mimetype2,
                                         baton));            
          }
        break;      
      }
    default:
      break;
    }

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
merge_file_deleted (svn_wc_adm_access_t *adm_access,
                    svn_wc_notify_state_t *state,
                    const char *mine,
                    const char *older,
                    const char *yours,
                    const char *mimetype1,
                    const char *mimetype2,
                    void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create (merge_b->pool);
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

  SVN_ERR (svn_io_check_path (mine, &kind, subpool));
  switch (kind)
    {
    case svn_node_file:
      svn_path_split (mine, &parent_path, NULL, merge_b->pool);
      SVN_ERR (svn_wc_adm_retrieve (&parent_access, adm_access, parent_path,
                                    merge_b->pool));
      {
        /* This is a bit ugly: we don't want svn_client__wc_delete to
           notify because repos_diff.c:delete_item will do it for us. */
        svn_wc_notify_func_t notify_func = merge_b->ctx->notify_func;
        merge_b->ctx->notify_func = NULL;

        err = svn_client__wc_delete (mine, parent_access, merge_b->force,
                                     merge_b->dry_run, merge_b->ctx, subpool);
        merge_b->ctx->notify_func = notify_func;
      }
      if (err && state)
        {
          *state = svn_wc_notify_state_obstructed;
          svn_error_clear (err);
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
      break;
    }
    
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
merge_dir_added (svn_wc_adm_access_t *adm_access,
                 svn_wc_notify_state_t *state,
                 const char *path,
                 svn_revnum_t rev,
                 void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create (merge_b->pool);
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
        *state = svn_wc_notify_state_missing;
      return SVN_NO_ERROR;
    }

  child = svn_path_is_child (merge_b->target, path, subpool);
  assert (child != NULL);
  copyfrom_url = svn_path_join (merge_b->url, child, subpool);
  SVN_ERR (check_schema_match (adm_access, copyfrom_url));

  SVN_ERR (svn_io_check_path (path, &kind, subpool));
  switch (kind)
    {
    case svn_node_none:
      if (! merge_b->dry_run)
        {
          SVN_ERR (svn_io_make_dir_recursively (path, subpool));
          SVN_ERR (svn_wc_add (path, adm_access,
                               copyfrom_url, rev,
                               merge_b->ctx->cancel_func,
                               merge_b->ctx->cancel_baton,
                               NULL, NULL, /* don't pass notification func! */
                               merge_b->pool));

          if (state)
              *state = svn_wc_notify_state_changed;
        }
      break;
    case svn_node_dir:
      /* Adding an unversioned directory doesn't destroy data */
      SVN_ERR (svn_wc_entry (&entry, path, adm_access, TRUE, subpool));
      if (!merge_b->dry_run
          && (! entry || (entry && entry->schedule == svn_wc_schedule_delete)))
        {
          SVN_ERR (svn_wc_add (path, adm_access,
                               copyfrom_url,
                               merge_b->revision->value.number,
                               merge_b->ctx->cancel_func,
                               merge_b->ctx->cancel_baton,
                               NULL, NULL, /* don't pass notification func! */
                               merge_b->pool));
          if (state)
              *state = svn_wc_notify_state_changed;
        }
      break;
    case svn_node_file:
      if (state)
        *state = svn_wc_notify_state_obstructed;
      break;
    default:
      break;
    }

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
merge_dir_deleted (svn_wc_adm_access_t *adm_access,
                   svn_wc_notify_state_t *state,
                   const char *path,
                   void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create (merge_b->pool);
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
  
  SVN_ERR (svn_io_check_path (path, &kind, subpool));
  switch (kind)
    {
    case svn_node_dir:
      svn_path_split (path, &parent_path, NULL, merge_b->pool);
      SVN_ERR (svn_wc_adm_retrieve (&parent_access, adm_access, parent_path,
                                    merge_b->pool));
      err = svn_client__wc_delete (path, parent_access, merge_b->force,
                                   merge_b->dry_run, merge_b->ctx, subpool);
      if (err && state)
        {
          *state = svn_wc_notify_state_obstructed;
          svn_error_clear (err);
        }
      else if (state)
        {
          *state = svn_wc_notify_state_changed;
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
      break;
    }

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}
  
static svn_error_t *
merge_props_changed (svn_wc_adm_access_t *adm_access,
                     svn_wc_notify_state_t *state,
                     const char *path,
                     const apr_array_header_t *propchanges,
                     apr_hash_t *original_props,
                     void *baton)
{
  apr_array_header_t *props;
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create (merge_b->pool);
  svn_error_t *err;

  SVN_ERR (svn_categorize_props (propchanges, NULL, NULL, &props, subpool));

  /* We only want to merge "regular" version properties:  by
     definition, 'svn merge' shouldn't touch any data within .svn/  */
  if (props->nelts)
    {
      err = svn_wc_merge_prop_diffs (state, path, adm_access, props,
                                     FALSE, merge_b->dry_run, subpool);
      if (err && (err->apr_err == SVN_ERR_ENTRY_NOT_FOUND))
        {
          /* if the entry doesn't exist in the wc, just 'skip' over
             this part of the tree-delta. */
          if (state)
            *state = svn_wc_notify_state_missing;
          svn_error_clear (err);
          return SVN_NO_ERROR;        
        }
      else if (err)
        return err;
    }

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}

/* The main callback table for 'svn merge'.  */
static const svn_wc_diff_callbacks_t 
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
convert_to_url (const char **url,
                const char *path,
                apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;  /* ### FIXME local */
  const svn_wc_entry_t *entry;      

  if (svn_path_is_url (path))
    {
      *url = path;
      return SVN_NO_ERROR;
    }

  /* ### This may not be a good idea, see issue 880 */
  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, path, FALSE, FALSE, pool));
  SVN_ERR (svn_wc_entry (&entry, path, adm_access, FALSE, pool));
  SVN_ERR (svn_wc_adm_close (adm_access));
  if (! entry)
    return svn_error_createf (SVN_ERR_ENTRY_NOT_FOUND, NULL,
                              "convert_to_url: '%s' is not versioned", path);

  if (entry->url)  
    *url = apr_pstrdup (pool, entry->url);
  else
    *url = apr_pstrdup (pool, entry->copyfrom_url);
  return SVN_NO_ERROR;
}



/* URL1/PATH1, URL2/PATH2, and TARGET_WCPATH all better be
   directories.  For the single file case, the caller does the merging
   manually.  PATH1 and PATH2 can be NULL. */
static svn_error_t *
do_merge (const char *URL1,
          const char *path1,
          const svn_opt_revision_t *revision1,
          const char *URL2,
          const char *path2,
          const svn_opt_revision_t *revision2,
          const char *target_wcpath,
          svn_wc_adm_access_t *adm_access,
          svn_boolean_t recurse,
          svn_boolean_t ignore_ancestry,
          svn_boolean_t dry_run,
          const svn_wc_diff_callbacks_t *callbacks,
          void *callback_baton,
          svn_client_ctx_t *ctx,
          apr_pool_t *pool)
{
  svn_revnum_t start_revnum, end_revnum;
  void *ra_baton, *session, *session2;
  svn_ra_plugin_t *ra_lib;
  const svn_ra_reporter_t *reporter;
  void *report_baton;
  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;
  const char *auth_dir;

  /* Sanity check -- ensure that we have valid revisions to look at. */
  if ((revision1->kind == svn_opt_revision_unspecified)
      || (revision2->kind == svn_opt_revision_unspecified))
    {
      return svn_error_create
        (SVN_ERR_CLIENT_BAD_REVISION, NULL,
         "do_merge: caller failed to specify all revisions");
    }

  SVN_ERR (svn_client__default_auth_dir (&auth_dir, target_wcpath, pool));

  /* Establish first RA session to URL1. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL1, pool));
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL1, auth_dir,
                                        NULL, NULL, FALSE, TRUE, 
                                        ctx, pool));
  /* Resolve the revision numbers. */
  SVN_ERR (svn_client__get_revision_number
           (&start_revnum, ra_lib, session, revision1, path1, pool));
  SVN_ERR (svn_client__get_revision_number
           (&end_revnum, ra_lib, session, revision2, path2, pool));

  /* Open a second session used to request individual file
     contents. Although a session can be used for multiple requests, it
     appears that they must be sequential. Since the first request, for
     the diff, is still being processed the first session cannot be
     reused. This applies to ra_dav, ra_local does not appears to have
     this limitation. */
  SVN_ERR (svn_client__open_ra_session (&session2, ra_lib, URL1, auth_dir,
                                        NULL, NULL, FALSE, TRUE,
                                        ctx, pool));
 
  SVN_ERR (svn_client__get_diff_editor (target_wcpath,
                                        adm_access,
                                        callbacks,
                                        callback_baton,
                                        recurse,
                                        dry_run,
                                        ra_lib, session2,
                                        start_revnum,
                                        ctx->notify_func,
                                        ctx->notify_baton,
                                        ctx->cancel_func,
                                        ctx->cancel_baton,
                                        &diff_editor,
                                        &diff_edit_baton,
                                        pool));

  SVN_ERR (ra_lib->do_diff (session,
                            &reporter, &report_baton,
                            end_revnum,
                            NULL,
                            recurse,
                            ignore_ancestry,
                            URL2,
                            diff_editor, diff_edit_baton, pool));
  
  SVN_ERR (reporter->set_path (report_baton, "", start_revnum, FALSE, pool));
  
  SVN_ERR (reporter->finish_report (report_baton));
  
  return SVN_NO_ERROR;
}


/* Get REVISION of the file at URL.  SOURCE is a path that refers to that 
   file's entry in the working copy, or NULL if we don't have one.  Return in 
   *FILENAME the name of a file containing the file contents, in *PROPS a hash 
   containing the properties and in *REV the revision.  All allocation occurs 
   in POOL. */
static svn_error_t *
single_file_merge_get_file (const char **filename,
                            apr_hash_t **props,
                            svn_revnum_t *rev,
                            const char *url,
                            const char *path,
                            const svn_opt_revision_t *revision,
                            void *ra_baton,
                            const char *auth_dir,
                            struct merge_cmd_baton *merge_b,
                            apr_pool_t *pool)
{
  svn_ra_plugin_t *ra_lib;
  void *session;
  apr_file_t *fp;
  apr_status_t status;

  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, url, pool));
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, url, auth_dir,
                                        NULL, NULL, FALSE, TRUE, 
                                        merge_b->ctx, pool));
  SVN_ERR (svn_client__get_revision_number (rev, ra_lib, session, revision,
                                            path, pool));
  SVN_ERR (svn_io_open_unique_file (&fp, filename, 
                                    merge_b->target, ".tmp",
                                    FALSE, pool));
  SVN_ERR (ra_lib->get_file (session, "", *rev,
                             svn_stream_from_aprfile (fp, pool),
                             NULL, props, pool));
  status = apr_file_close (fp);
  if (status)
    return svn_error_createf (status, NULL, "failed to close '%s'", *filename);

  return SVN_NO_ERROR;
}
                            

/* The single-file, simplified version of do_merge. */
static svn_error_t *
do_single_file_merge (const char *URL1,
                      const char *path1,
                      const svn_opt_revision_t *revision1,
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
  void *ra_baton;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_unknown;
  svn_wc_notify_state_t text_state = svn_wc_notify_state_unknown;
  const char *auth_dir;

  SVN_ERR (svn_client__default_auth_dir (&auth_dir, merge_b->target, pool));

  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));

  /* ### heh, funny.  we could be fetching two fulltexts from two
     *totally* different repositories here.  :-) */
  SVN_ERR (single_file_merge_get_file (&tmpfile1, &props1, &rev1,
                                       URL1, path1, revision1,
                                       ra_baton, auth_dir, merge_b, pool));

  SVN_ERR (single_file_merge_get_file (&tmpfile2, &props2, &rev2,
                                       merge_b->url, merge_b->path, 
                                       merge_b->revision, ra_baton, auth_dir,
                                       merge_b, pool));

  /* Discover any svn:mime-type values in the proplists */
  pval = apr_hash_get (props1, SVN_PROP_MIME_TYPE, strlen(SVN_PROP_MIME_TYPE));
  mimetype1 = pval ? pval->data : NULL;

  pval = apr_hash_get (props2, SVN_PROP_MIME_TYPE, strlen(SVN_PROP_MIME_TYPE));
  mimetype2 = pval ? pval->data : NULL;

  SVN_ERR (merge_file_changed (adm_access,
                               &text_state,
                               merge_b->target,
                               tmpfile1,
                               tmpfile2,
                               rev1,
                               rev2,
                               mimetype1, mimetype2,
                               merge_b));

  SVN_ERR (svn_io_remove_file (tmpfile1, pool));
  SVN_ERR (svn_io_remove_file (tmpfile2, pool));
  
  /* Deduce property diffs, and merge those too. */
  SVN_ERR (svn_prop_diffs (&propchanges, props2, props1, pool));

  SVN_ERR (merge_props_changed (adm_access,
                                &prop_state,
                                merge_b->target,
                                propchanges,
                                NULL,
                                merge_b));

  if (merge_b->ctx->notify_func)
    {
      (*merge_b->ctx->notify_func) (merge_b->ctx->notify_baton,
                                    merge_b->target,
                                    svn_wc_notify_update_update,
                                    svn_node_file,
                                    NULL,
                                    text_state, 
                                    prop_state,
                                    SVN_INVALID_REVNUM);
    }

  return SVN_NO_ERROR;
}


/* A Theoretical Note From Ben, regarding do_diff().

   This function is really svn_client_diff().  If you read the public
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

   Perhaps someday a brave soul will truly make svn_client_diff
   perfectly general.  For now, we live with the 90% case.  Certainly,
   the commandline client only calls this function in legal ways.
   When there are other users of svn_client.h, maybe this will become
   a more pressing issue.
 */

/* Return a "you can't do that" error, optionally wrapping another
   error CHILD_ERR. */
static svn_error_t *
unsupported_diff_error (svn_error_t *child_err)
{
  return svn_error_create (SVN_ERR_INCORRECT_PARAMS, child_err,
                           "Sorry, svn_client_diff was called in a way "
                           "that is not yet supported.");
}


/* Perform a diff between two working-copy paths.  
   
   PATH1 and PATH2 are both working copy paths.  REVISION1 and
   REVISION2 are their respective revisions.

   All other options are the same as those passed to svn_client_diff(). */
static svn_error_t *
diff_wc_wc (const apr_array_header_t *options,
            const char *path1,
            const svn_opt_revision_t *revision1,
            const char *path2,
            const svn_opt_revision_t *revision2,
            svn_boolean_t recurse,
            const svn_wc_diff_callbacks_t *callbacks,
            struct diff_cmd_baton *callback_baton,
            svn_client_ctx_t *ctx,
            apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const char *anchor, *target;
  svn_node_kind_t kind;

  /* Assert that we have valid input. */
  assert (! svn_path_is_url (path1));
  assert (! svn_path_is_url (path2));

  /* Currently we support only the case where path1 and path2 are the
     same path. */
  if ((strcmp (path1, path2) != 0)
      || (! ((revision1->kind == svn_opt_revision_base)
             && (revision2->kind == svn_opt_revision_working))))
    return unsupported_diff_error
      (svn_error_create 
       (SVN_ERR_INCORRECT_PARAMS, NULL,
        "diff_wc_wc: we only support diffs between a path's text-base "
        "and its working files at this time"));

  SVN_ERR (svn_wc_get_actual_target (path1, &anchor, &target, pool));
  SVN_ERR (svn_io_check_path (path1, &kind, pool));
  SVN_ERR (svn_wc_adm_open (&adm_access, NULL, anchor, FALSE,
                            (recurse && (! target)), pool));

  if (target && (kind == svn_node_dir))
    {
      /* Associate a potentially tree-locked access baton for the
         target with the anchor's access baton.  Note that we don't
         actually use the target's baton here; it just floats around
         in adm_access's set of associated batons, where the diff
         editor can find it. */
      svn_wc_adm_access_t *target_access;
      SVN_ERR (svn_wc_adm_open (&target_access, adm_access, path1,
                                FALSE, recurse, pool));
    }

  /* Resolve named revisions to real numbers. */
  SVN_ERR (svn_client__get_revision_number
           (&callback_baton->revnum1, NULL, NULL, revision1, path1, pool));
  callback_baton->revnum2 = SVN_INVALID_REVNUM;  /* WC */

  SVN_ERR (svn_wc_diff (adm_access, target, callbacks, callback_baton,
                        recurse, pool));
  SVN_ERR (svn_wc_adm_close (adm_access));
  return SVN_NO_ERROR;
}


/* Perform a diff between two repository paths.  
   
   PATH1 and PATH2 may be either URLs or the working copy paths.
   REVISION1 and REVISION2 are their respective revisions.

   All other options are the same as those passed to svn_client_diff(). */
static svn_error_t *
diff_repos_repos (const apr_array_header_t *options,
                  const char *path1,
                  const svn_opt_revision_t *revision1,
                  const char *path2,
                  const svn_opt_revision_t *revision2,
                  svn_boolean_t recurse,
                  svn_boolean_t ignore_ancestry,
                  const svn_wc_diff_callbacks_t *callbacks,
                  struct diff_cmd_baton *callback_baton,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  const char *url1, *url2;
  const char *anchor1, *target1, *anchor2, *target2, *base_path;
  svn_node_kind_t kind1, kind2;
  svn_revnum_t rev1, rev2;
  void *ra_baton, *session1, *session2;
  svn_ra_plugin_t *ra_lib;
  const svn_ra_reporter_t *reporter;
  void *report_baton;
  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;
  const char *auth_dir;
  apr_pool_t *temppool = svn_pool_create (pool);
  svn_boolean_t same_urls;

  /* Figure out URL1 and URL2. */
  SVN_ERR (convert_to_url (&url1, path1, pool));
  SVN_ERR (convert_to_url (&url2, path2, pool));
  same_urls = (strcmp (url1, url2) == 0);

  /* We need exactly one BASE_PATH, so we'll let the BASE_PATH
     calculated for PATH2 override the one for PATH1 (since the diff
     will be "applied" to URL2 anyway). */
  base_path = NULL;
  if (url1 != path1)
    base_path = path1;
  if (url2 != path2)
    base_path = path2;

  /* Setup our RA libraries. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, url1, pool));
  SVN_ERR (svn_client__dir_if_wc (&auth_dir, base_path ? base_path : "", 
                                  pool));

  /* Open temporary RA sessions to each URL. */
  SVN_ERR (svn_client__open_ra_session (&session1, ra_lib, url1, auth_dir,
                                        NULL, NULL, FALSE, TRUE, 
                                        ctx, temppool));
  SVN_ERR (svn_client__open_ra_session (&session2, ra_lib, url2, auth_dir,
                                        NULL, NULL, FALSE, TRUE, 
                                        ctx, temppool));

  /* Resolve named revisions to real numbers. */
  SVN_ERR (svn_client__get_revision_number
           (&rev1, ra_lib, session1, revision1, 
            (path1 == url1) ? NULL : path1, pool));
  callback_baton->revnum1 = rev1;
  SVN_ERR (svn_client__get_revision_number
           (&rev2, ra_lib, session2, revision2,
            (path2 == url2) ? NULL : path2, pool));
  callback_baton->revnum2 = rev2;

  /* Choose useful anchors and targets for our two URLs.  If the URLs
     are the same, we require that the thing exist in at least one
     revision.  Otherwise, both URLs must exist. */
  anchor1 = url1;
  anchor2 = url2;
  target1 = NULL;
  target2 = NULL;
  SVN_ERR (ra_lib->check_path (session1, "", rev1, &kind1, temppool));
  SVN_ERR (ra_lib->check_path (session2, "", rev2, &kind2, temppool));
  if (same_urls)
    {
      if ((kind1 == svn_node_none) && (kind2 == svn_node_none))
        return svn_error_createf 
          (SVN_ERR_FS_NOT_FOUND, NULL,
           "'%s' was not found in the repository at either revision "
           "%" SVN_REVNUM_T_FMT " or %" SVN_REVNUM_T_FMT, url1, rev1, rev2);
    }
  else
    {
      if (kind1 == svn_node_none)
        return svn_error_createf 
          (SVN_ERR_FS_NOT_FOUND, NULL,
           "'%s' was not found in the repository at revision %"
           SVN_REVNUM_T_FMT, url1, rev1);
      if (kind2 == svn_node_none)
        return svn_error_createf 
          (SVN_ERR_FS_NOT_FOUND, NULL,
           "'%s' was not found in the repository at revision %"
           SVN_REVNUM_T_FMT, url2, rev2);
    }
  if ((kind1 == svn_node_file) || (kind2 == svn_node_file))
    {
      svn_path_split (url1, &anchor1, &target1, pool); 
      target1 = svn_path_uri_decode (target1, pool);
      svn_path_split (url2, &anchor2, &target2, pool); 
      target2 = svn_path_uri_decode (target2, pool);
      if (base_path)
        base_path = svn_path_dirname (base_path, pool);
    }

  /* Destroy the temporary pool, which closes our RA session. */
  svn_pool_destroy (temppool);

  /* Now, we reopen two RA session to the correct anchor/target
     locations for our URLs. */
  SVN_ERR (svn_client__open_ra_session (&session1, ra_lib, anchor1,
                                        auth_dir, NULL, NULL, FALSE, TRUE, 
                                        ctx, pool));
  SVN_ERR (svn_client__open_ra_session (&session2, ra_lib, anchor1,
                                        auth_dir, NULL, NULL, FALSE, TRUE,
                                        ctx, pool));      

  /* Set up the repos_diff editor on BASE_PATH, if available.
     Otherwise, we just use "". */
  SVN_ERR (svn_client__get_diff_editor (base_path ? base_path : "",
                                        NULL,
                                        callbacks,
                                        callback_baton,
                                        recurse,
                                        FALSE, /* doesn't matter for diff */
                                        ra_lib, session2,
                                        rev1,
                                        NULL, /* no notify_func */
                                        NULL, /* no notify_baton */
                                        ctx->cancel_func,
                                        ctx->cancel_baton,
                                        &diff_editor,
                                        &diff_edit_baton,
                                        pool));
  
  /* We want to switch our txn into URL2 */
  SVN_ERR (ra_lib->do_diff (session1,
                            &reporter, &report_baton,
                            rev2,
                            target1,
                            recurse,
                            ignore_ancestry,
                            url2,
                            diff_editor, diff_edit_baton, pool));
  
  /* Drive the reporter; do the diff. */
  SVN_ERR (reporter->set_path (report_baton, "", rev1, FALSE, pool));
  SVN_ERR (reporter->finish_report (report_baton));

  return SVN_NO_ERROR;
}


/* Perform a diff between a repository path and a working-copy path.
   
   PATH1 may be either a URL or a working copy path.  PATH2 is a
   working copy path.  REVISION1 and REVISION2 are their respective
   revisions.  If REVERSE is TRUE, the diff will be done in reverse.

   All other options are the same as those passed to svn_client_diff(). */
static svn_error_t *
diff_repos_wc (const apr_array_header_t *options,
               const char *path1,
               const svn_opt_revision_t *revision1,
               const char *path2,
               const svn_opt_revision_t *revision2,
               svn_boolean_t reverse,
               svn_boolean_t recurse,
               svn_boolean_t ignore_ancestry,
               const svn_wc_diff_callbacks_t *callbacks,
               struct diff_cmd_baton *callback_baton,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  const char *url1;
  const char *anchor1, *target1, *anchor2, *target2;
  svn_node_kind_t kind;
  svn_wc_adm_access_t *adm_access, *dir_access;
  svn_revnum_t rev;
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  const svn_ra_reporter_t *reporter;
  void *report_baton;
  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;
  const char *auth_dir;
  svn_boolean_t rev2_is_base = (revision2->kind == svn_opt_revision_base);

  /* Assert that we have valid input. */
  assert (! svn_path_is_url (path2));

  /* Figure out URL1. */
  SVN_ERR (convert_to_url (&url1, path1, pool));

  /* Possibly split up PATH2 into anchor/target.  If we do so, then we
     must split URL1 as well. */
  anchor1 = url1;
  anchor2 = path2;
  target1 = NULL;
  target2 = NULL;
  SVN_ERR (svn_io_check_path (path2, &kind, pool));
  if (kind == svn_node_file)
    {
      svn_path_split (path2, &anchor2, &target2, pool);
      svn_path_split (url1, &anchor1, &target1, pool);
    }

  /* Establish RA session to URL1's anchor */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, anchor1, pool));
  SVN_ERR (svn_client__default_auth_dir (&auth_dir, path2, pool));
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, anchor1,
                                        auth_dir, NULL, NULL, FALSE, TRUE,
                                        ctx, pool));
      
  /* Set up diff editor according to path2's anchor/target. */
  SVN_ERR (svn_wc_adm_open (&adm_access, NULL, anchor2, FALSE,
                            (recurse && (! target2)), pool));
  if (target2 && (kind == svn_node_dir))
    {
      /* Associate a potentially tree-locked access baton for the
         target with the anchor's access baton.  Note that we don't
         actually use the target's baton here; it just floats around
         in adm_access's set of associated batons, where the diff
         editor can find it. */
      svn_wc_adm_access_t *target_access;
      SVN_ERR (svn_wc_adm_open (&target_access, adm_access, path2,
                                FALSE, recurse, pool));
    }

  SVN_ERR (svn_wc_get_diff_editor (adm_access, target2,
                                   callbacks, callback_baton,
                                   recurse,
                                   rev2_is_base,
                                   reverse,
                                   ctx->cancel_func, ctx->cancel_baton,
                                   &diff_editor, &diff_edit_baton,
                                   pool));

  /* Tell the RA layer we want a delta to change our txn to URL1 */
  SVN_ERR (svn_client__get_revision_number
           (&rev, ra_lib, session, revision1, 
            (path1 == url1) ? NULL : path1, pool));
  callback_baton->revnum1 = rev;
  SVN_ERR (ra_lib->do_update (session,
                              &reporter, &report_baton,
                              rev,
                              svn_path_uri_decode (target1, pool),
                              recurse, 
                              diff_editor, diff_edit_baton, pool));

  if (kind == svn_node_dir)
    SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access, path2, pool));
  else
    SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access,
                                  svn_path_dirname (path2, pool), pool));

  /* Create a txn mirror of path2;  the diff editor will print
     diffs in reverse.  :-)  */
  SVN_ERR (svn_wc_crawl_revisions (path2, dir_access,
                                   reporter, report_baton,
                                   FALSE, recurse, FALSE,
                                   NULL, NULL, /* notification is N/A */
                                   NULL, pool));

  SVN_ERR (svn_wc_adm_close (adm_access));
  return SVN_NO_ERROR;
}


/* This is basically just the guts of svn_client_diff(). */
static svn_error_t *
do_diff (const apr_array_header_t *options,
         const char *path1,
         const svn_opt_revision_t *revision1,
         const char *path2,
         const svn_opt_revision_t *revision2,
         svn_boolean_t recurse,
         svn_boolean_t ignore_ancestry,
         const svn_wc_diff_callbacks_t *callbacks,
         struct diff_cmd_baton *callback_baton,
         svn_client_ctx_t *ctx,
         apr_pool_t *pool)
{
  svn_boolean_t is_local_rev1, is_local_rev2;
  svn_boolean_t is_repos_path1, is_repos_path2;

  /* Either path could be a URL or a working copy path.  Let's figure
     out what's what. */
  is_repos_path1 = svn_path_is_url (path1);
  is_repos_path2 = svn_path_is_url (path2);

  /* Verify our revision arguments in light of the paths. */
  if ((revision1->kind == svn_opt_revision_unspecified)
      || (revision2->kind == svn_opt_revision_unspecified))
    return svn_error_create (SVN_ERR_CLIENT_BAD_REVISION, NULL,
                             "do_diff: not all revisions are specified");
  if ((revision1->kind == svn_opt_revision_committed)
      || (revision2->kind == svn_opt_revision_committed))
    return unsupported_diff_error 
      (svn_error_create (SVN_ERR_INCORRECT_PARAMS, NULL,
                         "do_diff: COMMITTED nomenclature not supported"));

  /* Revisions can be said to be local or remote.  BASE and WORKING,
     for example, are local.  */
  is_local_rev1 = ((revision1->kind == svn_opt_revision_base)
                   || (revision1->kind == svn_opt_revision_working));
  is_local_rev2 = ((revision2->kind == svn_opt_revision_base)
                   || (revision2->kind == svn_opt_revision_working));

  /* URLs and local revisions don't mix. */
  if (is_repos_path1 && is_local_rev1)
    return svn_error_create 
      (SVN_ERR_INCORRECT_PARAMS, NULL,
       "do_diff: invalid revision specifier for URL path");
  if (is_repos_path2 && is_local_rev2)
    return svn_error_create 
      (SVN_ERR_INCORRECT_PARAMS, NULL,
       "do_diff: invalid revision specifier for URL path");
  
  /* Working copy paths with non-local revisions get turned into
     URLs.  We don't do that here, though.  We simply record that it
     needs to be done, which is information that helps us choose our
     diff helper function.  */
  if ((! is_repos_path1) && (! is_local_rev1))
    is_repos_path1 = TRUE;
  if ((! is_repos_path2) && (! is_local_rev2))
    is_repos_path2 = TRUE;

  if (is_repos_path1) /* path1 is (effectively) a URL */
    {
      if (is_repos_path2) /* path2 is (effectively) a URL */
        {
          SVN_ERR (diff_repos_repos (options, path1, revision1, path2, 
                                     revision2, recurse, ignore_ancestry, 
                                     callbacks, callback_baton, ctx, pool));
        }
      else /* path2 is a working copy path */
        {
          SVN_ERR (diff_repos_wc (options, path1, revision1, path2, revision2,
                                  FALSE, recurse, ignore_ancestry, callbacks,
                                  callback_baton, ctx, pool));
        }
    }
  else /* path1 is a working copy path */
    {
      if (is_repos_path2) /* path2 is (effectively) a URL */
        {
          SVN_ERR (diff_repos_wc (options, path2, revision2, path1, revision1,
                                  TRUE, recurse, ignore_ancestry, callbacks,
                                  callback_baton, ctx, pool));
        }
      else /* path2 is a working copy path */
        {
          SVN_ERR (diff_wc_wc (options, path1, revision1, path2, revision2,
                               recurse, callbacks, callback_baton, ctx, pool));
        }
    }

  return SVN_NO_ERROR;
}




/*----------------------------------------------------------------------- */

/*** Public Interfaces. ***/

/* Display context diffs between two PATH/REVISION pairs.  Each of
   these input will be one of the following:

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

   Svn_client_diff() is the single entry point for all of the diff
   operations, and will be in charge of examining the inputs and
   making decisions about how to accurately report contextual diffs.

   NOTE:  In the near future, svn_client_diff() will likely only
   continue to report textual differences in files.  Property diffs
   are important, too, and will need to be supported in some fashion
   so that this code can be re-used for svn_client_merge(). 
*/
svn_error_t *
svn_client_diff (const apr_array_header_t *options,
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
  struct diff_cmd_baton diff_cmd_baton;
  svn_wc_diff_callbacks_t diff_callbacks;

  diff_callbacks.file_changed = diff_file_changed;
  diff_callbacks.file_added = diff_file_added;
  diff_callbacks.file_deleted = no_diff_deleted ? diff_file_deleted_no_diff :
                                                  diff_file_deleted_with_diff;
  diff_callbacks.dir_added =  diff_dir_added;
  diff_callbacks.dir_deleted = diff_dir_deleted;
  diff_callbacks.props_changed = diff_props_changed;
    
  diff_cmd_baton.orig_path_1 = path1;
  diff_cmd_baton.orig_path_2 = path2;

  diff_cmd_baton.options = options;
  diff_cmd_baton.pool = pool;
  diff_cmd_baton.outfile = outfile;
  diff_cmd_baton.errfile = errfile;
  diff_cmd_baton.revnum1 = SVN_INVALID_REVNUM;
  diff_cmd_baton.revnum2 = SVN_INVALID_REVNUM;

  diff_cmd_baton.config = ctx->config;

  return do_diff (options,
                  path1, revision1,
                  path2, revision2,
                  recurse,
                  ignore_ancestry,
                  &diff_callbacks, &diff_cmd_baton,
                  ctx,
                  pool);
}


svn_error_t *
svn_client_merge (const char *source1,
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
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  struct merge_cmd_baton merge_cmd_baton;
  const char *URL1, *URL2;
  const char *path1;

  /* if source1 or source2 are paths, we need to get the the underlying url
   * from the wc and save the initial path we were passed so we can use it as 
   * a path parameter (either in the baton or not).  otherwise, the path 
   * will just be NULL, which means we won't be able to figure out some kind 
   * of revision specifications, but in that case it won't matter, because 
   * those ways of specifying a revision are meaningless for a url.
   */
  SVN_ERR (svn_client_url_from_path (&URL1, source1, pool));
  if (! URL1)
    return svn_error_createf (SVN_ERR_ENTRY_MISSING_URL, NULL,
                              "'%s' has no URL", source1);

  SVN_ERR (svn_client_url_from_path (&URL2, source2, pool));
  if (! URL2)
    return svn_error_createf (SVN_ERR_ENTRY_MISSING_URL, NULL, 
                              "'%s' has no URL", source2);

  if (URL1 == source1)
    path1 = NULL;
  else
    path1 = source1;

  if (URL2 == source2)
    merge_cmd_baton.path = NULL;
  else
    merge_cmd_baton.path = source2;

  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, target_wcpath,
                                  ! dry_run, recurse, pool));

  SVN_ERR (svn_wc_entry (&entry, target_wcpath, adm_access, FALSE, pool));
  if (entry == NULL)
    return svn_error_createf (SVN_ERR_ENTRY_NOT_FOUND, NULL,
                              "Can't merge changes into '%s':"
                              "it's not under revision control.", 
                              target_wcpath);

  merge_cmd_baton.force = force;
  merge_cmd_baton.dry_run = dry_run;
  merge_cmd_baton.target = target_wcpath;
  merge_cmd_baton.url = URL2;
  merge_cmd_baton.revision = revision2;
  merge_cmd_baton.ctx = ctx;
  merge_cmd_baton.pool = pool;

  /* Set up the diff3 command, so various callers don't have to. */
  {
    svn_config_t *cfg = apr_hash_get (ctx->config,
                                      SVN_CONFIG_CATEGORY_CONFIG,
                                      APR_HASH_KEY_STRING);
    svn_config_get (cfg, &(merge_cmd_baton.diff3_cmd),
                    SVN_CONFIG_SECTION_HELPERS,
                    SVN_CONFIG_OPTION_DIFF3_CMD, NULL);
  }

  /* If our target_wcpath is a single file, assume that PATH1 and
     PATH2 are files as well, and do a single-file merge. */
  if (entry->kind == svn_node_file)
    {
      SVN_ERR (do_single_file_merge (URL1, path1, revision1,
                                     adm_access,
                                     &merge_cmd_baton,
                                     pool));
    }

  /* Otherwise, this must be a directory merge.  Do the fancy
     recursive diff-editor thing. */
  else if (entry->kind == svn_node_dir)
    {
      SVN_ERR (do_merge (URL1,
                         path1,
                         revision1,
                         URL2,
                         merge_cmd_baton.path,
                         revision2,
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

  SVN_ERR (svn_wc_adm_close (adm_access));

  return SVN_NO_ERROR;
}
