/*
 * diff.c: comparing and merging
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_test.h"
#include "svn_io.h"
#include "svn_utf.h"
#include "svn_pools.h"
#include "client.h"
#include <assert.h>


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

  SVN_ERR (svn_io_file_printf (file, "\nProperty changes on: %s\n", path));
  apr_file_printf (file, 
     "___________________________________________________________________\n");

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
      
      SVN_ERR (svn_io_file_printf (file, "Name: %s\n", propchange->name));

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
            
            apr_file_printf (file, "   - %s\n", printable_val);
          }
        
        if (propchange->value != NULL)
          {
            if (val_to_utf8)
              SVN_ERR (svn_utf_cstring_from_utf8
                       (&printable_val, propchange->value->data, pool));
            else
              printable_val = propchange->value->data;

            apr_file_printf (file, "   + %s\n", printable_val);
          }
      }
    }

  apr_file_printf (file, "\n");

  return SVN_NO_ERROR;
}




/*-----------------------------------------------------------------*/

/*** Callbacks for 'svn diff', invoked by the repos-diff editor. ***/


struct diff_cmd_baton {
  const apr_array_header_t *options;
  apr_pool_t *pool;
  apr_file_t *outfile;
  apr_file_t *errfile;

  /* These are the numeric representations of the revisions passed to
     svn_client_diff, either may be SVN_INVALID_REVNUM.  We need these
     because some of the svn_wc_diff_callbacks_t don't get revision
     arguments.

     ### Perhaps we should change the callback signatures and eliminate
     ### these?
  */
  svn_revnum_t revnum1;
  svn_revnum_t revnum2;
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
                   void *diff_baton)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;
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

  /* Print out the diff header. */
  SVN_ERR (svn_io_file_printf (outfile, "Index: %s\n", path));
  apr_file_printf (outfile, 
     "===================================================================\n");

  label1 = diff_label (path, rev1, subpool);
  label2 = diff_label (path, rev2, subpool);
  SVN_ERR (svn_io_run_diff (".", args, nargs, label1, label2,
                            tmpfile1, tmpfile2, 
                            &exitcode, outfile, errfile, subpool));

  /* ### todo: Handle exit code == 2 (i.e. errors with diff) here */
  
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
                 void *diff_baton)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;

  return diff_file_changed (adm_access, NULL, path, tmpfile1, tmpfile2, 
                            diff_cmd_baton->revnum1, diff_cmd_baton->revnum2,
                            diff_baton);
}

static svn_error_t *
diff_file_deleted (svn_wc_adm_access_t *adm_access,
                   const char *path,
                   const char *tmpfile1,
                   const char *tmpfile2,
                   void *diff_baton)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;

  return diff_file_changed (adm_access, NULL, path, tmpfile1, tmpfile2, 
                            diff_cmd_baton->revnum1, diff_cmd_baton->revnum2,
                            diff_baton);
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
  apr_array_header_t *entry_props, *wc_props, *regular_props;
  apr_pool_t *subpool = svn_pool_create (diff_cmd_baton->pool);

  SVN_ERR (svn_categorize_props (propchanges,
                                 &entry_props, &wc_props, &regular_props,
                                 subpool));

  if (regular_props->nelts > 0)
    SVN_ERR (display_prop_diffs (regular_props,
                                 original_props,
                                 path,
                                 diff_cmd_baton->outfile,
                                 subpool));

  if (state)
    *state = svn_wc_notify_state_unknown;

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}

/* The main callback table for 'svn diff'.  */
static const svn_wc_diff_callbacks_t 
diff_callbacks =
  {
    diff_file_changed,
    diff_file_added,
    diff_file_deleted,
    diff_dir_added,
    diff_dir_deleted,
    diff_props_changed
  };


/*-----------------------------------------------------------------*/

/*** Callbacks for 'svn merge', invoked by the repos-diff editor. ***/


struct merge_cmd_baton {
  svn_boolean_t force;
  svn_boolean_t dry_run;
  const char *target;                 /* Working copy target of merge */
  const char *url;                    /* The second URL in the merge */
  const svn_opt_revision_t *revision; /* Revision of second URL in the merge */
  svn_client_auth_baton_t *auth_baton;
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
                    void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create (merge_b->pool);
  const char *target_label = ".working";
  const char *left_label = apr_psprintf (subpool, ".r%" SVN_REVNUM_T_FMT,
                                         older_rev);
  const char *right_label = apr_psprintf (subpool, ".r%" SVN_REVNUM_T_FMT,
                                          yours_rev);
  svn_boolean_t has_local_mods;
  enum svn_wc_merge_outcome_t merge_outcome;

  /* This callback is essentially no more than a wrapper around
     svn_wc_merge().  Thank goodness that all the
     diff-editor-mechanisms are doing the hard work of getting the
     fulltexts! */

  SVN_ERR (svn_wc_text_modified_p (&has_local_mods, mine, adm_access, subpool));
  SVN_ERR (svn_wc_merge (older, yours, mine, adm_access,

                         left_label, right_label, target_label,
                         merge_b->dry_run, &merge_outcome,
                         subpool));

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
          /* ### FIXME: This will get the file again! */
          /* ### 838 When 838 stops using svn_client_copy the adm_access
             parameter can be removed from the function. */
          SVN_ERR (svn_client_copy (NULL, copyfrom_url, merge_b->revision, mine,
                                    adm_access,
                                    merge_b->auth_baton, NULL, NULL, NULL, NULL,
                                    merge_b->pool));
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
                               merge_b->dry_run, &merge_outcome, subpool));
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
      SVN_ERR (svn_client_delete (NULL, mine, parent_access, merge_b->force,
                                  NULL, NULL, NULL, NULL, NULL, subpool));
      break;
    case svn_node_dir:
      /* ### create a .drej conflict or something someday? */
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
        /* ### FIXME: This will get the directory tree again! */
        SVN_ERR (svn_client_copy (NULL, copyfrom_url, merge_b->revision, path,
                                  adm_access,
                                  merge_b->auth_baton, NULL, NULL, NULL, NULL,
                                  subpool));
      break;
    case svn_node_dir:
      /* Adding an unversioned directory doesn't destroy data */
      SVN_ERR (svn_wc_entry (&entry, path, adm_access, TRUE, subpool));
      if (!merge_b->dry_run
          && (! entry || (entry && entry->schedule == svn_wc_schedule_delete)))
        /* ### FIXME: This will get the directory tree again! */
        SVN_ERR (svn_client_copy (NULL, copyfrom_url, merge_b->revision, path,
                                  adm_access,
                                  merge_b->auth_baton, NULL, NULL, NULL, NULL,
                                  subpool));
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
      SVN_ERR (svn_client_delete (NULL, path, parent_access, merge_b->force,
                                  NULL, NULL, NULL, NULL, NULL, subpool));
      break;
    case svn_node_file:
      /* ### create a .drej conflict or something someday? */
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
  apr_array_header_t *entry_props, *wc_props, *regular_props;
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create (merge_b->pool);

  SVN_ERR (svn_categorize_props (propchanges,
                                 &entry_props, &wc_props, &regular_props,
                                 subpool));

  /* We only want to merge "regular" version properties:  by
     definition, 'svn merge' shouldn't touch any data within .svn/  */
  if (regular_props)
    SVN_ERR (svn_wc_merge_prop_diffs (state, path, adm_access, regular_props,
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
                              "convert_to_url: %s is not versioned", path);
  
  *url = apr_pstrdup (pool, entry->url);
  return SVN_NO_ERROR;
}



/* PATH1, PATH2, and TARGET_WCPATH all better be directories.   For
   the single file case, the caller do the merging manually. */
static svn_error_t *
do_merge (svn_wc_notify_func_t notify_func,
          void *notify_baton,
          svn_client_auth_baton_t *auth_baton,
          const char *URL1,
          const svn_opt_revision_t *revision1,
          const char *URL2,
          const svn_opt_revision_t *revision2,
          const char *target_wcpath,
          svn_wc_adm_access_t *adm_access,
          svn_boolean_t recurse,
          svn_boolean_t dry_run,
          const svn_wc_diff_callbacks_t *callbacks,
          void *callback_baton,
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
                                        NULL, NULL, FALSE, FALSE, TRUE, 
                                        auth_baton, pool));
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
                                        NULL, NULL, FALSE, FALSE, TRUE,
                                        auth_baton, pool));
  
  SVN_ERR (svn_client__get_diff_editor (target_wcpath,
                                        adm_access,
                                        callbacks,
                                        callback_baton,
                                        recurse,
                                        dry_run,
                                        ra_lib, session2,
                                        start_revnum,
                                        notify_func,
                                        notify_baton,
                                        &diff_editor,
                                        &diff_edit_baton,
                                        pool));

  SVN_ERR (ra_lib->do_diff (session,
                            &reporter, &report_baton,
                            end_revnum,
                            NULL,
                            recurse,
                            URL2,
                            diff_editor, diff_edit_baton));
  
  SVN_ERR (reporter->set_path (report_baton, "", start_revnum));
  
  SVN_ERR (reporter->finish_report (report_baton));
  
  SVN_ERR (ra_lib->close (session2));

  SVN_ERR (ra_lib->close (session));

  return SVN_NO_ERROR;
}



/* The single-file, simplified version of do_merge. */
static svn_error_t *
do_single_file_merge (svn_wc_notify_func_t notify_func,
                      void *notify_baton,
                      svn_client_auth_baton_t *auth_baton,
                      const char *URL1,
                      const svn_opt_revision_t *revision1,
                      const char *URL2,
                      const svn_opt_revision_t *revision2,
                      const char *target_wcpath,
                      svn_wc_adm_access_t *adm_access,
                      svn_boolean_t dry_run,
                      apr_pool_t *pool)
{
  apr_status_t status;
  apr_file_t *fp1 = NULL, *fp2 = NULL;
  const char *tmpfile1, *tmpfile2;
  svn_stream_t *fstream1, *fstream2;
  const char *oldrev_str, *newrev_str;
  svn_revnum_t rev1, rev2;
  apr_hash_t *props1, *props2;
  apr_array_header_t *propchanges, *wc_props, *entry_props, *regular_props;
  void *ra_baton, *session1, *session2;
  svn_ra_plugin_t *ra_lib;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_unknown;
  svn_wc_notify_state_t text_state = svn_wc_notify_state_unknown;
  enum svn_wc_merge_outcome_t merge_outcome;
  const char *auth_dir;
  
  props1 = apr_hash_make (pool);
  props2 = apr_hash_make (pool);
  
  SVN_ERR (svn_client__default_auth_dir (&auth_dir, target_wcpath, pool));

  /* Create two temporary files that contain the fulltexts of
     PATH1@REV1 and PATH2@REV2. */
  SVN_ERR (svn_io_open_unique_file (&fp1, &tmpfile1, 
                                    target_wcpath, ".tmp",
                                    FALSE, pool));
  SVN_ERR (svn_io_open_unique_file (&fp2, &tmpfile2, 
                                    target_wcpath, ".tmp",
                                    FALSE, pool));
  fstream1 = svn_stream_from_aprfile (fp1, pool);
  fstream2 = svn_stream_from_aprfile (fp2, pool);
  
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));

  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL1, pool));
  SVN_ERR (svn_client__open_ra_session (&session1, ra_lib, URL1, auth_dir,
                                        NULL, NULL, FALSE, FALSE, TRUE, 
                                        auth_baton, pool));
  SVN_ERR (svn_client__get_revision_number
           (&rev1, ra_lib, session1, revision1, NULL, pool));
  SVN_ERR (ra_lib->get_file (session1, "", rev1, fstream1, NULL, &props1));
  SVN_ERR (ra_lib->close (session1));

  /* ### heh, funny.  we could be fetching two fulltexts from two
     *totally* different repositories here.  :-) */

  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL2, pool));
  SVN_ERR (svn_client__open_ra_session (&session2, ra_lib, URL2, auth_dir,
                                        NULL, NULL, FALSE, FALSE, TRUE, 
                                        auth_baton, pool));
  SVN_ERR (svn_client__get_revision_number
           (&rev2, ra_lib, session2, revision2, NULL, pool));
  SVN_ERR (ra_lib->get_file (session2, "", rev2, fstream2, NULL, &props2));
  SVN_ERR (ra_lib->close (session2));

  status = apr_file_close (fp1);
  if (status)
    return svn_error_createf (status, NULL, "failed to close '%s'.",
                              tmpfile1);
  status = apr_file_close (fp2);
  if (status)
    return svn_error_createf (status, NULL, "failed to close '%s'.",
                              tmpfile2);   
  
  /* Perform a 3-way merge between the temporary fulltexts and the
     current working file. */
  oldrev_str = apr_psprintf (pool, ".r%" SVN_REVNUM_T_FMT, rev1);
  newrev_str = apr_psprintf (pool, ".r%" SVN_REVNUM_T_FMT, rev2); 
  SVN_ERR (svn_wc_merge (tmpfile1, tmpfile2,
                         target_wcpath, adm_access,
                         oldrev_str, newrev_str, ".working", dry_run,
                         &merge_outcome, pool));
  if (merge_outcome == svn_wc_merge_conflict)
    text_state = svn_wc_notify_state_conflicted;

  SVN_ERR (svn_io_remove_file (tmpfile1, pool));
  SVN_ERR (svn_io_remove_file (tmpfile2, pool));
  
  /* Deduce property diffs, and merge those too. */
  SVN_ERR (svn_wc_get_local_propchanges (&propchanges,
                                         props1, props2, pool));
  SVN_ERR (svn_categorize_props (propchanges,
                                 &entry_props, &wc_props, &regular_props,
                                 pool));
  SVN_ERR (svn_wc_merge_prop_diffs (&prop_state, target_wcpath, adm_access,
                                    regular_props, FALSE, dry_run, pool));

  if (notify_func)
    {

      /* First check that regular props changed. */
      if (regular_props->nelts == 0)
	prop_state = svn_wc_notify_state_unchanged;

      /* ### todo: Detect the actual text state, and pass that below.  */

      (*notify_func) (notify_baton,
                      target_wcpath,
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
         svn_client_auth_baton_t *auth_baton,
         const char *path1,
         const svn_opt_revision_t *revision1,
         const char *path2,
         const svn_opt_revision_t *revision2,
         svn_boolean_t recurse,
         const svn_wc_diff_callbacks_t *callbacks,
         struct diff_cmd_baton *callback_baton,
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
      SVN_ERR (svn_wc_adm_open (&adm_access, NULL, anchor, FALSE, recurse,
                                pool));
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
      svn_node_kind_t kind;

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
                                            NULL, NULL, FALSE, FALSE, TRUE,
                                            auth_baton, pool));
      
      /* Set up diff editor according to path2's anchor/target. */
      SVN_ERR (svn_wc_adm_open (&adm_access, NULL, anchor, FALSE, TRUE, pool));
      SVN_ERR (svn_wc_get_diff_editor (adm_access, target,
                                       callbacks, callback_baton,
                                       recurse,
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
                                  diff_editor, diff_edit_baton));

      SVN_ERR (svn_io_check_path (path2, &kind, pool));
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
                                            NULL, NULL, FALSE, FALSE, TRUE, 
                                            auth_baton, pool));
      SVN_ERR (svn_client__open_ra_session (&session2, ra_lib, URL2, auth_dir,
                                            NULL, NULL, FALSE, FALSE, TRUE, 
                                            auth_baton, pool));

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
          SVN_ERR (ra_lib->check_path (&path2_kind, session2, "", end_revnum));

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

      SVN_ERR (ra_lib->close (session));
      SVN_ERR (ra_lib->close (session2));
      
      /* The main session is opened to the anchor of URL1. */
      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, anchor1,
                                            auth_dir,
                                            NULL, NULL, FALSE, FALSE, TRUE, 
                                            auth_baton, pool));


      /* Open a second session used to request individual file
         contents from URL1's anchor.  */
      SVN_ERR (svn_client__open_ra_session (&session2, ra_lib, anchor1,
                                            auth_dir,
                                            NULL, NULL, FALSE, FALSE, TRUE,
                                            auth_baton, pool));      

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
                                            &diff_editor,
                                            &diff_edit_baton,
                                            pool));

      /* We want to switch our txn into URL2 */
      SVN_ERR (ra_lib->do_diff (session,
                                &reporter, &report_baton,
                                end_revnum,
                                target1,
                                recurse,
                                URL2,
                                diff_editor, diff_edit_baton));      

      SVN_ERR (reporter->set_path (report_baton, "", start_revnum));
      SVN_ERR (reporter->finish_report (report_baton));

      SVN_ERR (ra_lib->close (session2));
      SVN_ERR (ra_lib->close (session));      
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
                 svn_client_auth_baton_t *auth_baton,
                 const char *path1,
                 const svn_opt_revision_t *revision1,
                 const char *path2,
                 const svn_opt_revision_t *revision2,
                 svn_boolean_t recurse,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
                 apr_pool_t *pool)
{
  struct diff_cmd_baton diff_cmd_baton;

  diff_cmd_baton.options = options;
  diff_cmd_baton.pool = pool;
  diff_cmd_baton.outfile = outfile;
  diff_cmd_baton.errfile = errfile;
  diff_cmd_baton.revnum1 = SVN_INVALID_REVNUM;
  diff_cmd_baton.revnum2 = SVN_INVALID_REVNUM;

  return do_diff (options,
                  auth_baton,
                  path1, revision1,
                  path2, revision2,
                  recurse,
                  &diff_callbacks, &diff_cmd_baton,
                  pool);
}


svn_error_t *
svn_client_merge (svn_wc_notify_func_t notify_func,
                  void *notify_baton,
                  svn_client_auth_baton_t *auth_baton,
                  const char *URL1,
                  const svn_opt_revision_t *revision1,
                  const char *URL2,
                  const svn_opt_revision_t *revision2,
                  const char *target_wcpath,
                  svn_boolean_t recurse,
                  svn_boolean_t force,
                  svn_boolean_t dry_run,
                  apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;

  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, target_wcpath,
                                  ! dry_run, recurse, pool));

  SVN_ERR (svn_wc_entry (&entry, target_wcpath, adm_access, FALSE, pool));
  if (entry == NULL)
    return svn_error_createf (SVN_ERR_ENTRY_NOT_FOUND, NULL,
                              "Can't merge changes into '%s':"
                              "it's not under revision control.", 
                              target_wcpath);

  /* If our target_wcpath is a single file, assume that PATH1 and
     PATH2 are files as well, and do a single-file merge. */
  if (entry->kind == svn_node_file)
    {
      SVN_ERR (do_single_file_merge (notify_func,
                                     notify_baton,
                                     auth_baton,
                                     URL1, revision1,
                                     URL2, revision2,
                                     target_wcpath,
                                     adm_access,
                                     dry_run,
                                     pool));
    }

  /* Otherwise, this must be a directory merge.  Do the fancy
     recursive diff-editor thing. */
  else if (entry->kind == svn_node_dir)
    {
      struct merge_cmd_baton merge_cmd_baton;
      merge_cmd_baton.force = force;
      merge_cmd_baton.dry_run = dry_run;
      merge_cmd_baton.target = target_wcpath;
      merge_cmd_baton.url = URL2;
      merge_cmd_baton.revision = revision2;
      merge_cmd_baton.auth_baton = auth_baton;
      merge_cmd_baton.pool = pool;

      SVN_ERR (do_merge (notify_func,
                         notify_baton,
                         auth_baton,
                         URL1,
                         revision1,
                         URL2,
                         revision2,
                         target_wcpath,
                         adm_access,
                         recurse,
                         dry_run,
                         &merge_callbacks,
                         &merge_cmd_baton,
                         pool));
    }

  SVN_ERR (svn_wc_adm_close (adm_access));

  return SVN_NO_ERROR;
}
