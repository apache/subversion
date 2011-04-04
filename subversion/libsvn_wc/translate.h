/*
 * translate.h :  eol and keyword translation
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
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

/* If EOL is not-NULL query the SVN_PROP_EOL_STYLE property on file
   LOCAL_ABSPATH in DB.  If STYLE is non-null, set *STYLE to LOCAL_ABSPATH's
   eol style.  Set *EOL to

      - NULL for svn_subst_eol_style_none, or

      - a null-terminated C string containing the native eol marker
        for this platform, for svn_subst_eol_style_native, or

      - a null-terminated C string containing the eol marker indicated
        by the property value, for svn_subst_eol_style_fixed.

   If STYLE is null on entry, ignore it.  If *EOL is non-null on exit,
   it is a static string not allocated in POOL.

   If KEYWORDS is not NULL Expand keywords for the file at LOCAL_ABSPATH
   in DB, by parsing a whitespace-delimited list of keywords.  If any keywords
   are found in the list, allocate *KEYWORDS from RESULT_POOL and populate it
   with mappings from (const char *) keywords to their (svn_string_t *)
   values (also allocated in RESULT_POOL).

   If a keyword is in the list, but no corresponding value is
   available, do not create a hash entry for it.  If no keywords are
   found in the list, or if there is no list, set *KEYWORDS to NULL.

   If SPECIAL is not NULL determine if the svn:special flag is set on 
   LOCAL_ABSPATH in DB.  If so, set SPECIAL to TRUE, if not, set it to FALSE.

   If PROPS is not NULL, use PROPS instead of the properties on LOCAL_ABSPATH.

   Use SCRATCH_POOL for temporary allocation, RESULT_POOL for allocating
   *STYLE and *EOL.
*/
svn_error_t *
svn_wc__get_translate_info(svn_subst_eol_style_t *style,
                           const char **eol,
                           apr_hash_t **keywords,
                           svn_boolean_t *special,
                           svn_wc__db_t *db,
                           const char *local_abspath,
                           apr_hash_t *props,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/* Reverse parser.  Given a real EOL string ("\n", "\r", or "\r\n"),
   return an encoded *VALUE ("LF", "CR", "CRLF") that one might see in
   the property value. */
void svn_wc__eol_value_from_string(const char **value,
                                   const char *eol);

/* Expand keywords for the file at LOCAL_ABSPATH in DB, by parsing a
   whitespace-delimited list of keywords KEYWORD_LIST.  If any keywords
   are found in the list, allocate *KEYWORDS from RESULT_POOL and populate
   it with mappings from (const char *) keywords to their (svn_string_t *)
   values (also allocated in RESULT_POOL).

   If a keyword is in the list, but no corresponding value is
   available, do not create a hash entry for it.  If no keywords are
   found in the list, or if there is no list, set *KEYWORDS to NULL.

   Use LOCAL_ABSPATH to expand keyword values.

   Use SCRATCH_POOL for any temporary allocations.
*/
svn_error_t *
svn_wc__expand_keywords(apr_hash_t **keywords,
                        svn_wc__db_t *db,
                        const char *local_abspath,
                        const char *keyword_list,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* If the SVN_PROP_EXECUTABLE property is present at all, then set
   LOCAL_ABSPATH in DB executable.  If DID_SET is non-null, then set
   *DID_SET to TRUE if did set LOCAL_ABSPATH executable, or to FALSE if not.

   Use SCRATCH_POOL for any temporary allocations.
*/
svn_error_t *
svn_wc__maybe_set_executable(svn_boolean_t *did_set,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             apr_pool_t *scratch_pool);

/* If the SVN_PROP_NEEDS_LOCK property is present and there is no
   lock token for the file in the working copy, set LOCAL_ABSPATH to
   read-only. If DID_SET is non-null, then set *DID_SET to TRUE if
   did set LOCAL_ABSPATH read-write, or to FALSE if not.

   Use SCRATCH_POOL for any temporary allocations.
*/
svn_error_t * svn_wc__maybe_set_read_only(svn_boolean_t *did_set,
                                          svn_wc__db_t *db,
                                          const char *local_abspath,
                                          apr_pool_t *scratch_pool);

/* Internal version of svn_wc_translated_stream2(), which see. */
svn_error_t *
svn_wc__internal_translated_stream(svn_stream_t **stream,
                                   svn_wc__db_t *db,
                                   const char *local_abspath,
                                   const char *versioned_abspath,
                                   apr_uint32_t flags,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool);

/* Like svn_wc_translated_file3(), except the working copy database
 * is specified directly by DB instead of indirectly through a
 * svn_wc_context_t parameter. */
svn_error_t *
svn_wc__internal_translated_file(const char **xlated_abspath,
                                 const char *src,
                                 svn_wc__db_t *db,
                                 const char *versioned_abspath,
                                 apr_uint32_t flags,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_TRANSLATE_H */
