/*
 * deprecated.c:  holding file for all deprecated APIs.
 *                "we can't lose 'em, but we can shun 'em!"
 *
 * ====================================================================
 * Copyright (c) 2000-2008 CollabNet.  All rights reserved.
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

/* ==================================================================== */



/*** Includes. ***/

/* We define this here to remove any further warnings about the usage of
   deprecated functions in this file. */
#define SVN_DEPRECATED

#include "svn_subst.h"

#include "svn_private_config.h"




/*** Code. ***/

/*** From subst.c ***/
/* Convert an old-style svn_subst_keywords_t struct * into a new-style
 * keywords hash.  Keyword values are shallow copies, so the produced
 * hash must not be assumed to have lifetime longer than the struct it
 * is based on.  A NULL input causes a NULL output. */
static apr_hash_t *
kwstruct_to_kwhash(const svn_subst_keywords_t *kwstruct,
                   apr_pool_t *pool)
{
  apr_hash_t *kwhash;

  if (kwstruct == NULL)
    return NULL;

  kwhash = apr_hash_make(pool);

  if (kwstruct->revision)
    {
      apr_hash_set(kwhash, SVN_KEYWORD_REVISION_LONG,
                   APR_HASH_KEY_STRING, kwstruct->revision);
      apr_hash_set(kwhash, SVN_KEYWORD_REVISION_MEDIUM,
                   APR_HASH_KEY_STRING, kwstruct->revision);
      apr_hash_set(kwhash, SVN_KEYWORD_REVISION_SHORT,
                   APR_HASH_KEY_STRING, kwstruct->revision);
    }
  if (kwstruct->date)
    {
      apr_hash_set(kwhash, SVN_KEYWORD_DATE_LONG,
                   APR_HASH_KEY_STRING, kwstruct->date);
      apr_hash_set(kwhash, SVN_KEYWORD_DATE_SHORT,
                   APR_HASH_KEY_STRING, kwstruct->date);
    }
  if (kwstruct->author)
    {
      apr_hash_set(kwhash, SVN_KEYWORD_AUTHOR_LONG,
                   APR_HASH_KEY_STRING, kwstruct->author);
      apr_hash_set(kwhash, SVN_KEYWORD_AUTHOR_SHORT,
                   APR_HASH_KEY_STRING, kwstruct->author);
    }
  if (kwstruct->url)
    {
      apr_hash_set(kwhash, SVN_KEYWORD_URL_LONG,
                   APR_HASH_KEY_STRING, kwstruct->url);
      apr_hash_set(kwhash, SVN_KEYWORD_URL_SHORT,
                   APR_HASH_KEY_STRING, kwstruct->url);
    }
  if (kwstruct->id)
    {
      apr_hash_set(kwhash, SVN_KEYWORD_ID,
                   APR_HASH_KEY_STRING, kwstruct->id);
    }

  return kwhash;
}

svn_error_t *
svn_subst_translate_stream2(svn_stream_t *s, /* src stream */
                            svn_stream_t *d, /* dst stream */
                            const char *eol_str,
                            svn_boolean_t repair,
                            const svn_subst_keywords_t *keywords,
                            svn_boolean_t expand,
                            apr_pool_t *pool)
{
  apr_hash_t *kh = kwstruct_to_kwhash(keywords, pool);

  return svn_subst_translate_stream3(s, d, eol_str, repair, kh, expand, pool);
}

svn_error_t *
svn_subst_translate_stream(svn_stream_t *s, /* src stream */
                           svn_stream_t *d, /* dst stream */
                           const char *eol_str,
                           svn_boolean_t repair,
                           const svn_subst_keywords_t *keywords,
                           svn_boolean_t expand)
{
  apr_pool_t *pool = svn_pool_create(NULL);
  svn_error_t *err = svn_subst_translate_stream2(s, d, eol_str, repair,
                                                 keywords, expand, pool);
  svn_pool_destroy(pool);
  return err;
}

svn_error_t *
svn_subst_translate_cstring(const char *src,
                            const char **dst,
                            const char *eol_str,
                            svn_boolean_t repair,
                            const svn_subst_keywords_t *keywords,
                            svn_boolean_t expand,
                            apr_pool_t *pool)
{
  apr_hash_t *kh = kwstruct_to_kwhash(keywords, pool);

  return svn_subst_translate_cstring2(src, dst, eol_str, repair,
                                      kh, expand, pool);
}

svn_error_t *
svn_subst_copy_and_translate(const char *src,
                             const char *dst,
                             const char *eol_str,
                             svn_boolean_t repair,
                             const svn_subst_keywords_t *keywords,
                             svn_boolean_t expand,
                             apr_pool_t *pool)
{
  return svn_subst_copy_and_translate2(src, dst, eol_str, repair, keywords,
                                       expand, FALSE, pool);
}

svn_error_t *
svn_subst_copy_and_translate2(const char *src,
                              const char *dst,
                              const char *eol_str,
                              svn_boolean_t repair,
                              const svn_subst_keywords_t *keywords,
                              svn_boolean_t expand,
                              svn_boolean_t special,
                              apr_pool_t *pool)
{
  apr_hash_t *kh = kwstruct_to_kwhash(keywords, pool);

  return svn_subst_copy_and_translate3(src, dst, eol_str,
                                       repair, kh, expand, special,
                                       pool);
}
