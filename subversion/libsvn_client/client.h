/*
 * client.h :  shared stuff internal to the client library.
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
#include <apr_dso.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_wc.h"
#include "svn_ra.h"

/* ---------------------------------------------------------------- */

/*** RA library stuff ***/

/* "What's RA stuff doing in the private header of libsvn_client?"

   Remember that there's no such thing as "libsvn_ra"; just specific
   implementations like libsvn_ra_dav, libsvn_ra_local, etc.  The
   public header `svn_ra.h' defines the vtable that any RA library
   needs to implement.

   Meanwhile, libsvn_client has internal methods of loading and
   storing these vtables.  That's what this section is about.  */



/* A logic table that maps repository URL types to the name of the
   specific RA library that handles it.  Add new RA implentations
   here. */
const char *svn_client__ra_library_table [][2] =
{
  {"http",  "dav"   },  /* libsvn_ra_dav */
  {"file",  "local" }   /* libsvn_ra_local */
};


/* Structure representing a loaded RA library. */
typedef struct svn_client__ra_library_t
{
  const svn_ra_plugin_t *plugin;  /* the library's "vtable" */
  apr_dso_handle_t *dso;          /* the whole library */

} svn_client__ra_library_t;



/* Return a loaded RA library which can handle URL, alloc'd from
   POOL. */
svn_error_t *
svn_client_get_ra_library (const svn_client__ra_library_t **library,
                           const char *URL,
                           apr_pool_t *pool);


/* ---------------------------------------------------------------- */

/*** Checkout and update ***/

svn_error_t *
svn_client__checkout_internal (const svn_delta_edit_fns_t *before_editor,
                               void *before_edit_baton,
                               const svn_delta_edit_fns_t *after_editor,
                               void *after_edit_baton,
                               svn_string_t *path,
                               svn_string_t *xml_src,
                               svn_string_t *ancestor_path,
                               svn_revnum_t ancestor_revision,
                               apr_pool_t *pool);


svn_error_t *
svn_client__update_internal (const svn_delta_edit_fns_t *before_editor,
                             void *before_edit_baton,
                             const svn_delta_edit_fns_t *after_editor,
                             void *after_edit_baton,
                             svn_string_t *path,
                             svn_string_t *xml_src,
                             svn_revnum_t ancestor_revision,
                             apr_pool_t *pool);



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

