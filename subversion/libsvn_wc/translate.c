/*
 * translate.c :  wc-specific eol/keyword substitution
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
#include "svn_subst.h"
#include "svn_io.h"
#include "svn_hash.h"
#include "svn_wc.h"

#include "wc.h"
#include "adm_files.h"
#include "translate.h"


svn_error_t *
svn_wc_translated_file (const char **xlated_p,
                        const char *vfile,
                        svn_wc_adm_access_t *adm_access,
                        svn_boolean_t force_repair,
                        apr_pool_t *pool)
{
  enum svn_wc__eol_style style;
  const char *eol;
  svn_subst_keywords_t *keywords;
  
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

      svn_path_split (vfile, &tmp_dir, &tmp_vfile, pool);
      
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
          (0, 0, NULL,
           "svn_wc_translated_file: unable to close %s", tmp_vfile);
      
      if (style == svn_wc__eol_style_fixed)
        {
          SVN_ERR (svn_subst_copy_and_translate (vfile,
                                                 tmp_vfile,
                                                 eol,
                                                 TRUE,
                                                 keywords,
                                                 FALSE,
                                                 pool));
        }
      else if (style == svn_wc__eol_style_native)
        {
          SVN_ERR (svn_subst_copy_and_translate (vfile,
                                                 tmp_vfile,
                                                 SVN_WC__DEFAULT_EOL_MARKER,
                                                 force_repair,
                                                 keywords,
                                                 FALSE,
                                                 pool));
        }
      else if (style == svn_wc__eol_style_none)
        {
          SVN_ERR (svn_subst_copy_and_translate (vfile,
                                                 tmp_vfile,
                                                 NULL,
                                                 force_repair,
                                                 keywords,
                                                 FALSE,
                                                 pool));
        }
      else
        {
          return svn_error_createf
            (SVN_ERR_IO_INCONSISTENT_EOL, 0, NULL,
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
      /* property doesn't exist. */
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
expand_keyword (svn_subst_keywords_t *keywords,
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
          (svn_time_to_human_cstring (entry->cmt_date, pool), pool);
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
svn_wc__get_keywords (svn_subst_keywords_t **keywords,
                      const char *path,
                      svn_wc_adm_access_t *adm_access,
                      const char *force_list,
                      apr_pool_t *pool)
{
  const char *list;
  int offset = 0;
  svn_stringbuf_t *found_word;
  svn_subst_keywords_t tmp_keywords = { 0 };
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
