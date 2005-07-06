/*
 * hash.c :  dumping and reading hash tables to/from files.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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



#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_hash.h"
#include "svn_sorts.h"
#include "svn_io.h"
#include "svn_pools.h"
#include "svn_utf.h"
#include "svn_ebcdic.h"


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




/*** Dumping and loading hash files. */

/* Implements svn_hash_read2 and svn_hash_read_incremental. */
static svn_error_t *
hash_read (apr_hash_t *hash, svn_stream_t *stream, const char *terminator,
           svn_boolean_t incremental, apr_pool_t *pool)
{
  svn_stringbuf_t *buf;
  svn_boolean_t eof;
  apr_size_t len, keylen, vallen;
  char c, *end, *keybuf, *valbuf;

  while (1)
    {
      /* Read a key length line.  Might be END, though. */
      SVN_ERR (svn_stream_readline (stream, &buf, SVN_UTF8_NEWLINE_STR, &eof, pool));

      /* Check for the end of the hash. */
      if ((!terminator && eof && buf->len == 0)
          || (terminator && (strcmp (buf->data, terminator) == 0)))
        return SVN_NO_ERROR;

      /* Check for unexpected end of stream */
      if (eof)
        return svn_error_create (SVN_ERR_MALFORMED_FILE, NULL, NULL);

      if ((buf->len >= 3) && (buf->data[0] == SVN_UTF8_K) && (buf->data[1] == SVN_UTF8_SPACE))
        {
          /* Get the length of the key */
#if !APR_CHARSET_EBCDIC
          keylen = (size_t) strtoul (buf->data + 2, &end, 10);
#else
          SVN_ERR (svn_utf_stringbuf_from_utf8 (&buf, buf, pool));
          keylen = (size_t) strtoul (buf->data + 2, &end, 10);            
#endif            
          if (keylen == (size_t) ULONG_MAX || *end != '\0')
            return svn_error_create (SVN_ERR_MALFORMED_FILE, NULL, NULL);

          /* Now read that much into a buffer. */
          keybuf = apr_palloc (pool, keylen + 1);
          SVN_ERR (svn_stream_read (stream, keybuf, &keylen));
          keybuf[keylen] = '\0';

          /* Suck up extra newline after key data */
          len = 1;
          SVN_ERR (svn_stream_read (stream, &c, &len));
          if (c != SVN_UTF8_NEWLINE)
            return svn_error_create (SVN_ERR_MALFORMED_FILE, NULL, NULL);

          /* Read a val length line */
          SVN_ERR (svn_stream_readline (stream, &buf, SVN_UTF8_NEWLINE_STR, &eof, pool));

          if ((buf->data[0] == SVN_UTF8_V) && (buf->data[1] == SVN_UTF8_SPACE))
            {
#if !APR_CHARSET_EBCDIC
              vallen = (size_t) strtoul (buf->data + 2, &end, 10);
#else
              SVN_ERR (svn_utf_stringbuf_from_utf8 (&buf, buf, pool));
              vallen = (size_t) strtoul (buf->data + 2, &end, 10);                
#endif
              if (vallen == (size_t) ULONG_MAX || *end != '\0')
                return svn_error_create (SVN_ERR_MALFORMED_FILE, NULL, NULL);

              valbuf = apr_palloc (pool, vallen + 1);
              SVN_ERR (svn_stream_read (stream, valbuf, &vallen));
              valbuf[vallen] = '\0';

              /* Suck up extra newline after val data */
              len = 1;
              SVN_ERR (svn_stream_read (stream, &c, &len));
              if (c != SVN_UTF8_NEWLINE)
                return svn_error_create (SVN_ERR_MALFORMED_FILE, NULL, NULL);

              /* Add a new hash entry. */
              apr_hash_set (hash, keybuf, keylen,
                            svn_string_ncreate (valbuf, vallen, pool));
            }
          else
            return svn_error_create (SVN_ERR_MALFORMED_FILE, NULL, NULL);
        }
      else if (incremental && (buf->len >= 3)
               && (buf->data[0] == SVN_UTF8_D) && (buf->data[1] == SVN_UTF8_SPACE))
        {
          /* Get the length of the key */
#if !APR_CHARSET_EBCDIC
          keylen = (size_t) strtoul (buf->data + 2, &end, 10);
#else
          SVN_ERR (svn_utf_stringbuf_from_utf8 (&buf, buf, pool));
		  keylen = (size_t) strtoul (buf->data + 2, &end, 10);  
#endif
          if (keylen == (size_t) ULONG_MAX || *end != '\0')
            return svn_error_create (SVN_ERR_MALFORMED_FILE, NULL, NULL);

          /* Now read that much into a buffer. */
          keybuf = apr_palloc (pool, keylen + 1);
          SVN_ERR (svn_stream_read (stream, keybuf, &keylen));
          keybuf[keylen] = '\0';

          /* Suck up extra newline after key data */
          len = 1;
          SVN_ERR (svn_stream_read (stream, &c, &len));
          if (c != SVN_UTF8_NEWLINE)
            return svn_error_create (SVN_ERR_MALFORMED_FILE, NULL, NULL);

          /* Remove this hash entry. */
          apr_hash_set (hash, keybuf, keylen, NULL);
        }
      else
        return svn_error_create (SVN_ERR_MALFORMED_FILE, NULL, NULL);
    }
}


