/*
 * subst.c :  generic eol/keyword substitution routines
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#include <string.h>
#include <assert.h>
#include <apr_general.h>  /* for strcasecmp() */
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include <apr_lib.h>
#include "svn_types.h"
#include "svn_delta.h"
#include "svn_string.h"
#include "svn_time.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "svn_io.h"
#include "svn_hash.h"
#include "svn_subst.h"



/*** Helpers for svn_subst_translate_stream ***/


/* Write out LEN bytes of BUF into STREAM, using POOL to allocate any
   svn_error_t errors that might occur along the way. */
static svn_error_t *
translate_write (svn_stream_t *stream,
                 const void *buf,
                 apr_size_t len)
{
  apr_size_t wrote = len;
  svn_error_t *write_err = svn_stream_write (stream, buf, &wrote);
  if ((write_err) || (len != wrote))
    return write_err;

  return SVN_NO_ERROR;
}


/* Perform the substition of VALUE into keyword string BUF (with len
   *LEN), given a pre-parsed KEYWORD (and KEYWORD_LEN), and updating
   *LEN to the new size of the substituted result.  Return TRUE if all
   goes well, FALSE otherwise.  If VALUE is NULL, keyword will be
   contracted, else it will be expanded.  */
static svn_boolean_t
translate_keyword_subst (char *buf,
                         apr_size_t *len,
                         const char *keyword,
                         apr_size_t keyword_len,
                         const svn_string_t *value)
{
  char *buf_ptr;

  /* Make sure we gotz good stuffs. */
  assert (*len <= SVN_KEYWORD_MAX_LEN);
  assert ((buf[0] == '$') && (buf[*len - 1] == '$'));

  /* Need at least a keyword and two $'s. */
  if (*len < keyword_len + 2)
    return FALSE;

  /* The keyword needs to match what we're looking for. */
  if (strncmp (buf + 1, keyword, keyword_len))
    return FALSE;

  buf_ptr = buf + 1 + keyword_len;

  /* Check for unexpanded keyword. */
  if (buf_ptr[0] == '$')
    {
      /* unexpanded... */
      if (value)
        {
          /* ...so expand. */
          buf_ptr[0] = ':';
          buf_ptr[1] = ' ';
          if (value->len)
            {
              apr_size_t vallen = value->len;

              /* "$keyword: value $" */
              if (vallen > (SVN_KEYWORD_MAX_LEN - 5))
                vallen = SVN_KEYWORD_MAX_LEN - 5;
              strncpy (buf_ptr + 2, value->data, vallen);
              buf_ptr[2 + vallen] = ' ';
              buf_ptr[2 + vallen + 1] = '$';
              *len = 5 + keyword_len + vallen;
            }
          else
            {
              /* "$keyword: $"  */
              buf_ptr[2] = '$';
              *len = 4 + keyword_len;
            }
        }
      else
        {
          /* ...but do nothing. */
        }
      return TRUE;
    }

  /* Check for expanded keyword. */
  else if ((*len >= 4 + keyword_len ) /* holds at least "$keyword: $" */
           && (buf_ptr[0] == ':')     /* first char after keyword is ':' */
           && (buf_ptr[1] == ' ')     /* second char after keyword is ' ' */
           && (buf[*len - 2] == ' ')) /* has ' ' for next to last character */
    {
      /* expanded... */
      if (! value)
        {
          /* ...so unexpand. */
          buf_ptr[0] = '$';
          *len = 2 + keyword_len;
        }
      else
        {
          /* ...so re-expand. */
          buf_ptr[0] = ':';
          buf_ptr[1] = ' ';
          if (value->len)
            {
              apr_size_t vallen = value->len;

              /* "$keyword: value $" */
              if (vallen > (SVN_KEYWORD_MAX_LEN - 5))
                vallen = SVN_KEYWORD_MAX_LEN - 5;
              strncpy (buf_ptr + 2, value->data, vallen);
              buf_ptr[2 + vallen] = ' ';
              buf_ptr[2 + vallen + 1] = '$';
              *len = 5 + keyword_len + vallen;
            }
          else
            {
              /* "$keyword: $"  */
              buf_ptr[2] = '$';
              *len = 4 + keyword_len;
            }
        }
      return TRUE;
    }
  
  return FALSE;
}                         

/* Parse BUF (whose length is *LEN) for Subversion keywords.  If a
   keyword is found, optionally perform the substitution on it in
   place, update *LEN with the new length of the translated keyword
   string, and return TRUE.  If this buffer doesn't contain a known
   keyword pattern, leave BUF and *LEN untouched and return FALSE.

   See the docstring for svn_subst_copy_and_translate for how the
   EXPAND and KEYWORDS parameters work.

   NOTE: It is assumed that BUF has been allocated to be at least
   SVN_KEYWORD_MAX_LEN bytes longs, and that the data in BUF is less
   than or equal SVN_KEYWORD_MAX_LEN in length.  Also, any expansions
   which would result in a keyword string which is greater than
   SVN_KEYWORD_MAX_LEN will have their values truncated in such a way
   that the resultant keyword string is still valid (begins with
   "$Keyword:", ends in " $" and is SVN_KEYWORD_MAX_LEN bytes long).  */
static svn_boolean_t
translate_keyword (char *buf,
                   apr_size_t *len,
                   svn_boolean_t expand,
                   const svn_subst_keywords_t *keywords)
{
  /* Make sure we gotz good stuffs. */
  assert (*len <= SVN_KEYWORD_MAX_LEN);
  assert ((buf[0] == '$') && (buf[*len - 1] == '$'));

  /* Early return for ignored keywords */
  if (! keywords)
    return FALSE;

  /* Revision */
  if (keywords->revision)
    {
      if (translate_keyword_subst (buf, len,
                                   SVN_KEYWORD_REVISION_LONG,
                                   (sizeof (SVN_KEYWORD_REVISION_LONG)) - 1,
                                   expand ? keywords->revision : NULL))
        return TRUE;

      if (translate_keyword_subst (buf, len,
                                   SVN_KEYWORD_REVISION_SHORT,
                                   (sizeof (SVN_KEYWORD_REVISION_SHORT)) - 1,
                                   expand ? keywords->revision : NULL))
        return TRUE;
    }

  /* Date */
  if (keywords->date)
    {
      if (translate_keyword_subst (buf, len,
                                   SVN_KEYWORD_DATE_LONG,
                                   (sizeof (SVN_KEYWORD_DATE_LONG)) - 1,
                                   expand ? keywords->date : NULL))
        return TRUE;

      if (translate_keyword_subst (buf, len,
                                   SVN_KEYWORD_DATE_SHORT,
                                   (sizeof (SVN_KEYWORD_DATE_SHORT)) - 1,
                                   expand ? keywords->date : NULL))
        return TRUE;
    }

  /* Author */
  if (keywords->author)
    {
      if (translate_keyword_subst (buf, len,
                                   SVN_KEYWORD_AUTHOR_LONG,
                                   (sizeof (SVN_KEYWORD_AUTHOR_LONG)) - 1,
                                   expand ? keywords->author : NULL))
        return TRUE;

      if (translate_keyword_subst (buf, len,
                                   SVN_KEYWORD_AUTHOR_SHORT,
                                   (sizeof (SVN_KEYWORD_AUTHOR_SHORT)) - 1,
                                   expand ? keywords->author : NULL))
        return TRUE;
    }

  /* URL */
  if (keywords->url)
    {
      if (translate_keyword_subst (buf, len,
                                   SVN_KEYWORD_URL_LONG,
                                   (sizeof (SVN_KEYWORD_URL_LONG)) - 1,
                                   expand ? keywords->url : NULL))
        return TRUE;

      if (translate_keyword_subst (buf, len,
                                   SVN_KEYWORD_URL_SHORT,
                                   (sizeof (SVN_KEYWORD_URL_SHORT)) - 1,
                                   expand ? keywords->url : NULL))
        return TRUE;
    }

  /* Id */
  if (keywords->id)
    {
      if (translate_keyword_subst (buf, len,
                                   SVN_KEYWORD_ID,
                                   (sizeof (SVN_KEYWORD_ID)) - 1,
                                   expand ? keywords->id : NULL))
        return TRUE;
    }

  /* No translations were successful.  Return FALSE. */
  return FALSE;
}


/* Translate NEWLINE_BUF (length of NEWLINE_LEN) to the newline format
   specified in EOL_STR (length of EOL_STR_LEN), and write the
   translated thing to FILE (whose path is DST_PATH).  

   SRC_FORMAT (length *SRC_FORMAT_LEN) is a cache of the first newline
   found while processing SRC_PATH.  If the current newline is not the
   same style as that of SRC_FORMAT, look to the REPAIR parameter.  If
   REPAIR is TRUE, ignore the inconsistency, else return an
   SVN_ERR_IO_INCONSISTENT_EOL error.  If we are examining the first
   newline in the file, copy it to {SRC_FORMAT, *SRC_FORMAT_LEN} to
   use for later consistency checks.  

   Use POOL to allocate errors that may occur. */
static svn_error_t *
translate_newline (const char *eol_str,
                   apr_size_t eol_str_len,
                   char *src_format,
                   apr_size_t *src_format_len,
                   char *newline_buf,
                   apr_size_t newline_len,
                   svn_stream_t *dst,
                   svn_boolean_t repair)
{
  /* If this is the first newline we've seen, cache it
     future comparisons, else compare it with our cache to
     check for consistency. */
  if (*src_format_len)
    {
      /* Comparing with cache.  If we are inconsistent and
         we are NOT repairing the file, generate an error! */
      if ((! repair) &&
          ((*src_format_len != newline_len) ||
           (strncmp (src_format, newline_buf, newline_len)))) 
        return svn_error_create
          (SVN_ERR_IO_INCONSISTENT_EOL, 0, NULL,
           "inconsistent line-endings in source stream, repair flag is off.");
    }
  else
    {
      /* This is our first line ending, so cache it before
         handling it. */
      strncpy (src_format, newline_buf, newline_len);
      *src_format_len = newline_len;
    }
  /* Translate the newline */
  return translate_write (dst, eol_str, eol_str_len);
}



/*** Public interfaces. ***/

svn_boolean_t
svn_subst_keywords_differ (const svn_subst_keywords_t *a,
                           const svn_subst_keywords_t *b,
                           svn_boolean_t compare_values)
{
  if (((a == NULL) && (b == NULL)) /* no A or B */
      /* no A, and B has no contents */
      || ((a == NULL) 
          && (b->revision == NULL)
          && (b->date == NULL)
          && (b->author == NULL)
          && (b->url == NULL))
      /* no B, and A has no contents */
      || ((b == NULL)           && (a->revision == NULL)
          && (a->date == NULL)
          && (a->author == NULL)
          && (a->url == NULL))
      /* neither A nor B has any contents */
      || ((a != NULL) && (b != NULL) 
          && (b->revision == NULL)
          && (b->date == NULL)
          && (b->author == NULL)
          && (b->url == NULL)
          && (a->revision == NULL)
          && (a->date == NULL)
          && (a->author == NULL)
          && (a->url == NULL)))
    {
      return FALSE;
    }
  else if ((a == NULL) || (b == NULL))
    return TRUE;
  
  /* Else both A and B have some keywords. */
  
  if ((! a->revision) != (! b->revision))
    return TRUE;
  else if ((compare_values && (a->revision != NULL))
           && (strcmp (a->revision->data, b->revision->data) != 0))
    return TRUE;
    
  if ((! a->date) != (! b->date))
    return TRUE;
  else if ((compare_values && (a->date != NULL))
           && (strcmp (a->date->data, b->date->data) != 0))
    return TRUE;
    
  if ((! a->author) != (! b->author))
    return TRUE;
  else if ((compare_values && (a->author != NULL))
           && (strcmp (a->author->data, b->author->data) != 0))
    return TRUE;
  
  if ((! a->url) != (! b->url))
    return TRUE;
  else if ((compare_values && (a->url != NULL))
           && (strcmp (a->url->data, b->url->data) != 0))
    return TRUE;
  
  /* Else we never found a difference, so they must be the same. */  
  
  return FALSE;
}


/* ### for docstring:  a translation request MUST be set! */
svn_error_t *
svn_subst_translate_stream (svn_stream_t *s, /* src stream */
                            svn_stream_t *d, /* dst stream */
                            const char *eol_str,
                            svn_boolean_t repair,
                            const svn_subst_keywords_t *keywords,
                            svn_boolean_t expand)
{
  svn_error_t *err = SVN_NO_ERROR;
  svn_error_t *read_err, *close_err;
  char c;
  apr_size_t len, readlen;
  apr_size_t eol_str_len = eol_str ? strlen (eol_str) : 0;
  char       newline_buf[2] = { 0 };
  apr_size_t newline_off = 0;
  char       keyword_buf[SVN_KEYWORD_MAX_LEN] = { 0 };
  apr_size_t keyword_off = 0;
  char       src_format[2] = { 0 };
  apr_size_t src_format_len = 0;

  /* The docstring requires that *some* translation be requested. */
  assert (eol_str || keywords);

  /* Copy bytes till the cows come home (or until one of them breaks a
     leg, at which point you should trot out to the range with your
     trusty sidearm, put her down, and consider steak dinners for the
     next two weeks). */
  while (err == SVN_NO_ERROR)
    {
      /* Read a byte from SRC */
      readlen = 1;
      read_err = svn_stream_read (s, &c, &readlen);

      if (read_err)
        {
          /* Oops, something bad happened. */
          err = read_err;
          goto cleanup;
        }
      else if (readlen < 1)
        {
          /* A short read means we've reached the end of the stream.
             Close up shop.  This means flushing our temporary
             streams.  Since we shouldn't have data in *both*
             temporary streams, order doesn't matter here.  However,
             the newline buffer will need to be translated.  */
          if (newline_off)
            {
              if ((err = translate_newline (eol_str, eol_str_len, 
                                            src_format, &src_format_len,
                                            newline_buf, newline_off,
                                            d, repair)))
                goto cleanup;
            }
          if (((len = keyword_off)) && 
              ((err = translate_write (d, keyword_buf, len))))
            goto cleanup;
          
          /* Close the source and destination streams. */
          close_err = svn_stream_close (s);
          if (close_err)
            goto cleanup;

          close_err = svn_stream_close (d);
          if (close_err)
            goto cleanup;
          
          /* All done, all streams closed, all is well, and all that. */
          return SVN_NO_ERROR;
        }
    

      /* Handle the byte. */
      switch (c)
        {
        case '$':
          /* A-ha!  A keyword delimiter!  */

          /* If we are currently collecting up a possible newline
             string, this puts an end to that collection.  Flush the
             newline buffer (translating as necessary) and move
             along. */
          if (newline_off)
            {
              if ((err = translate_newline (eol_str, eol_str_len, 
                                            src_format, &src_format_len,
                                            newline_buf, newline_off,
                                            d, repair)))
                goto cleanup;
              newline_off = 0;
            }

          /* If we aren't paying attention to keywords, just skip the
             rest of this stuff. */
          if (! keywords)
            {
              if ((err = translate_write (d, (const void *)&c, 1)))
                goto cleanup;
              break;
            }

          /* Put this character into the keyword buffer. */
          keyword_buf[keyword_off++] = c;

          /* If this `$' is the beginning of a possible keyword, we're
             done with it for now.  */
          if (keyword_off == 1)
            break;

          /* Else, it must be the end of one!  Attempt to translate
             the buffer. */
          len = keyword_off;
          if (translate_keyword (keyword_buf, &len, expand, keywords))
            {
              /* We successfully found and translated a keyword.  We
                 can write out this buffer now. */
              if ((err = translate_write (d, keyword_buf, len)))
                goto cleanup;
              keyword_off = 0;
            }
          else
            {
              /* No keyword was found here.  We'll let our
                 "terminating `$'" become a "beginning `$'" now.  That
                 means, write out all the keyword buffer (except for
                 this `$') and reset it to hold only this `$'.  */
              if ((err = translate_write (d, keyword_buf, keyword_off - 1)))
                goto cleanup;
              keyword_buf[0] = c;
              keyword_off = 1;
            }
          break;

        case '\n':
        case '\r':
          /* Newline character.  If we currently bagging up a keyword
             string, this pretty much puts an end to that.  Flush the
             keyword buffer, then handle the newline. */
          if ((len = keyword_off))
            {
              if ((err = translate_write (d, keyword_buf, len)))
                goto cleanup;
              keyword_off = 0;
            }

          if (! eol_str)
            {
              /* Not doing newline translation...just write out the char. */
              if ((err = translate_write (d, (const void *)&c, 1)))
                goto cleanup;
              break;
            }

          /* If we aren't yet tracking the development of a newline
             string, begin so now. */
          if (! newline_off)
            {
              newline_buf[newline_off++] = c;
              break;
            }
          else
            {
              /* We're already tracking a newline string, so let's see
                 if this is part of the same newline, or the start of
                 a new one. */
              char c0 = newline_buf[0];

              if ((c0 == c) || ((c0 == '\n') && (c == '\r')))
                {
                  /* The first '\n' (or '\r') is the newline... */
                  if ((err = translate_newline (eol_str, eol_str_len, 
                                                src_format, &src_format_len,
                                                newline_buf, 1,
                                                d, repair)))
                    goto cleanup;

                  /* ...the second '\n' (or '\r') is at least part of our next
                     newline. */
                  newline_buf[0] = c;
                  newline_off = 1;
                }
              else 
                {
                  /* '\r\n' is our newline */
                  newline_buf[newline_off++] = c;
                  if ((err = translate_newline (eol_str, eol_str_len, 
                                                src_format, &src_format_len,
                                                newline_buf, 2,
                                                d, repair)))
                    goto cleanup;
                  newline_off = 0;
                }
            }
          break;

        default:
          /* If we're currently bagging up a keyword string, we'll
             add this character to the keyword buffer.  */
          if (keyword_off)
            {
              keyword_buf[keyword_off++] = c;
              
              /* If we've reached the end of this buffer without
                 finding a terminating '$', we just flush the buffer
                 and continue on. */
              if (keyword_off >= SVN_KEYWORD_MAX_LEN)
                {
                  if ((err = translate_write (d, keyword_buf, keyword_off)))
                    goto cleanup;
                  keyword_off = 0;
                }
              break;
            }

          /* If we're in a potential newline separator, this character
             terminates that search, so we need to flush our newline
             buffer (translating as necessary) and then output this
             character.  */
          if (newline_off)
            {
              if ((err = translate_newline (eol_str, eol_str_len, 
                                            src_format, &src_format_len,
                                            newline_buf, newline_off,
                                            d, repair)))
                goto cleanup;
              newline_off = 0;
            }

          /* Write out this character. */
          if ((err = translate_write (d, (const void *)&c, 1)))
            goto cleanup;
          break; 

        } /* switch (c) */
    }

  return SVN_NO_ERROR;

 cleanup:
  return err;
}


svn_error_t *
svn_subst_translate_cstring (const char *src,
                             const char **dst,
                             const char *eol_str,
                             svn_boolean_t repair,
                             const svn_subst_keywords_t *keywords,
                             svn_boolean_t expand,
                             apr_pool_t *pool)
{
  svn_stringbuf_t *src_stringbuf, *dst_stringbuf;
  svn_stream_t *src_stream, *dst_stream;
  svn_error_t *err;

  src_stringbuf = svn_stringbuf_create (src, pool);
  
  /* The easy way out:  no translation needed, just copy. */
  if (! (eol_str || keywords))
    {
      dst_stringbuf = svn_stringbuf_dup (src_stringbuf, pool);
      goto all_good;
    }

  /* Convert our stringbufs into streams. */
  src_stream = svn_stream_from_stringbuf (src_stringbuf, pool);
  dst_stringbuf = svn_stringbuf_create ("", pool);
  dst_stream = svn_stream_from_stringbuf (dst_stringbuf, pool);

  /* Translate src stream into dst stream. */
  err = svn_subst_translate_stream (src_stream, dst_stream,
                                    eol_str, repair, keywords, expand);
  if (err)
    {
      svn_stream_close (src_stream);
      svn_stream_close (dst_stream);      
      return 
        svn_error_create (err->apr_err, 0, err,
                          "stringbuf translation failed");
    }

  /* clean up nicely. */
  SVN_ERR (svn_stream_close (src_stream));
  SVN_ERR (svn_stream_close (dst_stream));

 all_good:
  *dst = dst_stringbuf->data;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_subst_copy_and_translate (const char *src,
                              const char *dst,
                              const char *eol_str,
                              svn_boolean_t repair,
                              const svn_subst_keywords_t *keywords,
                              svn_boolean_t expand,
                              apr_pool_t *pool)
{
  const char *src_native, *dst_native;
  const char *dst_tmp;
  apr_status_t apr_err;
  svn_stream_t *src_stream, *dst_stream;
  apr_file_t *s = NULL, *d = NULL;  /* init to null important for APR */
  svn_error_t *err, *err2;

  /* The easy way out:  no translation needed, just copy. */
  if (! (eol_str || keywords))
    return svn_io_copy_file (src, dst, FALSE, pool);

  /* Else, translate.  For atomicity, we translate to a tmp file and
     then rename the tmp file over the real destination. */

  SVN_ERR (svn_utf_cstring_from_utf8 (&src_native, src, pool));
  SVN_ERR (svn_utf_cstring_from_utf8 (&dst_native, dst, pool));

  SVN_ERR (svn_io_open_unique_file (&d, &dst_tmp, dst_native,
                                    ".tmp", FALSE, pool));

  /* Open source file. */
  SVN_ERR (svn_io_file_open (&s, src_native, APR_READ | APR_BUFFERED,
                             APR_OS_DEFAULT, pool));

  /* Now convert our two open files into streams. */
  src_stream = svn_stream_from_aprfile (s, pool);
  dst_stream = svn_stream_from_aprfile (d, pool);

  /* Translate src stream into dst stream. */
  err = svn_subst_translate_stream (src_stream, dst_stream,
                                    eol_str, repair, keywords, expand);

  if (err)
    {
      /* ignore closure errors if we're bailing. */
      svn_stream_close (src_stream);
      svn_stream_close (dst_stream);      
      if (s)
        apr_file_close (s);
      if (d)
        apr_file_close (d);      

      err2 = svn_io_remove_file (dst_tmp, pool);      
      if (err2)
        svn_error_clear (err2);
      return 
        svn_error_createf (err->apr_err, 0, err,
                           "file translation failed when copying '%s' to '%s'",
                           src_native, dst_native);
    }

  /* clean up nicely. */
  SVN_ERR (svn_stream_close (src_stream));
  SVN_ERR (svn_stream_close (dst_stream));

  apr_err = apr_file_close(s);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL,
                              "error closing %s", src_native);

  apr_err = apr_file_close(d);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL,
                              "error closing %s", dst_native);

  /* Now that dst_tmp contains the translated data, do the atomic rename. */
  SVN_ERR (svn_io_file_rename (dst_tmp, dst_native, pool));

  return SVN_NO_ERROR;
}



svn_error_t *
svn_subst_translate_string (svn_string_t **new_value,
                            const svn_string_t *value,
                            const char *encoding,
                            apr_pool_t *pool)
{
  const char *val_utf8;
  const char *val_utf8_lf;
  apr_xlate_t *xlator = NULL;

  if (value == NULL)
    {
      *new_value = NULL;
      return SVN_NO_ERROR;
    }

  if (encoding)
    {
      apr_status_t apr_err =  
        apr_xlate_open (&xlator, "UTF-8", encoding, pool);
      if (apr_err != APR_SUCCESS)
        return svn_error_create (apr_err, 0, NULL,
                                 "failed to create a converter to UTF-8");
    }

  SVN_ERR (svn_utf_cstring_to_utf8 (&val_utf8, value->data, xlator, pool));
  SVN_ERR (svn_subst_translate_cstring (val_utf8,
                                        &val_utf8_lf,
                                        "\n",  /* translate to LF */
                                        FALSE, /* no repair */
                                        NULL,  /* no keywords */
                                        FALSE, /* no expansion */
                                        pool));
  
  *new_value = svn_string_create (val_utf8_lf, pool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_subst_detranslate_string (svn_string_t **new_value,
                              const svn_string_t *value,
                              apr_pool_t *pool)
{
  svn_error_t *err;
  const char *val_nlocale;
  const char *val_nlocale_neol;

  if (value == NULL)
    {
      *new_value = NULL;
      return SVN_NO_ERROR;
    }

  err = svn_utf_cstring_from_utf8 (&val_nlocale, value->data, pool);
  if (err && (APR_STATUS_IS_EINVAL (err->apr_err)))
    val_nlocale = svn_utf_cstring_from_utf8_fuzzy (value->data, pool);
  else if (err)
    return err;

  SVN_ERR (svn_subst_translate_cstring (val_nlocale,
                                        &val_nlocale_neol,
                                        APR_EOL_STR,  /* 'native' eol */
                                        FALSE, /* no repair */
                                        NULL,  /* no keywords */
                                        FALSE, /* no expansion */
                                        pool));
  
  *new_value = svn_string_create (val_nlocale_neol, pool);

  return SVN_NO_ERROR;
}
