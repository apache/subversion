/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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
 * @endcopyright
 *
 * @file svn_wc_private.h
 * @brief The Subversion Working Copy Library - Internal routines
 *
 * Requires:  
 *            - A working copy
 * 
 * Provides: 
 *            - Ability to manipulate working copy's versioned data.
 *            - Ability to manipulate working copy's administrative files.
 *
 * Used By:   
 *            - Clients.
 */

#ifndef SVN_WC_PRIVATE_H
#define SVN_WC_PRIVATE_H

#include "svn_wc.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Internal function used by the svn_wc_entry_versioned() macro.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_wc__entry_versioned_internal(const svn_wc_entry_t **entry,
                                 const char *path,
                                 svn_wc_adm_access_t *adm_access,
                                 svn_boolean_t show_hidden,
                                 const char *caller_filename,
                                 int caller_lineno,
                                 apr_pool_t *pool);


/** Same as svn_wc_entry() except that the entry returned
 * is a non @c NULL entry.
 *
 * Returns an error when svn_wc_entry() would have returned a @c NULL entry.
 *
 * @since New in 1.5.
 */

#ifdef SVN_DEBUG
#define svn_wc__entry_versioned(entry, path, adm_access, show_hidden, pool) \
  svn_wc__entry_versioned_internal((entry), (path), (adm_access), \
                                   (show_hidden), __FILE__, __LINE__, (pool))
#else
#define svn_wc__entry_versioned(entry, path, adm_access, show_hidden, pool) \
  svn_wc__entry_versioned_internal((entry), (path), (adm_access), \
                                   (show_hidden), NULL, 0, (pool))
#endif

/** If @a path's properties are modified with regard to the base revision set
 * @a *which_props to a hashtable
 * (<tt>const char *name</tt> -> <tt>const svn_string_t *value</tt>)
 * that contains only the modified properties or an empty hash if there are
 * no modifications.  @a adm_access must be an access baton for @a path.
 * @a *which_props is allocated in @a pool.
 *
 * @since New in 1.5.
 */
svn_error_t *svn_wc__props_modified(const char *path,
                                    apr_hash_t **which_props,
                                    svn_wc_adm_access_t *adm_access,
                                    apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_WC_PRIVATE_H */
