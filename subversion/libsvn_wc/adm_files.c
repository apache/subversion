/*
 * adm_files.c: helper routines for handling files & dirs in the
 *              working copy administrative area (creating,
 *              deleting, opening, and closing).  This is the only
 *              code that actually knows where administrative
 *              information is kept.  
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



#include <stdarg.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include <apr_time.h>
#include <apr_strings.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_wc.h"
#include "wc.h"



/*** Names in the SVN/ directory. ***/

/* No one outside this file should ever need to know this.  In fact,
   no one outside adm_subdir() should ever need to know this. */
#define SVN_WC__ADM_DIR_DEFAULT   "SVN"
static const char *
adm_subdir (void)
{
  return SVN_WC__ADM_DIR_DEFAULT;
}


/* Extend PATH to the name of something in PATH's administrative area.
 * Returns the number of path components added to PATH.
 * 
 * First, the adm subdir is appended to PATH as a component, then each
 * of the varargs in AP (char *'s) is appended as a path component.
 *
 * Important: chances are you will want to call chop_admin_name() to
 * restore PATH to its original value before exiting anything that
 * calls this.  If you exit, say by returning an error, before calling
 * chop_admin_name(), then PATH will still be in its extended state.
 *
 * So, the safest recipe is for callers of extend_with_adm_name() to
 * always have exactly one return statement, occurring *after* an
 * unconditional call to chop_admin_name().
 */
static int
v_extend_with_adm_name (svn_string_t *path,
                        svn_boolean_t use_tmp,
                        apr_pool_t *pool,
                        int num_vaparams,  /* how annoying */
                        va_list ap)
{
  const char *this;
  int components_added = 0;
  int i;

  /* Tack on the administrative subdirectory. */
  svn_path_add_component_nts (path, adm_subdir (), SVN_PATH_LOCAL_STYLE, pool);
  components_added++;

  /* If this is a tmp file, name it into the tmp area. */
  if (use_tmp)
    {
      svn_path_add_component_nts
        (path, SVN_WC__ADM_TMP, SVN_PATH_LOCAL_STYLE, pool);
      components_added++;
    }

  /* Tack on everything else. */
  for (i = 0; i < num_vaparams; i++)
    {
      this = va_arg (ap, const char *);
      svn_path_add_component_nts (path, this, SVN_PATH_LOCAL_STYLE, pool);
      components_added++;
    }

  return components_added;
}


/* See v_extend_with_adm_name() for details. */
static int
extend_with_adm_name (svn_string_t *path,
                      svn_boolean_t use_tmp,
                      apr_pool_t *pool,
                      int num_vaparams,
                      ...)
{
  va_list ap;
  int components_added;

  va_start (ap, num_vaparams);
  components_added = v_extend_with_adm_name (path,
                                             use_tmp,
                                             pool,
                                             num_vaparams,
                                             ap);
  va_end (ap);

  return components_added;
}


/* Restore PATH to what it was before a call to
 * extend_with_adm_name(), by lopping off NUM_COMPONENTS
 * components.
 */
static void
chop_admin_name (svn_string_t *path, int num_components)
{
  while (num_components-- > 0)
    svn_path_remove_component (path, SVN_PATH_LOCAL_STYLE);
}


/* Create an empty THING in the adm area. 
 * If TMP is non-zero, then create THING in the tmp dir.
 *
 * Does not check if THING already exists, so be careful -- THING will
 * be empty after this no matter what.
 */
svn_error_t *
svn_wc__make_adm_thing (svn_string_t *path,
                        const char *thing,
                        int type,
                        svn_boolean_t tmp,
                        apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_file_t *f = NULL;
  apr_status_t apr_err = 0;
  int components_added;

  components_added = extend_with_adm_name (path, tmp, pool, 1, thing);

  if (type == svn_file_kind)
    {
      apr_err = apr_open (&f, path->data,
                          (APR_WRITE | APR_CREATE | APR_EXCL),
                          APR_OS_DEFAULT,
                          pool);

      if (apr_err)
        err = svn_create_error (apr_err, 0, path->data, NULL, pool);
      else
        {
          /* Creation succeeded, so close immediately. */
          apr_err = apr_close (f);
          if (apr_err)
            err = svn_create_error (apr_err, 0, path->data, NULL, pool);
        }
    }
  else if (type == svn_dir_kind)
    {
      apr_err = apr_make_dir (path->data, APR_OS_DEFAULT, pool);
      if (apr_err)
        err = svn_create_error (apr_err, 0, path->data, NULL, pool);
    }
  else   /* unknown type argument, wrongness */
    {
      /* We're only capturing this here because there wouldn't be a
         segfault or other obvious indicator that something went
         wrong.  Even so, not sure if it's appropriate.  Thoughts? */
      err = svn_create_error 
        (0, 0, "svn_wc__make_admin_thing: bad type indicator", NULL, pool);
    }

  /* Restore path to its original state no matter what. */
  chop_admin_name (path, components_added);

  return err;
}



/*** copying files ***/

#ifndef apr_copy_file
/**
 * copy one file to another
 * @param from_path The full path to the source file (using / on all systems)
 * @param to_path The full path to the dest file (using / on all systems)
 * @param pool The pool to use.
 * @tip If a file exists at the new location, then it will be overwritten.  
 * @tip The source file will be copied until EOF is reached, not until
 *      its size at the time of opening is reached.
 * @tip The dest file's permissions will be the same as the source file's.
 */
static apr_status_t
apr_copy_file (const char *src, const char *dst, apr_pool_t *pool)
{
  apr_file_t *s, *d;
  apr_status_t apr_err;
  apr_status_t read_err, write_err;
  apr_finfo_t finfo;
  apr_fileperms_t perms;
  char buf[BUFSIZ];

  /* Open source file. */
  apr_err = apr_open (&s, src, APR_READ, APR_OS_DEFAULT, pool);
  if (apr_err)
    return apr_err;
  
  /* Get its size. */
  apr_err = apr_getfileinfo (&finfo, s);
  if (apr_err)
    {
      apr_close (s);  /* toss any error */
      return apr_err;
    }
  else
    perms = finfo.protection;

  /* Open dest file. */
  apr_err = apr_open (&d, dst, (APR_WRITE | APR_CREATE), perms, pool);
  if (apr_err)
    {
      apr_close (s);  /* toss */
      return apr_err;
    }
  
  /* Copy bytes till the cows come home. */
  while (read_err != APR_EOF)
    {
      apr_ssize_t bytes_this_time = sizeof (buf);

      /* Read 'em. */
      read_err = apr_read (s, buf, &bytes_this_time);
      if (read_err && (read_err != APR_EOF))
        {
          apr_close (s);  /* toss */
          apr_close (d);  /* toss */
          return read_err;
        }

      /* Write 'em. */
      /* kff note: I wonder why apr_read() and apr_write() use a
         single argument to say both how much data is desired and how
         much actually got read, but apr_full_read() and
         apr_full_write() use two separate args? */
      write_err = apr_full_write (d, buf, bytes_this_time, NULL);
      if (write_err)
        {
          apr_close (s);  /* toss */
          apr_close (d);
          return write_err;
        }

      if (read_err && (read_err == APR_EOF))
        {
          apr_err = apr_close (s);
          if (apr_err)
            {
              apr_close (d);
              return apr_err;
            }
          
          apr_err = apr_close (d);
          if (apr_err)
            return apr_err;
        }
    }

  return 0;
}
#endif /* apr_copy_file */


static svn_error_t *
copy_file (svn_string_t *src, svn_string_t *dst, apr_pool_t *pool)
{
  apr_status_t apr_err;

  apr_err = apr_copy_file (src->data, dst->data, pool);
  if (apr_err)
    {
      const char *msg = apr_psprintf(pool, "copying %s to %s", src, dst);

      return svn_create_error (apr_err, 0, msg, NULL, pool);
    }
  else
    return SVN_NO_ERROR;
}



/*** opening all kinds of adm files ***/

/* Rename a tmp file in PATH's adm area to the real thing.
   The file had better already be closed. */
svn_error_t *
svn_wc__sync_adm_file (svn_string_t *path,
                       apr_pool_t *pool,
                       int num_vaparams,
                       ...)
{
  /* kff todo: big code duplication with close_adm_file(), see comment
     there */

  svn_string_t *tmp_path = svn_string_dup (path, pool);
  apr_status_t apr_err;
  svn_error_t *err;
  int components_added;
  va_list ap;
  
  /* Extend real name. */
  va_start (ap, num_vaparams);
  components_added
    = v_extend_with_adm_name (path, 0, pool, num_vaparams, ap);
  va_end (ap);
  
  /* Extend tmp name. */
  va_start (ap, num_vaparams);
  v_extend_with_adm_name (tmp_path, 1, pool, num_vaparams, ap);
  va_end (ap);
  
  /* Rename. */
  apr_err = apr_rename_file (tmp_path->data, path->data, pool);

  /* Unconditionally restore path. */
  chop_admin_name (path, components_added);
      
  if (apr_err)
    {
      const char *msg = apr_psprintf (pool, "error renaming %s to %s",
                                      tmp_path->data, path->data);
      return svn_create_error (apr_err, 0, msg, NULL, pool);
    }
  else
    return SVN_NO_ERROR;
}


/* Open a file somewhere in the adm area for directory PATH.
 * First, the adm subdir is appended as a path component, then each of
 * the varargs (they are char *'s) is appended as a path component,
 * and the resulting file opened.  The path to it is guaranteed,
 * caller does not have to worry if all components existed on disk
 * before this call.
 *
 * kff todo: finish documenting this function.
 *
 * kff todo: in general, we have a dichotomy between svn_string_t for
 * vs char * for filenames.  To convert the former to the latter we
 * just take svn_string_t_ptr->data (it's a deliberately open
 * structure interface), which I don't think is ever a problem in
 * practice, but maybe there's a better general solution...
 */
static svn_error_t *
open_adm_file (apr_file_t **handle,
               svn_string_t *path,
               apr_int32_t flags,
               apr_pool_t *pool,
               int num_vaparams,
               ...)
{
  svn_error_t *err = NULL;
  apr_status_t apr_err = 0;
  int components_added;
  va_list ap;

  /* kff todo: it is assumed that either both APR_WRITE and APR_APPEND
     are set, or none.  Is this a safe assumption? */

  /* If we're writing, always do it to a tmp file. */
  if (flags & APR_WRITE)
    {
      if (flags & APR_APPEND)
        {
          svn_string_t *opath, *tmp_path;  /* just keep it all local */

          opath    = svn_string_dup (path, pool);
          tmp_path = svn_string_dup (path, pool);

          va_start (ap, num_vaparams);
          v_extend_with_adm_name (opath, 0, pool, num_vaparams, ap);
          va_end (ap);

          va_start (ap, num_vaparams);
          v_extend_with_adm_name (tmp_path, 1, pool, num_vaparams, ap);
          va_end (ap);

          /* Copy the original thing to the tmp location. */
          err = copy_file (opath, tmp_path, pool);
          if (err)
            return err;
        }

      /* Extend with tmp name. */
      va_start (ap, num_vaparams);
      components_added
        = v_extend_with_adm_name (path, 1, pool, num_vaparams, ap);
      va_end (ap);
    }
  else
    {
      /* Extend with regular adm name. */
      va_start (ap, num_vaparams);
      components_added
        = v_extend_with_adm_name (path, 0, pool, num_vaparams, ap);
      va_end (ap);
    }

  apr_err = apr_open (handle, path->data, flags, APR_OS_DEFAULT, pool);
  if (apr_err)
    err = svn_create_error (apr_err, 0, path->data, NULL, pool);

  /* Restore path to its original state no matter what. */
  chop_admin_name (path, components_added);

  return err;
}


