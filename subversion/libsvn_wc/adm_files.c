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


/* Make name of wc admin file ADM_FILE by appending to directory PATH. 
 * Returns the number of path components added to PATH.
 * 
 * IMPORTANT: chances are you will want to call chop_admin_thing() to
 * restore PATH to its original value before exiting anything that
 * calls this.  If you exit, say by returning an error, before calling
 * chop_admin_thing(), then PATH will still be in its extended state.
 *
 * So, safest recipe:
 *
 * Callers of extend_with_admin_name() always have exactly one return
 * statement, and that return occurs *after* an unconditional call to
 * chop_admin_thing().
 */
static int
extend_with_admin_name (svn_string_t *path,
                        const char *adm_file,
                        apr_pool_t *pool)
{
  int components_added = 0;

  svn_path_add_component_nts (path, adm_subdir (), SVN_PATH_LOCAL_STYLE, pool);
  components_added++;

  if (adm_file && (adm_file[0] != '\0'))
    {
      svn_path_add_component_nts (path, adm_file, SVN_PATH_LOCAL_STYLE, pool);
      components_added++;
    }

  return components_added;
}


/* Restore PATH to what it was before a call to
 * extend_with_admin_name(), by lopping off NUM_COMPONENTS
 * components.
 */
static void
chop_admin_thing (svn_string_t *path, int num_components)
{
  while (num_components-- > 0)
    svn_path_remove_component (path, SVN_PATH_LOCAL_STYLE);
}


/* Helper func for the svn_wc__init_FILE() functions. */
svn_error_t *
svn_wc__make_adm_thing (svn_string_t *path,
                        const char *thing,
                        int type,
                        apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_file_t *f = NULL;
  apr_status_t apr_err = 0;
  int components_added;

  components_added = extend_with_admin_name (path, thing, pool);

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
      err = svn_create_error 
        (0, 0, "svn_wc__make_admin_thing: bad type indicator", NULL, pool);
    }

  /* Restore path to its original state no matter what. */
  chop_admin_thing (path, components_added);

  return err;
}


/* Anything beginning with a `.' is okay.
 * To avoid confusing people about purpose of tmp/ dir vs these
 * temporary files, am using .wkg instead of .tmp as the
 * extension.  I'd like to use the PID, but how to get that portably
 * via APR?
 */
#define SVN_WC__TMP_EXTENSION ".wkg"


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
  char buf[1024];

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


svn_error_t *
svn_wc__open_adm_file (apr_file_t **handle,
                       svn_string_t *path,
                       const char *fname,
                       apr_int32_t flags,
                       apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_status_t apr_err = 0;
  svn_string_t *true_path;  /* a tmp file, if we're writing */
  int components_added;

  components_added = extend_with_admin_name (path, fname, pool);

  /* If we're writing, always do it to a tmp file.  We'll rename on
     close. */
  if (flags & APR_WRITE)
    {
      true_path = svn_string_dup (path, pool);
      svn_string_appendbytes (true_path,
                              SVN_WC__TMP_EXTENSION,
                              sizeof (SVN_WC__TMP_EXTENSION),
                              pool);

      if (flags & APR_APPEND)
        {
          err = copy_file (path, true_path, pool);
          if (err)
            return err;
        }
    }
  else
    true_path = path;

  apr_err = apr_open (handle, true_path->data, flags, APR_OS_DEFAULT, pool);

  if (apr_err)
    err = svn_create_error (apr_err, 0, true_path->data, NULL, pool);

  /* Restore path to its original state no matter what. */
  chop_admin_thing (path, components_added);

  return err;
}


