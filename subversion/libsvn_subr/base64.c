/*
 * base64.c:  base64 encoding and decoding functions
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



#include <string.h>
#include "apr_pools.h"
#include "svn_io.h"
#include "svn_error.h"
#include "svn_base64.h"


#define BASE64_LINELEN 76
static const char base64tab[] = "ABCDEFGHIJKLMNOPQRSTWXYZ"
                                "abcdefghijklmnopqrstuvwxyz0123456789+/";



/* Binary input --> base64-encoded output */

struct encode_baton {
  svn_write_fn_t *output;
  void *output_baton;
  unsigned char buf[3];         /* Bytes waiting to be encoded */
  int buflen;                   /* Number of bytes waiting */
  int linelen;                  /* Bytes output so far on this line */
  apr_pool_t *pool;
};


/* Base64-encode a group.  IN needs to have three bytes and OUT needs
   to have room for four bytes.  The input group is treated as four
   six-bit units which are treated as lookups into base64tab for the
   bytes of the output group.  */
static APR_INLINE void
encode_group (const unsigned char *in, unsigned char *out)
{
  out[0] = base64tab[in[0] >> 2];
  out[1] = base64tab[((in[0] & 0x3) << 4) | (in[1] >> 4)];
  out[2] = base64tab[((in[1] & 0xf) << 2) | (in[2] >> 6)];
  out[3] = base64tab[in[2] & 0x3f];
}


static svn_error_t *
encode_data (void *baton, const char *data, apr_size_t *len, apr_pool_t *pool)
{
  struct encode_baton *eb = baton;
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_string_t *encoded = svn_string_create ("", subpool);
  const unsigned char *p = (unsigned char *) data;
  const unsigned char *end = (unsigned char *) data + *len;
  unsigned char group[4];
  apr_size_t enclen;
  svn_error_t *err = SVN_NO_ERROR;

  if (*len == 0)
    {
      /* We may need to encode a partial group.  The way we do this is
	 to zero-fill the remainder of the input group, encode it, and
	 then replace the unnecessary bytes of the output group with
	 '=' characters.  Also finish up with a newline if we aren't
	 at the beginning of a line.  */
      if (eb->buflen > 0)
	{
	  memset (eb->buf + eb->buflen, 0, 3 - eb->buflen);
	  encode_group (eb->buf, group);
	  memset (group + (eb->buflen + 1), '=', 4 - (eb->buflen + 1));
	  svn_string_appendbytes (encoded, group, 4);
	  eb->linelen += 4;
	}
      if (eb->linelen != 0)
	svn_string_appendcstr (encoded, "\n");
    }
  else
    {
      /* This code looks a little weird but it gets all the edge cases
	 right without extra logic.  Keep copying bytes of the input
	 into eb->buf and encoding them as long as we have groups of
	 three.  */
      while (eb->buflen + (end - p) >= 3)
	{
	  memcpy (eb->buf + eb->buflen, p, 3 - eb->buflen);
	  p += (3 - eb->buflen);
	  encode_group (eb->buf, group);
	  svn_string_appendbytes (encoded, group, 4);
	  eb->buflen = 0;
	  eb->linelen += 4;
	  if (eb->linelen == BASE64_LINELEN)
	    {
	      svn_string_appendcstr (encoded, "\n");
	      eb->linelen = 0;
	    }
	}

      /* Tack any extra input onto eb->buf.  */
      memcpy (eb->buf + eb->buflen, p, end - p);
      eb->buflen += (end - p);
    }

  /* Write the output, clean up, go home.  */
  enclen = encoded->len;
  if (enclen != 0)
    err = eb->output (eb->output_baton, encoded->data, &enclen, pool);
  apr_destroy_pool (subpool);
  if (*len == 0)
    {
      apr_destroy_pool (eb->pool);
      if (err == SVN_NO_ERROR)
	err = eb->output (eb->output_baton, NULL, len, pool);
    }
  return err;
}


void
svn_base64_encode (svn_write_fn_t *output, void *output_baton,
		   apr_pool_t *pool,
		   svn_write_fn_t **encode, void **encode_baton)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  struct encode_baton *eb = apr_palloc (subpool, sizeof (*eb));

  eb->output = output;
  eb->output_baton = output_baton;
  eb->buflen = 0;
  eb->linelen = 0;
  eb->pool = subpool;
  *encode = encode_data;
  *encode_baton = eb;
}



/* Base64-encoded input --> binary output */

struct decode_baton {
  svn_write_fn_t *output;
  void *output_baton;
  unsigned char buf[4];         /* Bytes waiting to be decoded */
  int buflen;                   /* Number of bytes waiting */
  svn_boolean_t done;		/* True if we already saw an '=' */
  apr_pool_t *pool;
};


/* Base64-decode a group.  IN needs to have four bytes and OUT needs
   to have room for three bytes.  The input bytes must already have
   been decoded from base64tab into the range 0..63.  The four
   six-byte values are pasted together to form three eight-bit bytes.  */
static APR_INLINE void
decode_group (const unsigned char *in, unsigned char *out)
{
  out[0] = (in[0] << 2) | (in[1] >> 4);
  out[1] = ((in[1] & 0xf) << 4) | (in[2] >> 2);
  out[2] = ((in[2] & 0x3) << 6) | in[3];
}


static svn_error_t *
decode_data (void *baton, const char *data, apr_size_t *len, apr_pool_t *pool)
{
  struct decode_baton *db = baton;
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_string_t *decoded = svn_string_create ("", subpool);
  const char *p = data;
  const char *end = data + *len;
  const char *find;
  unsigned char group[3];
  apr_size_t declen;
  svn_error_t *err = SVN_NO_ERROR;

  if (*len == 0)
    {
      err = db->output (db->output_baton, NULL, len, pool);
      apr_destroy_pool (subpool);
      apr_destroy_pool (db->pool);
      return err;
    }

  /* Keep adding characters to the input group (in db->buf) and
     decoding groups whenever we hit 4.  If there is a partial group
     at the end, an '=' will mark it.  If the input is invalid, just
     spit out something plausible; we're not too worried about
     nonconformant base64 encoders.  */
  while (p < end && !db->done)
    {
      if (*p == '=')
	{
	  /* We are at the end and have to decode a partial group.  */
	  if (db->buflen >= 2)
	    {
	      memset (db->buf + db->buflen, 0, 4 - db->buflen);
	      decode_group (db->buf, group);
	      svn_string_appendbytes (decoded, group, db->buflen - 1);
	    }
	  db->done = TRUE;
	}
      else
	{
	  find = strchr (base64tab, *p);
	  if (find != NULL)
	    db->buf[db->buflen++] = find - base64tab;
	  if (db->buflen == 4)
	    {
	      decode_group (db->buf, group);
	      svn_string_appendbytes (decoded, group, 3);
	      db->buflen = 0;
	    }
	}
      p++;
    }

  /* Write the output, clean up, go home.  */
  declen = decoded->len;
  if (declen != 0)
    err = db->output (db->output_baton, decoded->data, &declen, pool);
  apr_destroy_pool (subpool);
  return err;
}


void
svn_base64_decode (svn_write_fn_t *output, void *output_baton,
		   apr_pool_t *pool,
		   svn_write_fn_t **decode, void **decode_baton)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  struct decode_baton *db = apr_palloc (subpool, sizeof (*db));

  db->output = output;
  db->output_baton = output_baton;
  db->buflen = 0;
  db->done = FALSE;
  db->pool = subpool;
  *decode = decode_data;
  *decode_baton = db;
}



/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
