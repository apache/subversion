/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
 * @file svn_subst.h
 * @brief routines to perform data substitution
 */



#ifndef SVN_SUBST_H
#define SVN_SUBST_H

#include "svn_io.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Eol conversion and keyword expansion. */

/** Valid states for 'svn:eol-style' property.
 *
 * Valid states for 'svn:eol-style' property.  
 * Property nonexistence is equivalent to 'none'.
 */
typedef enum svn_subst_eol_style
{
  /** An unrecognized style */
  svn_subst_eol_style_unknown,

  /** EOL translation is "off" or ignored value */
  svn_subst_eol_style_none,

  /** Translation is set to client's native eol */
  svn_subst_eol_style_native,

  /** Translation is set to one of LF, CR, CRLF */
  svn_subst_eol_style_fixed

} svn_subst_eol_style_t;

/** Set @a *style to the appropriate @c svn_subst_eol_style_t and @a *eol to 
 * the appropriate cstring for a given svn:eol-style property value.
 *
 * Set @a *style to the appropriate @c svn_subst_eol_style_t and @a *eol to the 
 * appropriate cstring for a given svn:eol-style property value.
 *
 * Set @a *eol to
 *
 *    - @c NULL for @c svn_subst_eol_style_none, or
 *
 *    - a null-terminated C string containing the native eol marker
 *      for this platform, for @c svn_subst_eol_style_native, or
 *            
 *    - a null-terminated C string containing the eol marker indicated
 *      by the property value, for @c svn_subst_eol_style_fixed.
 *
 * If @a *style is @c NULL, then @a value was not a valid property value.
 */
void
svn_subst_eol_style_from_value (svn_subst_eol_style_t *style,
                                const char **eol,
                                const char *value);


/** Values used in keyword expansion. */
typedef struct svn_subst_keywords_t
{
  const svn_string_t *revision;
  const svn_string_t *date;
  const svn_string_t *author;
  const svn_string_t *url;
  const svn_string_t *id;
} svn_subst_keywords_t;


/** Return @c TRUE if @a a and @a b do not hold the same keywords.
 *
 * Return @a TRUE if @a a and @a b do not hold the same keywords.
 *
 * If @a compare_values is @c TRUE, "same" means that the @a a and @a b 
 * contain exactly the same set of keywords, and the values of corresponding
 * keywords match as well.  Else if @a compare_values is FALSE, then
 * "same" merely means that @a a and @a b hold the same set of keywords,
 * although those keywords' values might differ.
 *
 * @a a and/or @a b may be @c NULL; for purposes of comparison, @c NULL is
 * equivalent to holding no keywords.
 */
svn_boolean_t 
svn_subst_keywords_differ (const svn_subst_keywords_t *a,
                           const svn_subst_keywords_t *b,
                           svn_boolean_t compare_values);


/** Copy and translate the data in stream @a src into stream @a dst.
 *
 * Copy and translate the data in stream @a src into stream @a dst.  It is
 * assumed that @a src is a readable stream and @a dst is a writable stream.
 *
 * If @a eol_str is non-@c NULL, replace whatever bytestring @a src uses to
 * denote line endings with @a eol_str in the output.  If @a src has an
 * inconsistent line ending style, then: if @a repair is @c FALSE, return
 * @c SVN_ERR_IO_INCONSISTENT_EOL, else if @a repair is @c TRUE, convert any
 * line ending in @a src to @a eol_str in @a dst.  Recognized line endings are:
 * "\\n", "\\r", and "\\r\\n".
 *
 * Expand and contract keywords using the contents of @a keywords as the
 * new values.  If @a expand is @c TRUE, expand contracted keywords and
 * re-expand expanded keywords.  If @a expand is @c FALSE, contract expanded
 * keywords and ignore contracted ones.  @c NULL for any of the keyword
 * values (@a keywords->revision, e.g.) indicates that keyword should be
 * ignored (not contracted or expanded).  If the @a keywords structure
 * itself is @c NULL, keyword substitution will be altogether ignored.
 *
 * Detect only keywords that are no longer than @c SVN_IO_MAX_KEYWORD_LEN
 * bytes, including the delimiters and the keyword itself.
 *
 * Note that a translation request is *required*:  one of @a eol_str or
 * @a keywords must be non-@c NULL.
 *
 * Recommendation: if @a expand is false, then you don't care about the
 * keyword values, so pass empty strings as non-null signifiers.
 *
 * Notes: 
 *
 * See @c svn_wc__get_keywords() and @c svn_wc__get_eol_style() for a
 * convenient way to get @a eol_str and @a keywords if in libsvn_wc.
 */
svn_error_t *
svn_subst_translate_stream (svn_stream_t *src,
                            svn_stream_t *dst,
                            const char *eol_str,
                            svn_boolean_t repair,
                            const svn_subst_keywords_t *keywords,
                            svn_boolean_t expand);


/** A variant of @c svn_subst_translate_stream which operates on files.
 *
 * Convenience routine: a variant of @c svn_subst_translate_stream which
 * operates on files.  (See previous docstring for details.)
 *
 * Copy the contents of file-path @a src to file-path @a dst atomically,
 * either creating @a dst (or overwriting @a dst if it exists), possibly
 * performing line ending and keyword translations.
 *
 * If anything goes wrong during the copy, attempt to delete @a dst (if
 * it exists).
 *
 * If @a eol_str and @a keywords are @c NULL, behavior is just a byte-for-byte
 * copy.
 */
svn_error_t *
svn_subst_copy_and_translate (const char *src,
                              const char *dst,
                              const char *eol_str,
                              svn_boolean_t repair,
                              const svn_subst_keywords_t *keywords,
                              svn_boolean_t expand,
                              apr_pool_t *pool);

/** A variant of @c svn_subst_translate_stream which operates on cstrings.
 *
 * Convenience routine: a variant of @c svn_subst_translate_stream which
 * operates on cstrings.  (See previous docstring for details.)
 *
 * Return a new string in @a *dst, allocated in @a pool, by copying the
 * contents of string @a src, possibly performing line ending and keyword
 * translations.
 *
 * If @a eol_str and @a keywords are @c NULL, behavior is just a byte-for-byte
 * copy.
 */
svn_error_t *
svn_subst_translate_cstring (const char *src,
                             const char **dst,
                             const char *eol_str,
                             svn_boolean_t repair,
                             const svn_subst_keywords_t *keywords,
                             svn_boolean_t expand,
                             apr_pool_t *pool);


/* Eol conversion and character encodings */

/** Translate the data in @a value (assumed to be in encoded in charset
 * @a encoding) to UTF8 and LF line-endings.
 *
 * Translate the data in @a value (assumed to be in encoded in charset
 * @a encoding) to UTF8 and LF line-endings.  If @a encoding is @c NULL, 
 * then assume that @a value is in the system-default language encoding.
 * Return the translated data in @a *new_value, allocated in @a pool.  
 */
svn_error_t *svn_subst_translate_string (svn_string_t **new_value,
                                         const svn_string_t *value,
                                         const char *encoding,
                                         apr_pool_t *pool);

/** Translate the data in @a value from UTF8 and LF line-endings into
 * native locale and native line-endings.
 *
 * Translate the data in @a value from UTF8 and LF line-endings into
 * native locale and native line-endings.  Return the translated data
 * in @a *new_value, allocated in @a pool.  
 */
svn_error_t *svn_subst_detranslate_string (svn_string_t **new_value,
                                           const svn_string_t *value,
                                           apr_pool_t *pool);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_SUBST_H */
