/*
 * adm_files.c: helper routines for handling files & dirs in the
 *              working copy administrative area (creating,
 *              deleting, opening, and closing).  This is the only
 *              code that actually knows where administrative
 *              information is kept.  
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



#include <stdarg.h>
#include <assert.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include <apr_time.h>
#include <apr_strings.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_hash.h"
#include "svn_io.h"
#include "svn_path.h"
#include "svn_wc.h"

#include "wc.h"
#include "adm_files.h"
#include "entries.h"


/*** File names in the adm area. ***/

static const char *
adm_subdir (void)
{
  /* Functionized to make it easier for us to offer alternate
     administrative area locations someday. */

  return SVN_WC_ADM_DIR_NAME;
}


/* Return the path to something in PATH's administrative area.
 * 
 * First, the adm subdir is appended to PATH as a component, then the
 * "tmp" directory is added iff USE_TMP is set, then each of the
 * varargs in AP (char *'s) is appended as a path component.  The list
 * must be terminated with a NULL argument.
 *
 * Adding an empty component results in no effect (i.e., the separator
 * char is not doubled).
 *
 * If EXTENSION is non-null, it will be appended to the final string
 * without a separator character.
 */
static const char *
v_extend_with_adm_name (const char *path,
                        const char *extension,
                        svn_boolean_t use_tmp,
                        apr_pool_t *pool,
                        va_list ap)
{
  const char *this;

  /* Tack on the administrative subdirectory. */
  path = svn_path_join (path, adm_subdir (), pool);

  /* If this is a tmp file, name it into the tmp area. */
  if (use_tmp)
    path = svn_path_join (path, SVN_WC__ADM_TMP, pool);

  /* Tack on everything else. */
  while ((this = va_arg (ap, const char *)) != NULL)
    {
      if (this[0] == '\0')
        continue;

      path = svn_path_join (path, this, pool);
    }

  if (extension)
    path = apr_pstrcat (pool, path, extension, NULL);

  return path;
}


/* See v_extend_with_adm_name() for details. */
static const char *
extend_with_adm_name (const char *path,
                      const char *extension,
                      svn_boolean_t use_tmp,
                      apr_pool_t *pool,
                      ...)
{
  va_list ap;

  va_start (ap, pool);
  path = v_extend_with_adm_name (path, extension, use_tmp, pool, ap);
  va_end (ap);

  return path;
}


const char *
svn_wc__adm_path (const char *path,
                  svn_boolean_t tmp,
                  apr_pool_t *pool, 
                  ...)
{
  va_list ap;

  va_start (ap, pool);
  path = v_extend_with_adm_name (path, NULL, tmp, pool, ap);
  va_end (ap);

  return path;
}


svn_boolean_t
svn_wc__adm_path_exists (const char *path,
                         svn_boolean_t tmp,
                         apr_pool_t *pool, 
                         ...)
{
  enum svn_node_kind kind;
  va_list ap;

  va_start (ap, pool);
  path = v_extend_with_adm_name (path, NULL, tmp, pool, ap);
  va_end (ap);

  svn_io_check_path (path, &kind, pool);
  if (kind == svn_node_none)
    return FALSE;
  else
    return TRUE;
}



/*** Making and using files in the adm area. ***/


/* Create an empty THING in the adm area with permissions set to PERMS. 
 * If TMP is non-zero, then create THING in the tmp dir.
 *
 * Does not check if THING already exists, so be careful -- THING will
 * be empty after this no matter what.
 */
svn_error_t *
svn_wc__make_adm_thing (const char *path,
                        const char *thing,
                        int type,
                        apr_fileperms_t perms,
                        svn_boolean_t tmp,
                        apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_file_t *f = NULL;
  apr_status_t apr_err = 0;

  path = extend_with_adm_name (path, NULL, tmp, pool, thing, NULL);

  if (type == svn_node_file)
    {
      apr_err = apr_file_open (&f, path,
                          (APR_WRITE | APR_CREATE | APR_EXCL),
                          perms,
                          pool);

      if (apr_err)
        err = svn_error_create (apr_err, 0, NULL, pool, path);
      else
        {
          /* Creation succeeded, so close immediately. */
          apr_err = apr_file_close (f);
          if (apr_err)
            err = svn_error_create (apr_err, 0, NULL, pool, path);
        }
    }
  else if (type == svn_node_dir)
    {
      apr_err = apr_dir_make (path, perms, pool);
      if (apr_err)
        err = svn_error_create (apr_err, 0, NULL, pool, path);
    }
  else   /* unknown type argument, wrongness */
    {
      /* We're only capturing this here because there wouldn't be a
         segfault or other obvious indicator that something went
         wrong.  Even so, not sure if it's appropriate.  Thoughts? */
      err = svn_error_create 
        (0, 0, NULL, pool, "svn_wc__make_admin_thing: bad type indicator");
    }

  return err;
}



/* Copy SRC to DST if SRC exists, else create DST empty. */
static svn_error_t *
maybe_copy_file (const char *src, const char *dst, apr_pool_t *pool)
{
  enum svn_node_kind kind;
  svn_error_t *err;
  apr_status_t apr_err;

  /* First test if SRC exists. */
  err = svn_io_check_path (src, &kind, pool);
  if (err)
    return err;
  else if (kind == svn_node_none)
    {
      /* SRC doesn't exist, create DST empty. */
      apr_file_t *f = NULL;
      apr_err = apr_file_open (&f,
                          dst,
                          (APR_WRITE | APR_CREATE),
                          APR_OS_DEFAULT,
                          pool);
      if (apr_err)
        return svn_error_create (apr_err, 0, NULL, pool, dst);
      else
        {
          apr_err = apr_file_close (f);
          if (apr_err)
            return svn_error_create (apr_err, 0, NULL, pool, dst);
          else
            return SVN_NO_ERROR;
        }
    }
  else /* SRC exists, so copy it to DST. */
    {    
      err = svn_io_copy_file (src, dst, FALSE, pool);
      if (err)
        return err;
    }

  return SVN_NO_ERROR;
}



/*** Syncing files in the adm area. ***/

static svn_error_t *
sync_adm_file (const char *path,
               const char *extension,
               apr_pool_t *pool,
               ...)
{
  /* Some code duplication with close_adm_file() seems unavoidable,
     given how C va_lists work. */

  const char *tmp_path;
  apr_status_t apr_err;
  va_list ap;
  
  /* Extend tmp name. */
  va_start (ap, pool);
  tmp_path = v_extend_with_adm_name (path, extension, 1, pool, ap);
  va_end (ap);
  
  /* Extend real name. */
  va_start (ap, pool);
  path = v_extend_with_adm_name (path, extension, 0, pool, ap);
  va_end (ap);
  
  /* Remove read-only flag on destination. */
  SVN_ERR (svn_io_set_file_read_write (path, TRUE, pool));
 
  /* Rename. */
  apr_err = apr_file_rename (tmp_path, path, pool);
  if (! apr_err)
    SVN_ERR (svn_io_set_file_read_only (path, FALSE, pool));

  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "error renaming %s to %s", tmp_path, path);
  else
    return SVN_NO_ERROR;
}


/* Rename a tmp text-base file to its real text-base name.
   The file had better already be closed. */
svn_error_t *
svn_wc__sync_text_base (const char *path, apr_pool_t *pool)
{
  const char *parent_path, *base_name;
  svn_path_split_nts (path, &parent_path, &base_name, pool);
  return sync_adm_file (parent_path,
                        SVN_WC__BASE_EXT,
                        pool,
                        SVN_WC__ADM_TEXT_BASE,
                        base_name,
                        NULL);
}

const char *
svn_wc__text_base_path (const char *path,
                        svn_boolean_t tmp,
                        apr_pool_t *pool)
{
  const char *newpath, *base_name;

  svn_path_split_nts (path, &newpath, &base_name, pool);
  return extend_with_adm_name (newpath,
                               SVN_WC__BASE_EXT,
                               tmp,
                               pool,
                               SVN_WC__ADM_TEXT_BASE,
                               base_name,
                               NULL);
}


static svn_error_t *
prop_path_internal (const char **prop_path,
                    const char *path,
                    svn_boolean_t base,
                    svn_boolean_t tmp,
                    apr_pool_t *pool)
{
  svn_error_t *err;
  enum svn_node_kind kind;
  svn_boolean_t is_wc;
  const char *entry_name;

  err = svn_io_check_path (path, &kind, pool);
  if (err)
    return err;

  /* kff todo: some factorization can be done on most callers of
     svn_wc_check_wc()? */

  is_wc = FALSE;
  entry_name = NULL;
  if (kind == svn_node_dir)
    {
      err = svn_wc_check_wc (path, &is_wc, pool);
      if (err)
        return err;
    }
  
  if (is_wc)  /* It's not only a dir, it's a working copy dir */
    {
      *prop_path = extend_with_adm_name
        (path,
         NULL,
         tmp,
         pool,
         base ? SVN_WC__ADM_DIR_PROP_BASE : SVN_WC__ADM_DIR_PROPS,
         NULL);
    }
  else  /* It's either a file, or a non-wc dir (i.e., maybe an ex-file) */
    {
      svn_path_split_nts (path, prop_path, &entry_name, pool);
      if (svn_path_is_empty_nts (*prop_path))
        *prop_path = ".";

      err = svn_wc_check_wc (*prop_path, &is_wc, pool);
      if (err)
        return err;
      else if (! is_wc)
        return svn_error_createf
          (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, pool,
           "svn_wc__prop_path: %s is not a working copy directory",
           *prop_path);

      *prop_path = extend_with_adm_name
        (*prop_path,
         base ? SVN_WC__BASE_EXT : NULL,
         tmp,
         pool,
         base ? SVN_WC__ADM_PROP_BASE : SVN_WC__ADM_PROPS,
         entry_name,
         NULL);
    }

  return SVN_NO_ERROR;
}



/* Return a path to the 'wcprop' file for PATH, possibly in TMP area.  */
svn_error_t *
svn_wc__wcprop_path (const char **wcprop_path,
                     const char *path,
                     svn_boolean_t tmp,
                     apr_pool_t *pool)
{
  svn_error_t *err;
  enum svn_node_kind kind;
  svn_boolean_t is_wc;
  const char *entry_name;

  err = svn_io_check_path (path, &kind, pool);
  if (err)
    return err;

  /* kff todo: some factorization can be done on most callers of
     svn_wc_check_wc()? */

  is_wc = FALSE;
  entry_name = NULL;
  if (kind == svn_node_dir)
    {
      err = svn_wc_check_wc (path, &is_wc, pool);
      if (err)
        return err;
    }

  if (is_wc)  /* It's not only a dir, it's a working copy dir */
    {
      *wcprop_path = extend_with_adm_name (path,
                                           NULL,
                                           tmp,
                                           pool,
                                           SVN_WC__ADM_DIR_WCPROPS,
                                           NULL);
    }
  else  /* It's either a file, or a non-wc dir (i.e., maybe an ex-file) */
    {
      svn_path_split_nts (path, wcprop_path, &entry_name, pool);
 
      err = svn_wc_check_wc (*wcprop_path, &is_wc, pool);
      if (err)
        return err;
      else if (! is_wc)
        return svn_error_createf
          (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, pool,
           "wcprop_path: %s is not a working copy directory", *wcprop_path);

      *wcprop_path = extend_with_adm_name (*wcprop_path,
                                           NULL,
                                           tmp,
                                           pool,
                                           SVN_WC__ADM_WCPROPS,
                                           entry_name,
                                           NULL);
    }

  return SVN_NO_ERROR;
}




svn_error_t *
svn_wc__prop_path (const char **prop_path,
                   const char *path,
                   svn_boolean_t tmp,
                   apr_pool_t *pool)
{
  return prop_path_internal (prop_path, path, FALSE, tmp, pool);
}


svn_error_t *
svn_wc__prop_base_path (const char **prop_path,
                        const char *path,
                        svn_boolean_t tmp,
                        apr_pool_t *pool)
{
  return prop_path_internal (prop_path, path, TRUE, tmp, pool);
}




/*** Opening and closing files in the adm area. ***/

/* Open a file somewhere in the adm area for directory PATH.
 * First, the adm subdir is appended as a path component, then each of
 * the varargs (they are char *'s) is appended as a path component,
 * and the resulting file opened.  
 *
 * If FLAGS indicates writing, then the file is opened in the adm tmp
 * area, whence it must be renamed, either by passing the sync flag to
 * close_adm_file() or with an explicit call to sync_adm_file().
 */
static svn_error_t *
open_adm_file (apr_file_t **handle,
               const char *path,
               const char *extension,
               apr_int32_t flags,
               apr_pool_t *pool,
               ...)
{
  svn_error_t *err = NULL;
  apr_status_t apr_err = 0;
  va_list ap;

  /* If we're writing, always do it to a tmp file. */
  if (flags & APR_WRITE)
    {
      if (flags & APR_APPEND)
        {
          const char *opath, *tmp_path;

          va_start (ap, pool);
          opath = v_extend_with_adm_name (path, extension, 0, pool, ap);
          va_end (ap);

          va_start (ap, pool);
          tmp_path = v_extend_with_adm_name (path, extension, 1, pool, ap);
          va_end (ap);

          /* Copy the original thing to the tmp location. */
          err = maybe_copy_file (opath, tmp_path, pool);
          if (err)
            return err;
        }

      /* Extend with tmp name. */
      va_start (ap, pool);
      path = v_extend_with_adm_name (path, extension, 1, pool, ap);
      va_end (ap);
    }
  else
    {
      /* Extend with regular adm name. */
      va_start (ap, pool);
      path = v_extend_with_adm_name (path, extension, 0, pool, ap);
      va_end (ap);
    }

  apr_err = apr_file_open (handle, path, flags, APR_OS_DEFAULT, pool);
  if (apr_err)
    {
      /* Oddly enough, APR will set *HANDLE even if the open failed.
         You'll get a filehandle whose descriptor is -1.  There must
         be a reason this is useful... Anyway, we don't want the
         handle. */
      *handle = NULL;
      err = svn_error_create (apr_err, 0, NULL, pool, path);
    }

  return err;
}


/* Close the file indicated by FP (PATH is passed to make error
 * reporting better).  If SYNC is non-zero, then the file will be
 * sync'd from the adm tmp area to its permanent location, otherwise
 * it will remain in the tmp area.  See open_adm_file().
 */
static svn_error_t *
close_adm_file (apr_file_t *fp,
                const char *path,
                const char *extension,
                svn_boolean_t sync,
                apr_pool_t *pool,
                ...)
{
  apr_status_t apr_err = 0;
  const char *tmp_path;
  va_list ap;

  /* Get the full name of the thing we're closing. */
  va_start (ap, pool);
  tmp_path = v_extend_with_adm_name (path, extension, sync, pool, ap);
  va_end (ap);

  apr_err = apr_file_close (fp);

  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, pool, tmp_path);

  /* If we're syncing a tmp file, it needs to be renamed after closing. */
  if (sync)
    {
      /* Some code duplication with sync_adm_file() seems unavoidable,
         given how C va_lists work. */

      /* Obtain dest name. */
      va_start (ap, pool);
      path = v_extend_with_adm_name (path, extension, 0, pool, ap);
      va_end (ap);
      
      /* Temporarily remove read-only flag on destination. */
      SVN_ERR (svn_io_set_file_read_write (path, TRUE, pool));
      
      /* Rename. */
      apr_err = apr_file_rename (tmp_path, path, pool);
      if (! apr_err)
        SVN_ERR (svn_io_set_file_read_only (path, FALSE, pool));
      
      if (apr_err)
        return svn_error_createf (apr_err, 0, NULL, pool,
                                  "error renaming %s to %s", tmp_path, path);
      else
        return SVN_NO_ERROR;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__open_adm_file (apr_file_t **handle,
                       const char *path,
                       const char *fname,
                       apr_int32_t flags,
                       apr_pool_t *pool)
{
  return open_adm_file (handle, path, NULL, flags, pool, fname, NULL);
}


svn_error_t *
svn_wc__close_adm_file (apr_file_t *fp,
                        const char *path,
                        const char *fname,
                        int sync,
                        apr_pool_t *pool)
{
  return close_adm_file (fp, path, NULL, sync, pool, fname, NULL);
}


svn_error_t *
svn_wc__remove_adm_file (const char *path, apr_pool_t *pool, ...)
{
  svn_error_t *err = NULL;
  apr_status_t apr_err = 0;
  va_list ap;

  va_start (ap, pool);
  path = v_extend_with_adm_name (path, NULL, 0, pool, ap);
  va_end (ap);
      
  /* Remove read-only flag on path. */
  SVN_ERR(svn_io_set_file_read_write (path, FALSE, pool));

  apr_err = apr_file_remove (path, pool);
  if (apr_err)
    err = svn_error_create (apr_err, 0, NULL, pool, path);

  return err;
}


const char *
svn_wc__empty_file_path (const char *path,
                         apr_pool_t *pool)
{
  const char *parent_path = svn_path_remove_component_nts (path, pool);
  return extend_with_adm_name (parent_path, NULL, 0, pool,
                               SVN_WC__ADM_EMPTY_FILE, NULL);
}


svn_error_t *
svn_wc__open_empty_file (apr_file_t **handle,
                         const char *path,
                         apr_pool_t *pool)
{
  const char *parent_path = svn_path_remove_component_nts (path, pool);
  return open_adm_file (handle, parent_path, NULL, APR_READ, pool,
                        SVN_WC__ADM_EMPTY_FILE, NULL);
}


svn_error_t *
svn_wc__close_empty_file (apr_file_t *fp,
                          const char *path,
                          apr_pool_t *pool)
{
  const char *parent_path = svn_path_remove_component_nts (path, pool);
  return close_adm_file (fp, parent_path, NULL, 0, pool,
                         SVN_WC__ADM_EMPTY_FILE, NULL);
}


svn_error_t *
svn_wc__open_text_base (apr_file_t **handle,
                        const char *path,
                        apr_int32_t flags,
                        apr_pool_t *pool)
{
  const char *parent_path, *base_name;
  svn_path_split_nts (path, &parent_path, &base_name, pool);
  return open_adm_file (handle, parent_path, SVN_WC__BASE_EXT, flags, pool,
                        SVN_WC__ADM_TEXT_BASE, base_name, NULL);
}


svn_error_t *
svn_wc__close_text_base (apr_file_t *fp,
                         const char *path,
                         int write,
                         apr_pool_t *pool)
{
  const char *parent_path, *base_name;
  svn_path_split_nts (path, &parent_path, &base_name, pool);
  return close_adm_file (fp, parent_path, SVN_WC__BASE_EXT, write, pool,
                         SVN_WC__ADM_TEXT_BASE, base_name, NULL);
}


svn_error_t *
svn_wc__open_auth_file (apr_file_t **handle,
                        const char *path,
                        const char *auth_filename,
                        apr_int32_t flags,
                        apr_pool_t *pool)
{
  return open_adm_file (handle, path, NULL, flags, pool,
                        SVN_WC__ADM_AUTH_DIR, auth_filename, NULL);
}


svn_error_t *
svn_wc__close_auth_file (apr_file_t *handle,
                         const char *path,
                         const char *file,
                         int sync,
                         apr_pool_t *pool)
{
  return close_adm_file (handle, path, NULL, sync, pool,
                         SVN_WC__ADM_AUTH_DIR, file, NULL);
}

svn_error_t *
svn_wc__open_props (apr_file_t **handle,
                    const char *path,
                    apr_int32_t flags,
                    svn_boolean_t base,
                    svn_boolean_t wcprops,
                    apr_pool_t *pool)
{
  const char *parent_dir, *base_name;
  enum svn_node_kind kind;

  /* Check if path is a file or a dir. */
  SVN_ERR (svn_io_check_path (path, &kind, pool));

  /* If file, split the path. */
  if (kind == svn_node_file)
    svn_path_split_nts (path, &parent_dir, &base_name, pool);
  else    
    parent_dir = path;
  
  /* At this point, we know we need to open a file in the admin area
     of parent_dir.  Examine the flags to know -which- kind of prop
     file to get -- there are three types! */

  if (base && wcprops)
    return svn_error_create (SVN_ERR_WC_PATH_NOT_FOUND, 0, NULL, pool,
                             "open_props: no such thing as 'base' wcprops!");

  else if (base)
    {
      if (kind == svn_node_dir)
        return open_adm_file (handle, parent_dir, NULL, flags, pool,
                              SVN_WC__ADM_DIR_PROP_BASE, NULL);
      else
        return open_adm_file (handle, parent_dir, SVN_WC__BASE_EXT, flags,
                              pool, SVN_WC__ADM_PROP_BASE, base_name,
                              NULL);
    }
  else if (wcprops)
    {
      if (kind == svn_node_dir)
        return open_adm_file (handle, parent_dir, NULL, flags, pool,
                              SVN_WC__ADM_DIR_WCPROPS, NULL);
      else
        return open_adm_file (handle, parent_dir, NULL, flags,
                              pool, SVN_WC__ADM_WCPROPS, base_name,
                              NULL);
    }
  else /* plain old property file */
    {
      if (kind == svn_node_dir)
        return open_adm_file (handle, parent_dir, NULL, flags, pool,
                              SVN_WC__ADM_DIR_PROPS, NULL);
      else
        return open_adm_file (handle, parent_dir, NULL, flags,
                              pool, SVN_WC__ADM_PROPS, base_name,
                              NULL);
    }
}



svn_error_t *
svn_wc__close_props (apr_file_t *fp,
                     const char *path,
                     svn_boolean_t base,
                     svn_boolean_t wcprops,
                     int sync,
                     apr_pool_t *pool)
{
  const char *parent_dir, *base_name;
  enum svn_node_kind kind;

  /* Check if path is a file or a dir. */
  SVN_ERR (svn_io_check_path (path, &kind, pool));

  /* If file, split the path. */
  if (kind == svn_node_file)
    svn_path_split_nts (path, &parent_dir, &base_name, pool);
  else    
    parent_dir = path;
  
  /* At this point, we know we need to open a file in the admin area
     of parent_dir.  Examine the flags to know -which- kind of prop
     file to get -- there are three types! */

  if (base && wcprops)
    return svn_error_create (SVN_ERR_WC_PATH_NOT_FOUND, 0, NULL, pool,
                             "close_props: no such thing as 'base' wcprops!");

  else if (base)
    {
      if (kind == svn_node_dir)
        return close_adm_file (fp, parent_dir, NULL, sync, pool,
                               SVN_WC__ADM_DIR_PROP_BASE, NULL);
      else
        return close_adm_file (fp, parent_dir, SVN_WC__BASE_EXT, sync, pool,
                               SVN_WC__ADM_PROP_BASE, base_name, NULL);
    }
  else if (wcprops)
    {
      if (kind == svn_node_dir)
        return close_adm_file (fp, parent_dir, NULL, sync, pool,
                               SVN_WC__ADM_DIR_WCPROPS, NULL);
      else
        return close_adm_file (fp, parent_dir, NULL, sync, pool,
                               SVN_WC__ADM_WCPROPS, base_name, NULL);
    }
  else /* plain old property file */
    {
      if (kind == svn_node_dir)
        return close_adm_file (fp, parent_dir, NULL, sync, pool,
                               SVN_WC__ADM_DIR_PROPS, NULL);
      else
        return close_adm_file (fp, parent_dir, NULL, sync, pool,
                                 SVN_WC__ADM_PROPS, base_name, NULL);
    }

}



svn_error_t *
svn_wc__sync_props (const char *path,
                    svn_boolean_t base,
                    svn_boolean_t wcprops,
                    apr_pool_t *pool)
{
  const char *parent_dir, *base_name;
  enum svn_node_kind kind;

  /* Check if path is a file or a dir. */
  SVN_ERR (svn_io_check_path (path, &kind, pool));

  /* If file, split the path. */
  if (kind == svn_node_file)
    svn_path_split_nts (path, &parent_dir, &base_name, pool);
  else    
    parent_dir = path;
  
  /* At this point, we know we need to open a file in the admin area
     of parent_dir.  Examine the flags to know -which- kind of prop
     file to get -- there are three types! */

  if (base && wcprops)
    return svn_error_create (SVN_ERR_WC_PATH_NOT_FOUND, 0, NULL, pool,
                             "close_props: no such thing as 'base' wcprops!");

  else if (base)
    {
      if (kind == svn_node_dir)
        return sync_adm_file (parent_dir, NULL, pool,
                              SVN_WC__ADM_DIR_PROP_BASE, NULL);
      else
        return sync_adm_file (parent_dir, SVN_WC__BASE_EXT, pool,
                              SVN_WC__ADM_PROP_BASE, base_name, NULL);
    }
  else if (wcprops)
    {
      if (kind == svn_node_dir)
        return sync_adm_file (parent_dir, NULL, pool,
                              SVN_WC__ADM_DIR_WCPROPS, NULL);
      else
        return sync_adm_file (parent_dir, SVN_WC__BASE_EXT, pool,
                              SVN_WC__ADM_WCPROPS, base_name, NULL);
    }
  else /* plain old property file */
    {
      if (kind == svn_node_dir)
        return sync_adm_file (parent_dir, NULL, pool,
                              SVN_WC__ADM_DIR_PROPS, NULL);
      else
        return sync_adm_file (parent_dir, NULL, pool,
                              SVN_WC__ADM_PROPS, base_name, NULL);
    }

}




/*** Checking for and creating administrative subdirs. ***/

/* Set *EXISTS to non-zero iff there's an adm area for PATH, and it
 * matches URL and REVISION.
 * 
 * If an error occurs, just return the error and don't touch *EXISTS.
 */
static svn_error_t *
check_adm_exists (svn_boolean_t *exists,
                  const char *path,
                  const char *url,
                  svn_revnum_t revision,
                  apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  enum svn_node_kind kind;
  svn_boolean_t dir_exists = FALSE, wc_exists = FALSE;
  apr_file_t *f = NULL;
  const char *tmp_path;

  /** Step 1: check that the directory exists. **/

  tmp_path = extend_with_adm_name (path, NULL, 0, pool, NULL);

  err = svn_io_check_path (tmp_path, &kind, pool);
  if (!err)
    {
      if (kind != svn_node_none && kind != svn_node_dir)
        {
          /* If got an error other than dir non-existence, then
             something's weird and we should return a genuine error. */
          err = svn_error_create (APR_ENOTDIR, 0, NULL, pool, tmp_path);
        }
      else if (kind == svn_node_none)
        {
          dir_exists = FALSE;
        }
      else                      /* must be a dir. */
        {
          assert (kind == svn_node_dir);
          dir_exists = TRUE;
        }
    }

  /** Step 1.  If no adm directory, then we're done. */
  if (err)
    return err;
  else if (! dir_exists)
    {
      *exists = FALSE;
      return SVN_NO_ERROR;
    }

  /** The directory exists, but is it a valid working copy yet?
      Try step 2: checking that SVN_WC__ADM_README exists. **/

  err = svn_wc__open_adm_file (&f, path, SVN_WC__ADM_README, APR_READ, pool);
  if (err && !APR_STATUS_IS_EEXIST(err->apr_err))
    return err;
  else if (err)
    {
      svn_error_clear_all (err);
      wc_exists = FALSE;
    }
  else
    wc_exists = TRUE;

  SVN_ERR (svn_wc__close_adm_file (f, path, SVN_WC__ADM_README, 0, pool));

  /** Step 3: now check that repos and ancestry are correct **/

  if (wc_exists)
    {
      svn_wc_entry_t *entry;

      SVN_ERR (svn_wc_entry (&entry, path, FALSE, pool));
      if (!entry)
        return svn_error_createf (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool,
                                  "no entry for '%s'", path);

      /* The revisions must match except when adding a directory with a
         name that matches a directory scheduled for deletion. That's
         because the deleted directory's administrative dir will still be
         in place but will have an arbitrary revision. */
      if (entry->revision != revision
          && !(entry->schedule == svn_wc_schedule_delete && revision == 0))
        return
          svn_error_createf (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, pool,
                             "revison %" SVN_REVNUM_T_FMT
                             " doesn't match existing revision %"
                             SVN_REVNUM_T_FMT " in '%s'",
                             revision, entry->revision, path);

      /** ### comparing URLs, should they be canonicalized first? */
      if (strcmp (entry->url, url) != 0)
        return
          svn_error_createf
          (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, pool,
           "URL '%s' doesn't match existing URL '%s' in '%s'",
           url, entry->url, path);
    }

  *exists = wc_exists;

  return SVN_NO_ERROR;
}


static svn_error_t *
make_empty_adm (const char *path, apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_status_t apr_err;

  path = extend_with_adm_name (path, NULL, 0, pool, NULL);

  apr_err = apr_dir_make (path, APR_OS_DEFAULT, pool);
  if (apr_err)
    err = svn_error_create (apr_err, 0, NULL, pool, path);

  return err;
}


/* Init an adm file with some contents. 
   Don't call this until a tmp area exists in adm. */
static svn_error_t *
init_adm_file (const char *path,
               const char *thing,
               const char *contents,
               apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;
  apr_file_t *f = NULL;

  err = svn_wc__open_adm_file (&f, path, thing, APR_WRITE | APR_CREATE, pool);
  if (err)
    return err;

  apr_err = apr_file_write_full (f, contents, strlen (contents), NULL);

  err = svn_wc__close_adm_file (f, path, thing, 1, pool);
  if (err)
    return err;
  
  if (apr_err)
    err = svn_error_create (apr_err, 0, NULL, pool, path);

  return err;
}


static svn_error_t *
init_adm_tmp_area (const char *path,
                   apr_pool_t *pool)
{
  /* Default perms */
  apr_fileperms_t perms = APR_OS_DEFAULT;

  /* SVN_WC__ADM_TMP */
  SVN_ERR (svn_wc__make_adm_thing (path, SVN_WC__ADM_TMP,
                                   svn_node_dir, perms, 0, pool));
  
  /* SVN_WC__ADM_TMP/SVN_WC__ADM_TEXT_BASE */
  SVN_ERR (svn_wc__make_adm_thing (path, SVN_WC__ADM_TEXT_BASE,
                                   svn_node_dir, perms, 1, pool));

  /* SVN_WC__ADM_TMP/SVN_WC__ADM_PROP_BASE */
  SVN_ERR (svn_wc__make_adm_thing (path, SVN_WC__ADM_PROP_BASE,
                                   svn_node_dir, perms, 1, pool));

  /* SVN_WC__ADM_TMP/SVN_WC__ADM_PROPS */
  SVN_ERR (svn_wc__make_adm_thing (path, SVN_WC__ADM_PROPS,
                                   svn_node_dir, perms, 1, pool));

  /* SVN_WC__ADM_TMP/SVN_WC__ADM_WCPROPS */
  SVN_ERR (svn_wc__make_adm_thing (path, SVN_WC__ADM_WCPROPS,
                                   svn_node_dir, perms, 1, pool));

  /* SVN_WC__ADM_TMP/SVN_WC__ADM_AUTH_DIR */
  SVN_ERR (svn_wc__make_adm_thing (path, SVN_WC__ADM_AUTH_DIR,
                                   svn_node_dir,
                                   (APR_UREAD | APR_UWRITE | APR_UEXECUTE),
                                   1, pool));

  return SVN_NO_ERROR;
}


/* Set up a new adm area for PATH, with URL as the ancestor url.
   The adm area starts out locked; remember to unlock it when done. */
static svn_error_t *
init_adm (const char *path,
          const char *url,
          apr_pool_t *pool)
{
  /* Default perms */
  apr_fileperms_t perms = APR_OS_DEFAULT;

  /* Initial contents for certain adm files. */
  const char *format_contents = "1\n";
  const char *readme_contents =
    "This is a Subversion working copy administrative directory.\n"
    "Visit http://subversion.tigris.org/ for more information.\n";

  /* First, make an empty administrative area. */
  make_empty_adm (path, pool);

  /* Lock it immediately.  Theoretically, no compliant wc library
     would ever consider this an adm area until a README file were
     present... but locking it is still appropriately paranoid. */
  SVN_ERR (svn_wc_lock (path, 0, pool));


  /** Make subdirectories. ***/

  /* SVN_WC__ADM_TEXT_BASE */
  SVN_ERR (svn_wc__make_adm_thing (path, SVN_WC__ADM_TEXT_BASE,
                                   svn_node_dir, perms, 0, pool));

  /* SVN_WC__ADM_PROP_BASE */
  SVN_ERR (svn_wc__make_adm_thing (path, SVN_WC__ADM_PROP_BASE,
                                   svn_node_dir, perms, 0, pool));

  /* SVN_WC__ADM_PROPS */
  SVN_ERR (svn_wc__make_adm_thing (path, SVN_WC__ADM_PROPS,
                                   svn_node_dir, perms, 0, pool));

  /* SVN_WC__ADM_WCPROPS */
  SVN_ERR (svn_wc__make_adm_thing (path, SVN_WC__ADM_WCPROPS,
                                   svn_node_dir, perms, 0, pool));

  /* SVN_WC__ADM_AUTH_DIR */
  SVN_ERR (svn_wc__make_adm_thing (path, SVN_WC__ADM_AUTH_DIR,
                                   svn_node_dir, 
                                   (APR_UREAD | APR_UWRITE | APR_UEXECUTE),
                                   0, pool));

  /** Init the tmp area. ***/
  SVN_ERR (init_adm_tmp_area (path, pool));
  
  /** Initialize each administrative file. */

  /* SVN_WC__ADM_FORMAT */
  SVN_ERR (init_adm_file (path, SVN_WC__ADM_FORMAT, format_contents, pool));

  /* SVN_WC__ADM_ENTRIES */
  SVN_ERR (svn_wc__entries_init (path, url, pool));


  /* SVN_WC__ADM_EMPTY_FILE exists because sometimes an readable, empty
     file is required (in the repository diff for example). Creating such a
     file temporarily, only to delete it again, would appear to be less
     efficient than just having one around. It doesn't take up much space
     after all. */
  SVN_ERR (svn_wc__make_adm_thing (path, SVN_WC__ADM_EMPTY_FILE, svn_node_file,
                                   APR_UREAD, 0, pool));

  /* THIS FILE MUST BE CREATED LAST: 
     After this exists, the dir is considered complete. */
  SVN_ERR (init_adm_file (path, SVN_WC__ADM_README, readme_contents, pool));

  /* Now unlock it.  It's now a valid working copy directory, that
     just happens to be at revision 0. */
  SVN_ERR (svn_wc_unlock (path, pool));

  /* Else no problems, we're outta here. */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__ensure_adm (const char *path,
                    const char *url,
                    svn_revnum_t revision,
                    apr_pool_t *pool)
{
  svn_boolean_t exists_already;

  SVN_ERR (check_adm_exists (&exists_already, path, url, revision, pool));
  return (exists_already ? SVN_NO_ERROR : init_adm (path, url, pool));
}


svn_error_t *
svn_wc__adm_destroy (const char *path, apr_pool_t *pool)
{
  /* Try to lock the admin directory, hoping that this function will
     eject an error if we're already locked (which is fine, cause if
     it is already locked, we certainly don't want to blow it away. */
  SVN_ERR (svn_wc_lock (path, 0, pool));

  /* Well, I think the coast is clear for blowing away this directory
     (which will also remove the lock file we created above) */
  path = svn_path_join (path, adm_subdir (), pool);
  SVN_ERR (svn_io_remove_dir (path, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__adm_cleanup_tmp_area (const char *path, apr_pool_t *pool)
{
  svn_boolean_t was_locked;
  const char *tmp_path;

  /* Lock the admin area if it's not already locked. */
  SVN_ERR (svn_wc_locked (&was_locked, path, pool));
  if (! was_locked)
    SVN_ERR (svn_wc_lock (path, 0, pool));

  /* Get the path to the tmp area, and blow it away. */
  tmp_path = extend_with_adm_name (path, NULL, 0, pool, SVN_WC__ADM_TMP, NULL);
  SVN_ERR (svn_io_remove_dir (tmp_path, pool));

  /* Now, rebuild the tmp area. */
  SVN_ERR (init_adm_tmp_area (path, pool));

  /* Unlock the admin area if it wasn't locked when we entered this
     function. */
  if (! was_locked)
    SVN_ERR (svn_wc_unlock (path, pool));

  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc_create_tmp_file (apr_file_t **fp,
                        const char *path,
                        svn_boolean_t delete_on_close,
                        apr_pool_t *pool)
{
  const char *ignored_filename;

  /* Use a self-explanatory name for the file :-) . */
  path = svn_wc__adm_path (path, TRUE, pool, "tempfile", NULL);

  /* Open a unique file;  use APR_DELONCLOSE. */  
  SVN_ERR (svn_io_open_unique_file (fp, &ignored_filename,
                                    path, ".tmp", delete_on_close, pool));

  return SVN_NO_ERROR;
}





/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
