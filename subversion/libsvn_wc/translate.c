/*
 * translate.c :  wc-specific eol/keyword substitution
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
  svn_subst_eol_style_t style;
  const char *eol;
  svn_subst_keywords_t *keywords;
  
  SVN_ERR (svn_wc__get_eol_style (&style, &eol, vfile, adm_access, pool));
  SVN_ERR (svn_wc__get_keywords (&keywords, vfile, adm_access, NULL, pool));

  if ((style == svn_subst_eol_style_none) && (! keywords))
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
          (0, NULL,
           "svn_wc_translated_file: unable to close '%s'", tmp_vfile);
      
      if (style == svn_subst_eol_style_fixed)
        {
          SVN_ERR (svn_subst_copy_and_translate (vfile,
                                                 tmp_vfile,
                                                 eol,
                                                 TRUE,
                                                 keywords,
                                                 FALSE,
                                                 pool));
        }
      else if (style == svn_subst_eol_style_native)
        {
          SVN_ERR (svn_subst_copy_and_translate (vfile,
                                                 tmp_vfile,
                                                 SVN_WC__DEFAULT_EOL_MARKER,
                                                 force_repair,
                                                 keywords,
                                                 FALSE,
                                                 pool));
        }
      else if (style == svn_subst_eol_style_none)
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
            (SVN_ERR_IO_INCONSISTENT_EOL, NULL,
             "svn_wc_translated_file: '%s' has unknown eol style property",
             vfile);
        }

      *xlated_p = tmp_vfile;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__get_eol_style (svn_subst_eol_style_t *style,
                       const char **eol,
                       const char *path,
                       svn_wc_adm_access_t *adm_access,
                       apr_pool_t *pool)
{
  const svn_string_t *propval;

  /* Get the property value. */
  SVN_ERR (svn_wc_prop_get (&propval, SVN_PROP_EOL_STYLE, path, adm_access,
                            pool));

  /* Convert it. */
  svn_subst_eol_style_from_value (style, eol, propval ? propval->data : NULL);

  return SVN_NO_ERROR;
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


svn_error_t *
svn_wc__get_keywords (svn_subst_keywords_t **keywords,
                      const char *path,
                      svn_wc_adm_access_t *adm_access,
                      const char *force_list,
                      apr_pool_t *pool)
{
  const char *list;
  svn_subst_keywords_t tmp_keywords = { 0 };
  const svn_wc_entry_t *entry = NULL;

  /* Start by assuming no keywords. */
  *keywords = NULL;

  /* Choose a property list to parse:  either the one that came into
     this function, or the one attached to PATH. */
  if (force_list == NULL)
    {
      const svn_string_t *propval;

      SVN_ERR (svn_wc_prop_get (&propval, SVN_PROP_KEYWORDS, path, adm_access,
                                pool));
      
      list = propval ? propval->data : NULL;
    }
  else
    list = force_list;

  /* The easy answer. */
  if (list == NULL)
    return SVN_NO_ERROR;

  SVN_ERR (svn_wc_entry (&entry, path, adm_access, FALSE, pool));

  SVN_ERR (svn_subst_build_keywords (&tmp_keywords,
                                     list,
                                     apr_psprintf (pool, "%" SVN_REVNUM_T_FMT,
                                                   entry->revision),
                                     entry->url,
                                     entry->cmt_date,
                                     entry->cmt_author,
                                     pool));

  *keywords = apr_pmemdup (pool, &tmp_keywords, sizeof (tmp_keywords));
      
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__maybe_set_executable (svn_boolean_t *did_set,
                              const char *path,
                              svn_wc_adm_access_t *adm_access,
                              apr_pool_t *pool)
{
  const svn_string_t *propval;
  SVN_ERR (svn_wc_prop_get (&propval, SVN_PROP_EXECUTABLE, path, adm_access,
                            pool));

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
