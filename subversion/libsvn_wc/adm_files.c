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
  return SVN_WC_ADM_DIR_NAME;
}


svn_stringbuf_t *
svn_wc__adm_subdir (apr_pool_t *pool)
{
  return svn_stringbuf_create (adm_subdir (), pool);
}


/* Extend PATH to the name of something in PATH's administrative area.
 * Returns the number of path components added to PATH.
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
 *
 * Important: chances are you will want to call chop_admin_name() to
 * restore PATH to its original value before exiting anything that
 * calls this.  If you exit, say by returning an error, before calling
 * chop_admin_name(), then PATH will still be in its extended state.
 *
 * So, the safest recipe is for callers of extend_with_adm_name() to
 * always have exactly one return statement, occurring *after* an
 * unconditional call to chop_admin_name().  */
static int
v_extend_with_adm_name (svn_stringbuf_t *path,
                        const char *extension,
                        svn_boolean_t use_tmp,
                        apr_pool_t *pool,
                        va_list ap)
{
  const char *this;
  int components_added = 0;

  /* Tack on the administrative subdirectory. */
  svn_path_add_component_nts (path, adm_subdir ());
  components_added++;

  /* If this is a tmp file, name it into the tmp area. */
  if (use_tmp)
    {
      svn_path_add_component_nts (path, SVN_WC__ADM_TMP);
      components_added++;
    }

  /* Tack on everything else. */
  while ((this = va_arg (ap, const char *)) != NULL)
    {
      if (this[0] == '\0')
        continue;

      svn_path_add_component_nts (path, this);
      components_added++;
    }

  if (extension)
    svn_stringbuf_appendcstr (path, extension);

  return components_added;
}


/* See v_extend_with_adm_name() for details. */
static int
extend_with_adm_name (svn_stringbuf_t *path,
                      const char *extension,
                      svn_boolean_t use_tmp,
                      apr_pool_t *pool,
                      ...)
{
  va_list ap;
  int components_added;

  va_start (ap, pool);
  components_added = v_extend_with_adm_name (path,
                                             extension,
                                             use_tmp,
                                             pool,
                                             ap);
  va_end (ap);

  return components_added;
}


svn_stringbuf_t *
svn_wc__adm_path (svn_stringbuf_t *path,
                  svn_boolean_t tmp,
                  apr_pool_t *pool, 
                  ...)
{
  svn_stringbuf_t *newpath = svn_stringbuf_dup (path, pool);
  va_list ap;

  va_start (ap, pool);
  v_extend_with_adm_name (newpath, NULL, tmp, pool, ap);
  va_end (ap);

  return newpath;
}


svn_boolean_t
svn_wc__adm_path_exists (svn_stringbuf_t *path,
                         svn_boolean_t tmp,
                         apr_pool_t *pool, 
                         ...)
{
  enum svn_node_kind kind;
  svn_stringbuf_t *newpath = svn_stringbuf_dup (path, pool);
  va_list ap;

  va_start (ap, pool);
  v_extend_with_adm_name (newpath, NULL, tmp, pool, ap);
  va_end (ap);

  svn_io_check_path (newpath->data, &kind, pool);
  if (kind == svn_node_none)
    return FALSE;
  else
    return TRUE;
}



/* Restore PATH to what it was before a call to
 * extend_with_adm_name(), by lopping off NUM_COMPONENTS
 * components.
 */
static void
chop_admin_name (svn_stringbuf_t *path, int num_components)
{
  while (num_components-- > 0)
    svn_path_remove_component (path);
}



/*** Making and using files in the adm area. ***/


/* Create an empty THING in the adm area with permissions set to PERMS. 
 * If TMP is non-zero, then create THING in the tmp dir.
 *
 * Does not check if THING already exists, so be careful -- THING will
 * be empty after this no matter what.
 */
svn_error_t *
svn_wc__make_adm_thing (svn_stringbuf_t *path,
                        const char *thing,
                        int type,
                        apr_fileperms_t perms,
                        svn_boolean_t tmp,
                        apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_file_t *f = NULL;
  apr_status_t apr_err = 0;
  int components_added;

  components_added = extend_with_adm_name (path, NULL, tmp, pool, thing, NULL);

  if (type == svn_node_file)
    {
      apr_err = apr_file_open (&f, path->data,
                          (APR_WRITE | APR_CREATE | APR_EXCL),
                          perms,
                          pool);

      if (apr_err)
        err = svn_error_create (apr_err, 0, NULL, pool, path->data);
      else
        {
          /* Creation succeeded, so close immediately. */
          apr_err = apr_file_close (f);
          if (apr_err)
            err = svn_error_create (apr_err, 0, NULL, pool, path->data);
        }
    }
  else if (type == svn_node_dir)
    {
      apr_err = apr_dir_make (path->data, perms, pool);
      if (apr_err)
        err = svn_error_create (apr_err, 0, NULL, pool, path->data);
    }
  else   /* unknown type argument, wrongness */
    {
      /* We're only capturing this here because there wouldn't be a
         segfault or other obvious indicator that something went
         wrong.  Even so, not sure if it's appropriate.  Thoughts? */
      err = svn_error_create 
        (0, 0, NULL, pool, "svn_wc__make_admin_thing: bad type indicator");
    }

  /* Restore path to its original state no matter what. */
  chop_admin_name (path, components_added);

  return err;
}



/* Copy SRC to DST if SRC exists, else create DST empty. */
static svn_error_t *
maybe_copy_file (svn_stringbuf_t *src, svn_stringbuf_t *dst, apr_pool_t *pool)
{
  enum svn_node_kind kind;
  svn_error_t *err;
  apr_status_t apr_err;

  /* First test if SRC exists. */
  err = svn_io_check_path (src->data, &kind, pool);
  if (err)
    return err;
  else if (kind == svn_node_none)
    {
      /* SRC doesn't exist, create DST empty. */
      apr_file_t *f = NULL;
      apr_err = apr_file_open (&f,
                          dst->data,
                          (APR_WRITE | APR_CREATE),
                          APR_OS_DEFAULT,
                          pool);
      if (apr_err)
        return svn_error_create (apr_err, 0, NULL, pool, dst->data);
      else
        {
          apr_err = apr_file_close (f);
          if (apr_err)
            return svn_error_create (apr_err, 0, NULL, pool, dst->data);
          else
            return SVN_NO_ERROR;
        }
    }
  else /* SRC exists, so copy it to DST. */
    {    
      err = svn_io_copy_file (src->data, dst->data, FALSE, pool);
      if (err)
        return err;
    }

  return SVN_NO_ERROR;
}



/*** Syncing files in the adm area. ***/

static svn_error_t *
sync_adm_file (svn_stringbuf_t *path,
               const char *extension,
               apr_pool_t *pool,
               ...)
{
  /* Some code duplication with close_adm_file() seems unavoidable,
     given how C va_lists work. */

  svn_stringbuf_t *tmp_path = svn_stringbuf_dup (path, pool);
  apr_status_t apr_err;
  int components_added;
  va_list ap;
  
  /* Extend real name. */
  va_start (ap, pool);
  components_added = v_extend_with_adm_name (path, extension, 0, pool, ap);
  va_end (ap);
  
  /* Extend tmp name. */
  va_start (ap, pool);
  v_extend_with_adm_name (tmp_path, extension, 1, pool, ap);
  va_end (ap);
  
  /* Remove read-only flag on destination. */
  SVN_ERR (svn_io_set_file_read_write (path->data, TRUE, pool));
 
  /* Rename. */
  apr_err = apr_file_rename (tmp_path->data, path->data, pool);
  if (! apr_err)
    SVN_ERR (svn_io_set_file_read_only (path->data, FALSE, pool));

  /* Unconditionally restore path. */
  chop_admin_name (path, components_added);
      
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "error renaming %s to %s",
                              tmp_path->data, path->data);
  else
    return SVN_NO_ERROR;
}


/* Rename a tmp text-base file to its real text-base name.
   The file had better already be closed. */
svn_error_t *
svn_wc__sync_text_base (svn_stringbuf_t *path, apr_pool_t *pool)
{
  svn_stringbuf_t *newpath, *base_name;
  svn_path_split (path, &newpath, &base_name, pool);
  return sync_adm_file (newpath,
                        SVN_WC__BASE_EXT,
                        pool,
                        SVN_WC__ADM_TEXT_BASE,
                        base_name->data,
                        NULL);
}

svn_stringbuf_t *
svn_wc__text_base_path (const svn_stringbuf_t *path,
                        svn_boolean_t tmp,
                        apr_pool_t *pool)
{
  svn_stringbuf_t *newpath, *base_name;
  svn_path_split (path, &newpath, &base_name, pool);
  extend_with_adm_name (newpath,
                        SVN_WC__BASE_EXT,
                        0,
                        pool,
                        tmp ? SVN_WC__ADM_TMP : "",
                        SVN_WC__ADM_TEXT_BASE,
                        base_name->data,
                        NULL);
    
  return newpath;
}


static svn_error_t *
prop_path_internal (svn_stringbuf_t **prop_path,
                    const svn_stringbuf_t *path,
                    svn_boolean_t base,
                    svn_boolean_t tmp,
                    apr_pool_t *pool)
{
  svn_error_t *err;
  enum svn_node_kind kind;
  svn_boolean_t is_wc;
  svn_stringbuf_t *entry_name;

  err = svn_io_check_path (path->data, &kind, pool);
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
      *prop_path = svn_stringbuf_dup (path, pool);
      extend_with_adm_name 
        (*prop_path,
         NULL,
         0,
         pool,
         tmp ? SVN_WC__ADM_TMP : "",
         base ? SVN_WC__ADM_DIR_PROP_BASE : SVN_WC__ADM_DIR_PROPS,
         NULL);
    }
  else  /* It's either a file, or a non-wc dir (i.e., maybe an ex-file) */
    {
      svn_path_split (path, prop_path, &entry_name, pool);
      if (svn_path_is_empty (*prop_path))
        svn_stringbuf_set (*prop_path, ".");

      err = svn_wc_check_wc (*prop_path, &is_wc, pool);
      if (err)
        return err;
      else if (! is_wc)
        return svn_error_createf
          (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, pool,
           "svn_wc__prop_path: %s is not a working copy directory",
           (*prop_path)->data);

      extend_with_adm_name (*prop_path,
                            base ? SVN_WC__BASE_EXT : NULL,
                            0,
                            pool,
                            tmp ? SVN_WC__ADM_TMP : "",
                            base ? SVN_WC__ADM_PROP_BASE : SVN_WC__ADM_PROPS,
                            entry_name->data,
                            NULL);
    }

  return SVN_NO_ERROR;
}



/* Return a path to the 'wcprop' file for PATH, possibly in TMP area.  */
svn_error_t *
svn_wc__wcprop_path (svn_stringbuf_t **wcprop_path,
                     const svn_stringbuf_t *path,
                     svn_boolean_t tmp,
                     apr_pool_t *pool)
{
  svn_error_t *err;
  enum svn_node_kind kind;
  svn_boolean_t is_wc;
  svn_stringbuf_t *entry_name;

  err = svn_io_check_path (path->data, &kind, pool);
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
      *wcprop_path = svn_stringbuf_dup (path, pool);
      extend_with_adm_name 
        (*wcprop_path,
         NULL,
         0,
         pool,
         tmp ? SVN_WC__ADM_TMP : "",
         SVN_WC__ADM_DIR_WCPROPS,
         NULL);
    }
  else  /* It's either a file, or a non-wc dir (i.e., maybe an ex-file) */
    {
      svn_path_split (path, wcprop_path, &entry_name, pool);
 
      err = svn_wc_check_wc (*wcprop_path, &is_wc, pool);
      if (err)
        return err;
      else if (! is_wc)
        return svn_error_createf
          (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, pool,
           "wcprop_path: %s is not a working copy directory",
           (*wcprop_path)->data);

      extend_with_adm_name (*wcprop_path,
                            NULL,
                            0,
                            pool,
                            tmp ? SVN_WC__ADM_TMP : "",
                            SVN_WC__ADM_WCPROPS,
                            entry_name->data,
                            NULL);
    }

  return SVN_NO_ERROR;
}




svn_error_t *
svn_wc__prop_path (svn_stringbuf_t **prop_path,
                   const svn_stringbuf_t *path,
                   svn_boolean_t tmp,
                   apr_pool_t *pool)
{
  return prop_path_internal (prop_path, path, FALSE, tmp, pool);
}


svn_error_t *
svn_wc__prop_base_path (svn_stringbuf_t **prop_path,
                        const svn_stringbuf_t *path,
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
               svn_stringbuf_t *path,
               const char *extension,
               apr_int32_t flags,
               apr_pool_t *pool,
               ...)
{
  svn_error_t *err = NULL;
  apr_status_t apr_err = 0;
  int components_added;
  va_list ap;

  /* If we're writing, always do it to a tmp file. */
  if (flags & APR_WRITE)
    {
      if (flags & APR_APPEND)
        {
          svn_stringbuf_t *opath, *tmp_path;  /* just keep it all local */

          opath    = svn_stringbuf_dup (path, pool);
          tmp_path = svn_stringbuf_dup (path, pool);

          va_start (ap, pool);
          v_extend_with_adm_name (opath, extension, 0, pool, ap);
          va_end (ap);

          va_start (ap, pool);
          v_extend_with_adm_name (tmp_path, extension, 1, pool, ap);
          va_end (ap);

          /* Copy the original thing to the tmp location. */
          err = maybe_copy_file (opath, tmp_path, pool);
          if (err)
            return err;
        }

      /* Extend with tmp name. */
      va_start (ap, pool);
      components_added
        = v_extend_with_adm_name (path, extension, 1, pool, ap);
      va_end (ap);
    }
  else
    {
      /* Extend with regular adm name. */
      va_start (ap, pool);
      components_added
        = v_extend_with_adm_name (path, extension, 0, pool, ap);
      va_end (ap);
    }

  apr_err = apr_file_open (handle, path->data, flags, APR_OS_DEFAULT, pool);
  if (apr_err)
    {
      /* Oddly enough, APR will set *HANDLE even if the open failed.
         You'll get a filehandle whose descriptor is -1.  There must
         be a reason this is useful... Anyway, we don't want the
         handle. */
      *handle = NULL;
      err = svn_error_create (apr_err, 0, NULL, pool, path->data);
    }

  /* Restore path to its original state no matter what. */
  chop_admin_name (path, components_added);

  return err;
}


/* Close the file indicated by FP (PATH is passed to make error
 * reporting better).  If SYNC is non-zero, then the file will be
 * sync'd from the adm tmp area to its permanent location, otherwise
 * it will remain in the tmp area.  See open_adm_file().
 */
static svn_error_t *
close_adm_file (apr_file_t *fp,
                svn_stringbuf_t *path,
                const char *extension,
                svn_boolean_t sync,
                apr_pool_t *pool,
                ...)
{
  apr_status_t apr_err = 0;
  int components_added;
  va_list ap;

  /* Get the full name of the thing we want. */
  va_start (ap, pool);
  components_added = v_extend_with_adm_name (path, extension, sync, pool, ap);
  va_end (ap);

  apr_err = apr_file_close (fp);

  /* Restore path to its original state no matter what. */
  chop_admin_name (path, components_added);

  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, pool, path->data);

  /* If we were writing, then it was to a tmp file, which will have to
     be renamed after closing. */
  if (sync)
    {
      /* Some code duplication with sync_adm_file() seems unavoidable,
         given how C va_lists work. */

      svn_stringbuf_t *tmp_path = svn_stringbuf_dup (path, pool);
      
      /* Extend real name. */
      va_start (ap, pool);
      components_added = v_extend_with_adm_name (path, extension,
                                                 0, pool, ap);
      va_end (ap);
      
      /* Extend tmp name. */
      va_start (ap, pool);
      v_extend_with_adm_name (tmp_path, extension, 1, pool, ap);
      va_end (ap);
      
      /* Remove read-only flag on destination. */
      SVN_ERR (svn_io_set_file_read_write (path->data, TRUE, pool));
      
      /* Rename. */
      apr_err = apr_file_rename (tmp_path->data, path->data, pool);
      if (! apr_err)
        SVN_ERR (svn_io_set_file_read_only (path->data, FALSE, pool));
      
      /* Unconditionally restore path. */
      chop_admin_name (path, components_added);
      
      if (apr_err)
        return svn_error_createf (apr_err, 0, NULL, pool,
                                  "error renaming %s to %s",
                                  tmp_path->data, path->data);
      else
        return SVN_NO_ERROR;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__open_adm_file (apr_file_t **handle,
                       const svn_stringbuf_t *path,
                       const char *fname,
                       apr_int32_t flags,
                       apr_pool_t *pool)
{
  *handle = NULL;  /* satisfy APR's bizarre requirement */
  /* ### We are casting away const here, and open_adm_file will modify
     path.  (It will append things, possibly requiring memory allocation,
     before returning it will chop of the appended stuff, but the string 
     retains the allocated memory.) */
  return open_adm_file (handle, (svn_stringbuf_t *) path, NULL,
                        flags, pool, fname, NULL);
}


svn_error_t *
svn_wc__close_adm_file (apr_file_t *fp,
                        const svn_stringbuf_t *path,
                        const char *fname,
                        int sync,
                        apr_pool_t *pool)
{
  /* ### We are casting away const here, and open_adm_file will modify
     path.  (It will append things, possibly requiring memory allocation,
     before returning it will chop of the appended stuff, but the string 
     retains the allocated memory.) */
  return close_adm_file (fp, (svn_stringbuf_t *) path, NULL,
                         sync, pool, fname, NULL);
}


svn_error_t *
svn_wc__remove_adm_file (svn_stringbuf_t *path, apr_pool_t *pool, ...)
{
  svn_error_t *err = NULL;
  apr_status_t apr_err = 0;
  int components_added;
  va_list ap;

  va_start (ap, pool);
  components_added = v_extend_with_adm_name (path, NULL, 0, pool, ap);
  va_end (ap);
      
  /* Remove read-only flag on path. */
  SVN_ERR(svn_io_set_file_read_write (path->data, FALSE, pool));

  apr_err = apr_file_remove (path->data, pool);
  if (apr_err)
    err = svn_error_create (apr_err, 0, NULL, pool, path->data);

  /* Restore path to its original state no matter what. */
  chop_admin_name (path, components_added);

  return err;
}


svn_stringbuf_t *
svn_wc__empty_file_path (const svn_stringbuf_t *path,
                         apr_pool_t *pool)
{
  svn_stringbuf_t *empty_file_path = svn_stringbuf_dup (path, pool);
  svn_path_remove_component (empty_file_path);
  extend_with_adm_name (empty_file_path, NULL, 0, pool, SVN_WC__ADM_EMPTY_FILE,
                        NULL);
  return empty_file_path;
}


svn_error_t *
svn_wc__open_empty_file (apr_file_t **handle,
                         svn_stringbuf_t *path,
                         apr_pool_t *pool)
{
  svn_stringbuf_t *newpath;
  svn_path_split (path, &newpath, NULL, pool);
  return open_adm_file (handle, newpath, NULL, APR_READ, pool,
                        SVN_WC__ADM_EMPTY_FILE, NULL);
}


svn_error_t *
svn_wc__close_empty_file (apr_file_t *fp,
                          svn_stringbuf_t *path,
                          apr_pool_t *pool)
{
  svn_stringbuf_t *newpath, *base_name;
  svn_path_split (path, &newpath, &base_name, pool);
  return close_adm_file (fp, newpath, NULL, 0, pool,
                         SVN_WC__ADM_EMPTY_FILE, NULL);
}


svn_error_t *
svn_wc__open_text_base (apr_file_t **handle,
                        svn_stringbuf_t *path,
                        apr_int32_t flags,
                        apr_pool_t *pool)
{
  svn_stringbuf_t *newpath, *base_name;
  svn_path_split (path, &newpath, &base_name, pool);
  return open_adm_file (handle, newpath, SVN_WC__BASE_EXT, flags, pool,
                        SVN_WC__ADM_TEXT_BASE, base_name->data, NULL);
}


svn_error_t *
svn_wc__close_text_base (apr_file_t *fp,
                         svn_stringbuf_t *path,
                         int write,
                         apr_pool_t *pool)
{
  svn_stringbuf_t *newpath, *base_name;
  svn_path_split (path, &newpath, &base_name, pool);
  return close_adm_file (fp, newpath, SVN_WC__BASE_EXT, write, pool,
                         SVN_WC__ADM_TEXT_BASE, base_name->data, NULL);
}


svn_error_t *
svn_wc__open_auth_file (apr_file_t **handle,
                        svn_stringbuf_t *path,
                        svn_stringbuf_t *auth_filename,
                        apr_int32_t flags,
                        apr_pool_t *pool)
{
  return open_adm_file (handle, path, NULL, flags, pool,
                        SVN_WC__ADM_AUTH_DIR, auth_filename->data, NULL);
}


svn_error_t *
svn_wc__close_auth_file (apr_file_t *handle,
                         svn_stringbuf_t *path,
                         svn_stringbuf_t *file,
                         int sync,
                         apr_pool_t *pool)
{
  return close_adm_file (handle, path, NULL, sync, pool,
                         SVN_WC__ADM_AUTH_DIR, file->data, NULL);
}

svn_error_t *
svn_wc__open_props (apr_file_t **handle,
                    svn_stringbuf_t *path,
                    apr_int32_t flags,
                    svn_boolean_t base,
                    svn_boolean_t wcprops,
                    apr_pool_t *pool)
{
  svn_stringbuf_t *parent_dir, *base_name;
  enum svn_node_kind kind;

  /* Check if path is a file or a dir. */
  SVN_ERR (svn_io_check_path (path->data, &kind, pool));

  /* If file, split the path. */
  if (kind == svn_node_file)
    svn_path_split (path, &parent_dir, &base_name, pool);
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
                              pool, SVN_WC__ADM_PROP_BASE, base_name->data,
                              NULL);
    }
  else if (wcprops)
    {
      if (kind == svn_node_dir)
        return open_adm_file (handle, parent_dir, NULL, flags, pool,
                              SVN_WC__ADM_DIR_WCPROPS, NULL);
      else
        return open_adm_file (handle, parent_dir, NULL, flags,
                              pool, SVN_WC__ADM_WCPROPS, base_name->data,
                              NULL);
    }
  else /* plain old property file */
    {
      if (kind == svn_node_dir)
        return open_adm_file (handle, parent_dir, NULL, flags, pool,
                              SVN_WC__ADM_DIR_PROPS, NULL);
      else
        return open_adm_file (handle, parent_dir, NULL, flags,
                              pool, SVN_WC__ADM_PROPS, base_name->data,
                              NULL);
    }
}



svn_error_t *
svn_wc__close_props (apr_file_t *fp,
                     svn_stringbuf_t *path,
                     svn_boolean_t base,
                     svn_boolean_t wcprops,
                     int sync,
                     apr_pool_t *pool)
{
  svn_stringbuf_t *parent_dir, *base_name;
  enum svn_node_kind kind;

  /* Check if path is a file or a dir. */
  SVN_ERR (svn_io_check_path (path->data, &kind, pool));

  /* If file, split the path. */
  if (kind == svn_node_file)
    svn_path_split (path, &parent_dir, &base_name, pool);
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
                               SVN_WC__ADM_PROP_BASE, base_name->data, NULL);
    }
  else if (wcprops)
    {
      if (kind == svn_node_dir)
        return close_adm_file (fp, parent_dir, NULL, sync, pool,
                               SVN_WC__ADM_DIR_WCPROPS, NULL);
      else
        return close_adm_file (fp, parent_dir, NULL, sync, pool,
                               SVN_WC__ADM_WCPROPS, base_name->data, NULL);
    }
  else /* plain old property file */
    {
      if (kind == svn_node_dir)
        return close_adm_file (fp, parent_dir, NULL, sync, pool,
                               SVN_WC__ADM_DIR_PROPS, NULL);
      else
        return close_adm_file (fp, parent_dir, NULL, sync, pool,
                                 SVN_WC__ADM_PROPS, base_name->data, NULL);
    }

}



svn_error_t *
svn_wc__sync_props (svn_stringbuf_t *path,
                    svn_boolean_t base,
                    svn_boolean_t wcprops,
                    apr_pool_t *pool)
{
  svn_stringbuf_t *parent_dir, *base_name;
  enum svn_node_kind kind;

  /* Check if path is a file or a dir. */
  SVN_ERR (svn_io_check_path (path->data, &kind, pool));

  /* If file, split the path. */
  if (kind == svn_node_file)
    svn_path_split (path, &parent_dir, &base_name, pool);
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
                              SVN_WC__ADM_PROP_BASE, base_name->data, NULL);
    }
  else if (wcprops)
    {
      if (kind == svn_node_dir)
        return sync_adm_file (parent_dir, NULL, pool,
                              SVN_WC__ADM_DIR_WCPROPS, NULL);
      else
        return sync_adm_file (parent_dir, SVN_WC__BASE_EXT, pool,
                              SVN_WC__ADM_WCPROPS, base_name->data, NULL);
    }
  else /* plain old property file */
    {
      if (kind == svn_node_dir)
        return sync_adm_file (parent_dir, NULL, pool,
                              SVN_WC__ADM_DIR_PROPS, NULL);
      else
        return sync_adm_file (parent_dir, NULL, pool,
                              SVN_WC__ADM_PROPS, base_name->data, NULL);
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
                  svn_stringbuf_t *path,
                  svn_stringbuf_t *url,
                  svn_revnum_t revision,
                  apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  enum svn_node_kind kind;
  svn_boolean_t dir_exists = FALSE, wc_exists = FALSE;
  apr_file_t *f = NULL;
  int components_added;

  /** Step 1: check that the directory exists. **/

  components_added = extend_with_adm_name (path, NULL, 0, pool, NULL);

  err = svn_io_check_path (path->data, &kind, pool);
  if (!err)
    {
      if (kind != svn_node_none && kind != svn_node_dir)
        {
          /* If got an error other than dir non-existence, then
             something's weird and we should return a genuine error. */
          err = svn_error_create (APR_ENOTDIR, 0, NULL, pool, path->data);
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

  /* Restore path to its original state. */
  chop_admin_name (path, components_added);

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
                                  "no entry for '%s'", path->data);

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
                             revision, entry->revision, path->data);

      /** ### comparing URLs, should they be cannonicalized first? */
      if (! svn_stringbuf_compare (entry->url, url))
        return
          svn_error_createf (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, pool,
                             "URL '%s' doesn't match existing URL '%s' in '%s'",
                             url->data, entry->url->data, path->data);
    }

  *exists = wc_exists;

  return SVN_NO_ERROR;
}


static svn_error_t *
make_empty_adm (svn_stringbuf_t *path, apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_status_t apr_err;
  int components_added;

  components_added = extend_with_adm_name (path, NULL, 0, pool, NULL);

  apr_err = apr_dir_make (path->data, APR_OS_DEFAULT, pool);
  if (apr_err)
    err = svn_error_create (apr_err, 0, NULL, pool, path->data);
    
  chop_admin_name (path, components_added);

  return err;
}


/* Init an adm file with some contents. 
   Don't call this until a tmp area exists in adm. */
static svn_error_t *
init_adm_file (svn_stringbuf_t *path,
               const char *thing,
               svn_stringbuf_t *contents,
               apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;
  apr_file_t *f = NULL;
  apr_size_t written = 0;

  err = svn_wc__open_adm_file (&f, path, thing, APR_WRITE | APR_CREATE, pool);
  if (err)
    return err;

  apr_err = apr_file_write_full (f, contents->data, contents->len, &written);

  err = svn_wc__close_adm_file (f, path, thing, 1, pool);
  if (err)
    return err;
  
  if (apr_err)
    err = svn_error_create (apr_err, 0, NULL, pool, path->data);

  return err;
}


static svn_error_t *
init_adm_tmp_area (svn_stringbuf_t *path,
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
init_adm (svn_stringbuf_t *path,
          svn_stringbuf_t *url,
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
  SVN_ERR (init_adm_file (path, SVN_WC__ADM_FORMAT,
                          svn_stringbuf_create (format_contents, pool), pool));

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
  SVN_ERR (init_adm_file (path, SVN_WC__ADM_README,
                          svn_stringbuf_create (readme_contents, pool),
                          pool));

  /* Now unlock it.  It's now a valid working copy directory, that
     just happens to be at revision 0. */
  SVN_ERR (svn_wc_unlock (path, pool));

  /* Else no problems, we're outta here. */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__ensure_adm (svn_stringbuf_t *path,
                    svn_stringbuf_t *url,
                    svn_revnum_t revision,
                    apr_pool_t *pool)
{
  svn_boolean_t exists_already;

  SVN_ERR (check_adm_exists (&exists_already, path, url, revision, pool));
  return (exists_already ? SVN_NO_ERROR : init_adm (path, url, pool));
}


svn_error_t *
svn_wc__adm_destroy (svn_stringbuf_t *path, apr_pool_t *pool)
{
  /* Try to lock the admin directory, hoping that this function will
     eject an error if we're already locked (which is fine, cause if
     it is already locked, we certainly don't want to blow it away. */
  SVN_ERR (svn_wc_lock (path, 0, pool));

  /* Well, I think the coast is clear for blowing away this directory
     (which should also remove the lock file we created above) */
  {
    svn_error_t *err;
    svn_stringbuf_t *adm_path = svn_stringbuf_dup (path, pool);

    svn_path_add_component (adm_path, svn_wc__adm_subdir (pool));

    err = svn_io_remove_dir (adm_path->data, pool);
    if (err)
      return svn_error_createf
        (err->apr_err, err->src_err, err, err->pool,
         "error removing administrative directory for %s",
         path->data);
  }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__adm_cleanup_tmp_area (svn_stringbuf_t *path, apr_pool_t *pool)
{
  svn_boolean_t was_locked;
  svn_stringbuf_t *tmp_path;
  svn_error_t *err;

  /* Lock the admin area if it's not already locked. */
  SVN_ERR (svn_wc_locked (&was_locked, path, pool));
  if (! was_locked)
    SVN_ERR (svn_wc_lock (path, 0, pool));

  /* Get the path to the tmp area, and blow it away. */
  tmp_path = svn_stringbuf_dup (path, pool);
  extend_with_adm_name (tmp_path, NULL, 0, pool, SVN_WC__ADM_TMP, NULL);

  err = svn_io_remove_dir (tmp_path->data, pool);
  if (err)
    return svn_error_createf
      (err->apr_err, err->src_err, err, err->pool,
       "error removing tmp area in administrative directory for %s",
       path->data);

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
                        svn_stringbuf_t *path,
                        svn_boolean_t delete_on_close,
                        apr_pool_t *pool)
{
  svn_stringbuf_t *truepath = svn_stringbuf_dup (path, pool);
  svn_stringbuf_t *ignored_filename;

  /* Tack on the administrative subdirectory. */
  svn_path_add_component_nts (truepath, adm_subdir());

  /* Tack on the temp area. */
  svn_path_add_component_nts (truepath, SVN_WC__ADM_TMP);
  
  /* Tack on a made-up filename. */
  svn_path_add_component_nts (truepath, "tempfile");

  /* Open a unique file;  use APR_DELONCLOSE. */  
  SVN_ERR (svn_io_open_unique_file (fp, &ignored_filename,
                                    truepath->data, ".tmp",
                                    delete_on_close, pool));

  return SVN_NO_ERROR;
}





/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