/* Implements svn_hash_write2 and svn_hash_write_incremental. */
static svn_error_t *
hash_write (apr_hash_t *hash, apr_hash_t *oldhash, svn_stream_t *stream,
            const char *terminator, apr_pool_t *pool)
{
  apr_pool_t *subpool;
  apr_size_t len;
  apr_array_header_t *list;
  int i;
  const char *klen_str, *vlen_str;

  subpool = svn_pool_create (pool);

  list = svn_sort__hash (hash, svn_sort_compare_items_lexically, pool);
  for (i = 0; i < list->nelts; i++)
    {
      svn_sort__item_t *item = &APR_ARRAY_IDX (list, i, svn_sort__item_t);
      svn_string_t *valstr = item->value;

      svn_pool_clear (subpool);

      /* Don't output entries equal to the ones in oldhash, if present. */
      if (oldhash)
        {
          svn_string_t *oldstr = apr_hash_get (oldhash, item->key, item->klen);

          if (oldstr && svn_string_compare (valstr, oldstr))
            continue;
        }
 
      /* Write it out. */
      klen_str = APR_PSPRINTF2 (subpool, "%" APR_SSIZE_T_FMT, item->klen);
      vlen_str = APR_PSPRINTF2 (subpool, "%" APR_SIZE_T_FMT, valstr->len);
      SVN_ERR (svn_stream_printf (stream, subpool,
                                  "\x4B\x20%s\x0A%s\x0A\x56\x20%s\x0A",
                                  klen_str, (const char *) item->key,
                                  vlen_str));
      len = valstr->len;
      SVN_ERR (svn_stream_write (stream, valstr->data, &len));
      SVN_ERR (svn_stream_printf (stream, subpool, "\x0A"));
    }

  if (oldhash)
    {
      /* Output a deletion entry for each property in oldhash but not hash. */
      list = svn_sort__hash (oldhash, svn_sort_compare_items_lexically,
                             pool);
      for (i = 0; i < list->nelts; i++)
        {
          svn_sort__item_t *item = &APR_ARRAY_IDX (list, i, svn_sort__item_t);

          svn_pool_clear (subpool);

          /* If it's not present in the new hash, write out a D entry. */
          if (! apr_hash_get (hash, item->key, item->klen))
          {
            klen_str = APR_PSPRINTF2 (subpool, "%" APR_SSIZE_T_FMT,
                                      item->klen);
            SVN_ERR (svn_stream_printf (stream, subpool,
                                        "\x44\x20%s\x0A%s\x0A", klen_str,
                                        (const char *) item->key));
          }
        }
    }

  if (terminator)
    SVN_ERR (svn_stream_printf (stream, subpool, "%s\x0A", terminator));

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


svn_error_t *svn_hash_read2 (apr_hash_t *hash, svn_stream_t *stream,
                             const char *terminator, apr_pool_t *pool)
{
  return hash_read (hash, stream, terminator, FALSE, pool);
}


svn_error_t *svn_hash_read_incremental (apr_hash_t *hash,
                                        svn_stream_t *stream,
                                        const char *terminator,
                                        apr_pool_t *pool)
{
  return hash_read (hash, stream, terminator, TRUE, pool);
}


svn_error_t *
svn_hash_write2 (apr_hash_t *hash, svn_stream_t *stream,
                 const char *terminator, apr_pool_t *pool)
{
  return hash_write (hash, NULL, stream, terminator, pool);
}


svn_error_t *
svn_hash_write_incremental (apr_hash_t *hash, apr_hash_t *oldhash,
                            svn_stream_t *stream, const char *terminator,
                            apr_pool_t *pool)
{
  assert (oldhash != NULL);
  return hash_write (hash, oldhash, stream, terminator, pool);
}


svn_error_t *
svn_hash_write (apr_hash_t *hash, apr_file_t *destfile, apr_pool_t *pool)
{
  return hash_write (hash, NULL, svn_stream_from_aprfile (destfile, pool),
                     SVN_HASH_TERMINATOR, pool);
}


/* There are enough quirks in the deprecated svn_hash_read that we
   should just preserve its implementation. */
svn_error_t *
svn_hash_read (apr_hash_t *hash, 
               apr_file_t *srcfile,
               apr_pool_t *pool)
{
  svn_error_t *err;
  char buf[SVN_KEYLINE_MAXLEN];
  apr_size_t num_read;
  char c;
  int first_time = 1;
  

  while (1)
    {
      /* Read a key length line.  Might be END, though. */
      apr_size_t len = sizeof(buf);

      err = svn_io_read_length_line (srcfile, buf, &len, pool);
      if (err && APR_STATUS_IS_EOF(err->apr_err) && first_time)
        {
          /* We got an EOF on our very first attempt to read, which
             means it's a zero-byte file.  No problem, just go home. */        
          svn_error_clear (err);
          return SVN_NO_ERROR;
        }
      else if (err)
        /* Any other circumstance is a genuine error. */
        return err;

      first_time = 0;

      if (((len == 3) && (buf[0] == SVN_UTF8_E) && (buf[1] == SVN_UTF8_N) && (buf[2] == SVN_UTF8_D))
          || ((len == 9)
              && (buf[0] == SVN_UTF8_P)
              && (buf[1] == SVN_UTF8_R)     /* We formerly used just "END" to */
              && (buf[2] == SVN_UTF8_O)     /* end a property hash, but later */
              && (buf[3] == SVN_UTF8_P)     /* we added "PROPS-END", so that  */
              && (buf[4] == SVN_UTF8_S)     /* the fs dump format would be    */
              && (buf[5] == SVN_UTF8_MINUS) /* more human-readable.  That's   */
              && (buf[6] == SVN_UTF8_E)     /* why we accept either way here. */
              && (buf[7] == SVN_UTF8_N)
              && (buf[8] == SVN_UTF8_D)))
        {
          /* We've reached the end of the dumped hash table, so leave. */
          return SVN_NO_ERROR;
        }
      else if ((buf[0] == SVN_UTF8_K) && (buf[1] == SVN_UTF8_SPACE))
        {
          void *keybuf;          
          /* Get the length of the key */
#if !APR_CHARSET_EBCDIC
          size_t keylen = (size_t) atoi (buf + 2);
#else
          /* Get the length of the key */
          size_t keylen;
          const char *buf_native;
          /* Copy buf and append a null terminator.  Trying to treat buf[i] as a
           * string and converting it fails since the buffer is not NULL 
           * terminated. */
          const char *buf_utf8 = apr_pstrmemdup (pool, &buf[2], 
                                                 SVN_KEYLINE_MAXLEN - 2);
          SVN_ERR (svn_utf_cstring_from_utf8 (&buf_native, buf_utf8, pool));
          keylen = (size_t) atoi (buf_native); 
#endif      
          /* Now read that much into a buffer, + 1 byte for null terminator */
          keybuf = apr_palloc (pool, keylen + 1);
          
          SVN_ERR (svn_io_file_read_full (srcfile, 
                                          keybuf, keylen, &num_read, pool));
          ((char *) keybuf)[keylen] = '\0';

          /* Suck up extra newline after key data */
          SVN_ERR (svn_io_file_getc (&c, srcfile, pool));
          if (c != SVN_UTF8_NEWLINE) 
            return svn_error_create (SVN_ERR_MALFORMED_FILE, NULL, NULL);

          /* Read a val length line */
          len = sizeof(buf);
          SVN_ERR (svn_io_read_length_line (srcfile, buf, &len, pool));

          if ((buf[0] == SVN_UTF8_V) && (buf[1] == SVN_UTF8_SPACE))
            {
              int vallen;
              void *valbuf;            	
              svn_string_t *value = apr_palloc (pool, sizeof (*value));
              
              /* Get the length of the value */
#if !APR_CHARSET_EBCDIC
              vallen = atoi (buf + 2);
#else
              const char *buf_native;
              /* Copy buf and append a null terminator.  Trying to treat buf[i]
               * as a string and converting it fails since the buffer is not
               * NULL terminated. */
              const char *buf_utf8 = apr_pstrmemdup (pool, &buf[2], 
                                                     SVN_KEYLINE_MAXLEN - 2);
              SVN_ERR (svn_utf_cstring_from_utf8 (&buf_native, buf_utf8, pool));
              vallen = atoi (buf_native);
#endif
              
              /* Again, 1 extra byte for the null termination. */
              valbuf = apr_palloc (pool, vallen + 1);
              SVN_ERR (svn_io_file_read_full (srcfile, 
                                              valbuf, vallen, 
                                              &num_read, pool));
              ((char *) valbuf)[vallen] = '\0';

              /* Suck up extra newline after val data */
              SVN_ERR (svn_io_file_getc (&c, srcfile, pool));
              if (c != SVN_UTF8_NEWLINE)
                return svn_error_create (SVN_ERR_MALFORMED_FILE, NULL, NULL);

              value->data = valbuf;
              value->len = vallen;

              /* The Grand Moment:  add a new hash entry! */
              apr_hash_set (hash, keybuf, keylen, value);
            }
          else
            {
              return svn_error_create (SVN_ERR_MALFORMED_FILE, NULL, NULL);
            }
        }
      else
        {
          return svn_error_create (SVN_ERR_MALFORMED_FILE, NULL, NULL);
        }
    } /* while (1) */
}



/*** Diffing hashes ***/

svn_error_t *
svn_hash_diff (apr_hash_t *hash_a,
               apr_hash_t *hash_b,
               svn_hash_diff_func_t diff_func,
               void *diff_func_baton,
               apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  if (hash_a)
    for (hi = apr_hash_first (pool, hash_a); hi; hi = apr_hash_next (hi))
      {
        const void *key;
        apr_ssize_t klen;
        
        apr_hash_this (hi, &key, &klen, NULL);
        
        if (hash_b && (apr_hash_get (hash_b, key, klen)))
          SVN_ERR ((*diff_func) (key, klen, svn_hash_diff_key_both,
                                 diff_func_baton));
        else
          SVN_ERR ((*diff_func) (key, klen, svn_hash_diff_key_a,
                                 diff_func_baton));
      }

  if (hash_b)
    for (hi = apr_hash_first (pool, hash_b); hi; hi = apr_hash_next (hi))
      {
        const void *key;
        apr_ssize_t klen;
        
        apr_hash_this (hi, &key, &klen, NULL);
        
        if (! (hash_a && apr_hash_get (hash_a, key, klen)))
          SVN_ERR ((*diff_func) (key, klen, svn_hash_diff_key_b,
                                 diff_func_baton));
      }

  return SVN_NO_ERROR;
}
