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
#include "svn_pools.h"
#include "client.h"
#include <assert.h>


/*-----------------------------------------------------------------*/

/*** Callbacks for 'svn diff', invoked by the repos-diff editor. ***/


struct diff_cmd_baton {
  const apr_array_header_t *options;
  apr_pool_t *pool;
  apr_file_t *outfile;
  apr_file_t *errfile;
};


/* The main workhorse, which invokes an external 'diff' program on the
   two temporary files.   The path is the "true" label to use in the
   diff output, and the revnums are ignored. */
static svn_error_t *
diff_file_changed (const char *path,
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
  const char *label = path;

  /* Execute local diff command on these two paths, print to stdout. */
  nargs = diff_cmd_baton->options->nelts;
  if (nargs)
    {
      int i;
      args = apr_palloc (subpool, nargs * sizeof (char *));
      for (i = 0; i < diff_cmd_baton->options->nelts; i++)
        {
          args[i] = 
            ((svn_stringbuf_t **)(diff_cmd_baton->options->elts))[i]->data;
        }
      assert (i == nargs);
    }

  /* Print out the diff header. */
  apr_file_printf (outfile, "Index: %s\n", label ? label : tmpfile1);
  apr_file_printf (outfile, 
     "===================================================================\n");

  SVN_ERR (svn_io_run_diff (".", args, nargs, path, 
                            tmpfile1, tmpfile2, 
                            &exitcode, outfile, errfile, subpool));

  /* ### todo: Handle exit code == 2 (i.e. errors with diff) here */
  
  /* ### todo: someday we'll need to worry about whether we're going
     to need to write a diff plug-in mechanism that makes use of the
     two paths, instead of just blindly running SVN_CLIENT_DIFF.  */

  /* Destroy the subpool. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}

/* The because the repos-diff editor passes at least one empty file to
   each of these next two functions, they can be dumb wrappers around
   the main workhorse routine. */
static svn_error_t *
diff_file_added (const char *path,
                 const char *tmpfile1,
                 const char *tmpfile2,
                 void *diff_baton)
{
  return diff_file_changed (path, tmpfile1, tmpfile2, 
                            SVN_INVALID_REVNUM, SVN_INVALID_REVNUM,
                            diff_baton);
}

static svn_error_t *
diff_file_deleted (const char *path,
                   const char *tmpfile1,
                   const char *tmpfile2,
                   void *diff_baton)
{
  return diff_file_changed (path, tmpfile1, tmpfile2, 
                            SVN_INVALID_REVNUM, SVN_INVALID_REVNUM,
                            diff_baton);
}

/* For now, let's have 'svn diff' send feedback to the top-level
   application, so that something reasonable about directories and
   propsets gets printed to stdout. */
static svn_error_t *
diff_dir_added (const char *path,
                void *diff_baton)
{
  /* ### todo:  send feedback to app */
  return SVN_NO_ERROR;
}

static svn_error_t *
diff_dir_deleted (const char *path,
                  void *diff_baton)
{
  /* ### todo:  send feedback to app */
  return SVN_NO_ERROR;
}
  
static svn_error_t *
diff_prop_changed (const char *path,
                   const char *name,
                   const svn_string_t *value,
                   void *diff_baton)
{
  /* ### todo:  send feedback to app */
  return SVN_NO_ERROR;
}

/* The main callback table for 'svn diff'.  */
static const svn_diff_callbacks_t 
diff_callbacks =
  {
    diff_file_changed,
    diff_file_added,
    diff_file_deleted,
    diff_dir_added,
    diff_dir_deleted,
    diff_prop_changed
  };


/*-----------------------------------------------------------------*/

/*** Callbacks for 'svn merge', invoked by the repos-diff editor. ***/


struct merge_cmd_baton {
  apr_pool_t *pool;
};


static svn_error_t *
merge_file_changed (const char *mine,
                    const char *older,
                    const char *yours,
                    svn_revnum_t older_rev,
                    svn_revnum_t yours_rev,
                    void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create (merge_b->pool);
  const char *target_label = ".working";
  const char *left_label = apr_psprintf (subpool, ".r%ld", older_rev);
  const char *right_label = apr_psprintf (subpool, ".r%ld", yours_rev);
  svn_error_t *err;

  /* This callback is essentially no more than a wrapper around
     svn_wc_merge().  Thank goodness that all the
     diff-editor-mechanisms are doing the hard work of getting the
     fulltexts! */

  err = svn_wc_merge (older, yours, mine,
                      left_label, right_label, target_label,
                      subpool);
  if (err && (err->apr_err != SVN_ERR_WC_CONFLICT))
    return err;  

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
merge_file_added (const char *mine,
                  const char *older,
                  const char *yours,
                  void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create (merge_b->pool);
  enum svn_node_kind kind;

  SVN_ERR (svn_io_check_path (mine, &kind, subpool));
  switch (kind)
    {
    case svn_node_none:
      SVN_ERR (svn_io_copy_file (yours, mine, TRUE, subpool));
      SVN_ERR (svn_client_add (svn_stringbuf_create (mine, subpool), 
                               FALSE, NULL, NULL, subpool));      
      break;
    case svn_node_dir:
      /* ### create a .drej conflict or something someday? */
      return svn_error_createf (SVN_ERR_WC_NOT_FILE, 0, NULL, subpool,
                                "Cannot create file '%s' for addition, "
                                "because a directory by that name "
                                "already exists.", mine);
    case svn_node_file:
      {
        /* file already exists, is it under version control? */
        svn_wc_entry_t *entry;
        SVN_ERR (svn_wc_entry (&entry, svn_stringbuf_create (mine, subpool),
                               subpool));
        if (entry)
          {
            if (entry->schedule == svn_wc_schedule_delete)
              {
                /* If already scheduled for deletion, then carry out
                   an add, which is really a (R)eplacement.  */
                SVN_ERR (svn_io_copy_file (yours, mine, TRUE, subpool));
                SVN_ERR (svn_client_add (svn_stringbuf_create (mine, subpool), 
                                         FALSE, NULL, NULL, subpool));      
              }
            else
              {
                svn_error_t *err;
                err = svn_wc_merge (older, yours, mine,
                                    ".older", ".yours", ".working", /* ###? */
                                    subpool);
                if (err && (err->apr_err != SVN_ERR_WC_CONFLICT))
                  return err;
              }
          }
        else
          return svn_error_createf (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, 
                                    subpool,
                                    "Cannot create file '%s' for addition, "
                                    "because an unversioned file by that name "
                                    "already exists.", mine);
        break;      
      }
    default:
      break;
    }

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
merge_file_deleted (const char *mine,
                    const char *older,
                    const char *yours,
                    void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create (merge_b->pool);
  enum svn_node_kind kind;

  SVN_ERR (svn_io_check_path (mine, &kind, subpool));
  switch (kind)
    {
    case svn_node_file:
      SVN_ERR (svn_client_delete (NULL, 
                                  svn_stringbuf_create (mine, subpool),
                                  FALSE, /* don't force */
                                  NULL, NULL, NULL, NULL, NULL, subpool));
      break;
    case svn_node_dir:
      /* ### create a .drej conflict or something someday? */
      return svn_error_createf (SVN_ERR_WC_NOT_FILE, 0, NULL, subpool,
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
merge_dir_added (const char *path,
                 void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create (merge_b->pool);
  svn_stringbuf_t *path_s = svn_stringbuf_create (path, subpool);
  enum svn_node_kind kind;
  svn_boolean_t is_wc;

  SVN_ERR (svn_io_check_path (path, &kind, subpool));
  switch (kind)
    {
    case svn_node_none:
      SVN_ERR (svn_client_mkdir (NULL, path_s, NULL, NULL, NULL,
                                 NULL, NULL, subpool));
      SVN_ERR (svn_client_add (path_s, FALSE, NULL, NULL, subpool));
      break;
    case svn_node_dir:
      /* dir already exists.  make sure it's under version control. */
      SVN_ERR (svn_wc_check_wc (path_s, &is_wc, subpool));
      if (! is_wc)
        SVN_ERR (svn_client_add (path_s, FALSE, NULL, NULL, subpool));        
      break;
    case svn_node_file:
      /* ### create a .drej conflict or something someday? */
      return svn_error_createf (SVN_ERR_WC_NOT_DIRECTORY, 0, NULL, subpool,
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
merge_dir_deleted (const char *path,
                   void *baton)
{
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create (merge_b->pool);
  enum svn_node_kind kind;
  
  SVN_ERR (svn_io_check_path (path, &kind, subpool));
  switch (kind)
    {
    case svn_node_dir:
      SVN_ERR (svn_client_delete (NULL, 
                                  svn_stringbuf_create (path, subpool),
                                  FALSE, /* don't force */
                                  NULL, NULL, NULL, NULL, NULL, subpool));
      break;
    case svn_node_file:
      /* ### create a .drej conflict or something someday? */
      return svn_error_createf (SVN_ERR_WC_NOT_DIRECTORY, 0, NULL, subpool,
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
merge_prop_changed (const char *path,
                    const char *name,
                    const svn_string_t *value,
                    void *baton)
{
  int len;
  struct merge_cmd_baton *merge_b = baton;
  apr_pool_t *subpool = svn_pool_create (merge_b->pool);

  enum svn_prop_kind kind = svn_property_kind (&len, name);

  if (kind == svn_prop_regular_kind)
    SVN_ERR (svn_client_propset (name, value, path, FALSE, subpool));
  
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
  return SVN_NO_ERROR;
}

/* The main callback table for 'svn merge'.  */
static const svn_diff_callbacks_t 
merge_callbacks =
  {
    merge_file_changed,
    merge_file_added,
    merge_file_deleted,
    merge_dir_added,
    merge_dir_deleted,
    merge_prop_changed
  };


/*-----------------------------------------------------------------------*/

/** The logic behind 'svn diff' and 'svn merge'.  */


/* Hi!  I'm a soon-to-be-out-of-date comment regarding a
   soon-to-be-rewritten diff_or_merge() function!

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



/* PATH1, PATH2, and TARGET_WCPATH all better be directories.   For
   the single file case, the caller do the merging manually. */
static svn_error_t *
do_merge (const svn_delta_editor_t *after_editor,
          void *after_edit_baton,
          svn_client_auth_baton_t *auth_baton,
          svn_stringbuf_t *path1,
          const svn_client_revision_t *revision1,
          svn_stringbuf_t *path2,
          const svn_client_revision_t *revision2,
          svn_stringbuf_t *target_wcpath,
          svn_boolean_t recurse,
          const svn_diff_callbacks_t *callbacks,
          void *callback_baton,
          apr_pool_t *pool)
{
  svn_revnum_t start_revnum, end_revnum;
  svn_string_t path_str1, path_str2;
  svn_boolean_t path1_is_url, path2_is_url;
  svn_wc_entry_t *entry1, *entry2;
  svn_stringbuf_t *URL1, *URL2;
  void *ra_baton, *session, *session2;
  svn_ra_plugin_t *ra_lib;
  const svn_ra_reporter_t *reporter;
  void *report_baton;
  const svn_delta_edit_fns_t *diff_editor;
  const svn_delta_editor_t *composed_editor, *new_diff_editor;
  void *diff_edit_baton, *composed_edit_baton, *new_diff_edit_baton;

  /* Sanity check -- ensure that we have valid revisions to look at. */
  if ((revision1->kind == svn_client_revision_unspecified)
      || (revision2->kind == svn_client_revision_unspecified))
    {
      return svn_error_create
        (SVN_ERR_CLIENT_BAD_REVISION, 0, NULL, pool,
         "do_merge: caller failed to specify all revisions");
    }

  /* Make sure we have two URLs ready to go.*/
  URL1 = path1;
  path_str1.data = path1->data;
  path_str1.len = path1->len;
  if (! ((path1_is_url = svn_path_is_url (&path_str1))))
    {
      SVN_ERR (svn_wc_entry (&entry1, path1, pool));
      if (entry1)
        URL1 = svn_stringbuf_create (entry1->url->data, pool);
      else
        return svn_error_createf 
          (SVN_ERR_CLIENT_VERSIONED_PATH_REQUIRED, 0, NULL, pool,
           "do_merge: %s is not a versioned entity", path1->data);
    }

  URL2 = path2;
  path_str2.data = path2->data;
  path_str2.len = path2->len;
  if (! ((path2_is_url = svn_path_is_url (&path_str2))))
    {
      SVN_ERR (svn_wc_entry (&entry2, path2, pool));
      if (entry2)
        URL2 = svn_stringbuf_create (entry2->url->data, pool);
      else
        return svn_error_createf
          (SVN_ERR_CLIENT_VERSIONED_PATH_REQUIRED, 0, NULL, pool,
           "do_merge: %s is not a versioned entity", path2->data);
    }

  /* Establish first RA session to URL1. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL1->data, pool));
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL1, NULL,
                                        NULL, FALSE, FALSE, TRUE, 
                                        auth_baton, pool));
  /* Resolve the revision numbers. */
  SVN_ERR (svn_client__get_revision_number
           (&start_revnum, ra_lib, session, revision1, path1->data, pool));
  SVN_ERR (svn_client__get_revision_number
           (&end_revnum, ra_lib, session, revision2, path2->data, pool));

  /* Open a second session used to request individual file
     contents. Although a session can be used for multiple requests, it
     appears that they must be sequential. Since the first request, for
     the diff, is still being processed the first session cannot be
     reused. This applies to ra_dav, ra_local does not appears to have
     this limitation. */
  SVN_ERR (svn_client__open_ra_session (&session2, ra_lib, URL1, NULL,
                                        NULL, FALSE, FALSE, TRUE,
                                        auth_baton, pool));
  
  SVN_ERR (svn_client__get_diff_editor (target_wcpath,
                                        callbacks,
                                        callback_baton,
                                        recurse,
                                        ra_lib, session2,
                                        start_revnum,
                                        &new_diff_editor,
                                        &new_diff_edit_baton,
                                        pool));

  if (after_editor)
    {
      svn_delta_compose_editors (&composed_editor, &composed_edit_baton,
                                 new_diff_editor, new_diff_edit_baton,
                                 after_editor, after_edit_baton, pool);
    }
  else
    {
      composed_editor = new_diff_editor;
      composed_edit_baton = new_diff_edit_baton;
    }
  
  /* ### Make composed editor look "old" style.  Remove someday. */
  svn_delta_compat_wrap (&diff_editor, &diff_edit_baton,
                         composed_editor, composed_edit_baton, pool);
  
  SVN_ERR (ra_lib->do_switch (session,
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


static svn_error_t *
do_diff (const apr_array_header_t *options,
         svn_client_auth_baton_t *auth_baton,
         svn_stringbuf_t *path1,
         const svn_client_revision_t *revision1,
         svn_stringbuf_t *path2,
         const svn_client_revision_t *revision2,
         svn_boolean_t recurse,
         const svn_diff_callbacks_t *callbacks,
         void *callback_baton,
         apr_pool_t *pool)
{
  svn_revnum_t start_revnum, end_revnum;
  svn_string_t path_str;
  svn_boolean_t path_is_url;
  svn_stringbuf_t *anchor = NULL, *target = NULL;
  svn_wc_entry_t *entry;
  svn_stringbuf_t *URL = path1;
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  const svn_ra_reporter_t *reporter;
  void *report_baton;
  const svn_delta_edit_fns_t *diff_editor;
  void *diff_edit_baton;

  /* Return an error if PATH1 and PATH2 aren't the same (for now). */
  if (! svn_stringbuf_compare (path1, path2))
    return svn_error_createf (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
                              "Multi-path diff is currently unsupprted");

  /* Sanity check -- ensure that we have valid revisions to look at. */
  if ((revision1->kind == svn_client_revision_unspecified)
      || (revision2->kind == svn_client_revision_unspecified))
    {
      return svn_error_create
        (SVN_ERR_CLIENT_BAD_REVISION, 0, NULL, pool,
         "do_diff: caller failed to specify any revisions");
    }

  /* Determine if the target we have been given is a path or an URL.
     If it is a working copy path, we'll need to extract the URL from
     the entry for that path. */
  path_str.data = path1->data;
  path_str.len = path1->len;
  if (! ((path_is_url = svn_path_is_url (&path_str))))
    {
      SVN_ERR (svn_wc_get_actual_target (path1, &anchor, &target, pool));
      SVN_ERR (svn_wc_entry (&entry, anchor, pool));
      URL = svn_stringbuf_create (entry->url->data, pool);
    }

  /* If we are diffing a working copy path, simply use this 'quick'
     diff that does not contact the repository and simply uses the
     text base. */
  if ((! path_is_url)
      && ((revision1->kind == svn_client_revision_committed)
          || (revision1->kind == svn_client_revision_base))
      && (revision2->kind == svn_client_revision_working))
    {
      return svn_wc_diff (anchor, target, callbacks, callback_baton,
                          recurse, pool);
    }

  /* Else we must contact the repository. */

  /* Establish RA session */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL->data, pool));

  /* ### TODO: We have to pass null for the base_dir here, since the
     working copy does not match the requested revision.  It might be
     possible to have a special ra session for diff, where get_wc_prop
     cooperates with the editor and returns values when the file is in the
     wc, and null otherwise. */
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL, NULL,
                                        NULL, FALSE, FALSE, TRUE, 
                                        auth_baton, pool));

  /* ### todo: later, when we take two (or N) targets and a more
     sophisticated indication of their interaction with start and end,
     these calls will need to be rearranged. */
  SVN_ERR (svn_client__get_revision_number
           (&start_revnum, ra_lib, session, revision1, path1->data, pool));
  SVN_ERR (svn_client__get_revision_number
           (&end_revnum, ra_lib, session, revision2, path1->data, pool));

  /* ### todo: For the moment, we'll maintain the two distinct diff
     editors, svn_wc vs svn_client, in order to get this change done
     in a finite amount of time.  Next the two editors will be unified
     into one "lazy" editor that uses local data whenever possible. */

  if (revision2->kind == svn_client_revision_working)
    {
      /* The working copy is involved in this case. */
      SVN_ERR (svn_wc_get_diff_editor (anchor, target,
                                       callbacks, callback_baton,
                                       recurse,
                                       &diff_editor, &diff_edit_baton,
                                       pool));

      SVN_ERR (ra_lib->do_update (session,
                                  &reporter, &report_baton,
                                  start_revnum,
                                  target,
                                  recurse,
                                  diff_editor, diff_edit_baton));

      SVN_ERR (svn_wc_crawl_revisions (path1, reporter, report_baton,
                                       FALSE, recurse,
                                       NULL, NULL, /* notification is N/A */
                                       pool));
    }
  else   /* ### todo: there may be uncovered cases remaining */
    {
      /* Pure repository comparison. */
      const svn_delta_editor_t *new_diff_editor;
      void *new_diff_edit_baton;

      /* Open a second session used to request individual file
         contents. Although a session can be used for multiple requests, it
         appears that they must be sequential. Since the first request, for
         the diff, is still being processed the first session cannot be
         reused. This applies to ra_dav, ra_local does not appears to have
         this limitation. */
      void *session2;

      /* ### TODO: Forcing the base_dir to null. It might be possible to
         use a special ra session that cooperates with the editor to enable
         get_wc_prop to return values when the file is in the wc */
      SVN_ERR (svn_client__open_ra_session (&session2, ra_lib, URL, NULL,
                                            NULL, FALSE, FALSE, TRUE,
                                            auth_baton, pool));

      /* Get the true diff editor. */
      SVN_ERR (svn_client__get_diff_editor (target,
                                            callbacks,
                                            callback_baton,
                                            recurse,
                                            ra_lib, session2,
                                            start_revnum,
                                            &new_diff_editor,
                                            &new_diff_edit_baton,
                                            pool));

      /* ### Make diff editor look "old" style.  Remove someday. */
      svn_delta_compat_wrap (&diff_editor, &diff_edit_baton,
                             new_diff_editor, new_diff_edit_baton, pool);

      SVN_ERR (ra_lib->do_update (session,
                                  &reporter, &report_baton,
                                  end_revnum,
                                  target,
                                  recurse,
                                  diff_editor, diff_edit_baton));

      SVN_ERR (reporter->set_path (report_baton, "", start_revnum));

      SVN_ERR (reporter->finish_report (report_baton));

      SVN_ERR (ra_lib->close (session2));
    }

  SVN_ERR (ra_lib->close (session));

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
                 svn_stringbuf_t *path1,
                 const svn_client_revision_t *revision1,
                 svn_stringbuf_t *path2,
                 const svn_client_revision_t *revision2,
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

  return do_diff (options,
                  auth_baton,
                  path1, revision1,
                  path2, revision2,
                  recurse,
                  &diff_callbacks, &diff_cmd_baton,
                  pool);
}


svn_error_t *
svn_client_merge (const svn_delta_editor_t *after_editor,
                  void *after_edit_baton,
                  svn_client_auth_baton_t *auth_baton,
                  svn_stringbuf_t *path1,
                  const svn_client_revision_t *revision1,
                  svn_stringbuf_t *path2,
                  const svn_client_revision_t *revision2,
                  svn_stringbuf_t *target_wcpath,
                  svn_boolean_t recurse,
                  apr_pool_t *pool)
{
  struct merge_cmd_baton merge_cmd_baton;

  merge_cmd_baton.pool = pool;
  
  /* ### TODO:  when PATH1 and PATH2 represent -files- instead of
     directories, we can fetch fulltexts manually and run
     svn_wc_merge() by itself.

     In PATH1 and PATH2 are directories, then we do the fancy
     diff-editor thing:*/

  return do_merge (after_editor, after_edit_baton,
                   auth_baton,
                   path1,
                   revision1,
                   path2,
                   revision2,
                   target_wcpath,
                   recurse,
                   &merge_callbacks,
                   &merge_cmd_baton,
                   pool);
}



/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */


