/*
 * ra_loader.c:  logic for loading different RA library implementations
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

/* ==================================================================== */

/*** Includes. ***/

#include <string.h>
#include <apr_strings.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_io.h"
#include "client.h"




/* -------------------------------------------------------------- */

/*** Public Interfaces ***/

/* ADD NEW RA IMPLEMENTATIONS HERE as they're written!!  */

svn_error_t *
svn_client_init_ra_libs (void **ra_baton,
                         apr_pool_t *pool)
{
  svn_error_t *err;

  const char *url_type;
  const svn_ra_plugin_t *ra_plugin;
  
  /* A logic table that maps repository URL types (key) to the
     ra_plugin vtable (val) that handles it.  */
  apr_hash_t *ra_library_hash = apr_hash_make (pool);
 
  /* Fetch *all* RA vtables. */
  err = svn_ra_dav_init (0, pool, &url_type, &ra_plugin);
  if (err) return err;
  apr_hash_set (ra_library_hash, url_type, strlen(url_type), ra_plugin);

  err = svn_ra_local_init (0, pool, &url_type, &ra_plugin);
  if (err) return err;
  apr_hash_set (ra_library_hash, url_type, strlen(url_type), ra_plugin);

  /* Return the (opaque) list. */
  *ra_baton = ra_library_hash;
  return SVN_NO_ERROR;
}



svn_error_t *
svn_client_get_ra_library (svn_ra_plugin_t **library,
                           void *ra_baton,
                           const char *URL,
                           apr_pool_t *pool)
{
  apr_hash_index_t *this;
  apr_hash_t *hash = (apr_hash_t *) ra_baton;
  
  /* Figure out which RA library key matches URL */
  for (this = apr_hash_first (hash); this; this = apr_hash_next (this))
    {
      const void *key;
      void *val;
      size_t keylen;
      char *keystr;

      /* Get key and val. */
      apr_hash_this (this, &key, &keylen, &val);
      keystr = (char *) key;
      
      if (! strncmp (keystr, URL, keylen))
        {
          *library = (svn_ra_plugin_t *) val;          
          return SVN_NO_ERROR; 
        }
    }
    
  /* Couldn't find a match... */
  *library = NULL;
  return svn_error_createf (SVN_ERR_RA_ILLEGAL_URL, 0, NULL, pool,
                            "Unrecognized URL type: %s", (char *) URL);

}


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */

