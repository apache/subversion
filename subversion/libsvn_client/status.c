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
                   svn_client_auth_info_callback_t callback,
                   void *callback_baton,
                   apr_pool_t *pool)
{
  svn_error_t *err;
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  svn_wc_entry_t *entry;
  const char *URL;
  apr_hash_t *hash = apr_hash_make (pool);

  /* Ask the wc to give us a list of svn_wc_status_t structures. */
  err = svn_wc_statuses (hash, path, descend, pool);
  if (err) return err;
  
  /* Each status structure in the hash now has all fields filled in
   *except* the repos_rev field, which is SVN_INVALID_REVNUM.
   
   Attempt to contact the repos and get the latest revnum. */

  /* Get a URL out of the working copy. */
  SVN_ERR (svn_wc_entry (&entry, path, pool));

  if (entry)
    {
      URL = entry->ancestor->data;

      /* Get the RA vtable that matches URL. */
      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      err = svn_ra_get_ra_library (&ra_lib, ra_baton, URL, pool);

      /* If we got a valid RA layer by looking up the URL, then proceed.
         If the URL was bogus, or just has no RA match, that's okay.
         Leave the repository revnum fields invalid.  (Perhaps the wc came
         from xml or something.) */
      if (err && (err->apr_err != SVN_ERR_RA_ILLEGAL_URL))
        return err;

      if (! err)
        {
          apr_hash_index_t *hi;
          svn_revnum_t latest_revnum;
          void *storage_baton;
          svn_client_auth_storage_callback_t storage_callback;

          /* Open an RA session to URL, get latest revnum, close session. */
          SVN_ERR (svn_client_authenticate (&session, 
                                            &storage_callback, &storage_baton,
                                            ra_lib,
                                            svn_stringbuf_create (URL, pool),
                                            path,
                                            callback, callback_baton, pool));
          SVN_ERR (ra_lib->get_latest_revnum (session, &latest_revnum));
          SVN_ERR (ra_lib->close (session));
          if (storage_callback)
            SVN_ERR (storage_callback (storage_baton));

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
        }
    }

  *statushash = hash;

  return SVN_NO_ERROR;
}









/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
