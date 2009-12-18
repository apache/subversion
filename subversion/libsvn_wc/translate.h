/*
 * translate.h :  eol and keyword translation
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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


#ifndef SVN_LIBSVN_WC_TRANSLATE_H
#define SVN_LIBSVN_WC_TRANSLATE_H

#include <apr_pools.h>
#include "svn_types.h"
#include "svn_subst.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Newline and keyword translation properties */

/* Query the SVN_PROP_EOL_STYLE property on file PATH.  If STYLE is
   non-null, set *STYLE to PATH's eol style.  Set *EOL to

      - NULL for svn_subst_eol_style_none, or

      - a null-terminated C string containing the native eol marker
        for this platform, for svn_subst_eol_style_native, or

      - a null-terminated C string containing the eol marker indicated
        by the property value, for svn_subst_eol_style_fixed.

   If STYLE is null on entry, ignore it.  If *EOL is non-null on exit,
   it is a static string not allocated in POOL.

   ADM_ACCESS is an access baton set that contains PATH.

   Use POOL for temporary allocation.
*/
svn_error_t *svn_wc__get_eol_style(svn_subst_eol_style_t *style,
                                   const char **eol,
                                   const char *path,
                                   svn_wc_adm_access_t *adm_access,
                                   apr_pool_t *pool);

/* Reverse parser.  Given a real EOL string ("\n", "\r", or "\r\n"),
   return an encoded *VALUE ("LF", "CR", "CRLF") that one might see in
   the property value. */
void svn_wc__eol_value_from_string(const char **value,
                                   const char *eol);

/* Expand keywords for the file at PATH, by parsing a
   whitespace-delimited list of keywords.  If any keywords are found
   in the list, allocate *KEYWORDS from POOL and populate it with
   mappings from (const char *) keywords to their (svn_string_t *)
   values (also allocated in POOL).

   If a keyword is in the list, but no corresponding value is
   available, do not create a hash entry for it.  If no keywords are
   found in the list, or if there is no list, set *KEYWORDS to NULL.

   ADM_ACCESS must be an access baton for PATH.

   If FORCE_LIST is non-null, use it as the list; else use the
   SVN_PROP_KEYWORDS property for PATH.  In either case, use PATH to
   expand keyword values.
*/
svn_error_t *svn_wc__get_keywords(apr_hash_t **keywords,
                                  const char *path,
                                  svn_wc_adm_access_t *adm_access,
                                  const char *force_list,
                                  apr_pool_t *pool);


/* Determine if the svn:special flag is set on PATH.  If so, set
   SPECIAL to TRUE, if not, set it to FALSE.  ADM_ACCESS must be an
   access baton for PATH.  Perform any temporary allocations in
   POOL. */
svn_error_t *svn_wc__get_special(svn_boolean_t *special,
                                 const char *path,
                                 svn_wc_adm_access_t *adm_access,
                                 apr_pool_t *pool);

/* If the SVN_PROP_EXECUTABLE property is present at all, then set
   PATH executable.  If DID_SET is non-null, then set *DID_SET to
   TRUE if did set PATH executable, or to FALSE if not.  ADM_ACCESS
   is an access baton set that contains PATH. */
svn_error_t *
svn_wc__maybe_set_executable(svn_boolean_t *did_set,
                             const char *path,
                             svn_wc_adm_access_t *adm_access,
                             apr_pool_t *pool);

/* If the SVN_PROP_NEEDS_LOCK property is present and there is no
   lock token for the file in the working copy, set PATH to
   read-only. If DID_SET is non-null, then set *DID_SET to TRUE if
   did set PATH read-write, or to FALSE if not.  ADM_ACCESS is an
   access baton set that contains PATH. */
svn_error_t * svn_wc__maybe_set_read_only(svn_boolean_t *did_set,
                                          const char *path,
                                          svn_wc_adm_access_t *adm_access,
                                          apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_TRANSLATE_H */
