/*
 * svn_string.h:  routines to manipulate bytestrings (svn_string_t)
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



#include <string.h>      /* for memcpy(), memcmp(), strlen() */
#include <stdio.h>       /* for putch() and printf() */
#include <ctype.h>       /* for isspace() */
#include "svn_string.h"  /* loads "svn_types.h" and <apr_pools.h> */



/* Our own realloc, since APR doesn't have one.  Note: this is a
   generic realloc for memory pools, *not* for strings. */
static void *
my__realloc (char *data, const size_t oldsize, const size_t request, 
             apr_pool_t *pool)
{
  void *new_area;

  /* kff todo: it's a pity APR doesn't give us this -- sometimes it
     could realloc the block merely by extending in place, sparing us
     a memcpy(), but only the pool would know enough to be able to do
     this.  We should add a realloc() to APR if someone hasn't
     already. */

  /* malloc new area */
  new_area = apr_palloc (pool, request);

  /* copy data to new area */
  memcpy (new_area, data, oldsize);

  /* I'm NOT freeing old area here -- cuz we're using pools, ugh. */
  
  /* return new area */
  return new_area;
}



/* Create a new bytestring by copying SIZE bytes from BYTES; requires a
   memory POOL to allocate from. */
svn_string_t *
svn_string_ncreate (const char *bytes, const size_t size, 
                    apr_pool_t *pool)
{
  svn_string_t *new_string;

  new_string = (svn_string_t *) apr_palloc (pool, sizeof(svn_string_t)); 

  /* +1 to account for null terminator. */
  new_string->data = (char *) apr_palloc (pool, size + 1);
  new_string->len = size;
  new_string->blocksize = size + 1;

  memcpy (new_string->data, bytes, size);

  /* Null termination is the convention -- even if we suspect the data
     to be binary, it's not up to us to decide, it's the caller's
     call.  Heck, that's why they call it the caller! */
  new_string->data[new_string->len] = '\0';

  return new_string;
}


/* Create a new bytestring by copying CSTRING (null-terminated);
   requires a POOL to allocate from.  */
svn_string_t *
svn_string_create (const char *cstring, apr_pool_t *pool)
{
  return svn_string_ncreate (cstring, strlen (cstring), pool);
}



/* Overwrite bytestring STR with a character C. */
void 
svn_string_fillchar (svn_string_t *str, const unsigned char c)
{
  memset (str->data, c, str->len);
}



/* Set a bytestring STR to empty (0 length). */
void
svn_string_setempty (svn_string_t *str)
{
  if (str->len > 0)
    str->data[0] = '\0';

  str->len = 0;
}


/* Chop NBYTES bytes off end of STR, but not more than STR->len. */
void
svn_string_chop (svn_string_t *str, size_t nbytes)
{
  if (nbytes > str->len)
    str->len = 0;
  else
    str->len -= nbytes;

  str->data[str->len] = '\0';
}


/* Ask if a bytestring STR is empty (0 length) */
svn_boolean_t
svn_string_isempty (const svn_string_t *str)
{
  return (str->len == 0);
}


static void
ensure_block_capacity (svn_string_t *str, 
                       size_t minimum_size,
                       apr_pool_t *pool)
{
  /* Keep doubling capacity until have enough. */
  while (str->blocksize < minimum_size)
    str->blocksize *= 2;

  str->data = (char *) my__realloc (str->data, 
                                    str->len,
                                    str->blocksize,
                                    pool); 
}


/* Copy COUNT bytes from BYTES onto the end of bytestring STR. */
void
svn_string_appendbytes (svn_string_t *str, const char *bytes, 
                        const size_t count, apr_pool_t *pool)
{
  size_t total_len;
  void *start_address;

  if (str == NULL)
    {
      str = svn_string_ncreate (bytes, count, pool);
      return;
    }

  total_len = str->len + count;  /* total size needed */

  /* +1 for null terminator. */
  ensure_block_capacity (str, (total_len + 1), pool);

  /* get address 1 byte beyond end of original bytestring */
  start_address = (str->data + str->len);

  memcpy (start_address, (void *) bytes, count);
  str->len = total_len;

  str->data[str->len] = '\0';  /* We don't know if this is binary
                                  data or not, but convention is
                                  to null-terminate. */
}


/* Append APPENDSTR onto TARGETSTR. */
void
svn_string_appendstr (svn_string_t *targetstr, const svn_string_t *appendstr,
                      apr_pool_t *pool)
{
  svn_string_appendbytes (targetstr, appendstr->data, 
                          appendstr->len, pool);
}



/* Return a duplicate of ORIGNAL_STRING. */
svn_string_t *
svn_string_dup (const svn_string_t *original_string, apr_pool_t *pool)
{
  return (svn_string_ncreate (original_string->data,
                              original_string->len, pool));
}



/* Return true if STR1 and STR2 have identical length and data. */
svn_boolean_t
svn_string_compare (const svn_string_t *str1, const svn_string_t *str2)
{
  /* easy way out :)  */
  if (str1->len != str2->len)
    return FALSE;

  /* now that we know they have identical lengths... */
  
  if (memcmp (str1->data, str2->data, str1->len))
    return FALSE;
  else
    return TRUE;
}



/* Return offset of first non-whitespace character in STR, or -1 if none.  */
size_t
svn_string_first_non_whitespace (const svn_string_t *str)
{
  size_t i;

  for (i = 0; i < str->len; i++)
    {
      if (! isspace (str->data[i]))
        {
          return i;
        }
    }

  /* if we get here, then the string must be entirely whitespace */
  return (-1);  
}


/* Strips whitespace from both sides of STR (modified in place). */
void
svn_string_strip_whitespace (svn_string_t *str)
{
  size_t i;

  /* Find first non-whitespace character */
  size_t offset = svn_string_first_non_whitespace (str);

  /* Go ahead!  Waste some RAM, we've got pools! :)  */
  str->data += offset;
  str->len -= offset;

  /* Now that we've chomped whitespace off the front, search backwards
     from the end for the first non-whitespace. */

  for (i = (str->len - 1); i >= 0; i--)
    {
      if (! isspace (str->data[i]))
        {
          break;
        }
    }
  
  /* Mmm, waste some more RAM */
  str->len = i + 1;
}


/* Return position of last occurrence of CHAR in STR, or return
   STR->len if no occurrence. */ 
apr_size_t
svn_string_find_char_backward (svn_string_t *str, char ch)
{
  apr_size_t i;

  for (i = (str->len - 1); i >= 0; i--)
    {
      if (str->data[i] == ch)
        return i;
    }

  return str->len;
}


/* Chop STR back to CHAR, inclusive.  Returns number of chars
   chopped, so if no such CHAR in STR, chops nothing and returns 0. */
apr_size_t
svn_string_chop_back_to_char (svn_string_t *str, char ch)
{
  apr_size_t i = svn_string_find_char_backward (str, ch);

  if (i < str->len)
    {
      apr_size_t nbytes = (str->len - i);
      svn_string_chop (str, nbytes);
      return nbytes;
    }

  return 0;
}



/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

