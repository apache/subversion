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


/* Return a loaded RA library which can handle URL, alloc'd from
   POOL. */
svn_error_t *
svn_client_get_ra_library (const svn_client__ra_library_t **library,
                           const char *URL,
                           apr_pool_t *pool)
{
  const char *library_name, *initfunc_name;
  svn_client__ra_library_t *the_library; 
  apr_dso_handle_t *dso;
  apr_dso_handle_sym_t symbol;  /* ick, the pointer is in the type! */
  svn_ra_init_func_t *initfunc;
  apr_status_t status;
  svn_error_t *err;
  int i;

  /* Figure out which RA library suffix should handle URL */
  for (i = 0; i < sizeof(svn_client__ra_library_table); i++)
    {
      const char *url_type = svn_client__ra_library_table[i][0];
      
      if (! strncmp (url_type, URL, sizeof(url_type)))
        break;
    }
  
  if (i == sizeof(svn_client__ra_library_table))
    return 
      svn_error_createf (SVN_ERR_RA_ILLEGAL_URL, 0, NULL, pool,
                         "can't find RA library to handle URL `%s'", URL);

  /* Construct DSO and initfunc names from RA suffix */
  /* TODO:  uh-oh;  is `.so' portable?  Don't think so. */
  library_name = apr_psprintf (pool, "libsvn_ra_%s.so",
                               svn_client__ra_library_table[i][1]);
    
  initfunc_name = apr_psprintf (pool, "svn_ra_%s_init",
                                svn_client__ra_library_table[i][1]);

  the_library = apr_pcalloc (pool, sizeof(*the_library));

  /* Load the library */
  status = apr_dso_load (&dso, library_name, pool);
  if (status)
    return svn_error_createf (status, 0, NULL, pool,
                              "Can't load `%s'", library_name);
  the_library->dso = dso;

  /* Find its init routine */
  status = apr_dso_sym (&symbol, dso, initfunc_name);
  if (status)
    return svn_error_createf (status, 0, NULL, pool,
                              "Can't locate `%s'", initfunc_name);

  /* Call its init routine, get the const `plugin' back */
  initfunc = (svn_ra_init_func_t *) symbol;
  err = initfunc (1, /* abi_version (do we still need this?) */
                  pool,
                  &(the_library->plugin));
  if (err) 
     return err;
    
  *library = the_library;

  return SVN_NO_ERROR;
}


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */

