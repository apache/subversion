/*
 * adm_files.c: helper routines for handling files & dirs in the
 *              working copy administrative area (creating,
 *              deleting, opening, and closing).  This is the only
 *              code that actually knows where administrative
 *              information is kept.  
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
#include <apr_file_io.h>
#include <apr_strings.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_path.h"
#include "svn_wc.h"

#include "wc.h"
#include "adm_files.h"
#include "entries.h"
#include "lock.h"

#include "svn_private_config.h"


/*** File names in the adm area. ***/

/* The default name of the WC admin directory. This name is always
   checked by svn_wc_is_adm_dir. */
static const char default_adm_dir_name[] = ".svn";

/* The name that is actually used for the WC admin directory.  The
   commonest case where this won't be the default is in Windows
   ASP.NET development environments, which choke on ".svn". */
static const char *adm_dir_name = default_adm_dir_name;


svn_boolean_t
svn_wc_is_adm_dir(const char *name, apr_pool_t *pool)
{
  return (0 == strcmp(name, adm_dir_name)
          || 0 == strcmp(name, default_adm_dir_name));
}


const char *
svn_wc_get_adm_dir(apr_pool_t *pool)
{
  return adm_dir_name;
}


svn_error_t *
svn_wc_set_adm_dir(const char *name, apr_pool_t *pool)
{
  /* This is the canonical list of administrative directory names.

     FIXME:
     An identical list is used in
       libsvn_subr/opt.c:svn_opt_args_to_target_array2(),
     but that function can't use this list, because that use would
     create a circular dependency between libsvn_wc and libsvn_subr.
     Make sure changes to the lists are always synchronized! */
  static const char *valid_dir_names[] = {
    default_adm_dir_name,
    "_svn",
    NULL
  };

  const char **dir_name;
  for (dir_name = valid_dir_names; *dir_name; ++dir_name)
    if (0 == strcmp(name, *dir_name))
      {
        /* Use the pointer to the statically allocated string
           constant, to avoid potential pool lifetime issues. */
        adm_dir_name = *dir_name;
        return SVN_NO_ERROR;
      }
  return svn_error_createf
    (SVN_ERR_BAD_FILENAME, NULL,
     _("'%s' is not a valid administrative directory name"),
     svn_path_local_style(name, pool));
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
v_extend_with_adm_name(const char *path,
                       const char *extension,
                       svn_boolean_t use_tmp,
                       apr_pool_t *pool,
                       va_list ap)
{
  const char *this;

  /* Tack on the administrative subdirectory. */
  path = svn_path_join(path, adm_dir_name, pool);

  /* If this is a tmp file, name it into the tmp area. */
  if (use_tmp)
    path = svn_path_join(path, SVN_WC__ADM_TMP, pool);

  /* Tack on everything else. */
  while ((this = va_arg(ap, const char *)) != NULL)
    {
      if (this[0] == '\0')
        continue;

      path = svn_path_join(path, this, pool);
    }

  if (extension)
    path = apr_pstrcat(pool, path, extension, NULL);

  return path;
}


/* See v_extend_with_adm_name() for details. */
static const char *
extend_with_adm_name(const char *path,
                     const char *extension,
                     svn_boolean_t use_tmp,
                     apr_pool_t *pool,
                     ...)
{
  va_list ap;

  va_start(ap, pool);
  path = v_extend_with_adm_name(path, extension, use_tmp, pool, ap);
  va_end(ap);

  return path;
}


const char *
svn_wc__adm_path(const char *path,
                 svn_boolean_t tmp,
                 apr_pool_t *pool, 
                 ...)
{
  va_list ap;

  va_start(ap, pool);
  path = v_extend_with_adm_name(path, NULL, tmp, pool, ap);
  va_end(ap);

  return path;
}


svn_boolean_t
svn_wc__adm_path_exists(const char *path,
                        svn_boolean_t tmp,
                        apr_pool_t *pool, 
                        ...)
{
  svn_node_kind_t kind;
  svn_error_t *err;
  va_list ap;

  va_start(ap, pool);
  path = v_extend_with_adm_name(path, NULL, tmp, pool, ap);
  va_end(ap);

  err = svn_io_check_path(path, &kind, pool);
  if (err)
    {
      svn_error_clear(err);
      /* Return early, since kind is undefined in this case. */
      return FALSE;
    }

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
svn_wc__make_adm_thing(svn_wc_adm_access_t *adm_access,
                       const char *thing,
                       svn_node_kind_t type,
                       apr_fileperms_t perms,
                       svn_boolean_t tmp,
                       apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  apr_file_t *f = NULL;
  const char *path;

  SVN_ERR(svn_wc__adm_write_check(adm_access));

  path = extend_with_adm_name(svn_wc_adm_access_path(adm_access),
                              NULL, tmp, pool, thing, NULL);

  if (type == svn_node_file)
    {
      SVN_ERR(svn_io_file_open(&f, path,
                               (APR_WRITE | APR_CREATE | APR_EXCL),
                               perms,
                               pool));

      /* Creation succeeded, so close immediately. */
      SVN_ERR(svn_io_file_close(f, pool));
    }
  else if (type == svn_node_dir)
    {
      SVN_ERR(svn_io_dir_make(path, perms, pool));
    }
  else   /* unknown type argument, wrongness */
    {
      /* We're only capturing this here because there wouldn't be a
         segfault or other obvious indicator that something went
         wrong.  Even so, not sure if it's appropriate.  Thoughts? */
      err = svn_error_create 
        (0, NULL, _("Bad type indicator"));
    }

  return err;
}



/*** Syncing files in the adm area. ***/

static svn_error_t *
sync_adm_file(const char *path,
              const char *extension,
              apr_pool_t *pool,
              ...)
{
  /* Some code duplication with close_adm_file() seems unavoidable,
     given how C va_lists work. */

  const char *tmp_path;
  va_list ap;
  
  /* Extend tmp name. */
  va_start(ap, pool);
  tmp_path = v_extend_with_adm_name(path, extension, 1, pool, ap);
  va_end(ap);
  
  /* Extend real name. */
  va_start(ap, pool);
  path = v_extend_with_adm_name(path, extension, 0, pool, ap);
  va_end(ap);
  
  /* Rename. */
  SVN_ERR(svn_io_file_rename(tmp_path, path, pool));
  SVN_ERR(svn_io_set_file_read_only(path, FALSE, pool));

  return SVN_NO_ERROR;
}


/* Rename a tmp text-base file to its real text-base name.
   The file had better already be closed. */
svn_error_t *
svn_wc__sync_text_base(const char *path, apr_pool_t *pool)
{
  const char *parent_path, *base_name;
  svn_path_split(path, &parent_path, &base_name, pool);
  return sync_adm_file(parent_path,
                       SVN_WC__BASE_EXT,
                       pool,
                       SVN_WC__ADM_TEXT_BASE,
                       base_name,
                       NULL);
}

const char *
svn_wc__text_base_path(const char *path,
                       svn_boolean_t tmp,
                       apr_pool_t *pool)
{
  const char *newpath, *base_name;

  svn_path_split(path, &newpath, &base_name, pool);
  return extend_with_adm_name(newpath,
                              SVN_WC__BASE_EXT,
                              tmp,
                              pool,
                              SVN_WC__ADM_TEXT_BASE,
                              base_name,
                              NULL);
}

const char *
svn_wc__text_revert_path(const char *path,
                         svn_boolean_t tmp,
                         apr_pool_t *pool)
{
  const char *newpath, *base_name;

  svn_path_split(path, &newpath, &base_name, pool);
  return extend_with_adm_name(newpath,
                              SVN_WC__REVERT_EXT,
                              tmp,
                              pool,
                              SVN_WC__ADM_TEXT_BASE,
                              base_name,
                              NULL);
}

/* Kind for prop_path_internal. */
typedef enum prop_path_kind_t
{
  prop_path_kind_base = 0,
  prop_path_kind_revert,
  prop_path_kind_wcprop,
  prop_path_kind_working
} prop_path_kind_t;

static svn_error_t *
prop_path_internal(const char **prop_path,
                   const char *path,
                   svn_node_kind_t kind,
                   prop_path_kind_t path_kind,
                   svn_boolean_t tmp,
                   apr_pool_t *pool)
{
  if (kind == svn_node_dir)  /* It's a working copy dir */
    {
      static const char * names[] = {
        SVN_WC__ADM_DIR_PROP_BASE,    /* prop_path_kind_base */
        SVN_WC__ADM_DIR_PROP_REVERT,  /* prop_path_kind_revert */
        SVN_WC__ADM_DIR_WCPROPS,      /* prop_path_kind_wcprop */
        SVN_WC__ADM_DIR_PROPS         /* prop_path_kind_working */
      };

      *prop_path = extend_with_adm_name
        (path,
         NULL,
         tmp,
         pool,
         names[path_kind],
         NULL);
    }
  else  /* It's a file */
    {
      static const char * extensions[] = {
        SVN_WC__BASE_EXT,     /* prop_path_kind_base */
        SVN_WC__REVERT_EXT,   /* prop_path_kind_revert */
        SVN_WC__WORK_EXT,     /* prop_path_kind_wcprop */
        SVN_WC__WORK_EXT      /* prop_path_kind_working */
      };

      static const char * dirs[] = {
        SVN_WC__ADM_PROP_BASE,  /* prop_path_kind_base */
        SVN_WC__ADM_PROP_BASE,  /* prop_path_kind_revert */
        SVN_WC__ADM_WCPROPS,    /* prop_path_kind_wcprop */
        SVN_WC__ADM_PROPS       /* prop_path_kind_working */
      };

      const char *base_name;

      svn_path_split(path, prop_path, &base_name, pool);
      *prop_path = extend_with_adm_name
        (*prop_path,
         extensions[path_kind],
         tmp,
         pool,
         dirs[path_kind],
         base_name,
         NULL);
    }

  return SVN_NO_ERROR;
}



/* Return a path to the 'wcprop' file for PATH, possibly in TMP area.  */
svn_error_t *
svn_wc__wcprop_path(const char **wcprop_path,
                    const char *path,
                    svn_node_kind_t kind,
                    svn_boolean_t tmp,
                    apr_pool_t *pool)
{
  return prop_path_internal(wcprop_path, path, kind,
                            prop_path_kind_wcprop, tmp, pool);
}




svn_error_t *
svn_wc__prop_path(const char **prop_path,
                  const char *path,
                  svn_node_kind_t kind,
                  svn_boolean_t tmp,
                  apr_pool_t *pool)
{
  return prop_path_internal(prop_path, path, kind,
                            prop_path_kind_working, tmp, pool);
}


svn_error_t *
svn_wc__prop_base_path(const char **prop_path,
                       const char *path,
                       svn_node_kind_t kind,
                       svn_boolean_t tmp,
                       apr_pool_t *pool)
{
  return prop_path_internal(prop_path, path, kind,
                            prop_path_kind_base, tmp, pool);
}


svn_error_t *
svn_wc__prop_revert_path(const char **prop_path,
                         const char *path,
                         svn_node_kind_t kind,
                         svn_boolean_t tmp,
                         apr_pool_t *pool)
{
  return prop_path_internal(prop_path, path, kind,
                            prop_path_kind_revert, tmp, pool);
}


/*** Opening and closing files in the adm area. ***/

/* Open a file somewhere in the adm area for directory PATH.
 * First, add the adm subdir as the next component of PATH, then add
 * each of the varargs (they are char *'s), then add EXTENSION if it
 * is non-null, then open the resulting file as *HANDLE.
 *
 * If FLAGS indicates writing, open the file in the adm tmp area.
 * This means the file will probably need to be renamed from there,
 * either by passing the sync flag to close_adm_file() later, or with
 * an explicit call to sync_adm_file().
 */
static svn_error_t *
open_adm_file(apr_file_t **handle,
              const char *path,
              const char *extension,
              apr_fileperms_t protection,
              apr_int32_t flags,
              apr_pool_t *pool,
              ...)
{
  svn_error_t *err = SVN_NO_ERROR;
  va_list ap;

  /* If we're writing, always do it to a tmp file. */
  if (flags & APR_WRITE)
    {
      if (flags & APR_APPEND)
        {
          /* We don't handle append.  To do so we would need to copy the
             contents into the apr_file_t once it has been opened. */
          return svn_error_create
            (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
             _("APR_APPEND not supported for adm files"));
        }

      /* Need to own the temporary file, so don't reuse an existing one. */
      flags |= APR_EXCL | APR_CREATE;

      /* Extend with tmp name. */
      va_start(ap, pool);
      path = v_extend_with_adm_name(path, extension, 1, pool, ap);
      va_end(ap);
    }
  else
    {
      /* Extend with regular adm name. */
      va_start(ap, pool);
      path = v_extend_with_adm_name(path, extension, 0, pool, ap);
      va_end(ap);
    }

  err = svn_io_file_open(handle, path, flags, protection, pool);
  if ((flags & APR_WRITE) && err && APR_STATUS_IS_EEXIST(err->apr_err))
    {
      /* Exclusive open failed, delete and retry */
      svn_error_clear(err);
      SVN_ERR(svn_io_remove_file(path, pool));
      err = svn_io_file_open(handle, path, flags, protection, pool);
    }

  if (err)
    {
      /* Oddly enough, APR will set *HANDLE even if the open failed.
         You'll get a filehandle whose descriptor is -1.  There must
         be a reason this is useful... Anyway, we don't want the
         handle. */
      *handle = NULL;
      /* If we receive a failure to open a file in our temporary directory,
       * it may be because our temporary directories aren't created.
       * Older SVN clients did not create these directories.
       * 'svn cleanup' will fix this problem.
       */
      if (APR_STATUS_IS_ENOENT(err->apr_err) && (flags & APR_WRITE))
        {
          err = svn_error_quick_wrap(err,
                               _("Your .svn/tmp directory may be missing or "
                                 "corrupt; run 'svn cleanup' and try again"));
        }
    }

  return err;
}


/* Close the file indicated by FP (PATH is passed to make error
 * reporting better).  If SYNC is non-zero, then the file will be
 * sync'd from the adm tmp area to its permanent location, otherwise
 * it will remain in the tmp area.  See open_adm_file().
 */
static svn_error_t *
close_adm_file(apr_file_t *fp,
               const char *path,
               const char *extension,
               svn_boolean_t sync,
               apr_pool_t *pool,
               ...)
{
  const char *tmp_path;
  va_list ap;

  /* Get the full name of the thing we're closing. */
  va_start(ap, pool);
  tmp_path = v_extend_with_adm_name(path, extension, sync, pool, ap);
  va_end(ap);

  SVN_ERR(svn_io_file_close(fp, pool));

  /* If we're syncing a tmp file, it needs to be renamed after closing. */
  if (sync)
    {
      /* Some code duplication with sync_adm_file() seems unavoidable,
         given how C va_lists work. */

      /* Obtain dest name. */
      va_start(ap, pool);
      path = v_extend_with_adm_name(path, extension, 0, pool, ap);
      va_end(ap);
      
      /* Rename. */
      SVN_ERR(svn_io_file_rename(tmp_path, path, pool));
      SVN_ERR(svn_io_set_file_read_only(path, FALSE, pool));
      
      return SVN_NO_ERROR;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__open_adm_file(apr_file_t **handle,
                      const char *path,
                      const char *fname,
                      apr_int32_t flags,
                      apr_pool_t *pool)
{
  return open_adm_file(handle, path, NULL, APR_OS_DEFAULT, flags, pool,
                       fname, NULL);
}


svn_error_t *
svn_wc__close_adm_file(apr_file_t *fp,
                       const char *path,
                       const char *fname,
                       int sync,
                       apr_pool_t *pool)
{
  return close_adm_file(fp, path, NULL, sync, pool, fname, NULL);
}


svn_error_t *
svn_wc__remove_adm_file(const char *path, apr_pool_t *pool, ...)
{
  va_list ap;

  va_start(ap, pool);
  path = v_extend_with_adm_name(path, NULL, 0, pool, ap);
  va_end(ap);
      
  SVN_ERR(svn_io_remove_file(path, pool));

  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc__open_text_base(apr_file_t **handle,
                       const char *path,
                       apr_int32_t flags,
                       apr_pool_t *pool)
{
  const char *parent_path, *base_name;
  svn_path_split(path, &parent_path, &base_name, pool);
  return open_adm_file(handle, parent_path, SVN_WC__BASE_EXT, APR_OS_DEFAULT,
                       flags, pool, SVN_WC__ADM_TEXT_BASE, base_name, NULL);
}


svn_error_t *
svn_wc__open_revert_base(apr_file_t **handle,
                         const char *path,
                         apr_int32_t flags,
                         apr_pool_t *pool)
{
  const char *parent_path, *base_name;
  svn_path_split(path, &parent_path, &base_name, pool);
  return open_adm_file(handle, parent_path, SVN_WC__REVERT_EXT, APR_OS_DEFAULT,
                       flags, pool, SVN_WC__ADM_TEXT_BASE, base_name, NULL);
}



svn_error_t *
svn_wc__close_text_base(apr_file_t *fp,
                        const char *path,
                        int write,
                        apr_pool_t *pool)
{
  const char *parent_path, *base_name;
  svn_path_split(path, &parent_path, &base_name, pool);
  return close_adm_file(fp, parent_path, SVN_WC__BASE_EXT, write, pool,
                        SVN_WC__ADM_TEXT_BASE, base_name, NULL);
}

svn_error_t *
svn_wc__close_revert_base(apr_file_t *fp,
                          const char *path,
                          int write,
                          apr_pool_t *pool)
{
  const char *parent_path, *base_name;
  svn_path_split(path, &parent_path, &base_name, pool);
  return close_adm_file(fp, parent_path, SVN_WC__REVERT_EXT, write, pool,
                        SVN_WC__ADM_TEXT_BASE, base_name, NULL);
}



svn_error_t *
svn_wc__open_props(apr_file_t **handle,
                   const char *path,
                   apr_int32_t flags,
                   svn_boolean_t base,
                   svn_boolean_t wcprops,
                   apr_pool_t *pool)
{
  const char *parent_dir, *base_name;
  svn_node_kind_t kind;
  int wc_format_version;

  SVN_ERR(svn_io_check_path(path, &kind, pool));
  if (kind == svn_node_dir)
    parent_dir = path;
  else
    svn_path_split(path, &parent_dir, &base_name, pool);
  
  /* At this point, we know we need to open a file in the admin area
     of parent_dir.  First check that parent_dir is a working copy: */
  SVN_ERR(svn_wc_check_wc(parent_dir, &wc_format_version, pool));
  if (wc_format_version == 0)
    return svn_error_createf
      (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
       _("'%s' is not a working copy"),
       svn_path_local_style(parent_dir, pool));

  /* Then examine the flags to know -which- kind of prop file to get. */

  if (base && wcprops)
    return svn_error_create(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                            _("No such thing as 'base' "
                              "working copy properties!"));

  else if (base)
    {
      if (kind == svn_node_dir)
        return open_adm_file(handle, parent_dir, NULL, APR_OS_DEFAULT, flags,
                             pool, SVN_WC__ADM_DIR_PROP_BASE, NULL);
      else
        return open_adm_file(handle, parent_dir, SVN_WC__BASE_EXT,
                             APR_OS_DEFAULT, flags, pool,
                             SVN_WC__ADM_PROP_BASE, base_name, NULL);
    }
  else if (wcprops)
    {
      if (kind == svn_node_dir)
        return open_adm_file(handle, parent_dir, NULL, APR_OS_DEFAULT, flags,
                             pool, SVN_WC__ADM_DIR_WCPROPS, NULL);
      else
        {
          return open_adm_file
            (handle, parent_dir,
             SVN_WC__WORK_EXT, APR_OS_DEFAULT,
             flags, pool, SVN_WC__ADM_WCPROPS, base_name, NULL);
        }
    }
  else /* plain old property file */
    {
      if (kind == svn_node_dir)
        return open_adm_file(handle, parent_dir, NULL, APR_OS_DEFAULT, flags,
                             pool, SVN_WC__ADM_DIR_PROPS, NULL);
      else
        {
          return open_adm_file
            (handle, parent_dir,
             SVN_WC__WORK_EXT, APR_OS_DEFAULT,
             flags, pool, SVN_WC__ADM_PROPS, base_name, NULL);
        }
    }
}



svn_error_t *
svn_wc__close_props(apr_file_t *fp,
                    const char *path,
                    svn_boolean_t base,
                    svn_boolean_t wcprops,
                    int sync,
                    apr_pool_t *pool)
{
  const char *parent_dir, *base_name;
  svn_node_kind_t kind;

  SVN_ERR(svn_io_check_path(path, &kind, pool));
  if (kind == svn_node_dir)
    parent_dir = path;
  else    
    svn_path_split(path, &parent_dir, &base_name, pool);
  
  /* At this point, we know we need to close a file in the admin area
     of parent_dir.  Since the file must be open already, we know that
     parent_dir is a working copy. */

  /* Then examine the flags to know -which- kind of prop file to get. */

  if (base && wcprops)
    return svn_error_create(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                            _("No such thing as 'base' "
                              "working copy properties!"));

  else if (base)
    {
      if (kind == svn_node_dir)
        return close_adm_file(fp, parent_dir, NULL, sync, pool,
                              SVN_WC__ADM_DIR_PROP_BASE, NULL);
      else
        return close_adm_file(fp, parent_dir, SVN_WC__BASE_EXT, sync, pool,
                              SVN_WC__ADM_PROP_BASE, base_name, NULL);
    }
  else if (wcprops)
    {
      if (kind == svn_node_dir)
        return close_adm_file(fp, parent_dir, NULL, sync, pool,
                              SVN_WC__ADM_DIR_WCPROPS, NULL);
      else
        return close_adm_file
          (fp, parent_dir,
           SVN_WC__WORK_EXT,
           sync, pool, SVN_WC__ADM_WCPROPS, base_name, NULL);
    }
  else /* plain old property file */
    {
      if (kind == svn_node_dir)
        return close_adm_file(fp, parent_dir, NULL, sync, pool,
                              SVN_WC__ADM_DIR_PROPS, NULL);
      else
        return close_adm_file
          (fp, parent_dir,
           SVN_WC__WORK_EXT,
           sync, pool, SVN_WC__ADM_PROPS, base_name, NULL);
    }
}



svn_error_t *
svn_wc__sync_props(const char *path,
                   svn_boolean_t base,
                   svn_boolean_t wcprops,
                   apr_pool_t *pool)
{
  const char *parent_dir, *base_name;
  svn_node_kind_t kind;

  /* Check if path is a file or a dir. */
  SVN_ERR(svn_io_check_path(path, &kind, pool));

  /* If file, split the path. */
  if (kind == svn_node_file)
    svn_path_split(path, &parent_dir, &base_name, pool);
  else    
    parent_dir = path;
  
  /* At this point, we know we need to open a file in the admin area
     of parent_dir.  Examine the flags to know -which- kind of prop
     file to get -- there are three types! */

  if (base && wcprops)
    return svn_error_create(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                            _("No such thing as 'base' "
                              "working copy properties!"));

  else if (base)
    {
      if (kind == svn_node_dir)
        return sync_adm_file(parent_dir, NULL, pool,
                             SVN_WC__ADM_DIR_PROP_BASE, NULL);
      else
        return sync_adm_file(parent_dir, SVN_WC__BASE_EXT, pool,
                             SVN_WC__ADM_PROP_BASE, base_name, NULL);
    }
  else if (wcprops)
    {
      if (kind == svn_node_dir)
        return sync_adm_file(parent_dir, NULL, pool,
                             SVN_WC__ADM_DIR_WCPROPS, NULL);
      else
        return sync_adm_file(parent_dir, SVN_WC__BASE_EXT, pool,
                             SVN_WC__ADM_WCPROPS, base_name, NULL);
    }
  else /* plain old property file */
    {
      if (kind == svn_node_dir)
        return sync_adm_file(parent_dir, NULL, pool,
                             SVN_WC__ADM_DIR_PROPS, NULL);
      else
        return sync_adm_file(parent_dir, SVN_WC__WORK_EXT, pool,
                             SVN_WC__ADM_PROPS, base_name, NULL);
    }

}




/*** Checking for and creating administrative subdirs. ***/

/* Set *EXISTS to true iff there's an adm area for PATH, and it matches URL
 * and REVISION.  If there's no adm area, set *EXISTS to false; if
 * there's an adm area but it doesn't match URL and REVISION, then
 * return error and don't touch *EXISTS.
 *
 * ### These semantics are totally bizarre.  One wonders what the
 * ### callers' real needs are.  In the long term, this function
 * ### should probably be unified with svn_wc_check_wc.
 */
static svn_error_t *
check_adm_exists(svn_boolean_t *exists,
                 const char *path,
                 const char *url,
                 svn_revnum_t revision,
                 apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  svn_node_kind_t kind;
  svn_boolean_t dir_exists = FALSE, wc_exists = FALSE;
  const char *tmp_path;

  /** Step 1: check that the directory exists. **/

  tmp_path = extend_with_adm_name(path, NULL, 0, pool, NULL);

  SVN_ERR(svn_io_check_path(tmp_path, &kind, pool));
  if (kind != svn_node_none && kind != svn_node_dir)
    {
      /* If got an error other than dir non-existence, then
         something's weird and we should return a genuine error. */
      return svn_error_createf(APR_ENOTDIR, NULL,
                               _("'%s' is not a directory"),
                               svn_path_local_style(tmp_path, pool));
    }
  else if (kind == svn_node_none)
    {
      dir_exists = FALSE;
    }
  else                      /* must be a dir. */
    {
      assert(kind == svn_node_dir);
      dir_exists = TRUE;
    }

  /** Step 1.  If no adm directory, then we're done. */
  if (! dir_exists)
    {
      *exists = FALSE;
      return SVN_NO_ERROR;
    }

  /** The directory exists, but is it a valid working copy yet?
      Try step 2: checking that we can read the format number. */
  {
    int wc_format;

    err = svn_io_read_version_file
      (&wc_format, svn_path_join(tmp_path, SVN_WC__ADM_ENTRIES, pool), pool);

    /* Fall back on the format file for WCs before format 7. */
    if (err)
      {
        svn_error_clear(err);
        err = svn_io_read_version_file
          (&wc_format, svn_path_join(tmp_path, SVN_WC__ADM_FORMAT, pool),
           pool);
      }

    if (err)
      {
        svn_error_clear(err);
        wc_exists = FALSE;
      }
    else
      wc_exists = TRUE;
  }

  /** Step 3: now check that repos and ancestry are correct **/

  if (wc_exists)
    {
      /* This is a bit odd.  We have to open an access baton, which relies
         on this being a working copy, in order to determine if this is a
         working copy! */
      svn_wc_adm_access_t *adm_access;
      const svn_wc_entry_t *entry;

      SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, path, FALSE, 0,
                               NULL, NULL, pool));
      SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, pool));
      SVN_ERR(svn_wc_adm_close(adm_access));
      if (!entry)
        return svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                                 _("No entry for '%s'"),
                                 svn_path_local_style(path, pool));

      /* When the directory exists and is scheduled for deletion do not
       * check the revision or the URL.  The revision can be any 
       * arbitrary revision and the URL may differ if the add is
       * being driven from a merge which will have a different URL. */
      if (entry->schedule != svn_wc_schedule_delete)
        {
          if (entry->revision != revision)
            return
              svn_error_createf
              (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
               _("Revision %ld doesn't match existing revision %ld in '%s'"),
               revision, entry->revision, path);

          /** ### comparing URLs, should they be canonicalized first? */
          if (strcmp(entry->url, url) != 0)
            return
              svn_error_createf
              (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
               _("URL '%s' doesn't match existing URL '%s' in '%s'"),
               url, entry->url, path);
	}
    }

  *exists = wc_exists;

  return SVN_NO_ERROR;
}


static svn_error_t *
make_empty_adm(const char *path, apr_pool_t *pool)
{
  path = extend_with_adm_name(path, NULL, 0, pool, NULL);
  SVN_ERR(svn_io_dir_make_hidden(path, APR_OS_DEFAULT, pool));
  return SVN_NO_ERROR;
}


static svn_error_t *
init_adm_tmp_area(svn_wc_adm_access_t *adm_access,
                  apr_pool_t *pool)
{
  /* Default perms */
  apr_fileperms_t perms = APR_OS_DEFAULT;

  /* SVN_WC__ADM_TMP */
  SVN_ERR(svn_wc__make_adm_thing(adm_access, SVN_WC__ADM_TMP,
                                 svn_node_dir, perms, 0, pool));
  
  /* SVN_WC__ADM_TMP/SVN_WC__ADM_TEXT_BASE */
  SVN_ERR(svn_wc__make_adm_thing(adm_access, SVN_WC__ADM_TEXT_BASE,
                                 svn_node_dir, perms, 1, pool));

  /* SVN_WC__ADM_TMP/SVN_WC__ADM_PROP_BASE */
  SVN_ERR(svn_wc__make_adm_thing(adm_access, SVN_WC__ADM_PROP_BASE,
                                 svn_node_dir, perms, 1, pool));

  /* SVN_WC__ADM_TMP/SVN_WC__ADM_PROPS */
  SVN_ERR(svn_wc__make_adm_thing(adm_access, SVN_WC__ADM_PROPS,
                                 svn_node_dir, perms, 1, pool));

  return SVN_NO_ERROR;
}


/* Set up a new adm area for PATH, with URL as the ancestor url, and
   INITIAL_REV as the starting revision.  The entries file starts out
   marked as 'incomplete.  The adm area starts out locked; remember to
   unlock it when done. */
static svn_error_t *
init_adm(const char *path,
         const char *uuid,
         const char *url,
         const char *repos,
         svn_revnum_t initial_rev,
         apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;

  /* Default perms */
  apr_fileperms_t perms = APR_OS_DEFAULT;

  /* First, make an empty administrative area. */
  SVN_ERR(make_empty_adm(path, pool));

  /* Lock it immediately.  Theoretically, no compliant wc library
     would ever consider this an adm area until a README file were
     present... but locking it is still appropriately paranoid. */
  SVN_ERR(svn_wc__adm_pre_open(&adm_access, path, pool));

  /** Make subdirectories. ***/

  /* SVN_WC__ADM_TEXT_BASE */
  SVN_ERR(svn_wc__make_adm_thing(adm_access, SVN_WC__ADM_TEXT_BASE,
                                 svn_node_dir, perms, 0, pool));

  /* SVN_WC__ADM_PROP_BASE */
  SVN_ERR(svn_wc__make_adm_thing(adm_access, SVN_WC__ADM_PROP_BASE,
                                 svn_node_dir, perms, 0, pool));

  /* SVN_WC__ADM_PROPS */
  SVN_ERR(svn_wc__make_adm_thing(adm_access, SVN_WC__ADM_PROPS,
                                 svn_node_dir, perms, 0, pool));

  /** Init the tmp area. ***/
  SVN_ERR(init_adm_tmp_area(adm_access, pool));
  
  /** Initialize each administrative file. */

  /* SVN_WC__ADM_ENTRIES */
  /* THIS FILE MUST BE CREATED LAST: 
     After this exists, the dir is considered complete. */
  SVN_ERR(svn_wc__entries_init(path, uuid, url, repos, initial_rev, pool));

  /* We provide this for backwards compatibilty.  Clients that don't understand
     format version 7 or higher will display a nicer error message if this
     file exists.
     ### Consider removing this in svn 1.5 or 1.6. */
  SVN_ERR(svn_io_write_version_file 
          (extend_with_adm_name(path, NULL, FALSE, pool,
                                SVN_WC__ADM_FORMAT, NULL),
           SVN_WC__VERSION, pool));

  /* Now unlock it.  It's now a valid working copy directory, that
     just happens to be at revision 0. */
  SVN_ERR(svn_wc_adm_close(adm_access));

  /* Else no problems, we're outta here. */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_ensure_adm2(const char *path,
                   const char *uuid,
                   const char *url,
                   const char *repos,
                   svn_revnum_t revision,
                   apr_pool_t *pool)
{
  svn_boolean_t exists_already;

  SVN_ERR(check_adm_exists(&exists_already, path, url, revision, pool));
  return (exists_already ? SVN_NO_ERROR :
          init_adm(path, uuid, url, repos, revision, pool));
}

svn_error_t *
svn_wc_ensure_adm(const char *path,
                  const char *uuid,
                  const char *url,
                  svn_revnum_t revision,
                  apr_pool_t *pool)
{
  return svn_wc_ensure_adm2(path, uuid, url, NULL, revision, pool);
}

svn_error_t *
svn_wc__adm_destroy(svn_wc_adm_access_t *adm_access, 
                    apr_pool_t *pool)
{
  const char *path;

  SVN_ERR(svn_wc__adm_write_check(adm_access));

  /* Well, the coast is clear for blowing away the administrative
     directory, which also removes the lock file */
  path = extend_with_adm_name(svn_wc_adm_access_path(adm_access),
                              NULL, FALSE, pool, NULL);
  SVN_ERR(svn_io_remove_dir(path, pool));
  SVN_ERR(svn_wc_adm_close(adm_access));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__adm_cleanup_tmp_area(svn_wc_adm_access_t *adm_access, 
                             apr_pool_t *pool)
{
  const char *tmp_path;

  SVN_ERR(svn_wc__adm_write_check(adm_access));

  /* Get the path to the tmp area, and blow it away. */
  tmp_path = extend_with_adm_name(svn_wc_adm_access_path(adm_access),
                                  NULL, 0, pool, SVN_WC__ADM_TMP, NULL);
  SVN_ERR(svn_io_remove_dir(tmp_path, pool));

  /* Now, rebuild the tmp area. */
  SVN_ERR(init_adm_tmp_area(adm_access, pool));

  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc_create_tmp_file2(apr_file_t **fp,
                        const char **new_name,
                        const char *path,
                        svn_io_file_del_t delete_when,
                        apr_pool_t *pool)
{
  apr_file_t *file;

  assert(fp || new_name);

  /* Use a self-explanatory name for the file :-) . */
  path = svn_wc__adm_path(path, TRUE, pool, "tempfile", NULL);

  /* Open a unique file;  use APR_DELONCLOSE. */
  SVN_ERR(svn_io_open_unique_file2(&file, new_name,
                                   path, ".tmp", delete_when, pool));


  if (fp)
    *fp = file;
  else
    SVN_ERR(svn_io_file_close(file, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_create_tmp_file(apr_file_t **fp,
                       const char *path,
                       svn_boolean_t delete_on_close,
                       apr_pool_t *pool)
{
  return svn_wc_create_tmp_file2(fp, NULL, path,
                                 delete_on_close
                                 ? svn_io_file_del_on_close
                                 : svn_io_file_del_none,
                                 pool);
}
