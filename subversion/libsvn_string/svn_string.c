/*
 * svn_string.h:  routines to manipulate bytestrings (svn_string_t)
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
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
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLAB.NET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
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
 * individuals on behalf of Collab.Net.
 */

#include <string.h>      /* for memcpy(), memcmp(), strlen() */
#include <stdio.h>       /* for putch() and printf() */
#include <ctype.h>       /* for isspace() */
#include "svn_string.h"  /* loads "svn_types.h" and <apr_pools.h> */



/* Our own realloc, since APR doesn't have one.  Note: this is a
   generic realloc for memory pools, *not* for strings.  append()
   calls this on the svn_string_t's *data field.  */

static void *
my__realloc (char *data, const size_t oldsize, const size_t request, 
             apr_pool_t *pool)
{
  void *new_area;

  /* malloc new area */
  new_area = apr_palloc (pool, request);

  /* copy data to new area */
  memcpy (new_area, data, oldsize);

  /* I'm NOT freeing old area here -- cuz we're using pools, ugh. */
  
  /* return new area */
  return new_area;
}



/* create a new bytestring containing a C string (null-terminated);
   requires a memory pool to allocate from.  */

svn_string_t *
svn_string_create (const char *cstring, apr_pool_t *pool)
{
  svn_string_t *new_string;
  size_t l = strlen (cstring);

  /* this alloc gives us memory filled with zeros, yum. */
  new_string = (svn_string_t *) apr_palloc (pool, sizeof(svn_string_t)); 

  /* +1 to account for null byte. */
  new_string->data = (char *) apr_palloc (pool, l + 1);
  new_string->len = l;
  new_string->blocksize = l + 1;

  strcpy (new_string->data, cstring);
  
  return new_string;
}


/* create a new bytestring containing a specific array of bytes
   (NOT null-terminated!);  requires a memory pool to allocate from */

svn_string_t *
svn_string_ncreate (const char *bytes, const size_t size, 
                    apr_pool_t *pool)
{
  svn_string_t *new_string;

  /* this alloc gives us memory filled with zeros, yum. */
  new_string = (svn_string_t *) apr_palloc (pool, sizeof(svn_string_t)); 

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






/* overwrite bytestring with a character */

void 
svn_string_fillchar (svn_string_t *str, const unsigned char c)
{
  memset (str->data, c, str->len);
}



/* set a bytestring to empty */

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

  if (str->len > 0)
    str->data[str->len] = '\0';
}


/* Ask if a bytestring is empty */

svn_boolean_t
svn_string_isempty (const svn_string_t *str)
{
  return (str->len == 0);
}


/* append a number of bytes onto a bytestring */

void
svn_string_appendbytes (svn_string_t *str, const char *bytes, 
                        const size_t count, apr_pool_t *pool)
{
  size_t total_len;
  void *start_address;

  total_len = str->len + count;  /* total size needed */

  /* if we need to realloc our first buffer to hold the concatenation,
     then make it twice the total size we need. */

  if ((total_len + 1) >= str->blocksize)
    {
      str->blocksize = total_len * 2;
      str->data = (char *) my__realloc (str->data, 
                                        str->len,
                                        str->blocksize,
                                        pool); 
    }

  /* get address 1 byte beyond end of original bytestring */
  start_address = (str->data + str->len);

  memcpy (start_address, (void *) bytes, count);
  str->len = total_len;

  str->data[str->len] = '\0';  /* We don't know if this is binary
                                  data or not, but convention is
                                  to null-terminate. */
}


/* append one bytestring type onto another */

void
svn_string_appendstr (svn_string_t *targetstr, const svn_string_t *appendstr,
                      apr_pool_t *pool)
{
  svn_string_appendbytes (targetstr, appendstr->data, 
                          appendstr->len, pool);
}



/* duplicate a bytestring */

svn_string_t *
svn_string_dup (const svn_string_t *original_string, apr_pool_t *pool)
{
  return (svn_string_ncreate (original_string->data,
                              original_string->len, pool));
}



/* compare if two bytestrings' data fields are identical,
   byte-for-byte */

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


/* compare a bytestring with a traditional null-terminated C string */


/* 
   Handy routine.

   Input:  a bytestring

   Returns: offset of first non-whitespace character 

      (if bytestring is ALL whitespace, then it returns the size of
      the bytestring.  Be careful not to use this value as an array
      offset!)

*/

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
  return (str->len);  
}



/* 
   Another handy utility.

   Input:  a bytestring

   Output:  same bytestring, stripped of whitespace on both sides
            (input bytestring is modified IN PLACE)
*/

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




/* Utility: print bytestring to stdout, assuming that the string
   contains ASCII.  */

void
svn_string_print (const svn_string_t *str, 
                  FILE *stream, 
                  svn_boolean_t show_all_fields,
                  svn_boolean_t add_newline)
{
  if (str->len >= 0) 
    {

      fwrite (str->data, 1, str->len, stream);

      if (show_all_fields)
        {
          fprintf (stream, " (blocksize: %d, length: %d)", 
                   str->blocksize, str->len);
        }

      if (add_newline)
        {
          fprintf (stream, "\n");
        }
    }
}



/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

