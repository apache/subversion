/*  svn_subst.h:  routines to perform data substitution
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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



#ifndef SVN_SUBST_H
#define SVN_SUBST_H

#include "svn_io.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*** Eol conversion and keyword expansion. ***/


/* Values used in keyword expansion. */
typedef struct svn_subst_keywords_t
{
  const svn_string_t *revision;
  const svn_string_t *date;
  const svn_string_t *author;
  const svn_string_t *url;
  const svn_string_t *id;
} svn_subst_keywords_t;


/* Return TRUE if A and B do not hold the same keywords.
 *
 * If COMPARE_VALUES is true, "same" means that the A and B contain
 * exactly the same set of keywords, and the values of corresponding
 * keywords match as well.  Else if COMPARE_VALUES is false, then
 * "same" merely means that A and B hold the same set of keywords,
 * although those keywords' values might differ.
 *
 * A and/or B may be NULL; for purposes of comparison, NULL is
 * equivalent to holding no keywords.
 */
svn_boolean_t 
svn_subst_keywords_differ (const svn_subst_keywords_t *a,
                           const svn_subst_keywords_t *b,
                           svn_boolean_t compare_values);


/* Copy and translate the data in stream SRC into stream DST.  It is
   assumed that SRC is a readable stream and DST is a writable stream.

   If EOL_STR is non-NULL, replace whatever bytestring SRC uses to
   denote line endings with EOL_STR in the output.  If SRC has an
   inconsistent line ending style, then: if REPAIR is FALSE, return
   SVN_ERR_IO_INCONSISTENT_EOL, else if REPAIR is TRUE, convert any
   line ending in SRC to EOL_STR in DST.  Recognized line endings are:
   "\n", "\r", and "\r\n".

   Expand and contract keywords using the contents of KEYWORDS as the
   new values.  If EXPAND is TRUE, expand contracted keywords and
   re-expand expanded keywords.  If EXPAND is FALSE, contract expanded
   keywords and ignore contracted ones.  NULL for any of the keyword
   values (KEYWORDS->revision, e.g.) indicates that keyword should be
   ignored (not contracted or expanded).  If the KEYWORDS structure
   itself is NULL, keyword substitution will be altogether ignored.

   Detect only keywords that are no longer than SVN_IO_MAX_KEYWORD_LEN
   bytes, including the delimiters and the keyword itself.

   Note that a translation request is *required*:  one of EOL_STR or
   KEYWORDS must be non-NULL.

   Recommendation: if EXPAND is false, then you don't care about the
   keyword values, so pass empty strings as non-null signifiers.

   Notes: 

   See svn_wc__get_keywords() and svn_wc__get_eol_style() for a
   convenient way to get EOL_STR and KEYWORDS if in libsvn_wc.
  */
svn_error_t *
svn_subst_translate_stream (svn_stream_t *src,
                            svn_stream_t *dst,
                            const char *eol_str,
                            svn_boolean_t repair,
                            const svn_subst_keywords_t *keywords,
                            svn_boolean_t expand);


/* Convenience routine: a variant of svn_subst_translate_stream which
   operates on files.  (See previous docstring for details.)

   Copy the contents of file-path SRC to file-path DST atomically,
   either creating DST (or overwriting DST if it exists), possibly
   performing line ending and keyword translations.

   If anything goes wrong during the copy, attempt to delete DST (if
   it exists).

   If EOL_STR and KEYWORDS are NULL, behavior is just a byte-for-byte
   copy.
 */
svn_error_t *
svn_subst_copy_and_translate (const char *src,
                              const char *dst,
                              const char *eol_str,
                              svn_boolean_t repair,
                              const svn_subst_keywords_t *keywords,
                              svn_boolean_t expand,
                              apr_pool_t *pool);

/* Convenience routine: a variant of svn_subst_translate_stream which
   operates on cstrings.  (See previous docstring for details.)

   Return a new string in *DST, allocated in POOL, by copying the
   contents of string SRC, possibly performing line ending and keyword
   translations.

   If EOL_STR and KEYWORDS are NULL, behavior is just a byte-for-byte
   copy.
 */
svn_error_t *
svn_subst_translate_cstring (const char *src,
                             const char **dst,
                             const char *eol_str,
                             svn_boolean_t repair,
                             const svn_subst_keywords_t *keywords,
                             svn_boolean_t expand,
                             apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_SUBST_H */
