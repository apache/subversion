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

#include <apr.h>
#include <apr_strings.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_dso.h>

#include "svn_error.h"
#include "svn_io.h"
#include "svn_ra.h"
#include "svn_private_config.h"


/* ### this file maps URL schemes to particular RA libraries. This is not
   ### entirely correct, as a single scheme could potentially be served
   ### by more than one loader. However, we can ignore that until we
   ### actually run into a conflict within the scheme portion of a URL. */

/* ### if we want to lazy-load the RA libraries, then we will need to
   ### know the RA libraries' URL schemes *before* loading them. the
   ### current implementation just loads everything, and asks the libraries
   ### what they handle. */



static const struct ra_lib_defn {
  /* the name of this RA library (e.g. "dav" or "local") */
  const char *ra_name;

  /* the initialization function if linked in; otherwise, NULL */
  svn_ra_init_func_t initfunc;

} ra_libraries[] = {
  {
    "dav",
#ifdef SVN_LIBSVN_CLIENT_LINKS_RA_DAV
    svn_ra_dav_init
#endif
  },

  {
    "local",
#ifdef SVN_LIBSVN_CLIENT_LINKS_RA_LOCAL
    svn_ra_local_init
#endif
  },

  /* ADD NEW RA IMPLEMENTATIONS HERE (as they're written) */

  /* sentinel */
  { NULL }
};


static svn_error_t *
load_ra_module (svn_ra_init_func_t *func,
                const char *ra_name, apr_pool_t *pool)
{
  *func = NULL;

#if APR_HAS_DSO
  {
    apr_dso_handle_t *dso;
    apr_dso_handle_sym_t symbol;
    const char *libname;
    const char *funcname;
    apr_status_t status;

    /* ### fix the .so part */
    libname = apr_psprintf (pool, "libsvn_ra_%s.so", ra_name);
    funcname = apr_psprintf (pool, "svn_ra_%s_init", ra_name);

    /* find/load the specified library */
    status = apr_dso_load (&dso, libname, pool);
    if (status)
      {
        /* Just ignore the error. Assume the library isn't present */
        return SVN_NO_ERROR;
      }
    /* note: the library will be unloaded at pool cleanup */

    /* find the initialization routine */
    status = apr_dso_sym (&symbol, dso, funcname);
    if (status)
      {
        return svn_error_createf (status, 0, NULL, pool,
                                  "%s does not define %s()",
                                  libname, funcname);
      }

    *func = (svn_ra_init_func_t) symbol;
  }
#endif /* APR_HAS_DSO */

  return SVN_NO_ERROR;
}


/* -------------------------------------------------------------- */

/*** Public Interfaces ***/

svn_error_t *
svn_ra_init_ra_libs (void **ra_baton,
                     apr_pool_t *pool)
{
  const struct ra_lib_defn *defn;
  apr_hash_t *ra_library_hash;

  /* Our baton is a hash table that maps repository URL schemes to the
     ra_plugin vtable that will handle it. */
  ra_library_hash = apr_hash_make (pool);

  for (defn = ra_libraries; defn->ra_name != NULL; ++defn)
    {
      svn_ra_init_func_t initfunc = defn->initfunc;

      if (initfunc == NULL)
        {
          /* see if we can find a dynload module */
          SVN_ERR( load_ra_module (&initfunc, defn->ra_name, pool) );
        }

      if (initfunc != NULL)
        {
          /* linked in or successfully dynloaded */

          const char *url_scheme;
          const svn_ra_plugin_t *ra_plugin;

          SVN_ERR( (*initfunc)(SVN_RA_ABI_VERSION,
                               pool, &url_scheme, &ra_plugin) );

          apr_hash_set (ra_library_hash,
                        url_scheme, APR_HASH_KEY_STRING, ra_plugin);
        }
    }

  /* Return the (opaque) list. */
  *ra_baton = ra_library_hash;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_ra_get_ra_library (svn_ra_plugin_t **library,
                       void *ra_baton,
                       const char *URL,
                       apr_pool_t *pool)
{
  apr_hash_index_t *this;
  apr_hash_t *hash = ra_baton;
  
  /* Figure out which RA library key matches URL */
  for (this = apr_hash_first (hash); this; this = apr_hash_next (this))
    {
      const void *key;
      void *val;
      size_t keylen;
      const char *keystr;

      /* Get key and val. */
      apr_hash_this (this, &key, &keylen, &val);
      keystr = (const char *) key;

      /* case-sensitive scheme comparison */
      if (memcmp (keystr, URL, keylen) == 0 && URL[keylen] == ':')
        {
          *library = (svn_ra_plugin_t *) val;          
          return SVN_NO_ERROR; 
        }
    }
    
  /* Couldn't find a match... */
  *library = NULL;
  return svn_error_createf (SVN_ERR_RA_ILLEGAL_URL, 0, NULL, pool,
                            "Unrecognized URL scheme: %s", URL);
}


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */

