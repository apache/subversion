/*
 * svn_string.h :  byte-string routines for Subversion
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


#ifndef __SVN_STRING_H__
#define __SVN_STRING_H__



/* a string of bytes  */

typedef struct svn_string_t
{
  char *data;                /* pointer to the bytestring */
  size_t len;                /* length of bytestring */
  size_t blocksize;          /* total size of buffer allocated */
} svn_string_t;




/* Create a new bytestring containing a C string (null-terminated), or
   containing a generic string of bytes (NON-null-terminated) */

svn_string_t * svn_string_create (char *cstring);
svn_string_t * svn_string_ncreate (char *bytes, size_t size);

/* Free a string structure */

void svn_string_free (svn_string_t *str);

/* Set a bytestring to NULL */

void svn_string_setnull (svn_string_t *str);

/* Fill bytestring with a character */

void svn_string_fillchar (svn_string_t *str, unsigned char c);

/* Is bytestring NULL or empty? */

svn_boolean_t svn_string_isnull (svn_string_t *str);
svn_boolean_t svn_string_isempty (svn_string_t *str);

/* Append either a string of bytes or an svn_string_t onto a
   svn_string_t.  reallocs() if necessary. */

void svn_string_appendbytes (svn_string_t *str, char *bytes, size_t count);
void svn_string_appendstr (svn_string_t *targetstr, svn_string_t *appendstr);

/* Duplicate a bytestring;  returns freshly malloc'd copy.  */

svn_string_t * svn_string_dup (svn_string_t *original_string);


/* compare if two bytestrings' data fields are identical,
   byte-for-byte */

svn_boolean_t svn_string_compare (svn_string_t *str1, svn_string_t *str2);



#endif  /* __SVN_STRING_H__ */


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