/* kff todo: finish documenting this function. */
static svn_error_t *
close_adm_file (apr_file_t *fp,
                svn_string_t *path,
                svn_boolean_t sync,
                apr_pool_t *pool,
                int num_vaparams,
                ...)
{
  svn_error_t *err = NULL;
  svn_string_t *tmp_path;
  apr_status_t apr_err = 0;
  int components_added;
  va_list ap;

  /* Get the full name of the thing we want. */
  va_start (ap, num_vaparams);
  components_added =
    v_extend_with_adm_name (path, sync, pool, num_vaparams, ap);
  va_end (ap);

  apr_err = apr_close (fp);

  /* Restore path to its original state no matter what. */
  chop_admin_name (path, components_added);

  if (apr_err)
    return svn_create_error (apr_err, 0, tmp_path->data, NULL, pool);

  /* If we were writing, then it was to a tmp file, which will have to
     be renamed after closing. */
  if (sync)
    {
      /* kff todo: this is a big code duplication with
         svn_wc__sync_adm_file(), perhaps temporary if certain
         properties I hope for in va_lists turn out to be present.
         Will talk to greg or jimb, they probably know. */

      svn_string_t *tmp_path = svn_string_dup (path, pool);
      apr_status_t apr_err;
      svn_error_t *err;
      int components_added;
      
      /* Extend real name. */
      va_start (ap, num_vaparams);
      components_added
        = v_extend_with_adm_name (path, 0, pool, num_vaparams, ap);
      va_end (ap);
      
      /* Extend tmp name. */
      va_start (ap, num_vaparams);
      v_extend_with_adm_name (tmp_path, 1, pool, num_vaparams, ap);
      va_end (ap);
      
      /* Rename. */
      apr_err = apr_rename_file (tmp_path->data, path->data, pool);
      
      /* Unconditionally restore path. */
      chop_admin_name (path, components_added);
      
      if (apr_err)
        {
          const char *msg = apr_psprintf (pool, "error renaming %s to %s",
                                          tmp_path->data, path->data);
          return svn_create_error (apr_err, 0, msg, NULL, pool);
        }
      else
        return SVN_NO_ERROR;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__open_adm_file (apr_file_t **handle,
                       svn_string_t *path,
                       const char *fname,
                       apr_int32_t flags,
                       apr_pool_t *pool)
{
  *handle = NULL;  /* satisfy APR's bizarre requirement */
  return open_adm_file (handle, path, flags, pool, 1, fname);
}


svn_error_t *
svn_wc__close_adm_file (apr_file_t *fp,
                        svn_string_t *path,
                        const char *fname,
                        int sync,
                        apr_pool_t *pool)
{
  return close_adm_file (fp, path, sync, pool, 1, fname);
}


/* kff todo: svn_wc__*_text_base() are all essentially the same except
   for one function call... Abstracting their guts might be nice, but
   then again it's not a lot of code and tossing void *'s and pointers
   to vararg functions into the pot might just make things muddier,
   too. */


svn_error_t *
svn_wc__open_text_base (apr_file_t **handle,
                        svn_string_t *path,
                        apr_int32_t flags,
                        apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  svn_string_t *last_component
    = svn_path_last_component (path, SVN_PATH_LOCAL_STYLE, pool);

  svn_path_remove_component (path, SVN_PATH_LOCAL_STYLE);

  err = open_adm_file (handle, path, flags, pool, 2,
                       SVN_WC__ADM_TEXT_BASE, last_component->data);

  /* Restore caller's path unconditionally. */
  svn_path_add_component (path, last_component,
                          SVN_PATH_LOCAL_STYLE, pool);

  return err;
}


svn_error_t *
svn_wc__close_text_base (apr_file_t *fp,
                         svn_string_t *path,
                         int write,
                         apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  svn_string_t *last_component
    = svn_path_last_component (path, SVN_PATH_LOCAL_STYLE, pool);

  svn_path_remove_component (path, SVN_PATH_LOCAL_STYLE);

  err = close_adm_file (fp, path, write, pool, 2,
                        SVN_WC__ADM_TEXT_BASE, last_component->data);
  
  /* Restore caller's path unconditionally. */
  svn_path_add_component (path, last_component,
                          SVN_PATH_LOCAL_STYLE, pool);

  return err;
}


/* Rename a tmp text-base file to its real text-base name.
   The file had better already be closed. */
svn_error_t *
svn_wc__sync_text_base (svn_string_t *path, apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  svn_string_t *last_component
    = svn_path_last_component (path, SVN_PATH_LOCAL_STYLE, pool);

  svn_path_remove_component (path, SVN_PATH_LOCAL_STYLE);

  err = svn_wc__sync_adm_file (path,
                               pool,
                               2,
                               SVN_WC__ADM_TEXT_BASE,
                               last_component->data);
  
  /* Restore caller's path unconditionally. */
  svn_path_add_component (path, last_component,
                          SVN_PATH_LOCAL_STYLE, pool);

  return err;
}


/* Remove path/SVN/thing. 
   kff todo: not variadic yet, maybe never needs to be */
svn_error_t *
svn_wc__remove_adm_thing (svn_string_t *path,
                          const char *thing,
                          apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_status_t apr_err = 0;
  int components_added;

  components_added = extend_with_adm_name (path, 0, pool, 1, thing);

  apr_err = apr_remove_file (path->data, pool);
  if (apr_err)
    err = svn_create_error (apr_err, 0, path->data, NULL, pool);

  /* Restore path to its original state no matter what. */
  chop_admin_name (path, components_added);

  return err;
}



/*** Checking for and creating administrative subdirs. ***/

/* Set *EXISTS to non-zero iff there's an adm area for PATH.
   If an error occurs, just return error and don't touch *EXISTS. */
static svn_error_t *
check_adm_exists (int *exists, svn_string_t *path, apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_status_t apr_err;
  apr_dir_t *ignore_me = NULL;
  int dir_exists = 0;
  apr_file_t *f = NULL;
  int components_added;

  /** Step 1: check that the directory exists. **/

  components_added = extend_with_adm_name (path, 0, pool, 0);
  apr_err = apr_opendir (&ignore_me, path->data, pool);

  if (apr_err && (apr_err != APR_ENOENT))
    {
      /* If got an error other than dir non-existence, then
         something's weird and we should return a genuine error. */
      err = svn_create_error (apr_err, 0, path->data, NULL, pool);
    }
  else if (apr_err)   /* APR_ENOENT */
    {
      dir_exists = 0;
      apr_err = 0;
    }
  else                /* dir opened, so it must exist */
    {
      dir_exists = 1;
      apr_err = apr_closedir (ignore_me);
    }

  if (apr_err)
    err = svn_create_error (apr_err, 0, path->data, NULL, pool);

  /* Restore path to its original state. */
  chop_admin_name (path, components_added);

  /* Okay, first stopping point; see how we're doing. */
  if (err)
    return err;
  else if (! dir_exists)
    {
      *exists = 0;
      return SVN_NO_ERROR;
    }

  /** The directory exists, but is it a valid working copy yet?
      Try step 2: checking that SVN_WC__ADM_README exists. **/

  err = svn_wc__open_adm_file (&f, path, SVN_WC__ADM_README, APR_READ, pool);
  if (err && (err->apr_err != APR_EEXIST))
    return err;
  else if (err)
    *exists = 0;
  else
    *exists = 1;

  err = svn_wc__close_adm_file (f, path, SVN_WC__ADM_README, 0, pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}


static svn_error_t *
make_empty_adm (svn_string_t *path, apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;
  int components_added;

  components_added = extend_with_adm_name (path, 0, pool, 0);

  apr_err = apr_make_dir (path->data, APR_OS_DEFAULT, pool);
  if (apr_err)
    err = svn_create_error (apr_err, 0, path->data, NULL, pool);
    
  chop_admin_name (path, components_added);

  return err;
}


/* Init an adm file with some contents. 
   Don't call this until a tmp area exists in adm. */
static svn_error_t *
init_adm_file (svn_string_t *path,
               const char *thing,
               svn_string_t *contents,
               apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;
  apr_file_t *f = NULL;
  apr_size_t written = 0;

  err = svn_wc__open_adm_file (&f, path, thing, APR_WRITE | APR_CREATE, pool);
  if (err)
    return err;

  apr_err = apr_full_write (f, contents->data, contents->len, &written);

  err = svn_wc__close_adm_file (f, path, thing, 1, pool);
  if (err)
    return err;
  
  if (apr_err)
    err = svn_create_error (apr_err, 0, path->data, NULL, pool);

  return err;
}


/** kff todo: I think everything below here is ignorant of actual adm
    locations, and could be moved to adm_ops.c or some other place.
    But wait on that for a bit. **/

/* Set up a new adm area, with appropriate ancestry. 
   The adm area starts out locked; remember to unlock it when done. */
static svn_error_t *
init_adm (svn_string_t *path,
          svn_string_t *repository,
          apr_pool_t *pool)
{
  svn_error_t *err;

  /* Initial contents for certain adm files. */
  const char *format_contents = "1\n";
  const char *readme_contents =
    "This is a Subversion working copy administrative directory.\n"
    "Visit http://subversion.tigris.org/ for more information.\n";

  /* Always create at version 0. */
  svn_string_t *versions_contents
    = svn_wc__versions_init_contents (0, pool);

  /* First, make an empty administrative area. */
  err = make_empty_adm (path, pool);

  /* Lock it immediately.  Theoretically, no compliant wc library
     would ever consider this an adm area until a README file were
     present... but locking it is still appropriately paranoid. */
  err = svn_wc__lock (path, 0, pool);
  if (err)
    return err;


  /** Make subdirectories. ***/

  /* SVN_WC__ADM_TMP */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_TMP,
                                svn_dir_kind, 0, pool);
  if (err)
    return err;
  
  /* SVN_WC__ADM_TEXT_BASE */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_TEXT_BASE,
                                svn_dir_kind, 0, pool);
  if (err)
    return err;


  /* SVN_WC__ADM_PROP_BASE */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_PROP_BASE,
                                svn_dir_kind, 0, pool);
  if (err)
    return err;


  /* SVN_WC__ADM_DPROP_BASE */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_DPROP_BASE,
                                svn_dir_kind, 0, pool);
  if (err)
    return err;


  /** Make sub-subdirectories. ***/

  /* SVN_WC__ADM_TMP/SVN_WC__ADM_TEXT_BASE */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_TEXT_BASE,
                                svn_dir_kind, 1, pool);
  if (err)
    return err;


  /* SVN_WC__ADM_TMP/SVN_WC__ADM_PROP_BASE */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_PROP_BASE,
                                svn_dir_kind, 1, pool);
  if (err)
    return err;


  /* SVN_WC__ADM_TMP/SVN_WC__ADM_DPROP_BASE */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_DPROP_BASE,
                                svn_dir_kind, 1, pool);
  if (err)
    return err;


  /** Initialize each administrative file. */

  /* SVN_WC__ADM_FORMAT */
  err = init_adm_file (path, SVN_WC__ADM_FORMAT,
                       svn_string_create (format_contents, pool), pool);
  if (err)
    return err;


  /* SVN_WC__ADM_REPOSITORY */
  err = init_adm_file (path, SVN_WC__ADM_REPOSITORY, repository, pool);
  if (err)
    return err;


  /* SVN_WC__ADM_VERSIONS */
  err = init_adm_file (path, SVN_WC__ADM_VERSIONS, versions_contents, pool);
  if (err)
    return err;


  /* SVN_WC__ADM_DELTA_HERE */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_DELTA_HERE,
                                svn_file_kind, 0, pool);
  if (err)
    return err;
  

  /* SVN_WC__ADM_PROPERTIES */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_PROPERTIES,
                                svn_file_kind, 0, pool);
  if (err)
    return err;


  /* THIS FILE MUST BE CREATED LAST: 
     After this exists, the dir is considered complete. */
  err = init_adm_file (path, SVN_WC__ADM_README,
                       svn_string_create (readme_contents, pool),
                       pool);
  if (err)
    return err;


  /* Now unlock it.  It's now a valid working copy directory, that
     just happens to be at version 0. */
  err = svn_wc__unlock (path, pool);
  if (err)
    return err;

  /* Else no problems, we're outta here. */
  return SVN_NO_ERROR;
}


/* Make sure that PATH (a directory) contains a complete adm area,
 * based at REPOSITORY.
 *
 * Creates the adm area if none, in which case PATH starts out at
 * version 0.
 */
svn_error_t *
svn_wc__ensure_adm (svn_string_t *path,
                    svn_string_t *repository,
                    apr_pool_t *pool)
{
  svn_error_t *err;
  int exists_already;

  /* kff todo: check repos... and ancestry? */
  err = check_adm_exists (&exists_already, path, pool);
  if (err)
    return err;

  if (! exists_already)
    {
      err = init_adm (path, repository, pool);
      if (err)
        return err;
    }
        
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
