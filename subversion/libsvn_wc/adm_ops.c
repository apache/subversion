/*
 * adm_ops.c: routines for affecting working copy administrative
 *            information.  NOTE: this code doesn't know where the adm
 *            info is actually stored.  Instead, generic handles to
 *            adm data are requested via a reference to some PATH
 *            (PATH being a regular, non-administrative directory or
 *            file in the working copy).
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include <apr_time.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_wc.h"
#include "wc.h"



/*** adm area guarantees ***/

/* Make sure that PATH (a directory) contains a complete adm area,
 * based at REPOSITORY.
 *
 * Creates the adm area if none, in which case PATH starts out at
 * revision 0.
 *
 * Note: The adm area's lock-state is not changed by this function,
 * and if the adm area is created, it is left in an unlocked state.
 */
svn_error_t *
svn_wc__ensure_wc (svn_string_t *path,
                   svn_string_t *ancestor_path,
                   svn_revnum_t ancestor_revision,
                   apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_wc__ensure_adm (path,
                            ancestor_path,
                            ancestor_revision,
                            pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



/*** Closing commits. ***/

svn_error_t *
svn_wc_set_revision (void *baton,
                     svn_string_t *target,
                     svn_revnum_t new_revnum)
{
  svn_error_t *err;
  apr_status_t apr_err;
  svn_string_t *log_parent, *logtag, *basename;
  apr_file_t *log_fp = NULL;
  struct svn_wc_close_commit_baton *bumper =
    (struct svn_wc_close_commit_baton *) baton;
  apr_pool_t *pool = bumper->pool;  /* cute, eh? */
  char *revstr = apr_psprintf (pool, "%ld", new_revnum);

  /* Construct the -full- path */
  svn_string_t *path = svn_string_dup (bumper->prefix_path, pool);
  svn_path_add_component (path, target, svn_path_local_style);

  /* Write a log file in the adm dir of path. */

  /* (First, try to write a logfile directly in PATH.) */
  log_parent = path;
  basename = svn_string_create (SVN_WC_ENTRY_THIS_DIR, pool);
  err = svn_wc__open_adm_file (&log_fp, log_parent, SVN_WC__ADM_LOG,
                               (APR_WRITE | APR_APPEND | APR_CREATE),
                               pool);
  if (err)
    {
      /* (Ah, PATH must be a file.  So create a logfile in its
         parent instead.) */      
      svn_path_split (path, &log_parent, &basename,
                      svn_path_local_style, pool);
      SVN_ERR (svn_wc__open_adm_file (&log_fp, log_parent, SVN_WC__ADM_LOG,
                                      (APR_WRITE|APR_APPEND|APR_CREATE),
                                      pool));
    }
  
  logtag = svn_string_create ("", pool);
  svn_xml_make_open_tag (&logtag, pool, svn_xml_self_closing,
                         SVN_WC__LOG_COMMITTED,
                         SVN_WC__LOG_ATTR_NAME, basename,
                         SVN_WC__LOG_ATTR_REVISION, 
                         svn_string_create (revstr, pool),
                         NULL);
      
  apr_err = apr_file_write_full (log_fp, logtag->data, logtag->len, NULL);
  if (apr_err)
    {
      apr_file_close (log_fp);
      return svn_error_createf (apr_err, 0, NULL, pool,
                                "svn_wc_set_revision: "
                                "error writing %s's log file", 
                                path->data);
    }
      
  SVN_ERR (svn_wc__close_adm_file (log_fp, log_parent, SVN_WC__ADM_LOG,
                                   TRUE, /* sync */
                                   pool));


  /* Run the log file we just created. */
  SVN_ERR (svn_wc__run_log (log_parent, pool));
            
  /* The client's commit routine will take care of removing all
     locks en masse. */

  return SVN_NO_ERROR;
}




/* kff todo: not all of these really belong in wc_adm.  Some may get
   broken out into other files later.  They're just here to satisfy
   the public header file that they exist. */

svn_error_t *
svn_wc_rename (svn_string_t *src, svn_string_t *dst, apr_pool_t *pool)
{
  /* kff todo */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_copy (svn_string_t *src, svn_string_t *dst, apr_pool_t *pool)
{
  /* kff todo */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_delete_file (svn_string_t *file, apr_pool_t *pool)
{
  svn_string_t *dir, *basename;

  svn_path_split (file, &dir, &basename, svn_path_local_style, pool);

  SVN_ERR (svn_wc__entry_fold_sync_intelligently 
           (dir, basename, SVN_INVALID_REVNUM, svn_node_file,
            SVN_WC_ENTRY_DELETED, 0, 0, pool, NULL, NULL));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_add_directory (svn_string_t *dir, apr_pool_t *pool)
{
  svn_string_t *parent_dir, *basename;
  svn_wc_entry_t *entry;
  svn_string_t *ancestor_path;

  svn_path_split (dir, &parent_dir, &basename, svn_path_local_style, pool);

  /* Get the entry for this directory's parent.  We need to snatch the
     ancestor path out of there. */
  if (svn_path_is_empty (parent_dir, svn_path_local_style))
    parent_dir = svn_string_create (".", pool);
  SVN_ERR (svn_wc_entry (&entry, parent_dir, pool));
  
  /* Derive the ancestor path for our new addition here. */
  ancestor_path = svn_string_dup (entry->ancestor, pool);
  svn_path_add_component (ancestor_path, basename, svn_path_repos_style);
  
  /* Make sure this new directory has an admistrative subdirectory
     created inside of it */
  SVN_ERR (svn_wc__ensure_adm (dir, ancestor_path, 0, pool));

  /* And finally, add the entry for this directory to the parent_dir's
     entries file, marking it for addition. */
  SVN_ERR (svn_wc__entry_fold_sync_intelligently 
           (parent_dir, basename, 0, svn_node_dir, SVN_WC_ENTRY_ADDED,
            0, 0, pool, NULL, NULL));


  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_add_file (svn_string_t *file, apr_pool_t *pool)
{
  svn_string_t *dir, *basename;

  svn_path_split (file, &dir, &basename, svn_path_local_style, pool);

  SVN_ERR (svn_wc__entry_fold_sync_intelligently 
           (dir, basename, 0, svn_node_file, SVN_WC_ENTRY_ADDED,
            0, 0, pool, NULL, NULL));

  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc_get_pristine_copy_path (svn_string_t *path,
                               svn_string_t **pristine_path,
                               apr_pool_t *pool)
{
  *pristine_path = svn_wc__text_base_path (path, FALSE, pool);
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
