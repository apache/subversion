/*
 * svn_string.h :  counted_length strings for Subversion
 *                 (using apr's memory pools)
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */


#ifndef SVN_STRING_H
#define SVN_STRING_H

#include <apr_pools.h>       /* APR memory pools for everyone. */
#include <apr_strings.h>
#include "svn_types.h"



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
#ifdef __GNUC__
    __attribute__ ((format (printf, 2, 3)))
#endif /* __GNUC__ */
;

/* Create a new bytestring by formatting CSTRING (null-terminated)
   from a va_list (see svn_string_createf). */
svn_string_t *svn_string_createv (apr_pool_t *pool,
                                  const char *fmt,
                                  va_list ap)
#ifdef __GNUC__
    __attribute__ ((format (printf, 2, 0)))
#endif /* __GNUC__ */
;

/* Make sure that the string STR has at least MINIMUM_SIZE bytes of
   space available in the memory block.  (MINIMUM_SIZE should include
   space for the terminating null character.)  */
void svn_string_ensure (svn_string_t *str,
                        apr_size_t minimum_size);

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
apr_size_t svn_string_find_char_backward (svn_string_t *str, char ch);

/* Chop STR back to CHAR, inclusive.  Returns number of chars
   chopped, so if no such CHAR in STR, chops nothing and returns 0. */
apr_size_t svn_string_chop_back_to_char (svn_string_t *str, char ch);
#endif  /* SVN_STRING_H */


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
