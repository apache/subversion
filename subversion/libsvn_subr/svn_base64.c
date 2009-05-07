/*
 * base64.c:  base64 encoding and decoding functions
 *
 * ====================================================================
 * Copyright (c) 2000-2004, 2009 CollabNet.  All rights reserved.
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



#include <string.h>

#include <apr.h>
#include <apr_pools.h>
#include <apr_general.h>        /* for APR_INLINE */

#include "svn_pools.h"
#include "svn_io.h"
#include "svn_error.h"
#include "svn_base64.h"


#define BASE64_LINELEN 76
static const char base64tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                "abcdefghijklmnopqrstuvwxyz0123456789+/";



/* Binary input --> base64-encoded output */

struct encode_baton {
  svn_stream_t *output;
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
encode_group(const unsigned char *in, char *out)
{
  out[0] = base64tab[in[0] >> 2];
  out[1] = base64tab[((in[0] & 0x3) << 4) | (in[1] >> 4)];
  out[2] = base64tab[((in[1] & 0xf) << 2) | (in[2] >> 6)];
  out[3] = base64tab[in[2] & 0x3f];
}


/* Base64-encode a byte string which may or may not be the totality of
   the data being encoded.  INBUF and *INBUFLEN carry the leftover
   data from call to call, and *LINELEN carries the length of the
   current output line.  Make INBUF have room for three characters and
   initialize *INBUFLEN and *LINELEN to 0.  Output will be appended to
   STR.  Include newlines every so often if BREAK_LINES is true. */
static void
encode_bytes(svn_stringbuf_t *str, const void *data, apr_size_t len,
             unsigned char *inbuf, int *inbuflen, int *linelen,
             svn_boolean_t break_lines)
{
  char group[4];
  const char *p = data, *end = p + len;

  /* Keep encoding three-byte groups until we run out.  */
  while (*inbuflen + (end - p) >= 3)
    {
      memcpy(inbuf + *inbuflen, p, 3 - *inbuflen);
      p += (3 - *inbuflen);
      encode_group(inbuf, group);
      svn_stringbuf_appendbytes(str, group, 4);
      *inbuflen = 0;
      *linelen += 4;
      if (break_lines && *linelen == BASE64_LINELEN)
        {
          svn_stringbuf_appendcstr(str, "\n");
          *linelen = 0;
        }
    }

  /* Tack any extra input onto *INBUF.  */
  memcpy(inbuf + *inbuflen, p, end - p);
  *inbuflen += (end - p);
}


/* Encode leftover data, if any, and possibly a final newline (if
   there has been any data and BREAK_LINES is set), appending to STR.
   LEN must be in the range 0..2.  */
static void
encode_partial_group(svn_stringbuf_t *str, const unsigned char *extra,
                     int len, int linelen, svn_boolean_t break_lines)
{
  unsigned char ingroup[3];
  char outgroup[4];

  if (len > 0)
    {
      memcpy(ingroup, extra, len);
      memset(ingroup + len, 0, 3 - len);
      encode_group(ingroup, outgroup);
      memset(outgroup + (len + 1), '=', 4 - (len + 1));
      svn_stringbuf_appendbytes(str, outgroup, 4);
      linelen += 4;
    }
  if (break_lines && linelen > 0)
    svn_stringbuf_appendcstr(str, "\n");
}


/* Write handler for svn_base64_encode.  */
static svn_error_t *
encode_data(void *baton, const char *data, apr_size_t *len)
{
  struct encode_baton *eb = baton;
  apr_pool_t *subpool = svn_pool_create(eb->pool);
  svn_stringbuf_t *encoded = svn_stringbuf_create("", subpool);
  apr_size_t enclen;
  svn_error_t *err = SVN_NO_ERROR;

  /* Encode this block of data and write it out.  */
  encode_bytes(encoded, data, *len, eb->buf, &eb->buflen, &eb->linelen, TRUE);
  enclen = encoded->len;
  if (enclen != 0)
    err = svn_stream_write(eb->output, encoded->data, &enclen);
  svn_pool_destroy(subpool);
  return err;
}


/* Close handler for svn_base64_encode().  */
static svn_error_t *
finish_encoding_data(void *baton)
{
  struct encode_baton *eb = baton;
  svn_stringbuf_t *encoded = svn_stringbuf_create("", eb->pool);
  apr_size_t enclen;
  svn_error_t *err = SVN_NO_ERROR;

  /* Encode a partial group at the end if necessary, and write it out.  */
  encode_partial_group(encoded, eb->buf, eb->buflen, eb->linelen, TRUE);
  enclen = encoded->len;
  if (enclen != 0)
    err = svn_stream_write(eb->output, encoded->data, &enclen);

  /* Pass on the close request and clean up the baton.  */
  if (err == SVN_NO_ERROR)
    err = svn_stream_close(eb->output);
  svn_pool_destroy(eb->pool);
  return err;
}


svn_stream_t *
svn_base64_encode(svn_stream_t *output, apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  struct encode_baton *eb = apr_palloc(subpool, sizeof(*eb));
  svn_stream_t *stream;

  eb->output = output;
  eb->buflen = 0;
  eb->linelen = 0;
  eb->pool = subpool;
  stream = svn_stream_create(eb, pool);
  svn_stream_set_write(stream, encode_data);
  svn_stream_set_close(stream, finish_encoding_data);
  return stream;
}


const svn_string_t *
svn_base64_encode_string2(const svn_string_t *str,
                          svn_boolean_t break_lines,
                          apr_pool_t *pool)
{
  svn_stringbuf_t *encoded = svn_stringbuf_create("", pool);
  svn_string_t *retval = apr_pcalloc(pool, sizeof(*retval));
  unsigned char ingroup[3];
  int ingrouplen = 0, linelen = 0;

  encode_bytes(encoded, str->data, str->len, ingroup, &ingrouplen, &linelen,
               break_lines);
  encode_partial_group(encoded, ingroup, ingrouplen, linelen,
                       break_lines);
  retval->data = encoded->data;
  retval->len = encoded->len;
  return retval;
}

const svn_string_t *
svn_base64_encode_string(const svn_string_t *str, apr_pool_t *pool)
{
  return svn_base64_encode_string2(str, TRUE, pool);
}



/* Base64-encoded input --> binary output */

struct decode_baton {
  svn_stream_t *output;
  unsigned char buf[4];         /* Bytes waiting to be decoded */
  int buflen;                   /* Number of bytes waiting */
  svn_boolean_t done;           /* True if we already saw an '=' */
  apr_pool_t *pool;
};


/* Base64-decode a group.  IN needs to have four bytes and OUT needs
   to have room for three bytes.  The input bytes must already have
   been decoded from base64tab into the range 0..63.  The four
   six-byte values are pasted together to form three eight-bit bytes.  */
static APR_INLINE void
decode_group(const unsigned char *in, char *out)
{
  out[0] = (in[0] << 2) | (in[1] >> 4);
  out[1] = ((in[1] & 0xf) << 4) | (in[2] >> 2);
  out[2] = ((in[2] & 0x3) << 6) | in[3];
}

/* Lookup table for base64 characters; reverse_base64[ch] gives a
   negative value if ch is not a valid base64 character, or otherwise
   the value of the byte represented; 'A' => 0 etc. */
static const signed char reverse_base64[256] = {
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
-1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
-1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

/* Decode a byte string which may or may not be the total amount of
   data being decoded.  INBUF and *INBUFLEN carry the leftover bytes
   from call to call, and *DONE keeps track of whether we've seen an
   '=' which terminates the encoded data.  Have room for four bytes in
   INBUF and initialize *INBUFLEN to 0 and *DONE to FALSE.  Output
   will be appended to STR.  */
static void
decode_bytes(svn_stringbuf_t *str, const char *data, apr_size_t len,
             unsigned char *inbuf, int *inbuflen, svn_boolean_t *done)
{
  const char *p;
  char group[3];
  signed char find;

  /* Resize the stringbuf to make room for the (approximate) size of
     output, to avoid repeated resizes later. */
  svn_stringbuf_ensure(str, (len / 4) * 3 + 3);

  for (p = data; !*done && p < data + len; p++)
    {
      if (*p == '=')
        {
          /* We are at the end and have to decode a partial group.  */
          if (*inbuflen >= 2)
            {
              memset(inbuf + *inbuflen, 0, 4 - *inbuflen);
              decode_group(inbuf, group);
              svn_stringbuf_appendbytes(str, group, *inbuflen - 1);
            }
          *done = TRUE;
        }
      else
        {
          find = reverse_base64[(unsigned char)*p];
          if (find >= 0)
            inbuf[(*inbuflen)++] = find;
          if (*inbuflen == 4)
            {
              decode_group(inbuf, group);
              svn_stringbuf_appendbytes(str, group, 3);
              *inbuflen = 0;
            }
        }
    }
}


/* Write handler for svn_base64_decode.  */
static svn_error_t *
decode_data(void *baton, const char *data, apr_size_t *len)
{
  struct decode_baton *db = baton;
  apr_pool_t *subpool;
  svn_stringbuf_t *decoded;
  apr_size_t declen;
  svn_error_t *err = SVN_NO_ERROR;

  /* Decode this block of data.  */
  subpool = svn_pool_create(db->pool);
  decoded = svn_stringbuf_create("", subpool);
  decode_bytes(decoded, data, *len, db->buf, &db->buflen, &db->done);

  /* Write the output, clean up, go home.  */
  declen = decoded->len;
  if (declen != 0)
    err = svn_stream_write(db->output, decoded->data, &declen);
  svn_pool_destroy(subpool);
  return err;
}


/* Close handler for svn_base64_decode().  */
static svn_error_t *
finish_decoding_data(void *baton)
{
  struct decode_baton *db = baton;
  svn_error_t *err;

  /* Pass on the close request and clean up the baton.  */
  err = svn_stream_close(db->output);
  svn_pool_destroy(db->pool);
  return err;
}


svn_stream_t *
svn_base64_decode(svn_stream_t *output, apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  struct decode_baton *db = apr_palloc(subpool, sizeof(*db));
  svn_stream_t *stream;

  db->output = output;
  db->buflen = 0;
  db->done = FALSE;
  db->pool = subpool;
  stream = svn_stream_create(db, pool);
  svn_stream_set_write(stream, decode_data);
  svn_stream_set_close(stream, finish_decoding_data);
  return stream;
}


const svn_string_t *
svn_base64_decode_string(const svn_string_t *str, apr_pool_t *pool)
{
  svn_stringbuf_t *decoded = svn_stringbuf_create("", pool);
  svn_string_t *retval = apr_pcalloc(pool, sizeof(*retval));
  unsigned char ingroup[4];
  int ingrouplen = 0;
  svn_boolean_t done = FALSE;

  decode_bytes(decoded, str->data, str->len, ingroup, &ingrouplen, &done);
  retval->data = decoded->data;
  retval->len = decoded->len;
  return retval;
}


/* Return a base64-encoded representation of CHECKSUM, allocated in POOL.
   If CHECKSUM->kind is not recognized, return NULL.
   ### That 'NULL' claim was in the header file when this was public, but
   doesn't look true in the implementation.

   ### This is now only used as a new implementation of svn_base64_from_md5();
   it would probably be safer to revert that to its old implementation. */
static svn_stringbuf_t *
base64_from_checksum(const svn_checksum_t *checksum, apr_pool_t *pool)
{
  svn_stringbuf_t *checksum_str;
  unsigned char ingroup[3];
  int ingrouplen = 0, linelen = 0;
  checksum_str = svn_stringbuf_create("", pool);

  encode_bytes(checksum_str, checksum->digest,
               svn_checksum_size(checksum), ingroup, &ingrouplen,
               &linelen, TRUE);
  encode_partial_group(checksum_str, ingroup, ingrouplen, linelen, TRUE);

  /* Our base64-encoding routines append a final newline if any data
     was created at all, so let's hack that off. */
  if (checksum_str->len)
    {
      checksum_str->len--;
      checksum_str->data[checksum_str->len] = 0;
    }

  return checksum_str;
}


svn_stringbuf_t *
svn_base64_from_md5(unsigned char digest[], apr_pool_t *pool)
{
  svn_checksum_t *checksum
    = svn_checksum__from_digest(digest, svn_checksum_md5, pool);

  return base64_from_checksum(checksum, pool);
}
