/*
 * translate.c :  eol substitution and keyword expansion
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
#include "svn_io.h"
#include "svn_hash.h"
#include "svn_wc.h"

#include "wc.h"
#include "adm_files.h"
#include "translate.h"



/*** Helpers for svn_wc_copy_and_translate ***/
/* #define this to turn on translation */

#define SVN_TRANSLATE


#ifdef SVN_TRANSLATE
/* Return an SVN error for status ERR, using VERB and PATH to describe
   the error, and allocating the svn_error_t in POOL.  */
static svn_error_t *
translate_err (apr_status_t err, 
               const char *verb, 
               const char *path,
               apr_pool_t *pool)
{
  return svn_error_createf 
    (err, 0, NULL, pool,
     "svn_wc_copy_and_translate: error %s `%s'", verb, path);
}

/* Write out LEN bytes of BUF into FILE (whose path is PATH), using
   POOL to allocate any svn_error_t errors that might occur along the
   way. */
static svn_error_t *
translate_write (apr_file_t *file,
                 const char *path,
                 const void *buf,
                 apr_size_t len,
                 apr_pool_t *pool)
{
  apr_size_t wrote = len;
  apr_status_t write_err = apr_file_write (file, buf, &wrote);
  if ((write_err) || (len != wrote))
    return translate_err (write_err, "writing", path, pool);

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

   See the docstring for svn_wc_copy_and_translate for how the EXPAND
   and KEYWORDS parameters work.

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
                   const svn_wc_keywords_t *keywords)
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
                   const char *src_path,
                   const char *dst_path,
                   apr_file_t *dst,
                   svn_boolean_t repair,
                   apr_pool_t *pool)
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
          (SVN_ERR_IO_INCONSISTENT_EOL, 0, NULL, pool, src_path);
    }
  else
    {
      /* This is our first line ending, so cache it before
         handling it. */
      strncpy (src_format, newline_buf, newline_len);
      *src_format_len = newline_len;
    }
  /* Translate the newline */
  return translate_write (dst, dst_path, eol_str, eol_str_len, pool);
}
#endif /* SVN_TRANSLATE */



/*** Public interfaces. ***/

svn_boolean_t
svn_wc_keywords_differ (const svn_wc_keywords_t *a,
                        const svn_wc_keywords_t *b,
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
      || ((b == NULL) 
          && (a->revision == NULL)
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


svn_error_t *
svn_wc_copy_and_translate (const char *src,
                           const char *dst,
                           const char *eol_str,
                           svn_boolean_t repair,
                           const svn_wc_keywords_t *keywords,
                           svn_boolean_t expand,
                           apr_pool_t *pool)
{
#ifndef SVN_TRANSLATE
  return svn_io_copy_file (src, dst, FALSE, pool);
#else /* ! SVN_TRANSLATE */
  apr_file_t *s = NULL, *d = NULL;  /* init to null important for APR */
  apr_status_t apr_err;
  svn_error_t *err = SVN_NO_ERROR;
  apr_status_t read_err;
  char c;
  apr_size_t len;
  apr_size_t eol_str_len = eol_str ? strlen (eol_str) : 0;
  char       newline_buf[2] = { 0 };
  apr_size_t newline_off = 0;
  char       keyword_buf[SVN_KEYWORD_MAX_LEN] = { 0 };
  apr_size_t keyword_off = 0;
  char       src_format[2] = { 0 };
  apr_size_t src_format_len = 0;

  if (! (eol_str || keywords))
    return svn_io_copy_file (src, dst, FALSE, pool);

  /* Open source file. */
  err = svn_io_file_open (&s, src, APR_READ | APR_BUFFERED,
                          APR_OS_DEFAULT, pool);
  if (err)
    return svn_error_quick_wrap (err, "opening source file");
  
  /* Open dest file. */
  err
    = svn_io_file_open (&d, dst,
                        APR_WRITE | APR_CREATE | APR_TRUNCATE | APR_BUFFERED,
                        APR_OS_DEFAULT, pool);
  if (err)
    {
      apr_file_close (s); /* toss */
      return svn_error_quick_wrap (err, "opening dest file");
    }

  /*** Any errors after this point require us to close the two files and
       remove DST. */
  
  /* Copy bytes till the cows come home (or until one of them breaks a
     leg, at which point you should trot out to the range with your
     trusty sidearm, put her down, and consider steak dinners for the
     next two weeks). */
  while (err == SVN_NO_ERROR)
    {
      /* Read a byte from SRC */
      read_err = apr_file_getc (&c, s);

      /* Check for read errors.  The docstring for apr_file_read
         states that we cannot *both* read bytes AND get an error
         while doing so (include APR_EOF).  Since apr_file_getc is simply a
         wrapper around apr_file_read, we know that if we get any
         error at all, we haven't read any bytes.  */
      if (read_err)
        {
          if (!APR_STATUS_IS_EOF(read_err))
            {
              /* This was some error other than EOF! */
              err = translate_err (read_err, "reading", src, pool);
              goto cleanup;
            }
          else
            {
              /* We've reached the end of the file.  Close up shop.
                 This means flushing our temporary streams.  Since we
                 shouldn't have data in *both* temporary streams,
                 order doesn't matter here.  However, the newline
                 buffer will need to be translated.  */
              if (newline_off)
                {
                  if ((err = translate_newline (eol_str, eol_str_len, 
                                                src_format, &src_format_len,
                                                newline_buf, newline_off,
                                                src, dst, d, repair, pool)))
                    goto cleanup;
                }
              if (((len = keyword_off)) && 
                  ((err = translate_write (d, dst, keyword_buf, len, pool))))
                goto cleanup;

              /* Close the source and destination files. */
              apr_err = apr_file_close (s);
              if (apr_err)
                {
                  s = NULL;
                  err = translate_err (apr_err, "closing", src, pool);
                  goto cleanup;
                }
              apr_err = apr_file_close (d);
              if (apr_err)
                {
                  d = NULL;
                  err = translate_err (apr_err, "closing", dst, pool);
                  goto cleanup;
                }

              /* All done, all files closed, all is well, and all that. */
              return SVN_NO_ERROR;
            }
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
                                            src, dst, d, repair, pool)))
                goto cleanup;
              newline_off = 0;
            }

          /* If we aren't paying attention to keywords, just skip the
             rest of this stuff. */
          if (! keywords)
            {
              if ((err = translate_write (d, dst, (const void *)&c, 1, pool)))
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
              if ((err = translate_write (d, dst, keyword_buf, len, pool)))
                goto cleanup;
              keyword_off = 0;
            }
          else
            {
              /* No keyword was found here.  We'll let our
                 "terminating `$'" become a "beginning `$'" now.  That
                 means, write out all the keyword buffer (except for
                 this `$') and reset it to hold only this `$'.  */
              if ((err = translate_write (d, dst, keyword_buf, 
                                          keyword_off - 1, pool)))
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
              if ((err = translate_write (d, dst, keyword_buf, len, pool)))
                goto cleanup;
              keyword_off = 0;
            }

          if (! eol_str)
            {
              /* Not doing newline translation...just write out the char. */
              if ((err = translate_write (d, dst, (const void *)&c, 1, pool)))
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
                                                src, dst, d, repair, pool)))
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
                                                src, dst, d, repair, pool)))
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
                  if ((err = translate_write (d, dst, keyword_buf, 
                                              keyword_off, pool)))
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
                                            src, dst, d, repair, pool)))
                goto cleanup;
              newline_off = 0;
            }

          /* Write out this character. */
          if ((err = translate_write (d, dst, (const void *)&c, 1, pool)))
            goto cleanup;
          break; 

        } /* switch (c) */
    }
  return SVN_NO_ERROR;

 cleanup:
  if (s)
    apr_file_close (s); /* toss error */
  if (d)
    apr_file_close (d); /* toss error */
  svn_io_remove_file (dst, pool); /* toss error */
  return err;
#endif /* ! SVN_TRANSLATE */
}


svn_error_t *
svn_wc_translated_file (const char **xlated_p,
                        const char *vfile,
                        svn_wc_adm_access_t *adm_access,
                        apr_pool_t *pool)
{
  enum svn_wc__eol_style style;
  const char *eol;
  svn_wc_keywords_t *keywords;
  
  SVN_ERR (svn_wc__get_eol_style (&style, &eol, vfile, pool));
  SVN_ERR (svn_wc__get_keywords (&keywords, vfile, adm_access, NULL, pool));

  if ((style == svn_wc__eol_style_none) && (! keywords))
    {
      /* Translation would be a no-op, so return the original file. */
      *xlated_p = vfile;
    }
  else  /* some translation is necessary */
    {
      const char *tmp_dir, *tmp_vfile;
      apr_status_t apr_err;
      apr_file_t *ignored;

      /* First, reserve a tmp file name. */

      svn_path_split_nts (vfile, &tmp_dir, &tmp_vfile, pool);
      
      tmp_vfile = svn_wc__adm_path (tmp_dir, 1, pool,
                                    tmp_vfile, NULL);
      
      SVN_ERR (svn_io_open_unique_file (&ignored,
                                        &tmp_vfile,
                                        tmp_vfile,
                                        SVN_WC__TMP_EXT,
                                        FALSE,
                                        pool));
      
      /* We were just reserving the name and don't actually need the
         filehandle, so close immediately. */
      apr_err = apr_file_close (ignored);
      if (apr_err)
        return svn_error_createf
          (0, 0, NULL, pool,
           "svn_wc_translated_file: unable to close %s", tmp_vfile);
      
      if (style == svn_wc__eol_style_fixed)
        {
          SVN_ERR (svn_wc_copy_and_translate (vfile,
                                              tmp_vfile,
                                              eol,
                                              TRUE,
                                              keywords,
                                              FALSE,
                                              pool));
        }
      else if (style == svn_wc__eol_style_native)
        {
          SVN_ERR (svn_wc_copy_and_translate (vfile,
                                              tmp_vfile,
                                              SVN_WC__DEFAULT_EOL_MARKER,
                                              FALSE,
                                              keywords,
                                              FALSE,
                                              pool));
        }
      else if (style == svn_wc__eol_style_none)
        {
          SVN_ERR (svn_wc_copy_and_translate (vfile,
                                              tmp_vfile,
                                              NULL,
                                              FALSE,
                                              keywords,
                                              FALSE,
                                              pool));
        }
      else
        {
          return svn_error_createf
            (SVN_ERR_IO_INCONSISTENT_EOL, 0, NULL, pool,
             "svn_wc_translated_file: %s has unknown eol style property",
             vfile);
        }

      *xlated_p = tmp_vfile;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__get_eol_style (enum svn_wc__eol_style *style,
                       const char **eol,
                       const char *path,
                       apr_pool_t *pool)
{
  const svn_string_t *propval;

  /* Get the property value. */
  SVN_ERR (svn_wc_prop_get (&propval, SVN_PROP_EOL_STYLE, path, pool));

  /* Convert it. */
  svn_wc__eol_style_from_value (style, eol, propval ? propval->data : NULL);

  return SVN_NO_ERROR;
}


void 
svn_wc__eol_style_from_value (enum svn_wc__eol_style *style,
                              const char **eol,
                              const char *value)
{
  if (value == NULL)
    {
      /* property dosen't exist. */
      *eol = NULL;
      if (style)
        *style = svn_wc__eol_style_none;
    }
  else if (! strcmp ("native", value))
    {
      *eol = APR_EOL_STR;       /* whee, a portability library! */
      if (style)
        *style = svn_wc__eol_style_native;
    }
  else if (! strcmp ("LF", value))
    {
      *eol = "\n";
      if (style)
        *style = svn_wc__eol_style_fixed;
    }
  else if (! strcmp ("CR", value))
    {
      *eol = "\r";
      if (style)
        *style = svn_wc__eol_style_fixed;
    }
  else if (! strcmp ("CRLF", value))
    {
      *eol = "\r\n";
      if (style)
        *style = svn_wc__eol_style_fixed;
    }
  else
    {
      *eol = NULL;
      if (style)
        *style = svn_wc__eol_style_unknown;
    }
}


void
svn_wc__eol_value_from_string (const char **value, const char *eol)
{
  if (eol == NULL)
    *value = NULL;
  else if (! strcmp ("\n", eol))
    *value = "LF";
  else if (! strcmp ("\r", eol))
    *value = "CR";
  else if (! strcmp ("\r\n", eol))
    *value = "CRLF";
  else
    *value = NULL;
}


/* Return time T as a string in the form "YYYY-MM-DD HH:MM:SSZ",
   allocated in POOL.  The "Z" at the end is a literal 'Z', to
   indicate UTC. */
static const char *
time_to_keyword_time (apr_time_t t, apr_pool_t *pool)
{
  const char *t_cstr;
  apr_time_exp_t exploded_time;

  /* We toss apr_status_t return value here -- for one thing, caller
     should pass in good information.  But also, where APR's own code
     calls these functions it tosses the return values, and
     furthermore their current implementations can only return success
     anyway. */

  /* We get the date in GMT now -- and expect the tm_gmtoff and
     tm_isdst to be not set. We also ignore the weekday and yearday,
     since those are not needed. */

  apr_time_exp_gmt (&exploded_time, t);

  /* It would be nice to use apr_strftime(), but APR doesn't give a
     way to convert back, so we wouldn't be able to share the format
     string between the writer and reader. */
  t_cstr = apr_psprintf (pool, "%04d-%02d-%02d %02d:%02d:%02dZ",
                         exploded_time.tm_year + 1900,
                         exploded_time.tm_mon + 1,
                         exploded_time.tm_mday,
                         exploded_time.tm_hour,
                         exploded_time.tm_min,
                         exploded_time.tm_sec);

  return t_cstr;
}


/* Helper for svn_wc__get_keywords().
   
   If KEYWORD is a valid keyword, look up its value in ENTRY, fill in
   the appropriate field in KEYWORDS with that value (allocated in
   POOL), and set *IS_VALID_P to TRUE.  If the value is not available,
   use "" instead.

   If KEYWORD is not a valid keyword, set *IS_VALID_P to FALSE and
   return with no error.
*/
static svn_error_t *
expand_keyword (svn_wc_keywords_t *keywords,
                svn_boolean_t *is_valid_p,
                const char *keyword,
                const svn_wc_entry_t *entry,
                apr_pool_t *pool)
{
  *is_valid_p = TRUE;

  /* Using strcasecmp() to accept downcased short versions of
   * keywords.  Note that this doesn't apply to the strings being
   * expanded in the file -- rather, it's so users can do
   *
   *    $ svn propset svn:keywords "date url" readme.txt
   *
   * and not have to worry about capitalization in the property
   * value.
   */

  if ((! strcmp (keyword, SVN_KEYWORD_REVISION_LONG))
      || (! strcasecmp (keyword, SVN_KEYWORD_REVISION_SHORT)))
    {
      if ((entry) && (entry->cmt_rev))
        keywords->revision = svn_string_createf (pool, "%" SVN_REVNUM_T_FMT,
                                                 entry->cmt_rev);
      else
        /* We found a recognized keyword, so it needs to be expanded
           no matter what.  If the expansion value isn't available,
           we at least send back an empty string.  */
        keywords->revision = svn_string_create ("", pool);
    }
  else if ((! strcmp (keyword, SVN_KEYWORD_DATE_LONG))
           || (! strcasecmp (keyword, SVN_KEYWORD_DATE_SHORT)))
    {
      if (entry && (entry->cmt_date))
        keywords->date = svn_string_create
          (svn_time_to_human_nts (entry->cmt_date, pool), pool);
      else
        keywords->date = svn_string_create ("", pool);
    }
  else if ((! strcmp (keyword, SVN_KEYWORD_AUTHOR_LONG))
           || (! strcasecmp (keyword, SVN_KEYWORD_AUTHOR_SHORT)))
    {
      if (entry && (entry->cmt_author))
        keywords->author = svn_string_create (entry->cmt_author, pool);
      else
        keywords->author = svn_string_create ("", pool);
    }
  else if ((! strcmp (keyword, SVN_KEYWORD_URL_LONG))
           || (! strcasecmp (keyword, SVN_KEYWORD_URL_SHORT)))
    {
      if (entry && (entry->url))
        keywords->url = svn_string_create (entry->url, pool);
      else
        keywords->url = svn_string_create ("", pool);
    }
  else if ((! strcasecmp (keyword, SVN_KEYWORD_ID)))
    {
      if (entry && (entry->cmt_rev && entry->cmt_date
                    && entry->cmt_author && entry->url))
        {
          char *base_name = svn_path_basename (entry->url, pool);
          svn_string_t *rev = svn_string_createf (pool, "%" SVN_REVNUM_T_FMT,
                                                   entry->cmt_rev);
          const char *date = time_to_keyword_time (entry->cmt_date, pool);

          keywords->id = svn_string_createf (pool, "%s %s %s %s",
                                             base_name,
                                             rev->data,
                                             date,
                                             entry->cmt_author);
        }
      else
        keywords->id = svn_string_create ("", pool);
    }
  else
    *is_valid_p = FALSE;
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__get_keywords (svn_wc_keywords_t **keywords,
                      const char *path,
                      svn_wc_adm_access_t *adm_access,
                      const char *force_list,
                      apr_pool_t *pool)
{
  const char *list;
  int offset = 0;
  svn_stringbuf_t *found_word;
  svn_wc_keywords_t tmp_keywords = { 0 };
  svn_boolean_t got_one = FALSE;
  const svn_wc_entry_t *entry = NULL;

  /* Start by assuming no keywords. */
  *keywords = NULL;

  /* Choose a property list to parse:  either the one that came into
     this function, or the one attached to PATH. */
  if (force_list == NULL)
    {
      const svn_string_t *propval;

      SVN_ERR (svn_wc_prop_get (&propval, SVN_PROP_KEYWORDS, path, pool));
      
      list = propval ? propval->data : NULL;
    }
  else
    list = force_list;

  /* Now parse the list for words.  For now, this parser assumes that
     the list will contain keywords separated by whitespaces.  This
     can be made more complex later if somebody cares. */

  /* The easy answer. */
  if (list == NULL)
    return SVN_NO_ERROR;

  do 
    {
      /* Find the start of a word by skipping past whitespace. */
      while ((list[offset] != '\0') && (apr_isspace (list[offset])))
        offset++;
    
      /* Hit either a non-whitespace or NULL char. */

      if (list[offset] != '\0') /* found non-whitespace char */
        {
          svn_boolean_t is_valid;
          int word_start, word_end;
          
          word_start = offset;
          
          /* Find the end of the word by skipping non-whitespace chars */
          while ((list[offset] != '\0') && (! apr_isspace (list[offset])))
            offset++;
          
          /* Hit either a whitespace or NULL char.  Either way, it's the
             end of the word. */
          word_end = offset;
          
          /* Make a temporary copy of the word */
          found_word = svn_stringbuf_ncreate (list + word_start,
                                              (word_end - word_start),
                                              pool);
          
          /* If we haven't already read the entry in, do so now. */
          if (! entry)
             SVN_ERR (svn_wc_entry (&entry, path, adm_access, FALSE, pool));

          /* Now, try to expand the keyword. */
          SVN_ERR (expand_keyword (&tmp_keywords, &is_valid,
                                   found_word->data, entry, pool));
          if (is_valid)
            got_one = TRUE;
        }
      
    } while (list[offset] != '\0');

  if (got_one)
    {
      *keywords = apr_pmemdup (pool, &tmp_keywords, sizeof (tmp_keywords));
    }
      
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__maybe_set_executable (svn_boolean_t *did_set,
                              const char *path,
                              apr_pool_t *pool)
{
  const svn_string_t *propval;
  SVN_ERR (svn_wc_prop_get (&propval, SVN_PROP_EXECUTABLE, path, pool));

  if (propval != NULL)
    {
      SVN_ERR (svn_io_set_file_executable (path, TRUE, FALSE, pool));
      if (did_set)
        *did_set = TRUE;
    }
  else if (did_set)
    *did_set = FALSE;

  return SVN_NO_ERROR;
}




/*
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
