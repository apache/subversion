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

#include <apr_strings.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include "svn_ra.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_io.h"


/* ---------------------------------------------------------------*/

/*** Structures ***/

/* A const table that maps repository URL types to the name of the
   specific RA library that handles it.  Add new RA implentations
   here. */
static const svn_ra__library_table [][] =
{
  {"http",  "ra_dav"   },
  {"file",  "ra_local" }
};


/* Structure representing a loaded RA library. */
typedef struct svn_ra__library_t
{
  const svn_ra_plugin_t *plugin;  /* the library's "vtable" */
  apr_dso_handle_t *dso;          /* handle on the actual library loaded */

} svn_ra__library_t;



/* A global hash which represents all RA implementations that are
   currently loaded and re-usable. 

   The hash maps (const char *name) --> (svn_ra_library_t *library)

   To make this threadsafe, this hash may not be edited without first
   locking it down. */
apr_hash_t *svn_ra__loaded_libraries = NULL;



/* -------------------------------------------------------------- */

/*** Public Interface ***/

/* Return a loaded RA implementation which can handle URL.

   If the library is already loaded, return it in LIBRARY.

   If the library is not yet loaded, alloc and load it (using POOL),
   then return it.  */
const svn_ra_library_t *
svn_ra_get_ra_library (const svn_ra_library_t **library,
                       const char *URL,
                       apr_pool_t *pool)
{
  const char *library_name;
  const svn_ra_library_t *the_library;

  /* Figure out which library should handle URL */
  match the beginning of the URL to svn_ra__library_table[i][0];
  library_name = svn_ra__library_table[n][1];



  if (svn_ra__loaded_libraries == NULL)
    {
      /* try to lock the hash, repeat until successful; */
      /* if it's still null, apr_make_hash (pool). */
      /* unlock the hash */
    }

  the_library = apr_hash_get (library_name);

  if (the_library == NULL)
    {
      /* try to lock the hash, repeat until successful */
      /* do another apr_hash_get;  if still NULL... */
      /* 1. allocate new library object */
      /* 2. apr_dso_load (library_name) */
      /* 3. apr_dso_sym (svn_ra_FOO_init) */
      /* 4. call svn_ra_FOO_init, get back a const plugin, add to
         library. */
      /* 5. apr_hash_set (library_name, the_library) */
      /* unlock hash */
    }
  
  *library = the_library;

  return SVN_NO_ERROR;
}


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */

