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
#include <svn_string.h>  /* loads <svn_types.h> and <apr_pools.h> */



/* Our own realloc, since APR doesn't have one.  Note: this is a
   generic realloc for memory pools, *not* for strings.  append()
   calls this on the svn_string_t's *data field.  */

void *
my__realloc (char *data, size_t oldsize, size_t request, 
             ap_pool_t *pool)
{
  void *new_area;

  /* malloc new area */
  new_area = ap_palloc (pool, request);

  /* copy data to new area */
  memcpy (new_area, data, oldsize);

  /* I'm NOT freeing old area here -- cuz we're using pools, ugh. */
  
  /* return new area */
  return new_area;
}



/* create a new bytestring containing a C string (null-terminated);
   requires a memory pool to allocate from.  */

svn_string_t *
svn_string_create (char *cstring, ap_pool_t *pool)
{
  svn_string_t *new_string;
  size_t l = strlen (cstring);

  /* this alloc gives us memory filled with zeros, yum. */
  new_string = (svn_string_t *) ap_palloc (pool, sizeof(svn_string_t)); 

  new_string->data = (char *) ap_palloc (pool, l);
  new_string->len = l;
  new_string->blocksize = l;

  strcpy (new_string->data, cstring);
  
  return new_string;
}


/* create a new bytestring containing a specific array of bytes
   (NOT null-terminated!);  requires a memory pool to allocate from */

svn_string_t *
svn_string_ncreate (char *bytes, size_t size, ap_pool_t *pool)
{
  svn_string_t *new_string;

  /* this alloc gives us memory filled with zeros, yum. */
  new_string = (svn_string_t *) ap_palloc (pool, sizeof(svn_string_t)); 

  new_string->data = (char *) ap_palloc (pool, size);
  new_string->len = size;
  new_string->blocksize = size;

  memcpy (new_string->data, bytes, size);

  return new_string;
}






/* overwrite bytestring with a character */

void 
svn_string_fillchar (svn_string_t *str, unsigned char c)
{
  /* safety check */
  if (str->len > str->blocksize)
    str->len = str->blocksize;

  memset (str->data,  (int) c, str->len);
}



/* set a bytestring to empty */

void
svn_string_setempty (svn_string_t *str)
{
  str->data = NULL;
  str->len = 0;
  str->blocksize = 0;
}


/* Ask if a bytestring is empty */

svn_boolean_t
svn_string_isempty (svn_string_t *str)
{
  if (str->len == 0)
    return TRUE;
  else
    return FALSE;
}


/* append a number of bytes onto a bytestring */

void
svn_string_appendbytes (svn_string_t *str, char *bytes, size_t count,
                        ap_pool_t *pool)
{
  size_t total_len;
  void *start_address;

  total_len = str->len + count;  /* total size needed */

  /* if we need to realloc our first buffer to hold the concatenation,
     then make it twice the total size we need. */

  if (total_len >= str->blocksize)
    {
      str->blocksize = total_len * 2;
      str->data = (char *) my__realloc (str->data, 
                                        str->len,
                                        str->blocksize,
                                        pool); 
    }

  /* get address 1 byte beyond end of original bytestring */
  start_address = &str->data[str->len];

  memcpy (start_address, (void *) bytes, count);
  str->len = total_len;
}


/* append one bytestring type onto another */

void
svn_string_appendstr (svn_string_t *targetstr, svn_string_t *appendstr,
                      ap_pool_t *pool)
{
  svn_string_appendbytes (targetstr, appendstr->data, 
                          appendstr->len, pool);
}



/* duplicate a bytestring */

svn_string_t *
svn_string_dup (svn_string_t *original_string, ap_pool_t *pool)
{
  return (svn_string_ncreate (original_string->data,
                              original_string->len, pool));
}



/* compare if two bytestrings' data fields are identical,
   byte-for-byte */

svn_boolean_t
svn_string_compare (svn_string_t *str1, svn_string_t *str2)
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



/* Utility: print bytestring to stdout, assuming that the string
   contains ASCII.  */

void
svn_string_print (svn_string_t *str, FILE *stream)
{
  if (str->len >= 0) 
    {
      fprintf (stream, "String blocksize: %d, length: %d\n", 
               str->blocksize, str->len);
      fwrite (str->data, 1, str->len, stream);
      fprintf (stream, "\n");
    }
}



/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
