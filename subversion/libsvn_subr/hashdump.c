/*
 * hashdump.c :  dumping and reading hash tables to/from files.
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
 * software developed by CollabNet (http://www.Collab.Net/)."
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



#include <stdio.h>       /* for sprintf() */
#include <stdlib.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_hash.h"




/* 
 * The format of a dumped hash table is:
 *
 *   K <nlength>
 *   name (a string of <nlength> bytes, followed by a newline)
 *   V <vlength>
 *   val (a string of <vlength> bytes, followed by a newline)
 *   [... etc, etc ...]
 *   END
 *
 *
 * (Yes, there is a newline after END.)
 *
 * For example:
 *
 *   K 5
 *   color
 *   V 3
 *   red
 *   K 11
 *   wine review
 *   V 376
 *   A forthright entrance, yet coquettish on the tongue, its deceptively
 *   fruity exterior hides the warm mahagony undercurrent that is the
 *   hallmark of Chateau Fraisant-Pitre.  Connoisseurs of the region will
 *   be pleased to note the familiar, subtle hints of mulberries and
 *   carburator fluid.  Its confident finish is marred only by a barely
 *   detectable suggestion of rancid squid ink.
 *   K 5 
 *   price
 *   V 8
 *   US $6.50
 *   END
 *
 */

#define SVN_KEYLINE_MAXLEN 100     /* The longest a "key" line can be */



/*** Code. ***/

apr_size_t 
svn_unpack_bytestring (char **returndata, void *value)
{
  svn_string_t *valstring = (svn_string_t *) value;

  *returndata = valstring->data;

  return (size_t) valstring->len;
}


void *
svn_pack_bytestring (size_t len, const char *val, apr_pool_t *pool)
{
  svn_string_t *valstring = apr_palloc (pool, sizeof (*valstring)); 

  valstring->len       = len;
  valstring->blocksize = len;
  valstring->data      = (void *) val;

  return valstring;
}


apr_status_t
svn_hash_write (apr_hash_t *hash, 
                apr_size_t (*unpack_func) (char **unpacked_data, void *val),
                apr_file_t *destfile)
{
  apr_hash_index_t *this;      /* current hash entry */
  apr_status_t err;
  char buf[SVN_KEYLINE_MAXLEN];

  for (this = apr_hash_first (hash); this; this = apr_hash_next (this))
    {
      const void *key;
      void *val;
      size_t keylen;
      size_t vallen;
      int bytes_used;
      char *valstring;

      /* Get this key and val. */
      apr_hash_this (this, &key, &keylen, &val);

      /* Output name length, then name. */

      err = apr_full_write (destfile, "K ", 2, NULL);
      if (err) return err;

      sprintf (buf, "%ld%n", (long int) keylen, &bytes_used);
      err = apr_full_write (destfile, buf, bytes_used, NULL);
      if (err) return err;

      err = apr_full_write (destfile, "\n", 1, NULL);
      if (err) return err;

      err = apr_full_write (destfile, (char *) key, keylen, NULL);
      if (err) return err;

      err = apr_full_write (destfile, "\n", 1, NULL);
      if (err) return err;

      /* Output value length, then value. */

      vallen = (size_t) (*unpack_func) (&valstring, val); /* secret decoder! */
      err = apr_full_write (destfile, "V ", 2, NULL);
      if (err) return err;

      sprintf (buf, "%ld%n", (long int) vallen, &bytes_used);
      err = apr_full_write (destfile, buf, bytes_used, NULL);
      if (err) return err;

      err = apr_full_write (destfile, "\n", 1, NULL);
      if (err) return err;

      err = apr_full_write (destfile, valstring, vallen, NULL);
      if (err) return err;

      err = apr_full_write (destfile, "\n", 1, NULL);
      if (err) return err;
    }

  err = apr_full_write (destfile, "END\n", 4, NULL);
  if (err) return err;

  return SVN_NO_ERROR;
}


