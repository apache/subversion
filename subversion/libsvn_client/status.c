/*
 * status.c:  return the status of a working copy dirent
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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



/*** Helper functions. ***/

/* Open an RA session to URL to retrieve the YOUNGEST revision.  If
   any errors occur, *YOUNGEST will NOT be assigned to.  Use PATH to
   find authentication information, and use POOL for any necessary
   allocations.  RA layer errors will be considered non-fatal, all
   others will be passed up the caller.  */
static svn_error_t *
get_youngest_from_ra (svn_revnum_t *youngest,
                      svn_stringbuf_t *path,
                      const char *URL,
                      apr_pool_t *pool)
{ 
  svn_error_t *err;
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  svn_revnum_t latest_revnum;
  svn_boolean_t close_session = FALSE, rev_known = FALSE;
  svn_client_auth_t *auth_obj;

  /* Get the RA vtable that matches URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  err = svn_ra_get_ra_library (&ra_lib, ra_baton, URL, pool);

  /* If we got a valid RA layer by looking up the URL, then proceed.
     If the URL was bogus, or just has no RA match, that's okay.
     We'll just not touch *YOUNGEST. */
  if (err)
    {
      if (err->apr_err != SVN_ERR_RA_ILLEGAL_URL)
        return err;
      else
        return SVN_NO_ERROR;
    }
 
  err = svn_client_authenticate (&session, ra_lib,
                                 svn_stringbuf_create (URL, pool),
                                 path, auth_obj, pool);
  if (! err)
    close_session = TRUE;

  if (! err)
    err = ra_lib->get_latest_revnum (session, &latest_revnum);

  if (! err)
    rev_known = TRUE;

  if (close_session)
    {
      ra_lib->close (session);
      if (auth_obj->storage_callback)
        SVN_ERR (auth_obj->storage_callback (auth_obj->storage_baton));
    }
  
  if (rev_known)
    *youngest = latest_revnum;

  return SVN_NO_ERROR;
}


/*** Public Interface. ***/

/* Given PATH to a working copy directory or file, allocate and return
   a STATUSHASH structure containing the stati of all entries.  If
   DESCEND is non-zero, recurse fully, else do only immediate
   children.  (See svn_wc.h:svn_wc_statuses() for more verbiage on
   this). */
svn_error_t *
svn_client_status (apr_hash_t **statushash,
                   svn_stringbuf_t *path,
                   svn_boolean_t descend,
                   svn_client_auth_t *auth_obj,
                   apr_pool_t *pool)
{
  svn_wc_entry_t *entry;
  svn_stringbuf_t *parent = NULL;
  svn_revnum_t latest_revnum = SVN_INVALID_REVNUM;
  apr_hash_t *hash = apr_hash_make (pool);
  apr_hash_index_t *hi;

  /* Ask the wc to give us a list of svn_wc_status_t structures. */
  SVN_ERR (svn_wc_statuses (hash, path, descend, pool));
  
  /* Each status structure in the hash now has all fields filled in
     *except* the repos_rev field, which is SVN_INVALID_REVNUM.
     Attempt to contact the repos and get the latest revnum. */

  /* Get PATH's entry from the working copy. */
  SVN_ERR (svn_wc_entry (&entry, path, pool));
  if (! entry)
    return svn_error_createf 
      (SVN_ERR_WC_ENTRY_NOT_FOUND, 0, NULL, pool,
       "svn_client_status: '%s' is not a versioned resource", 
       path->data);

  /* If the entry is not a directory, we'll be looking at its parent
     for authentication stuffs. */
  if (entry->kind != svn_node_dir)
    {
      parent = svn_stringbuf_dup (path, pool);
      svn_path_remove_component (parent, svn_path_local_style);
    }

  /* If the entry has no ancestry, get the ancestry from its parent. */
  if ((! entry->ancestor) && (parent))
    {
      SVN_ERR (svn_wc_entry (&entry, parent, pool));
      if (! entry)
        return svn_error_createf 
          (SVN_ERR_WC_ENTRY_NOT_FOUND, 0, NULL, pool,
           "svn_client_status: parent '%s' is not a versioned resource", 
           parent->data);
    }

  /* If we have a valid URL, open an RA session to it, get latest
     revnum, close session. Don't throw network errors; just treat
     them as non-fatal. */
  if (entry->ancestor)
    SVN_ERR (get_youngest_from_ra (&latest_revnum,
                                   parent ? parent : path,
                                   entry->ancestor->data,
                                   pool));

  /* Write the latest revnum into each status structure. */
  for (hi = apr_hash_first (pool, hash); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      apr_size_t klen;
      svn_wc_status_t *status;
      
      apr_hash_this (hi, &key, &klen, &val);
      status = (svn_wc_status_t *) val;
      status->repos_rev = latest_revnum;
    }

  *statushash = hash;

  return SVN_NO_ERROR;
}









/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
