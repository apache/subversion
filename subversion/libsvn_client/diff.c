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
        svn_boolean_t val_to_utf8 = svn_prop_is_svn_prop (propchange->name);
        const char *printable_val;
        
        if (original_value != NULL)
          {
            if (val_to_utf8)
              SVN_ERR (svn_utf_cstring_from_utf8
                       (&printable_val, original_value->data, pool));
            else
              printable_val = original_value->data;
            
            apr_file_printf (file, "   - %s" APR_EOL_STR, printable_val);
          }
        
        if (propchange->value != NULL)
          {
            if (val_to_utf8)
              SVN_ERR (svn_utf_cstring_from_utf8
                       (&printable_val, propchange->value->data, pool));
            else
              printable_val = propchange->value->data;

            apr_file_printf (file, "   + %s" APR_EOL_STR, printable_val);
          }
      }
    }

  apr_file_printf (file, APR_EOL_STR);

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
  const char *label1, *label2, *path_native;

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
      SVN_ERR (svn_utf_cstring_from_utf8 (&path_native, path, subpool));
      SVN_ERR (svn_io_file_printf (outfile, "Index: %s" APR_EOL_STR
                                   "%s" APR_EOL_STR,
                                   path_native, equal_string));

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
          SVN_ERR (svn_utf_cstring_from_utf8 (&path_native, path, subpool));
          SVN_ERR (svn_io_file_printf (outfile, "Index: %s" APR_EOL_STR
                                       "%s" APR_EOL_STR,
                                       path_native, equal_string));

          /* If either file is marked as a known binary type, just
             print a warning. */
          if (mimetype1)
            mt1_binary = svn_mime_type_is_binary (mimetype1);
          if (mimetype2)
            mt2_binary = svn_mime_type_is_binary (mimetype2);

          if (mt1_binary || mt2_binary)
            {
              svn_io_file_printf 
                (outfile,
                 "Cannot display: file marked as a binary type." APR_EOL_STR);
              
              if (mt1_binary && !mt2_binary)
                svn_io_file_printf (outfile,
                                    "svn:mime-type = %s" APR_EOL_STR,
                                    mimetype1);
              else if (mt2_binary && !mt1_binary)
                svn_io_file_printf (outfile,
                                    "svn:mime-type = %s" APR_EOL_STR,
                                    mimetype2);
              else if (mt1_binary && mt2_binary)
                {
                  if (strcmp (mimetype1, mimetype2) == 0)
                    svn_io_file_printf (outfile,
                                        "svn:mime-type = %s" APR_EOL_STR,
                                        mimetype1);
                  else
                    svn_io_file_printf (outfile,
                                        "svn:mime-type = (%s, %s)" APR_EOL_STR,
                                        mimetype1, mimetype2);
                }
            }
          else
            {
              /* Output the actual diff */
              SVN_ERR(svn_diff_file_output_unified(outfile, diff,
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
                 const char *path,
                 const char *tmpfile1,
                 const char *tmpfile2,
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

  SVN_ERR (diff_file_changed (adm_access, NULL, path, tmpfile1, tmpfile2, 
                              diff_cmd_baton->revnum1, diff_cmd_baton->revnum2,
                              mimetype1, mimetype2, diff_baton));
  
  diff_cmd_baton->force_diff_output = FALSE;

  return SVN_NO_ERROR;
}

static svn_error_t *
diff_file_deleted_with_diff (svn_wc_adm_access_t *adm_access,
                             const char *path,
                             const char *tmpfile1,
                             const char *tmpfile2,
                             const char *mimetype1,
                             const char *mimetype2,
                             void *diff_baton)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;

  return diff_file_changed (adm_access, NULL, path, tmpfile1, tmpfile2, 
                            diff_cmd_baton->revnum1, diff_cmd_baton->revnum2,
                            mimetype1, mimetype2, diff_baton);
}

static svn_error_t *
diff_file_deleted_no_diff (svn_wc_adm_access_t *adm_access,
                           const char *path,
                           const char *tmpfile1,
                           const char *tmpfile2,
                           const char *mimetype1,
                           const char *mimetype2,
                           void *diff_baton)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;

  svn_io_file_printf(diff_cmd_baton->outfile,
                     "Index: %s (deleted)" APR_EOL_STR "%s" APR_EOL_STR, 
                     path, equal_string);

  return SVN_NO_ERROR;
}

/* For now, let's have 'svn diff' send feedback to the top-level
   application, so that something reasonable about directories and
   propsets gets printed to stdout. */
static svn_error_t *
diff_dir_added (svn_wc_adm_access_t *adm_access,
                const char *path,
                void *diff_baton)
{
  /* ### todo:  send feedback to app */
  return SVN_NO_ERROR;
}

static svn_error_t *
diff_dir_deleted (svn_wc_adm_access_t *adm_access,
                  const char *path,
                  void *diff_baton)
{
  /* ### todo:  send feedback to app */
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
  enum svn_wc_merge_outcome_t merge_outcome;

  /* This callback is essentially no more than a wrapper around
     svn_wc_merge().  Thank goodness that all the
     diff-editor-mechanisms are doing the hard work of getting the
     fulltexts! */

  SVN_ERR (svn_wc_text_modified_p (&has_local_mods, mine, FALSE,
                                   adm_access, subpool));
  SVN_ERR (svn_wc_merge (older, yours, mine, adm_access,
                         left_label, right_label, target_label,
                         merge_b->dry_run, &merge_outcome, 
                         merge_b->diff3_cmd, subpool));

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
        *state = svn_wc_notify_state_unknown;
      else /* merge_outcome == svn_wc_merge_unchanged */
        *state = svn_wc_notify_state_unchanged;
    }

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
merge_file_added (svn_wc_adm_access_t *adm_access,
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
  const char *copyfrom_url;
  const char *child;

  SVN_ERR (svn_io_check_path (mine, &kind, subpool));
  switch (kind)
    {
    case svn_node_none:
      if (! merge_b->dry_run)
        {
          child = svn_path_is_child(merge_b->target, mine, merge_b->pool);
          assert (child != NULL);
          copyfrom_url = svn_path_join (merge_b->url, child, merge_b->pool);

          {
            svn_wc_notify_func_t notify_func = merge_b->ctx->notify_func;
            svn_error_t *err;

            /* FIXME: This is lame, we should have some way of doing this 
               that doesn't involve messing with the client context, but 
               for now we have to or we get double notifications for each 
               add. */
            merge_b->ctx->notify_func = NULL; 

            /* ### FIXME: This will get the file again! */
            /* ### 838 When 838 stops using svn_client_copy the adm_access
               parameter can be removed from the function. */
            err = svn_client_copy (NULL, copyfrom_url, merge_b->revision, 
                                   mine, adm_access, merge_b->ctx, subpool);

            merge_b->ctx->notify_func = notify_func;

            if (err) return err;
          }
        }
      break;
    case svn_node_dir:
      /* ### create a .drej conflict or something someday? */
      return svn_error_createf (SVN_ERR_WC_NOT_FILE, NULL,
                                "Cannot create file '%s' for addition, "
                                "because a directory by that name "
                                "already exists.", mine);
    case svn_node_file:
      {
        /* file already exists, is it under version control? */
        const svn_wc_entry_t *entry;
        enum svn_wc_merge_outcome_t merge_outcome;
        SVN_ERR (svn_wc_entry (&entry, mine, adm_access, FALSE, subpool));

        /* If it's an unversioned file, don't touch it.  If its scheduled
           for deletion, then rm removed it from the working copy and the
           user must have recreated it, don't touch it */
        if (!entry || entry->schedule == svn_wc_schedule_delete)
          return svn_error_createf (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
                                    "Cannot create file '%s' for addition, "
                                    "because an unversioned file by that name "
                                    "already exists.", mine);
        SVN_ERR (svn_wc_merge (older, yours, mine, adm_access,
                               ".older", ".yours", ".working", /* ###? */
                               merge_b->dry_run, &merge_outcome, 
                               merge_b->diff3_cmd, subpool));
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

  SVN_ERR (svn_io_check_path (mine, &kind, subpool));
  switch (kind)
    {
    case svn_node_file:
      svn_path_split (mine, &parent_path, NULL, merge_b->pool);
      SVN_ERR (svn_wc_adm_retrieve (&parent_access, adm_access, parent_path,
                                    merge_b->pool));
      SVN_ERR (svn_client__wc_delete (mine, parent_access, merge_b->force,
                                      merge_b->dry_run, merge_b->ctx, subpool));
      break;
    case svn_node_dir:
      /* ### Create a .drej conflict or something someday?  If force is set
         ### should we carry out the delete? */
      return svn_error_createf (SVN_ERR_WC_NOT_FILE, NULL,
                                "Cannot schedule file '%s' for deletion, "
                                "because a directory by that name "
                                "already exists.", mine);
    case svn_node_none:
      /* file is already non-existent, this is a no-op. */
      break;
    default:
      break;
    }
    
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
merge_dir_added (svn_wc_adm_access_t *adm_access,
                 const char *path,
                 void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create (merge_b->pool);
  svn_node_kind_t kind;
  const svn_wc_entry_t *entry;
  const char *copyfrom_url, *child;

  child = svn_path_is_child (merge_b->target, path, subpool);
  assert (child != NULL);
  copyfrom_url = svn_path_join (merge_b->url, child, subpool);

  SVN_ERR (svn_io_check_path (path, &kind, subpool));
  switch (kind)
    {
    case svn_node_none:
      if (! merge_b->dry_run)
        {
          svn_wc_notify_func_t notify_func = merge_b->ctx->notify_func;
          svn_error_t *err;

          /* FIXME: This is lame, we should have some way of doing this 
             that doesn't involve messing with the client context, but 
             for now we have to or we get double notifications for each 
             add. */
          merge_b->ctx->notify_func = NULL;

          /* ### FIXME: This will get the directory tree again! */
          err = svn_client_copy (NULL, copyfrom_url, merge_b->revision, path,
                                  adm_access, merge_b->ctx, subpool);

          merge_b->ctx->notify_func = notify_func;

          if (err) return err;
        }
      break;
    case svn_node_dir:
      /* Adding an unversioned directory doesn't destroy data */
      SVN_ERR (svn_wc_entry (&entry, path, adm_access, TRUE, subpool));
      if (!merge_b->dry_run
          && (! entry || (entry && entry->schedule == svn_wc_schedule_delete)))
        {
          svn_wc_notify_func_t notify_func = merge_b->ctx->notify_func;
          svn_error_t *err;

          /* FIXME: This is lame, we should have some way of doing this 
             that doesn't involve messing with the client context, but 
             for now we have to or we get double notifications for each 
             add. */
          merge_b->ctx->notify_func = NULL;

          /* ### FIXME: This will get the directory tree again! */
          err = svn_client_copy (NULL, copyfrom_url, merge_b->revision, path,
                                 adm_access, merge_b->ctx, subpool);

          merge_b->ctx->notify_func = notify_func;

          if (err) return err;
        }
      break;
    case svn_node_file:
      /* ### create a .drej conflict or something someday? */
      return svn_error_createf (SVN_ERR_WC_NOT_DIRECTORY, NULL,
                                "Cannot create directory '%s' for addition, "
                                "because a file by that name "
                                "already exists.", path);
      break;
    default:
      break;
    }

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
merge_dir_deleted (svn_wc_adm_access_t *adm_access,
                   const char *path,
                   void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create (merge_b->pool);
  svn_node_kind_t kind;
  svn_wc_adm_access_t *parent_access;
  const char *parent_path;
  
  SVN_ERR (svn_io_check_path (path, &kind, subpool));
  switch (kind)
    {
    case svn_node_dir:
      svn_path_split (path, &parent_path, NULL, merge_b->pool);
      SVN_ERR (svn_wc_adm_retrieve (&parent_access, adm_access, parent_path,
                                    merge_b->pool));
      SVN_ERR (svn_client__wc_delete (path, parent_access, merge_b->force,
                                      merge_b->dry_run, merge_b->ctx, subpool));
      break;
    case svn_node_file:
      /* ### Create a .drej conflict or something someday?  If force is set
         ### should we carry out the delete? */
      return svn_error_createf (SVN_ERR_WC_NOT_DIRECTORY, NULL,
                                "Cannot schedule directory '%s' for deletion, "
                                "because a file by that name "
                                "already exists.", path);
    case svn_node_none:
      /* dir is already non-existent, this is a no-op. */
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

  SVN_ERR (svn_categorize_props (propchanges, NULL, NULL, &props, subpool));

  /* We only want to merge "regular" version properties:  by
     definition, 'svn merge' shouldn't touch any data within .svn/  */
  if (props->nelts)
    SVN_ERR (svn_wc_merge_prop_diffs (state, path, adm_access, props,
                                      FALSE, merge_b->dry_run, subpool));
 
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



/* PATH1, PATH2, and TARGET_WCPATH all better be directories.   For
   the single file case, the caller do the merging manually. */
static svn_error_t *
do_merge (const char *URL1,
          const svn_opt_revision_t *revision1,
          const char *URL2,
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
           (&start_revnum, ra_lib, session, revision1, NULL, pool));
  SVN_ERR (svn_client__get_revision_number
           (&end_revnum, ra_lib, session, revision2, NULL, pool));

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


/* Get REVISION of the file at URL.  Return in *FILENAME the name of a file
   containing the file contents, in *PROPS a hash containing the properties
   and in *REV the revision.  All allocation occurs in POOL. */
static svn_error_t *
single_file_merge_get_file (const char **filename,
                            apr_hash_t **props,
                            svn_revnum_t *rev,
                            const char *url,
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
                                            NULL, pool));
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
                                       URL1, revision1,
                                       ra_baton, auth_dir, merge_b, pool));

  SVN_ERR (single_file_merge_get_file (&tmpfile2, &props2, &rev2,
                                       merge_b->url, merge_b->revision,
                                       ra_baton, auth_dir, merge_b, pool));

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
static svn_error_t *
polite_error (svn_error_t *child_err,
              apr_pool_t *pool)
{
  return svn_error_create (SVN_ERR_INCORRECT_PARAMS, child_err,
                           "Sorry, svn_client_diff was called in a way "
                           "that is not yet supported.");
}
              

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
  svn_revnum_t start_revnum, end_revnum;
  const char *anchor = NULL, *target = NULL;
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  const svn_ra_reporter_t *reporter;
  void *report_baton;
  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;
  const char *auth_dir;
  svn_node_kind_t kind;

  /* Sanity check -- ensure that we have valid revisions to look at. */
  if ((revision1->kind == svn_opt_revision_unspecified)
      || (revision2->kind == svn_opt_revision_unspecified))
    return svn_error_create (SVN_ERR_CLIENT_BAD_REVISION, NULL,
                             "do_diff: not all revisions are specified.");

  /* The simplest use-case.  No repository contact required. */
  if ((revision1->kind == svn_opt_revision_base)
      && (revision2->kind == svn_opt_revision_working))
    {
      svn_wc_adm_access_t *adm_access;
      /* Sanity check -- path1 and path2 are the same working-copy path. */
      if (strcmp (path1, path2) != 0) 
        return polite_error (svn_error_create 
                             (SVN_ERR_INCORRECT_PARAMS, NULL,
                              "do_diff: paths aren't equal!"),
                             pool);
      if (svn_path_is_url (path1))
        return polite_error (svn_error_create 
                             (SVN_ERR_INCORRECT_PARAMS, NULL,
                              "do_diff: path isn't a working-copy path."),
                             pool);

      SVN_ERR (svn_wc_get_actual_target (path1, &anchor, &target, pool));
      SVN_ERR (svn_io_check_path (path1, &kind, pool));
      SVN_ERR (svn_wc_adm_open (&adm_access, NULL, anchor, FALSE,
                                (recurse && (! target)),
                                pool));

      if (target && (kind == svn_node_dir))
        {
          /* Associate a potentially tree-locked access baton for the
             target with the anchor's access baton. */ 
          svn_wc_adm_access_t *target_access;
          SVN_ERR (svn_wc_adm_open (&target_access, adm_access, path1,
                                    FALSE, recurse, pool));
          /* We don't actually use target_access here; it just floats
             around in adm_access's set of associated batons, where
             the diff editor can find it. */
        }

      SVN_ERR (svn_wc_diff (adm_access, target, callbacks, callback_baton,
                            recurse, pool));
      SVN_ERR (svn_wc_adm_close (adm_access));
    }

  /* Next use-case:  some repos-revision compared against wcpath@WORKING */
  else if ((revision2->kind == svn_opt_revision_working)
           && (revision1->kind != svn_opt_revision_working)
           && (revision1->kind != svn_opt_revision_base))
    {
      const char *URL1;
      const char *url_anchor, *url_target;
      svn_wc_adm_access_t *adm_access, *dir_access;

      /* Sanity check -- path2 better be a working-copy path. */
      if (svn_path_is_url (path2))
        return polite_error (svn_error_create 
                             (SVN_ERR_INCORRECT_PARAMS, NULL,
                              "do_diff: path isn't a working-copy path."),
                             pool);

      /* Extract a URL and revision from path1 (if not already a URL) */
      SVN_ERR (convert_to_url (&URL1, path1, pool));

      /* Trickiness:  possibly split up path2 into anchor/target.  If
         we do so, then we must split URL1 as well.  We shouldn't go
         assuming that URL1 is equal to path2's URL, as we used to. */
      SVN_ERR (svn_wc_get_actual_target (path2, &anchor, &target, pool));
      if (target)
        {
          svn_path_split (URL1, &url_anchor, &url_target, pool);
        }
      else
        {
          url_anchor = URL1;
          url_target = NULL;
        }

      /* Establish RA session to URL1's anchor */
      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton,
                                      url_anchor, pool));

      SVN_ERR (svn_client__default_auth_dir (&auth_dir, path2, pool));

      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, url_anchor,
                                            auth_dir,
                                            NULL, NULL, FALSE, TRUE,
                                            ctx, pool));
      
      /* Set up diff editor according to path2's anchor/target. */
      SVN_ERR (svn_io_check_path (path2, &kind, pool));
      SVN_ERR (svn_wc_adm_open (&adm_access, NULL, anchor, FALSE,
                                (recurse && (! target)),
                                pool));

      if (target && (kind == svn_node_dir))
        {
          /* Associate a potentially tree-locked access baton for the
             target with the anchor's access baton. */ 
          svn_wc_adm_access_t *target_access;
          SVN_ERR (svn_wc_adm_open (&target_access, adm_access, path2,
                                    FALSE, recurse, pool));
          /* We don't actually use target_access here; it just floats
             around in adm_access's set of associated batons, where
             the diff editor can find it. */
        }

      SVN_ERR (svn_wc_get_diff_editor (adm_access, target,
                                       callbacks, callback_baton,
                                       recurse,
                                       FALSE, /* examine working files */
                                       FALSE, /* don't do it backwards */
                                       ctx->cancel_func, ctx->cancel_baton,
                                       &diff_editor, &diff_edit_baton,
                                       pool));

      /* Tell the RA layer we want a delta to change our txn to URL1 */
      SVN_ERR (svn_client__get_revision_number
               (&start_revnum, ra_lib, session, revision1, path1, pool));
      callback_baton->revnum1 = start_revnum;
      SVN_ERR (ra_lib->do_update (session,
                                  &reporter, &report_baton,
                                  start_revnum,
                                  svn_path_uri_decode (url_target, pool),
                                  recurse,                                  
                                  diff_editor, diff_edit_baton, pool));

      if (kind == svn_node_dir)
        SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access, path2, pool));
      else
        SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access,
                                      svn_path_dirname (path2, pool),
                                      pool));

      /* Create a txn mirror of path2;  the diff editor will print
         diffs in reverse.  :-)  */
      SVN_ERR (svn_wc_crawl_revisions (path2, dir_access,
                                       reporter, report_baton,
                                       FALSE, recurse,
                                       NULL, NULL, /* notification is N/A */
                                       NULL, pool));

      SVN_ERR (svn_wc_adm_close (adm_access));
    }
  
  /* Next use-case:  some repos-revision compared against wcpath@BASE */
  else if (((revision2->kind == svn_opt_revision_base)
            && (revision1->kind != svn_opt_revision_working)
            && (revision1->kind != svn_opt_revision_base))
           ||
           ((revision1->kind == svn_opt_revision_base)
            && (revision2->kind != svn_opt_revision_working)
            && (revision2->kind != svn_opt_revision_base)))
    {
      const char *URL, *wcpath, *repospath;
      const char *url_anchor, *url_target;
      svn_wc_adm_access_t *adm_access, *dir_access;
      const svn_opt_revision_t *baserev, *reposrev;
      svn_boolean_t diff_backwards = FALSE;
      
      if (revision1->kind == svn_opt_revision_base)
        {
          wcpath = path1;
          repospath = path2;
          baserev = revision1;
          reposrev = revision2;
          diff_backwards = TRUE;
        }
      else
        {
          wcpath = path2;
          repospath = path1;
          baserev = revision2;
          reposrev = revision1;
        }

      /* Sanity check -- wcpath better be a working-copy path. */
      if (svn_path_is_url (wcpath))
        return polite_error (svn_error_create 
                             (SVN_ERR_INCORRECT_PARAMS, NULL,
                              "do_diff: path isn't a working-copy path."),
                             pool);

      /* Extract a URL and revision from repospath (if not already a URL) */
      SVN_ERR (convert_to_url (&URL, repospath, pool));

      /* Trickiness:  possibly split up wcpath into anchor/target.  If
         we do so, then we must split URL as well.  We shouldn't go
         assuming that URL is equal to wcpath's URL, as we used to. */
      SVN_ERR (svn_wc_get_actual_target (wcpath, &anchor, &target, pool));
      if (target)
        {
          svn_path_split (URL, &url_anchor, &url_target, pool);
        }
      else
        {
          url_anchor = URL;
          url_target = NULL;
        }

      /* Establish RA session to URL's anchor */
      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton,
                                      url_anchor, pool));

      SVN_ERR (svn_client__default_auth_dir (&auth_dir, wcpath, pool));

      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, url_anchor,
                                            auth_dir,
                                            NULL, NULL, FALSE, TRUE,
                                            ctx, pool));
      
      /* Set up diff editor according to wcpath's anchor/target. */
      SVN_ERR (svn_wc_adm_open (&adm_access, NULL, anchor, FALSE, TRUE, pool));
      SVN_ERR (svn_wc_get_diff_editor (adm_access, target,
                                       callbacks, callback_baton,
                                       recurse,
                                       TRUE, /* examine text-bases */
                                       diff_backwards, /* set above */
                                       ctx->cancel_func, ctx->cancel_baton,
                                       &diff_editor, &diff_edit_baton,
                                       pool));

      /* Tell the RA layer we want a delta to change our txn to URL */
      SVN_ERR (svn_client__get_revision_number
               (&start_revnum, ra_lib, session, reposrev, repospath, pool));
      callback_baton->revnum1 = start_revnum;
      SVN_ERR (ra_lib->do_update (session,
                                  &reporter, &report_baton,
                                  start_revnum,
                                  svn_path_uri_decode (url_target, pool),
                                  recurse,                                  
                                  diff_editor, diff_edit_baton, pool));

      SVN_ERR (svn_io_check_path (wcpath, &kind, pool));
      if (kind == svn_node_dir)
        SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access, wcpath, pool));
      else
        SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access,
                                      svn_path_dirname (wcpath, pool),
                                      pool));

      /* Create a txn mirror of wcpath;  the diff editor will print
         diffs in reverse.  :-)  */
      SVN_ERR (svn_wc_crawl_revisions (wcpath, dir_access,
                                       reporter, report_baton,
                                       FALSE, recurse,
                                       NULL, NULL, /* notification is N/A */
                                       NULL, pool));

      SVN_ERR (svn_wc_adm_close (adm_access));
    }


  /* Last use-case:  comparing path1@rev1 and path2@rev2, where both revs
     require repository contact.  */
  else if ((revision2->kind != svn_opt_revision_working)
           && (revision2->kind != svn_opt_revision_base)
           && (revision1->kind != svn_opt_revision_working)
           && (revision1->kind != svn_opt_revision_base))
    {
      const char *URL1, *URL2;
      const char *anchor1, *target1, *anchor2, *target2;
      svn_boolean_t path1_is_url, path2_is_url;
      svn_node_kind_t path1_kind;
      svn_node_kind_t path2_kind;
      void *session2;

      /* The paths could be *either* wcpaths or urls... */
      SVN_ERR (convert_to_url (&URL1, path1, pool));
      SVN_ERR (convert_to_url (&URL2, path2, pool));

      path1_is_url = svn_path_is_url (path1);
      path2_is_url = svn_path_is_url (path2);

      /* Open temporary RA sessions to each URL. */
      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL1, pool));
      SVN_ERR (svn_client__dir_if_wc (&auth_dir, "", pool));
      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL1, auth_dir,
                                            NULL, NULL, FALSE, TRUE, 
                                            ctx, pool));
      SVN_ERR (svn_client__open_ra_session (&session2, ra_lib, URL2, auth_dir,
                                            NULL, NULL, FALSE, TRUE, 
                                            ctx, pool));

      /* Do the right thing in resolving revisions;  if the caller
         does something foolish like pass in URL@committed, then they
         should rightfully get an error when we pass a NULL path below. */
      SVN_ERR (svn_client__get_revision_number
               (&start_revnum, ra_lib, session, revision1, 
                path1_is_url ? NULL : path1, 
                pool));
      callback_baton->revnum1 = start_revnum;
      SVN_ERR (svn_client__get_revision_number
               (&end_revnum, ra_lib, session2, revision2,
                path2_is_url ? NULL : path2, 
                pool));
      callback_baton->revnum2 = end_revnum;

      if (path1_is_url)
        {
          SVN_ERR (ra_lib->check_path (&path1_kind, session, "", start_revnum,
                                       pool));

          switch (path1_kind)
            {
            case svn_node_file:
            case svn_node_dir:
              break;

            default:
              return svn_error_createf (SVN_ERR_FS_NOT_FOUND, NULL,
                                        "'%s' at rev %" SVN_REVNUM_T_FMT
                                        " wasn't found in repository.",
                                        path1, start_revnum);
            }
        }
      
      /* Now down to the -real- business.  We gotta figure out anchors
         and targets, whether things are urls or wcpaths.

         Like we do in the 2nd use-case, we have PATH1 follow PATH2's
         lead.  If PATH2 is split into anchor/target, then so must
         PATH1 (URL1) be. 

         Now, at the end of all this, we want ANCHOR2 to be "" if
         PATH2 is a URL, or the actual path anchor otherwise.
         TARGET2, if non-NULL, will be a filesystem path component.
         Likewise, ANCHOR1 will be a URL, and TARGET1, if non-NULL,
         will be a filesystem path component.  */
      if (path2_is_url)
        {
          anchor2 = "";
          SVN_ERR (ra_lib->check_path (&path2_kind, session2, "", end_revnum,
                                       pool));

          switch (path2_kind)
            {
            case svn_node_file:
              target2 = svn_path_uri_decode (svn_path_basename (path2, pool),
                                             pool);
              break;

            case svn_node_dir:
              target2 = NULL;
              break;

            default:
              return svn_error_createf (SVN_ERR_FS_NOT_FOUND, NULL,
                                        "'%s' at rev %" SVN_REVNUM_T_FMT
                                        " wasn't found in repository.",
                                        path2, end_revnum);
            }                   
        }
      else
        {
          SVN_ERR (svn_wc_get_actual_target (path2, &anchor2, &target2, pool));
        }

      if (target2)
        {
          svn_path_split (URL1, &anchor1, &target1, pool); 
          target1 = svn_path_uri_decode (target1, pool);
        }
      else
        {
          anchor1 = URL1;
          target1 = NULL;
        }

      /* The main session is opened to the anchor of URL1. */
      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, anchor1,
                                            auth_dir,
                                            NULL, NULL, FALSE, TRUE, 
                                            ctx, pool));


      /* Open a second session used to request individual file
         contents from URL1's anchor.  */
      SVN_ERR (svn_client__open_ra_session (&session2, ra_lib, anchor1,
                                            auth_dir,
                                            NULL, NULL, FALSE, TRUE,
                                            ctx, pool));      

      /* Set up the repos_diff editor on path2's anchor, assuming
         path2 is a wc_dir.  if path2 is a URL, then we want to anchor
         the diff editor on "", because we don't want to see any url's
         in the diff headers. */
      SVN_ERR (svn_client__get_diff_editor (anchor2,
                                            NULL,
                                            callbacks,
                                            callback_baton,
                                            recurse,
                                            FALSE,  /* does't matter for diff */
                                            ra_lib, session2,
                                            start_revnum,
                                            NULL, /* no notify_func */
                                            NULL, /* no notify_baton */
                                            ctx->cancel_func,
                                            ctx->cancel_baton,
                                            &diff_editor,
                                            &diff_edit_baton,
                                            pool));

      /* We want to switch our txn into URL2 */
      SVN_ERR (ra_lib->do_diff (session,
                                &reporter, &report_baton,
                                end_revnum,
                                target1,
                                recurse,
                                ignore_ancestry,
                                URL2,
                                diff_editor, diff_edit_baton, pool));

      SVN_ERR (reporter->set_path (report_baton, "", start_revnum,
                                   FALSE, pool));
      SVN_ERR (reporter->finish_report (report_baton));
    }

  else
    {
      /* can't pigeonhole our inputs into one of the three use-cases. */
      return polite_error (NULL, pool);
    }
    
  return SVN_NO_ERROR;
}




/*----------------------------------------------------------------------- */

/*** Public Interfaces. ***/

/* Display context diffs between two PATH/REVISION pairs.  Each of
   these input will be one of the following:

   - a repository URL at a given revision.
   - a working copy path, ignoring no local mods.
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
svn_client_merge (const char *URL1,
                  const svn_opt_revision_t *revision1,
                  const char *URL2,
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
      SVN_ERR (do_single_file_merge (URL1, revision1,
                                     adm_access,
                                     &merge_cmd_baton,
                                     pool));
    }

  /* Otherwise, this must be a directory merge.  Do the fancy
     recursive diff-editor thing. */
  else if (entry->kind == svn_node_dir)
    {
      SVN_ERR (do_merge (URL1,
                         revision1,
                         URL2,
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