/* Read a line from FILE into BUF, but not exceeding *LIMIT bytes.
 * Does not include newline, instead '\0' is put there.
 * Length (as in strlen) is returned in *LIMIT.
 * BUF should be pre-allocated.
 * FILE should be already opened. 
 *
 * (This is meant for reading length lines from hashdump files.) 
 */
static apr_status_t
read_length_line (apr_file_t *file, char *buf, size_t *limit)
{
  size_t i;
  apr_status_t err;
  char c;

  for (i = 0; i < *limit; i++)
  {
    err = apr_getc (&c, file); 
    if (err)
      return err;   /* Note: this status code could be APR_EOF, which
                       is totally fine.  The caller should be aware of
                       this. */
    if (c == '\n')
      {
        buf[i] = '\0';
        *limit = i;
        return SVN_NO_ERROR;
      }
    else
      {
        buf[i] = c;
      }
  }

  /* todo: make a custom error "SVN_LENGTH_TOO_LONG" or something? */
  return SVN_WARNING;
}


apr_status_t
svn_hash_read (apr_hash_t *hash, 
               void * (*pack_func) (size_t len,
                                    const char *val,
                                    apr_pool_t *pool),
               apr_file_t *srcfile,
               apr_pool_t *pool)
{
  apr_status_t err;
  char buf[SVN_KEYLINE_MAXLEN];
  apr_size_t num_read;
  char c;
  void *package;
  int first_time = 1;
  

  while (1)
    {
      /* Read a key length line.  Might be END, though. */
      int len = SVN_KEYLINE_MAXLEN;
      err = read_length_line (srcfile, buf, &len);
      if ((err == APR_EOF) && first_time)
        /* We got an EOF on our very first attempt to read, which
           means it's a zero-byte file.  No problem, just go home. */        
        return SVN_NO_ERROR;
      else if (err)
        /* Any other circumstance is a genuine error. */
        return err;

      first_time = 0;

      if ((len == 3)
          && (buf[0] == 'E')       /* We've reached the end of the  */
          && (buf[1] == 'N')       /* dumped hash table, so leave.  */
          && (buf[2] == 'D'))
        {
          return SVN_NO_ERROR;
        }
      else if ((buf[0] == 'K') && (buf[1] == ' '))
        {
          /* Get the length of the key */
          size_t keylen = (size_t) atoi (buf + 2);

          /* Now read that much into a buffer, + 1 byte for null terminator */
          void *keybuf = apr_palloc (pool, keylen + 1);
          err = apr_full_read (srcfile, keybuf, keylen, &num_read);
          if (err) return err;
          ((char *) keybuf)[keylen] = '\0';

          /* Suck up extra newline after key data */
          err = apr_getc (&c, srcfile);
          if (err) return err;
          if (c != '\n') return SVN_ERR_MALFORMED_FILE;

          /* Read a val length line */
          len = SVN_KEYLINE_MAXLEN;
          err = read_length_line (srcfile, buf, &len);
          if (err) return err;

          if ((buf[0] == 'V') && (buf[1] == ' '))
            {
              /* Get the length of the value */
              int vallen = atoi (buf + 2);

              /* Again, 1 extra byte for the null termination. */
              void *valbuf = apr_palloc (pool, vallen + 1);
              err = apr_full_read (srcfile, valbuf, vallen, &num_read);
              if (err) return err;
              ((char *) valbuf)[vallen] = '\0';

              /* Suck up extra newline after val data */
              err = apr_getc (&c, srcfile);
              if (err) return err;
              if (c != '\n') return SVN_ERR_MALFORMED_FILE;

              /* Send the val data for packaging... */
              package = (void *) (*pack_func) (vallen, valbuf, pool);

              /* The Grand Moment:  add a new hash entry! */
              apr_hash_set (hash, keybuf, keylen, package);
            }
          else
            {
              return SVN_ERR_MALFORMED_FILE;
            }
        }
      else
        {
          return SVN_ERR_MALFORMED_FILE;
        }
    } /* while (1) */

}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