svn_error_t *
svn_wc__close_adm_file (apr_file_t *fp,
                        svn_string_t *path,
                        const char *fname,
                        int write,
                        apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  svn_string_t *true_path;
  apr_status_t apr_err = 0;
  int components_added;

  components_added = extend_with_admin_name (path, fname, pool);

  apr_err = apr_close (fp);

  if (write)
    {
      true_path = svn_string_dup (path, pool);
      svn_string_appendbytes (true_path,
                              SVN_WC__TMP_EXTENSION,
                              sizeof (SVN_WC__TMP_EXTENSION),
                              pool);

      if (apr_err)
        {
          /* We already encountered an error, so just set the right
             error message and leave. */
          err = svn_create_error (apr_err, 0, true_path->data, NULL, pool);
        }
      else
        {
          apr_err = apr_rename_file (true_path->data, path->data, pool);
          if (apr_err)
            err = svn_create_error (apr_err, 0, true_path->data, NULL, pool);
        }
    }
  else
    {
      /* Just check for an error on the original close. */
      if (apr_err)
        err = svn_create_error (apr_err, 0, true_path->data, NULL, pool);
    }

  /* Restore path to its original state no matter what. */
  chop_admin_thing (path, components_added);

  return err;
}


/* Remove path/SVN/thing. */
svn_error_t *
svn_wc__remove_adm_thing (svn_string_t *path,
                          const char *thing,
                          apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_status_t apr_err = 0;
  int components_added;

  components_added = extend_with_admin_name (path, thing, pool);

  apr_err = apr_remove_file (path->data, pool);
  if (apr_err)
    err = svn_create_error (apr_err, 0, path->data, NULL, pool);

  /* Restore path to its original state no matter what. */
  chop_admin_thing (path, components_added);

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

  components_added = extend_with_admin_name (path, NULL, pool);
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
  chop_admin_thing (path, components_added);

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

  components_added = extend_with_admin_name (path, NULL, pool);

  apr_err = apr_make_dir (path->data, APR_OS_DEFAULT, pool);
  if (apr_err)
    err = svn_create_error (apr_err, 0, path->data, NULL, pool);
    
  chop_admin_thing (path, components_added);

  return err;
}


/* This doesn't have to do the usual tmpfile/rename routine, because
   we're initializing the files and the adm area isn't valid until
   we're done anyway. */
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


  /** Now initialize each of the administrative files. */


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
                                svn_file_kind, pool);
  if (err)
    return err;
  

  /* SVN_WC__ADM_PROPERTIES */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_PROPERTIES,
                                svn_file_kind, pool);
  if (err)
    return err;


  /* SVN_WC__ADM_TEXT_BASE */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_TEXT_BASE,
                                svn_dir_kind, pool);
  if (err)
    return err;


  /* SVN_WC__ADM_PROP_BASE */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_PROP_BASE,
                                svn_dir_kind, pool);
  if (err)
    return err;


  /* SVN_WC__ADM_DPROP_BASE */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_DPROP_BASE,
                                svn_dir_kind, pool);
  if (err)
    return err;


  /* SVN_WC__ADM_TMP */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_TMP,
                                svn_dir_kind, pool);
  if (err)
    return err;
  

  /* SVN_WC__ADM_DOING */
  err = svn_wc__make_adm_thing (path, SVN_WC__ADM_DOING,
                                svn_dir_kind, pool);
  if (err)
    return err;


  /* THIS FILE MUST BE CREATED LAST: 
     After this exists, the dir is considered complete. */
  err = init_adm_file (path, SVN_WC__ADM_README,
                       svn_string_create (readme_contents, pool),
                       pool);
  if (err)
    return err;


  /* Now unlock it.  It's already a valid working copy directory, it
     just happens to be at version 0. */
  err = svn_wc__unlock (path, pool);
  if (err)
    return err;

  /* Else no problems, we're outta here. */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__ensure_adm (svn_string_t *path,
                    svn_string_t *repository,
                    int *exists_already,
                    apr_pool_t *pool)
{
  svn_error_t *err;

  err = check_adm_exists (exists_already, path, pool);
  if (err)
    return err;

  if (! *exists_already)
    {
      /* kff todo: modify call chain to pass ancestry down? */
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
