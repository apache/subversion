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

#include <svn_string.h>  /* defines svn_string_t */
#include <string.h>      /* memcpy and memcmp */


/* create a new bytestring containing a C string (null-terminated) */

svn_string_t *
svn_string_create (char *cstring)
{
  svn_string_t *new_string;
  
  new_string = xmalloc (sizeof(svn_string_t)); /* TODO:  xmalloc */
  new_string->data = NULL;
  new_string->len = 0;
  new_string->blocksize = 0;

  /* This routine will actually call realloc(); realloc() behaves like
     malloc() as long as data == NULL */

  svn_string_appendbytes (new_string, cstring, sizeof(cstring));

  return new_string;
}


/* create a new bytestring containing a specific array of bytes
   (NOT null-terminated!) */

svn_string_t *
svn_string_ncreate (char *bytes, size_t size)
{
  svn_string_t *new_string;

  new_string = xmalloc (sizeof(svn_string_t)); /* TODO:  xmalloc */
  new_string->data = NULL;
  new_string->len = 0;
  new_string->blocksize = 0;

  svn_string_appendbytes (new_string, bytes, size);

  return new_string;
}


/* free a bytestring structure */

void svn_string_free (svn_string_t *str)
{
  free (str->data);
  free (str);
}



/* set a bytestring to null */

void
svn_string_setnull (svn_string_t *str)
{
  free (str->data);
  str->data = NULL;
  str->len = 0;
}


/* overwrite bytestring with a character */

void 
svn_string_fillchar (svn_string_t *str, unsigned char c)
{
  int i;
  
  if (c == 0) 
    {
      bzero (str->data, str->len);  /* for speed */
    }
  else
    { 
      /* not using memset(), because it wants an int */
      for (i = 0; i < str->len; i++)
        {
          str->data[i] = c;
        }
    }
}



/* Ask if a bytestring is null */

svn_boolean_t
svn_string_isnull (svn_string_t *str)
{
  if (str->data == NULL)
    return TRUE;
  else
    return FALSE;
}


/* Ask if a bytestring is empty */

svn_boolean_t
svn_string_isempty (svn_string_t *str)
{
  if (str->len <= 0)
    return TRUE;
  else
    return FALSE;
}


/* append a number of bytes onto a bytestring */

void
svn_string_appendbytes (svn_string_t *str, char *bytes, size_t count)
{
  size_t total_len;
  void *start_address;

  total_len = str->len + count;  /* total size needed */

  /* if we need to realloc our first buffer to hold the concatenation,
     then make it twice the total size we need. */

  if (total_len >= str->blocksize)
    {
      str->blocksize = total_len * 2;
      str->data = xrealloc (str->data, str->blocksize); /* TODO: xrealloc */
    }

  /* get address 1 byte beyond end of original bytestring */
  start_address = str->data[str->len];

  memcpy (start_address, (void *) bytes, count);
  str->len = total_len;
}


/* append one bytestring type onto another */

void
svn_string_appendstr (svn_string_t *targetstr, svn_string_t *appendstr)
{
  svn_string_appendbytes (targetstr, appendstr->data, appendstr->len);
}



/* duplicate a bytestring */

svn_string_t *
svn_string_dup (svn_string_t original_string)
{
  return (svn_string_ncreate (original_string->data,
                              original_string->len));
}



/* compare if two bytestrings' data fields are identical,
   byte-for-byte */

svn_boolean_t
svn_string_compare (svn_string_t *str1, svn_string_t *str2)
{
  int i;

  /* easy way out :)  */
  if (str1->len != str2->len)
    return FALSE;

  /* now that we know they have identical lengths... */
  
  if (memcmp (str1->data, str2->data, str1->len))
    return FALSE;
  else
    return TRUE;
}




/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
