/*
 * svn_string.h :  counted_length strings for Subversion
 *                 (using apr's memory pools)
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */


#ifndef SVN_STRING_H
#define SVN_STRING_H

#include <apr.h>
#include <apr_pools.h>       /* APR memory pools for everyone. */
#include <apr_strings.h>

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* The svn_string_t data type.  Pretty much what you expected. */
typedef struct svn_string_t
{
  char *data;                /* pointer to the bytestring */
  apr_size_t len;            /* length of bytestring */
  apr_size_t blocksize;      /* total size of buffer allocated */
  /* pool from which this string was originally allocated, and is not
     necessarily specific to this string.  This is used only for
     allocating more memory from when the string needs to grow.  */
  apr_pool_t *pool;          
} svn_string_t;



/* Create a new bytestring containing a C string (null-terminated), or
   containing a generic string of bytes (NON-null-terminated) */
svn_string_t * svn_string_create (const char *cstring, 
                                  apr_pool_t *pool);
svn_string_t * svn_string_ncreate (const char *bytes, const apr_size_t size, 
                                   apr_pool_t *pool);

/* Create a new bytestring by formatting CSTRING (null-terminated)
   from varargs, which are as appropriate for apr_psprintf. */
svn_string_t *svn_string_createf (apr_pool_t *pool,
                                  const char *fmt,
                                  ...)
       __attribute__ ((format (printf, 2, 3)));

/* Create a new bytestring by formatting CSTRING (null-terminated)
   from a va_list (see svn_string_createf). */
svn_string_t *svn_string_createv (apr_pool_t *pool,
                                  const char *fmt,
                                  va_list ap)
       __attribute__ ((format (printf, 2, 0)));

/* Make sure that the string STR has at least MINIMUM_SIZE bytes of
   space available in the memory block.  (MINIMUM_SIZE should include
   space for the terminating null character.)  */
void svn_string_ensure (svn_string_t *str,
                        apr_size_t minimum_size);

/* Set a bytestring STR to VALUE */
void svn_string_set (svn_string_t *str, const char *value);

/* Set a bytestring STR to empty (0 length). */
void svn_string_setempty (svn_string_t *str);

/* Return true if a bytestring is empty (has length zero). */
svn_boolean_t svn_string_isempty (const svn_string_t *str);

/* Chop NBYTES bytes off end of STR, but not more than STR->len. */
void svn_string_chop (svn_string_t *str, apr_size_t bytes);

/* Fill bytestring STR with character C. */
void svn_string_fillchar (svn_string_t *str, const unsigned char c);

/* Append either a string of bytes, an svn_string_t, or a C-string
   onto TARGETSTR.  reallocs() if necessary.  TARGETSTR is affected,
   nothing else is. */
void svn_string_appendbytes (svn_string_t *targetstr,
                             const char *bytes, 
                             const apr_size_t count);
void svn_string_appendstr (svn_string_t *targetstr, 
                           const svn_string_t *appendstr);
void svn_string_appendcstr (svn_string_t *targetstr,
                            const char *cstr);

/* Return a duplicate of ORIGNAL_STRING. */
svn_string_t *svn_string_dup (const svn_string_t *original_string,
                              apr_pool_t *pool);


/* Return TRUE iff STR1 and STR2 have identical length and data. */
svn_boolean_t svn_string_compare (const svn_string_t *str1, 
                                  const svn_string_t *str2);

/** convenience routines **/

/* Return offset of first non-whitespace character in STR, or -1 if none. */
apr_size_t svn_string_first_non_whitespace (const svn_string_t *str);

/* Strips whitespace from both sides of STR (modified in place). */
void svn_string_strip_whitespace (svn_string_t *str);

/* Return position of last occurrence of CHAR in STR, or return
   STR->len if no occurrence. */ 
apr_size_t svn_string_find_char_backward (const svn_string_t *str, char ch);

/* Chop STR back to CHAR, inclusive.  Returns number of chars
   chopped, so if no such CHAR in STR, chops nothing and returns 0. */
apr_size_t svn_string_chop_back_to_char (svn_string_t *str, char ch);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_STRING_H */

/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
