/*
 * io.c:   shared file reading, writing, and probing code.
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



#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <apr_lib.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_file_info.h>
#include <apr_general.h>
#include <apr_strings.h>
#include <apr_thread_proc.h>
#include <apr_portable.h>
#include <apr_md5.h>
#include "svn_types.h"
#include "svn_path.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_base64.h"
#include "svn_pools.h"
#include "svn_utf.h"
#include "svn_config.h"
#include "svn_private_config.h" /* for SVN_CLIENT_DIFF */


struct svn_stream_t {
  void *baton;
  svn_read_fn_t read_fn;
  svn_write_fn_t write_fn;
  svn_close_fn_t close_fn;
};



svn_error_t *
svn_io_check_path (const char *path,
                   svn_node_kind_t *kind,
                   apr_pool_t *pool)
{
  apr_finfo_t finfo;
  apr_status_t apr_err;
  const char *path_native;

  if (path[0] == '\0')
    path = ".";

  /* Not using svn_io_stat() here because we want to check the
     apr_err return anyway. */
  SVN_ERR (svn_utf_cstring_from_utf8 (&path_native, path, pool));
  apr_err = apr_stat (&finfo, path_native, APR_FINFO_MIN, pool);

  if (apr_err && !APR_STATUS_IS_ENOENT(apr_err))
    return svn_error_createf
      (apr_err, 0, NULL,
       "svn_io_check_path: problem checking path \"%s\"", path);
  else if (APR_STATUS_IS_ENOENT(apr_err))
    *kind = svn_node_none;
  else if (finfo.filetype == APR_NOFILE)
    *kind = svn_node_unknown;
  else if (finfo.filetype == APR_REG)
    *kind = svn_node_file;
  else if (finfo.filetype == APR_DIR)
    *kind = svn_node_dir;
#if 0
  else if (finfo.filetype == APR_LINK)
    *kind = svn_node_symlink;  /* we support symlinks someday, but not yet */
#endif /* 0 */
  else
    *kind = svn_node_unknown;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_open_unique_file (apr_file_t **f,
                         const char **unique_name,
                         const char *path,
                         const char *suffix,
                         svn_boolean_t delete_on_close,
                         apr_pool_t *pool)
{
  char number_buf[6];
  int i;
  apr_size_t iterating_portion_idx;
  const char *unique_name_native;
  svn_stringbuf_t *unique_name_buf;

  /* The random portion doesn't have to be very random; it's just to
     avoid a series of collisions where someone has filename NAME and
     also NAME.00001.tmp, NAME.00002.tmp, etc, under version control
     already, which might conceivably happen.  The random portion is a
     last-ditch safeguard against that case.  It's okay, and even
     preferable, for tmp files to collide with each other, though, so
     that the iterating portion changes instead.  Taking the pointer
     as an unsigned short int has more or less this effect. */
  int random_portion_width;
  char *random_portion = apr_psprintf 
    (pool, "%hu%n",
     (unsigned int)unique_name,
     &random_portion_width);

  unique_name_buf = svn_stringbuf_create (path, pool);

  /* Not sure of a portable PATH_MAX constant to use here, so just
     guessing at 255. */
  if (unique_name_buf->len >= 255)
    {
      int chop_amt = (unique_name_buf->len - 255)
                      + random_portion_width
                      + 3  /* 2 dots */
                      + 5  /* 5 digits of iteration portion */
                      + strlen (suffix);
      svn_stringbuf_chop (unique_name_buf, chop_amt);
    }

  iterating_portion_idx = unique_name_buf->len + random_portion_width + 2;
  svn_stringbuf_appendcstr (unique_name_buf,
                            apr_psprintf (pool, ".%s.00000%s",
                                          random_portion, suffix));

  for (i = 1; i <= 99999; i++)
    {
      apr_status_t apr_err;
      apr_int32_t flag = (APR_READ | APR_WRITE | APR_CREATE | APR_EXCL);

      if (delete_on_close)
        flag |= APR_DELONCLOSE;

      /* Tweak last attempted name to get the next one. */
      sprintf (number_buf, "%05d", i);
      unique_name_buf->data[iterating_portion_idx + 0] = number_buf[0];
      unique_name_buf->data[iterating_portion_idx + 1] = number_buf[1];
      unique_name_buf->data[iterating_portion_idx + 2] = number_buf[2];
      unique_name_buf->data[iterating_portion_idx + 3] = number_buf[3];
      unique_name_buf->data[iterating_portion_idx + 4] = number_buf[4];

      /* Hmmm.  Ideally, we would append to a native-encoding buf
         while iterating, then convert it back to UTF-8 for
         return. But I suppose that would make the appending code
         sensitive to i18n in a way it shouldn't be... Oh well. */
      SVN_ERR (svn_utf_cstring_from_utf8_stringbuf (&unique_name_native,
                                                    unique_name_buf,
                                                    pool));
      
      apr_err = apr_file_open (f, unique_name_native, flag,
                               APR_OS_DEFAULT, pool);

      if (APR_STATUS_IS_EEXIST(apr_err))
        continue;
      else if (apr_err)
        {
          char *filename = unique_name_buf->data;
          *f = NULL;
          *unique_name = NULL;
          return svn_error_createf (apr_err,
                                    0,
                                    NULL,
                                    "svn_io_open_unique_file: "
                                    "error attempting %s",
                                    filename);
        }
      else
        {
          *unique_name = unique_name_buf->data;
          return SVN_NO_ERROR;
        }
    }

  *f = NULL;
  *unique_name = NULL;
  return svn_error_createf (SVN_ERR_IO_UNIQUE_NAMES_EXHAUSTED,
                            0,
                            NULL,
                            "svn_io_open_unique_file: unable to make name for "
                            "%s", path);
}



/*** Copying and appending files. ***/

svn_error_t *
svn_io_copy_file (const char *src,
                  const char *dst,
                  svn_boolean_t copy_perms,
                  apr_pool_t *pool)
{
  apr_file_t *d;
  apr_status_t apr_err;
  const char *src_native, *dst_native;
  const char *dst_tmp;

  /* ### FIXME: apr_file_copy with perms may fail on Win32.  We need a
     platform-specific implementation to get the permissions right. */
#ifndef SVN_WIN32
  apr_int32_t perms = copy_perms ? APR_FILE_SOURCE_PERMS : APR_OS_DEFAULT;
#else
  apr_int32_t perms = APR_OS_DEFAULT;
#endif

  SVN_ERR (svn_utf_cstring_from_utf8 (&src_native, src, pool));
  SVN_ERR (svn_utf_cstring_from_utf8 (&dst_native, dst, pool));

  /* For atomicity, we translate to a tmp file and then rename the tmp
     file over the real destination. */

  SVN_ERR (svn_io_open_unique_file (&d, &dst_tmp, dst_native,
                                    ".tmp", FALSE, pool));

  apr_err = apr_file_close (d);
  if (apr_err)
    {
      return svn_error_createf
        (apr_err, 0, NULL,
         "svn_io_copy_file: error closing %s", dst_tmp);
    }

  apr_err = apr_file_copy (src_native, dst_tmp, perms, pool);
  if (apr_err)
    return svn_error_createf
      (apr_err, 0, NULL, "svn_io_copy_file: error copying %s to %s",
       src_native, dst_tmp);

  /* We already have the dst_tmp filename in native encoding, so call
     apr_file_rename directly, instead of svn_io_file_rename. */
  apr_err = apr_file_rename (dst_tmp, dst_native, pool);
  if (apr_err)
    return svn_error_createf
      (apr_err, 0, NULL,
       "svn_io_copy_file: error renaming '%s' to '%s'", dst_tmp, dst_native);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_append_file (const char *src, const char *dst, apr_pool_t *pool)
{
  apr_status_t apr_err;
  const char *src_native, *dst_native;

  SVN_ERR (svn_utf_cstring_from_utf8 (&src_native, src, pool));
  SVN_ERR (svn_utf_cstring_from_utf8 (&dst_native, dst, pool));

  apr_err = apr_file_append (src_native, dst_native, APR_OS_DEFAULT, pool);

  if (apr_err)
    {
      const char *msg
        = apr_psprintf (pool, "svn_io_append_file: appending %s to %s",
                        src, dst);
      return svn_error_create (apr_err, 0, NULL, msg);
    }
  
  return SVN_NO_ERROR;
}


svn_error_t *svn_io_copy_dir_recursively (const char *src,
                                          const char *dst_parent,
                                          const char *dst_basename,
                                          svn_boolean_t copy_perms,
                                          apr_pool_t *pool)
{
  svn_node_kind_t kind;
  apr_status_t status;
  apr_hash_t *dirents;
  apr_hash_index_t *hi;
  const char *dst_path;
  const char *dst_path_native;

  /* Make a subpool for recursion */
  apr_pool_t *subpool = svn_pool_create (pool);

  /* The 'dst_path' is simply dst_parent/dst_basename */
  dst_path = svn_path_join (dst_parent, dst_basename, pool);

  /* Sanity checks:  SRC and DST_PARENT are directories, and
     DST_BASENAME doesn't already exist in DST_PARENT. */
  SVN_ERR (svn_io_check_path (src, &kind, subpool));
  if (kind != svn_node_dir)
    return svn_error_createf (SVN_ERR_NODE_UNEXPECTED_KIND, 0, NULL,
                              "svn_io_copy_dir: '%s' is not a directory.",
                              src);

  SVN_ERR (svn_io_check_path (dst_parent, &kind, subpool));
  if (kind != svn_node_dir)
    return svn_error_createf (SVN_ERR_NODE_UNEXPECTED_KIND, 0, NULL,
                              "svn_io_copy_dir: '%s' is not a directory.",
                              dst_parent);

  SVN_ERR (svn_io_check_path (dst_path, &kind, subpool));
  if (kind != svn_node_none)
    return svn_error_createf (SVN_ERR_ENTRY_EXISTS, 0, NULL,
                              "svn_io_copy_dir: '%s' already exists.",
                              dst_path);
  
  SVN_ERR (svn_utf_cstring_from_utf8 (&dst_path_native, dst_path, pool));

  /* Create the new directory. */
  /* ### TODO: copy permissions? */
  status = apr_dir_make (dst_path_native, APR_OS_DEFAULT, pool);
  if (status)
    return svn_error_createf (status, 0, NULL,
                              "svn_io_copy_dir: "
                              "Unable to create directory '%s'",
                              dst_path);

  /* Loop over the dirents in SRC.  ('.' and '..' are auto-excluded) */
  SVN_ERR (svn_io_get_dirents (&dirents, src, subpool));

  for (hi = apr_hash_first (subpool, dirents); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      const char *entryname;
      svn_node_kind_t *entrykind;
      const char *src_target;

      /* Get next entry and its kind */
      apr_hash_this (hi, &key, NULL, &val);
      entryname = key;
      entrykind = val;

      /* Telescope the entryname onto the source dir. */
      src_target = svn_path_join (src, entryname, subpool);

      /* If it's a file, just copy it over. */
      if (*entrykind == svn_node_file)
        {
          /* Telescope and de-telescope the dst_target in here */
          const char *dst_target
            = svn_path_join (dst_path, entryname, subpool);
          SVN_ERR (svn_io_copy_file (src_target, dst_target,
                                     copy_perms, subpool));
        }
      else if (*entrykind == svn_node_dir)  /* recurse */
        SVN_ERR (svn_io_copy_dir_recursively (src_target,
                                              dst_path,
                                              entryname,
                                              copy_perms,
                                              subpool));

      /* ### someday deal with other node kinds? */
    }
    

  /* Free any memory used by recursion */
  apr_pool_destroy (subpool);
           
  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_make_dir_recursively (const char *path, apr_pool_t *pool)
{
  const char *path_native;
  apr_status_t apr_err;
  char *dir;

  SVN_ERR (svn_utf_cstring_from_utf8 (&path_native, path, pool));

#if 0
  /* ### Use this implementation if/when apr_dir_make_recursive is
     available on all platforms, not just on Unix. --xbc */
  apr_err = apr_dir_make_recursive (path_native, APR_OS_DEFAULT, pool);

  if (apr_err)
    return svn_error_createf
      (apr_err, 0, NULL,
       "svn_io_make_dir_recursively: error making directory %s", path);

  return SVN_NO_ERROR;
#else

  /* Try to make PATH right out */
  apr_err = apr_dir_make (path_native, APR_OS_DEFAULT, pool);

  /* It's OK if PATH exists */
  if (!apr_err || APR_STATUS_IS_EEXIST(apr_err))
    return SVN_NO_ERROR;

  if (APR_STATUS_IS_ENOENT(apr_err))
    {
      /* ### Unfortunately, this won't work on Win32 without the following
         patch to APR:

Index: apr_errno.h
===================================================================
RCS file: /home/cvs/apr/include/apr_errno.h,v
retrieving revision 1.91
diff -u -p -r1.91 apr_errno.h
--- apr_errno.h 20 May 2002 13:22:36 -0000      1.91
+++ apr_errno.h 8 Jun 2002 10:18:30 -0000
@@ -923,6 +923,7 @@ APR_DECLARE(char *) apr_strerror(apr_sta
                 || (s) == APR_OS_START_SYSERR + WSAENAMETOOLONG)
 #define APR_STATUS_IS_ENOENT(s)         ((s) == APR_ENOENT \
                 || (s) == APR_OS_START_SYSERR + ERROR_FILE_NOT_FOUND \
+                || (s) == APR_OS_START_SYSERR + ERROR_PATH_NOT_FOUND \
                 || (s) == APR_OS_START_SYSERR + ERROR_OPEN_FAILED \
                 || (s) == APR_OS_START_SYSERR + ERROR_NO_MORE_FILES)
 #define APR_STATUS_IS_ENOTDIR(s)        ((s) == APR_ENOTDIR \


         I had long discussions about this on the apr list with wrowe,
         but nothing has come of it yet. --xbc */

      /* Missing an intermediate dir */
      svn_error_t *svn_err;

      dir = svn_path_remove_component_nts (path, pool);
      svn_err = svn_io_make_dir_recursively (dir, pool);

      if (!svn_err)
        {
          apr_err = apr_dir_make (path_native, APR_OS_DEFAULT, pool);
          if (apr_err)
            svn_err = svn_error_createf
              (apr_err, 0, NULL,
               "svn_io_make_dir_recursively: error creating directory %s",
               path);
        }

      return svn_err;
    }

  /* If we get here, there must be an apr_err. */
  return svn_error_createf
    (apr_err, 0, NULL,
     "svn_io_make_dir_recursively: error making %s", path);
#endif
}



/*** Modtime checking. ***/

svn_error_t *
svn_io_file_affected_time (apr_time_t *apr_time,
                           const char *path,
                           apr_pool_t *pool)
{
  apr_finfo_t finfo;

  SVN_ERR (svn_io_stat (&finfo, path, APR_FINFO_MIN, pool));

  if (finfo.mtime > finfo.ctime)
    *apr_time = finfo.mtime;
  else
    *apr_time = finfo.ctime;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_filesizes_different_p (svn_boolean_t *different_p,
                              const char *file1,
                              const char *file2,
                              apr_pool_t *pool)
{
  apr_finfo_t finfo1;
  apr_finfo_t finfo2;
  apr_status_t status;
  const char *file1_native, *file2_native;

  /* Not using svn_io_stat() because don't want to generate
     svn_error_t objects for non-error conditions. */

  SVN_ERR (svn_utf_cstring_from_utf8 (&file1_native, file1, pool));
  SVN_ERR (svn_utf_cstring_from_utf8 (&file2_native, file2, pool));

  /* Stat both files */
  status = apr_stat (&finfo1, file1_native, APR_FINFO_MIN, pool);
  if (status)
    {
      /* If we got an error stat'ing a file, it could be because the
         file was removed... or who knows.  Whatever the case, we
         don't know if the filesizes are definitely different, so
         assume that they're not. */
      *different_p = FALSE;
      return SVN_NO_ERROR;
    }

  status = apr_stat (&finfo2, file2_native, APR_FINFO_MIN, pool);
  if (status)
    {
      /* See previous comment. */
      *different_p = FALSE;
      return SVN_NO_ERROR;
    }

  /* Examine file sizes */
  if (finfo1.size == finfo2.size)
    *different_p = FALSE;
  else
    *different_p = TRUE;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_file_checksum (svn_stringbuf_t **checksum_p,
                      const char *file,
                      apr_pool_t *pool)
{
  struct apr_md5_ctx_t context;
  apr_file_t *f = NULL;
  apr_status_t apr_err;
  unsigned char digest[MD5_DIGESTSIZE];
  char buf[BUFSIZ];  /* What's a good size for a read chunk? */
  svn_stringbuf_t *md5str;

  /* ### The apr_md5 functions return apr_status_t, but they only
     return success, and really, what could go wrong?  So below, we
     ignore their return values. */

  apr_md5_init (&context);

  SVN_ERR (svn_io_file_open (&f, file, APR_READ, APR_OS_DEFAULT, pool));
  
  do { 
    apr_size_t len = BUFSIZ;

    apr_err = apr_file_read (f, buf, &len);

    if (apr_err && ! APR_STATUS_IS_EOF(apr_err))
      return svn_error_createf
        (apr_err, 0, NULL,
         "svn_io_file_checksum: error reading from '%s'", file);

    apr_md5_update (&context, buf, len);

  } while (! APR_STATUS_IS_EOF(apr_err));

  apr_err = apr_file_close (f);
  if (apr_err)
    return svn_error_createf
      (apr_err, 0, NULL,
       "svn_io_file_checksum: error closing '%s'", file);

  apr_md5_final (digest, &context);
  md5str = svn_stringbuf_ncreate (digest, MD5_DIGESTSIZE, pool);
  *checksum_p = svn_base64_encode_string (md5str, pool);
  
  /* ### Our base64-encoding routines append a final newline if any
     data was created at all, so let's hack that off. */
  if ((*checksum_p)->len)
    {
      (*checksum_p)->len--;
      (*checksum_p)->data[(*checksum_p)->len] = 0;
    }
  return SVN_NO_ERROR;
}



/*** Permissions and modes. ***/

svn_error_t *
svn_io_set_file_read_only (const char *path,
                           svn_boolean_t ignore_enoent,
                           apr_pool_t *pool)
{
  apr_status_t status;
  const char *path_native;

  SVN_ERR (svn_utf_cstring_from_utf8 (&path_native, path, pool));

  status = apr_file_attrs_set (path_native,
                               APR_FILE_ATTR_READONLY,
                               APR_FILE_ATTR_READONLY,
                               pool);

  if (status && status != APR_ENOTIMPL)
    if (!ignore_enoent || !APR_STATUS_IS_ENOENT(status))
      return svn_error_createf (status, 0, NULL,
                               "svn_io_set_file_read_only: "
                                "failed to set file '%s' read-only",
                                path);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_set_file_read_write (const char *path,
                            svn_boolean_t ignore_enoent,
                            apr_pool_t *pool)
{
  apr_status_t status;
  const char *path_native;

  SVN_ERR (svn_utf_cstring_from_utf8 (&path_native, path, pool));

  status = apr_file_attrs_set (path_native,
                               0,
                               APR_FILE_ATTR_READONLY,
                               pool);

  if (status && status != APR_ENOTIMPL)
    if (!ignore_enoent || !APR_STATUS_IS_ENOENT(status))
      return svn_error_createf (status, 0, NULL,
                                "svn_io_set_file_read_write: "
                                "failed to set file '%s' read-write",
                                path);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_set_file_executable (const char *path,
                            svn_boolean_t executable,
                            svn_boolean_t ignore_enoent,
                            apr_pool_t *pool)
{
  apr_status_t status;
  const char *path_native;

  SVN_ERR (svn_utf_cstring_from_utf8 (&path_native, path, pool));

  if (executable)
    status = apr_file_attrs_set (path_native,
                                 APR_FILE_ATTR_EXECUTABLE,
                                 APR_FILE_ATTR_EXECUTABLE,
                                 pool);
  else
    status = apr_file_attrs_set (path_native,
                                 0,
                                 APR_FILE_ATTR_EXECUTABLE,
                                 pool);
    
  if (status && status != APR_ENOTIMPL)
    if (!ignore_enoent || !APR_STATUS_IS_ENOENT(status))
      return svn_error_createf (status, 0, NULL,
                                "svn_io_set_file_executable: "
                                "failed to change executability of file '%s'",
                                path);
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_is_file_executable(svn_boolean_t *executable, 
                          const char *path, 
                          apr_pool_t *pool)
{
#if defined(APR_HAS_USER) && !defined(SVN_WIN32)
  apr_finfo_t file_info;
  apr_status_t apr_err;
  apr_uid_t uid;
  apr_gid_t gid;

  *executable = FALSE;
  
  /* Get file and user info. */
  SVN_ERR (svn_io_stat (&file_info, path, 
                        (APR_FINFO_PROT | APR_FINFO_OWNER), 
                        pool));
  apr_err = apr_current_userid (&uid, &gid, pool);

  if (apr_err)
    return svn_error_create(apr_err, 0, NULL,
                            "Error getting UID of process.");
    
  /* Check executable bit for current user. */
  if (apr_compare_users(uid, file_info.user) == APR_SUCCESS)
    *executable = (file_info.protection & APR_UEXECUTE);

  else if (apr_compare_groups(gid, file_info.group) == APR_SUCCESS)
    *executable = (file_info.protection & APR_GEXECUTE);

  else
    *executable = (file_info.protection & APR_WEXECUTE);

#else  /* defined(SVN_WIN32) || !defined(APR_HAS_USER) */
  *executable = FALSE;
#endif

  return SVN_NO_ERROR;
}




/*** Generic streams. ***/

svn_stream_t *
svn_stream_create (void *baton, apr_pool_t *pool)
{
  svn_stream_t *stream;

  stream = apr_palloc (pool, sizeof (*stream));
  stream->baton = baton;
  stream->read_fn = NULL;
  stream->write_fn = NULL;
  stream->close_fn = NULL;
  return stream;
}


svn_stream_t *
svn_stream_dup (svn_stream_t *stream, apr_pool_t *pool)
{
  svn_stream_t *new_stream;

  new_stream = apr_palloc (pool, sizeof (*new_stream));
  new_stream->baton = stream->baton;
  new_stream->read_fn = stream->read_fn;
  new_stream->write_fn = stream->write_fn;
  new_stream->close_fn = stream->close_fn;
  return stream;
}


void
svn_stream_set_baton (svn_stream_t *stream, void *baton)
{
  stream->baton = baton;
}


void
svn_stream_set_read (svn_stream_t *stream, svn_read_fn_t read_fn)
{
  stream->read_fn = read_fn;
}


void
svn_stream_set_write (svn_stream_t *stream, svn_write_fn_t write_fn)
{
  stream->write_fn = write_fn;
}


void
svn_stream_set_close (svn_stream_t *stream, svn_close_fn_t close_fn)
{
  stream->close_fn = close_fn;
}


svn_error_t *
svn_stream_read (svn_stream_t *stream, char *buffer, apr_size_t *len)
{
  assert (stream->read_fn != NULL);
  return stream->read_fn (stream->baton, buffer, len);
}


svn_error_t *
svn_stream_write (svn_stream_t *stream, const char *data, apr_size_t *len)
{
  assert (stream->write_fn != NULL);
  return stream->write_fn (stream->baton, data, len);
}


svn_error_t *
svn_stream_close (svn_stream_t *stream)
{
  if (stream->close_fn == NULL)
    return SVN_NO_ERROR;
  return stream->close_fn (stream->baton);
}


svn_error_t *
svn_stream_printf (svn_stream_t *stream,
                   apr_pool_t *pool,
                   const char *fmt,
                   ...)
{
  const char *message;
  va_list ap;
  apr_size_t len;

  va_start (ap, fmt);
  message = apr_pvsprintf (pool, fmt, ap);
  va_end (ap);
  
  len = strlen(message);
  return svn_stream_write (stream, message, &len);
}


svn_error_t *
svn_stream_readline (svn_stream_t *stream,
                     svn_stringbuf_t **stringbuf,
                     apr_pool_t *pool)
{
  apr_size_t numbytes;
  char c;
  svn_stringbuf_t *str = svn_stringbuf_create ("", pool);

  /* Since we're reading one character at a time, let's at least
     optimize for the 90% case.  90% of the time, we can avoid the
     stringbuf ever having to realloc() itself if we start it out at
     80 chars.  */
  svn_stringbuf_ensure (str, 80);

  while (1)
    {
      numbytes = 1;
      SVN_ERR (svn_stream_read (stream, &c, &numbytes));
      if (numbytes != 1)
        {
          /* a 'short' read means the stream has run out. */
          *stringbuf = NULL;
          return SVN_NO_ERROR;
        }

      if ((c == '\n'))
        break;

      svn_stringbuf_appendbytes (str, &c, 1);
    }
  
  *stringbuf = str;
  return SVN_NO_ERROR;
}




/*** Generic readable empty stream ***/

static svn_error_t *
read_handler_empty (void *baton, char *buffer, apr_size_t *len)
{
  *len = 0;
  return SVN_NO_ERROR;
}


svn_stream_t *
svn_stream_empty (apr_pool_t *pool)
{
  svn_stream_t *stream;

  stream = svn_stream_create (NULL, pool);
  svn_stream_set_read (stream, read_handler_empty);
  return stream;
}



/*** Generic stream for APR files ***/
struct baton_apr {
  apr_file_t *file;
  apr_pool_t *pool;
};


static svn_error_t *
read_handler_apr (void *baton, char *buffer, apr_size_t *len)
{
  struct baton_apr *btn = baton;
  apr_status_t status;

  status = apr_file_read_full (btn->file, buffer, *len, len);
  if (status && ! APR_STATUS_IS_EOF(status))
    return svn_error_create (status, 0, NULL,
                             "read_handler_apr: error reading file");
  else
    return SVN_NO_ERROR;
}


static svn_error_t *
write_handler_apr (void *baton, const char *data, apr_size_t *len)
{
  struct baton_apr *btn = baton;
  apr_status_t status;

  status = apr_file_write_full (btn->file, data, *len, len);
  if (status)
    return svn_error_create (status, 0, NULL,
                             "write_handler_apr: error writing file");
  else
    return SVN_NO_ERROR;
}


svn_stream_t *
svn_stream_from_aprfile (apr_file_t *file, apr_pool_t *pool)
{
  struct baton_apr *baton;
  svn_stream_t *stream;

  if (file == NULL)
    return svn_stream_empty(pool);
  baton = apr_palloc (pool, sizeof (*baton));
  baton->file = file;
  baton->pool = pool;
  stream = svn_stream_create (baton, pool);
  svn_stream_set_read (stream, read_handler_apr);
  svn_stream_set_write (stream, write_handler_apr);
  return stream;
}



/*** Generic stream for stdio files ***/
struct baton_stdio {
  FILE *fp;
  apr_pool_t *pool;
};


static svn_error_t *
read_handler_stdio (void *baton, char *buffer, apr_size_t *len)
{
  struct baton_stdio *btn = baton;
  svn_error_t *err = SVN_NO_ERROR;
  apr_size_t count;

  count = fread (buffer, 1, *len, btn->fp);
  if (count < *len && ferror(btn->fp))
    err = svn_error_create (0, errno, NULL,
                            "read_handler_stdio: error reading");
  *len = count;
  return err;
}


static svn_error_t *
write_handler_stdio (void *baton, const char *data, apr_size_t *len)
{
  struct baton_stdio *btn = baton;
  svn_error_t *err = SVN_NO_ERROR;
  apr_size_t count;

  count = fwrite (data, 1, *len, btn->fp);
  if (count < *len)
    err = svn_error_create (0, errno, NULL,
                            "write_handler_stdio: error writing");
  *len = count;
  return err;
}


svn_stream_t *svn_stream_from_stdio (FILE *fp, apr_pool_t *pool)
{
  struct baton_stdio *baton;
  svn_stream_t *stream;

  if (fp == NULL)
    return svn_stream_empty (pool);
  baton = apr_palloc (pool, sizeof (*baton));
  baton->fp = fp;
  baton->pool = pool;
  stream = svn_stream_create (baton, pool);
  svn_stream_set_read (stream, read_handler_stdio);
  svn_stream_set_write (stream, write_handler_stdio);
  return stream;
}



/* TODO write test for these two functions, then refactor. */

svn_error_t *
svn_stringbuf_from_file (svn_stringbuf_t **result,
                         const char *filename,
                         apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_file_t *f = NULL;

  if (filename[0] == '-' && filename[1] == '\0')
    return svn_error_create
        (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL,
         "svn_stringbuf_from_file: "
         "reading from stdin is currently broken, so disabled");

  SVN_ERR (svn_io_file_open (&f, filename, APR_READ, APR_OS_DEFAULT, pool));

  SVN_ERR (svn_stringbuf_from_aprfile (result, f, pool));

  apr_err = apr_file_close (f);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL,
                              "svn_stringbuf_from_file: failed to close '%s'",
                              filename);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_stringbuf_from_aprfile (svn_stringbuf_t **result,
                            apr_file_t *file,
                            apr_pool_t *pool)
{
  apr_size_t len;
  apr_status_t apr_err;
  svn_stringbuf_t *res = svn_stringbuf_create("", pool);
  const char *fname;
  char buf[BUFSIZ];

  /* XXX: We should check the incoming data for being of type binary. */

  apr_err = apr_file_name_get (&fname, file);
  if (apr_err)
    return svn_error_create
      (apr_err, 0, NULL,
       "svn_stringbuf_from_aprfile: failed to get filename");

  /* If the apr_file_t was opened with apr_file_open_std{in,out,err}, then we
   * wont get a filename for it. We assume that since we are reading, that in
   * this case we would only ever be using stdin.  */
  if (NULL == fname)
    fname = "stdin";

  /* apr_file_read will not return data and eof in the same call. So this loop
   * is safe from missing read data.  */
  len = sizeof(buf);
  apr_err = apr_file_read (file, buf, &len);
  while (! apr_err)
    {
      svn_stringbuf_appendbytes(res, buf, len);
      len = sizeof(buf);
      apr_err = apr_file_read (file, buf, &len);
    }

  /* Having read all the data we *expect* EOF */
  if (!APR_STATUS_IS_EOF(apr_err))
    {
      const char *fname_utf8;
      
      SVN_ERR (svn_utf_cstring_to_utf8 (&fname_utf8, fname, NULL, pool));
      
      return svn_error_createf 
        (apr_err, 0, NULL,
         "svn_stringbuf_from_aprfile: EOF not seen for '%s'", fname_utf8);
    }

  /* Null terminate the stringbuf. */
  res->data[res->len] = 0;

  *result = res;
  return SVN_NO_ERROR;
}



/* Deletion. */

svn_error_t *
svn_io_remove_file (const char *path, apr_pool_t *pool)
{
  apr_status_t apr_err;
  const char *path_native;

  /* Remove read-only flag on terminated file. */
  SVN_ERR (svn_io_set_file_read_write (path, TRUE, pool));

  SVN_ERR (svn_utf_cstring_from_utf8 (&path_native, path, pool));

  apr_err = apr_file_remove (path_native, pool);

  if (apr_err)
    return svn_error_createf
      (apr_err, 0, NULL,
       "svn_io_remove_file: failed to remove file \"%s\"",
       path);

  return SVN_NO_ERROR;
}


/* Neither windows nor unix allows us to delete a non-empty
   directory.  

   This is a function to perform the equivalent of 'rm -rf'. */

svn_error_t *
svn_io_remove_dir (const char *path, apr_pool_t *pool)
{
  static const char err_msg_fmt[] = "svn_io_remove_dir: removing `%s'";
  apr_status_t status;
  apr_dir_t *this_dir;
  apr_finfo_t this_entry;
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_int32_t flags = APR_FINFO_TYPE | APR_FINFO_NAME;
  const char *path_native;

  /* APR doesn't like "" directories */
  if (path[0] == '\0')
    path = ".";

  /* Convert path to native here and call apr_dir_open directly,
     instead of just using svn_io_dir_open, because we're going to
     need path_native later anyway when we remove the dir itself. */

  SVN_ERR (svn_utf_cstring_from_utf8 (&path_native, path, subpool));

  status = apr_dir_open (&this_dir, path_native, subpool);
  if (status)
    return svn_error_createf (status, 0, NULL, err_msg_fmt, path);

  for (status = apr_dir_read (&this_entry, flags, this_dir);
       status == APR_SUCCESS;
       status = apr_dir_read (&this_entry, flags, this_dir))
    {
      if ((this_entry.filetype == APR_DIR)
          && ((this_entry.name[0] == '.')
              && ((this_entry.name[1] == '\0')
                  || ((this_entry.name[1] == '.')
                      && (this_entry.name[2] == '\0')))))
        {
          continue;
        }
      else  /* something other than "." or "..", so proceed */
        {
          const char *fullpath, *entry_utf8;

          SVN_ERR (svn_utf_cstring_to_utf8 (&entry_utf8, this_entry.name,
                                            NULL, subpool));
          
          fullpath = svn_path_join (path, entry_utf8, pool);
          
          if (this_entry.filetype == APR_DIR)
            {
              SVN_ERR (svn_io_remove_dir (fullpath, subpool));
            }
          else if (this_entry.filetype == APR_REG)
            {
              /* ### Do we really need the check for APR_REG here? Shouldn't
                 we remove symlinks, pipes and whatnot, too?  --xbc */
              svn_error_t *err = svn_io_remove_file (fullpath, subpool);
              if (err)
                return svn_error_createf (err->apr_err, err->src_err,
                                          err, err_msg_fmt, path);
            }
        }
    }

  if (!APR_STATUS_IS_ENOENT (status))
    return svn_error_createf (status, 0, NULL, err_msg_fmt, path);
  else
    {
      status = apr_dir_close (this_dir);
      if (status)
        return svn_error_createf (status, 0, NULL, err_msg_fmt, path);
    }

  status = apr_dir_remove (path_native, subpool);
  if (status)
    return svn_error_createf (status, 0, NULL, err_msg_fmt, path);

  apr_pool_destroy (subpool);

  return APR_SUCCESS;
}



svn_error_t *
svn_io_get_dirents (apr_hash_t **dirents,
                    const char *path,
                    apr_pool_t *pool)
{
  apr_status_t status; 
  apr_dir_t *this_dir;
  apr_finfo_t this_entry;
  apr_int32_t flags = APR_FINFO_TYPE | APR_FINFO_NAME;

  /* These exist so we can use their addresses as hash values! */
  static const svn_node_kind_t static_svn_node_file = svn_node_file;
  static const svn_node_kind_t static_svn_node_dir = svn_node_dir;
  static const svn_node_kind_t static_svn_node_unknown = svn_node_unknown;

  *dirents = apr_hash_make (pool);
  
  SVN_ERR (svn_io_dir_open (&this_dir, path, pool));

  for (status = apr_dir_read (&this_entry, flags, this_dir);
       status == APR_SUCCESS;
       status = apr_dir_read (&this_entry, flags, this_dir))
    {
      if ((this_entry.name[0] == '.')
          && ((this_entry.name[1] == '\0')
              || ((this_entry.name[1] == '.')
                  && (this_entry.name[2] == '\0'))))
        {
          continue;
        }
      else
        {
          const char *name;

          SVN_ERR (svn_utf_cstring_to_utf8 (&name, this_entry.name,
                                            NULL, pool));
          
          if (this_entry.filetype == APR_REG)
            apr_hash_set (*dirents, name, APR_HASH_KEY_STRING,
                          &static_svn_node_file);
          else if (this_entry.filetype == APR_DIR)
            apr_hash_set (*dirents, name, APR_HASH_KEY_STRING,
                          &static_svn_node_dir);
          else
            /* ### symlinks, etc. will fall into this category for now.
               someday subversion will recognize them. :)  */
            apr_hash_set (*dirents, name, APR_HASH_KEY_STRING,
                          &static_svn_node_unknown);
        }
    }

  if (! (APR_STATUS_IS_ENOENT (status)))
    return 
      svn_error_createf (status, 0, NULL,
                         "svn_io_get_dirents:  error while reading dir '%s'",
                         path);

  status = apr_dir_close (this_dir);
  if (status) 
    return
      svn_error_createf (status, 0, NULL,
                         "svn_io_get_dirents:  failed to close dir '%s'",
                         path);
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_run_cmd (const char *path,
                const char *cmd,
                const char *const *args,
                int *exitcode,
                apr_exit_why_e *exitwhy,
                svn_boolean_t inherit,
                apr_file_t *infile,
                apr_file_t *outfile,
                apr_file_t *errfile,
                apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_proc_t cmd_proc;
  apr_procattr_t *cmdproc_attr;
  apr_exit_why_e exitwhy_val;
  int exitcode_val, num_args;
  const char **args_native;
  const char *cmd_native;

  /* Create the process attributes. */
  apr_err = apr_procattr_create (&cmdproc_attr, pool); 
  if (apr_err)
    return svn_error_createf
      (apr_err, 0, NULL,
       "svn_io_run_cmd: error creating %s process attributes",
       cmd);

  /* Make sure we invoke cmd directly, not through a shell. */
  apr_err = apr_procattr_cmdtype_set (cmdproc_attr,
                                      inherit?APR_PROGRAM_PATH:APR_PROGRAM);

  if (apr_err)
    return svn_error_createf 
      (apr_err, 0, NULL,
       "svn_io_run_cmd: error setting %s process cmdtype",
       cmd);

  /* Set the process's working directory. */
  if (path)
    {
      const char *path_native;

      SVN_ERR (svn_utf_cstring_from_utf8 (&path_native, path, pool));

      apr_err = apr_procattr_dir_set (cmdproc_attr, path_native);

      if (apr_err)
        return svn_error_createf 
          (apr_err, 0, NULL,
           "svn_io_run_cmd: error setting %s process directory",
           cmd);
    }

  /* Use requested inputs and outputs.

     ### Unfortunately each of these apr functions creates a pipe and then
     overwrites the pipe file descriptor with the descriptor we pass
     in. The pipes can then never be closed. This is an APR bug. */
  if (infile)
    {
      apr_err = apr_procattr_child_in_set (cmdproc_attr, infile, NULL);
      if (apr_err)
        return svn_error_createf 
          (apr_err, 0, NULL,
           "svn_io_run_cmd: error setting %s process child input",
           cmd);
    }
  if (outfile)
    {
      apr_err = apr_procattr_child_out_set (cmdproc_attr, outfile, NULL);
      if (apr_err)
        return svn_error_createf 
          (apr_err, 0, NULL,
           "svn_io_run_cmd: error setting %s process child outfile",
           cmd);
    }
  if (errfile)
    {
      apr_err = apr_procattr_child_err_set (cmdproc_attr, errfile, NULL);
      if (apr_err)
        return svn_error_createf 
          (apr_err, 0, NULL,
           "svn_io_run_cmd: error setting %s process child errfile",
           cmd);
    }

  /* Convert cmd and args from UTF-8 */
  SVN_ERR (svn_utf_cstring_from_utf8 (&cmd_native, cmd, pool));
  for (num_args = 0; args[num_args]; num_args++)
    ;
  args_native = apr_palloc (pool, (num_args + 1) * sizeof(char *));
  args_native[num_args] = NULL;
  while (num_args--)
    {
      SVN_ERR (svn_utf_cstring_from_utf8 (&args_native[num_args],
                                          args[num_args],
                                          pool));
    }


  /* Start the cmd command. */ 
  apr_err = apr_proc_create (&cmd_proc, cmd_native, args_native, NULL,
                             cmdproc_attr, pool);
  if (apr_err)
    return svn_error_createf 
      (apr_err, 0, NULL,
       "svn_io_run_cmd: error starting %s process",
       cmd);

  /* The Win32 apr_proc_wait doesn't set this... */
  exitwhy_val = APR_PROC_EXIT;

  /* Wait for the cmd command to finish. */
  apr_err = apr_proc_wait (&cmd_proc, &exitcode_val, &exitwhy_val, APR_WAIT);
  if (APR_STATUS_IS_CHILD_NOTDONE (apr_err))
    return svn_error_createf
      (apr_err, 0, NULL,
       "svn_io_run_cmd: error waiting for %s process",
       cmd);

  if (exitwhy)
    *exitwhy = exitwhy_val;
  else if (! APR_PROC_CHECK_EXIT(exitwhy_val))
    return svn_error_createf
      (SVN_ERR_EXTERNAL_PROGRAM, 0, NULL,
       "svn_io_run_cmd: error exitwhy %d for process %s",
       exitwhy_val, cmd);

  if (exitcode)
    *exitcode = exitcode_val;
  else if (exitcode_val != 0)
    return svn_error_createf
      (SVN_ERR_EXTERNAL_PROGRAM, 0, NULL,
       "svn_io_run_cmd: error exitcode %d for process %s",
       exitcode_val, cmd);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_run_diff (const char *dir, 
                 const char *const *user_args,
                 const int num_user_args, 
                 const char *label1,
                 const char *label2,
                 const char *from,
                 const char *to,
                 int *pexitcode, 
                 apr_file_t *outfile, 
                 apr_file_t *errfile, 
                 apr_pool_t *pool)
{
  const char **args;
  int i; 
  int exitcode;
  int nargs = 4; /* the diff command itself, two paths, plus a trailing NULL */
  const char *diff_utf8;

  apr_pool_t *subpool = svn_pool_create (pool);

  svn_config_t *cfg;
  const char *diff_cmd;
  SVN_ERR (svn_config_read_config (&cfg, subpool));
  svn_config_get (cfg, &diff_cmd, "helpers", "diff_cmd", SVN_CLIENT_DIFF);

  if (pexitcode == NULL)
    pexitcode = &exitcode;

  if (user_args != NULL)
    nargs += num_user_args;
  else
    nargs += 1; /* -u */

  if (label1 != NULL)
    nargs += 2; /* the -L and the label itself */
  if (label2 != NULL)
    nargs += 2; /* the -L and the label itself */

  args = apr_palloc (subpool, nargs * sizeof(char *));

  i = 0;
  args[i++] = diff_cmd;

  if (user_args != NULL)
    {
      int j;
      for (j = 0; j < num_user_args; ++j)
        args[i++] = user_args[j];
    }
  else
    args[i++] = "-u"; /* assume -u if the user didn't give us any args */

  if (label1 != NULL)
    {
      args[i++] = "-L";
      args[i++] = label1;
    }
  if (label2 != NULL)
    {
      args[i++] = "-L";
      args[i++] = label2;
    }

  args[i++] = from;
  args[i++] = to;
  args[i++] = NULL;

  assert (i == nargs);

  SVN_ERR (svn_utf_cstring_to_utf8 (&diff_utf8, diff_cmd, NULL, pool));
  
  SVN_ERR (svn_io_run_cmd (dir, diff_utf8, args, pexitcode, NULL, FALSE, 
                           NULL, outfile, errfile, subpool));

  /* The man page for (GNU) diff describes the return value as:

       "An exit status of 0 means no differences were found, 1 means
        some differences were found, and 2 means trouble."

     A return value of 2 typically occurs when diff cannot read its input
     or write to its output, but in any case we probably ought to return an
     error for anything other than 0 or 1 as the output is likely to be
     corrupt.
   */
  if (*pexitcode != 0 && *pexitcode != 1)
    return svn_error_createf (SVN_ERR_EXTERNAL_PROGRAM, 0, NULL, 
                              "%s returned %d", diff_cmd, *pexitcode);

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}



svn_error_t *
svn_io_run_diff3 (const char *dir,
                  const char *mine,
                  const char *older,
                  const char *yours,
                  const char *mine_label,
                  const char *older_label,
                  const char *yours_label,
                  apr_file_t *merged,
                  int *exitcode,
                  apr_pool_t *pool)
{
  const char *args[14];
  const char *diff3_utf8;
  int nargs = 13, i = 0;

  svn_config_t *cfg;
  const char *diff3_cmd;
  SVN_ERR (svn_config_read_config (&cfg, pool));
  svn_config_get (cfg, &diff3_cmd, "helpers", "diff3_cmd", SVN_CLIENT_DIFF3);

  /* Labels fall back to sensible defaults if not specified. */
  if (mine_label == NULL)
    mine_label = ".working";
  if (older_label == NULL)
    older_label = ".old";
  if (yours_label == NULL)
    yours_label = ".new";
  
  /* Set up diff3 command line. */
  args[i++] = diff3_cmd;
  args[i++] = "-E";             /* We tried "-A" here, but that caused
                                   overlapping identical changes to
                                   conflict.  See issue #682. */
  args[i++] = "-m";
  args[i++] = "-L";
  args[i++] = mine_label;
  args[i++] = "-L";
  args[i++] = older_label;      /* note:  this label is ignored if
                                   using 2-part markers, which is the
                                   case with "-E". */
  args[i++] = "-L";
  args[i++] = yours_label;
#ifdef SVN_DIFF3_HAS_DIFF_PROGRAM_ARG
  {
    const char *has_arg;
    svn_config_get (cfg, &has_arg, "helpers", "diff3_has_program_arg", "yes");
    if (0 == strcasecmp(has_arg, "yes")
        || 0 == strcasecmp(has_arg, "true"))
      {
        const char *diff_cmd;
        svn_config_get (cfg, &diff_cmd,
                        "helpers", "diff_cmd", SVN_CLIENT_DIFF);
        args[i++] = apr_pstrcat(pool, "--diff-program=", diff_cmd, NULL);
        ++nargs;
      }
  }
#endif
  args[i++] = mine;
  args[i++] = older;
  args[i++] = yours;
  args[i++] = NULL;
  assert (i == nargs);

  SVN_ERR (svn_utf_cstring_to_utf8 (&diff3_utf8, diff3_cmd, NULL, pool));

  /* Run diff3, output the merged text into the scratch file. */
  SVN_ERR (svn_io_run_cmd (dir, diff3_utf8, args, 
                           exitcode, NULL, 
                           FALSE, /* clean environment */
                           NULL, merged, NULL,
                           pool));

  /* According to the diff3 docs, a '0' means the merge was clean, and
     '1' means conflict markers were found.  Anything else is real
     error. */
  if ((*exitcode != 0) && (*exitcode != 1))
    return svn_error_createf (SVN_ERR_EXTERNAL_PROGRAM, 0, NULL, 
                              "svn_io_run_diff3: "
                              "Error running %s:  exitcode was %d, args were:"
                              "\nin directory %s, basenames:\n%s\n%s\n%s",
                              diff3_cmd, *exitcode,
                              dir, mine, older, yours);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_detect_mimetype (const char **mimetype,
                        const char *file,
                        apr_pool_t *pool)
{
  static const char * const generic_binary = "application/octet-stream";

  svn_node_kind_t kind;
  apr_file_t *fh;
  apr_status_t apr_err;
  unsigned char block[1024];
  apr_size_t amt_read = sizeof (block);

  /* Default return value is NULL. */
  *mimetype = NULL;

  /* See if this file even exists, and make sure it really is a file. */
  SVN_ERR (svn_io_check_path (file, &kind, pool));
  if (kind != svn_node_file)
    return svn_error_createf (SVN_ERR_BAD_FILENAME, 0, NULL,
                              "svn_io_detect_mimetype: "
                              "Can't detect mimetype of non-file '%s'",
                              file);

  SVN_ERR (svn_io_file_open (&fh, file, APR_READ, 0, pool));

  /* Read a block of data from FILE. */
  apr_err = apr_file_read (fh, block, &amt_read);
  if (apr_err && ! APR_STATUS_IS_EOF(apr_err))
    return svn_error_createf (apr_err, 0, NULL,
                              "svn_io_detect_mimetype: error reading '%s'",
                              file);

  /* Now close the file.  No use keeping it open any more.  */
  apr_file_close (fh);


  /* Right now, this function is going to be really stupid.  It's
     going to examine the first block of data, and make sure that 85%
     of the bytes are such that their value is in the ranges 0x07-0x0D
     or 0x20-0x7F, and that 100% of those bytes is not 0x00.

     If those criteria are not met, we're calling it binary. */
  if (amt_read > 0)
    {
      apr_size_t i;
      int binary_count = 0;
      
      /* Run through the data we've read, counting the 'binary-ish'
         bytes.  HINT: If we see a 0x00 byte, we'll set our count to its
         max and stop reading the file. */
      for (i = 0; i < amt_read; i++)
        {
          if (block[i] == 0)
            {
              binary_count = amt_read;
              break;
            }
          if ((block[i] < 0x07)
              || ((block[i] > 0x0D) && (block[i] < 0x20))
              || (block[i] > 0x7F))
            {
              binary_count++;
            }
        }
      
      if (((binary_count * 1000) / amt_read) > 850)
        {
          *mimetype = generic_binary;
          return SVN_NO_ERROR;
        }
    }
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_file_open (apr_file_t **new_file, const char *fname,
                  apr_int32_t flag, apr_fileperms_t perm,
                  apr_pool_t *pool)
{
  const char *fname_native;
  apr_status_t status;

  SVN_ERR (svn_utf_cstring_from_utf8 (&fname_native, fname, pool));
  status = apr_file_open (new_file, fname_native, flag, perm, pool);

  if (status)
    return svn_error_createf (status, 0, NULL,
                              "svn_io_file_open: can't open `%s'", fname);
  else
    return SVN_NO_ERROR;  
}


svn_error_t *
svn_io_stat (apr_finfo_t *finfo, const char *fname,
             apr_int32_t wanted, apr_pool_t *pool)
{
  apr_status_t status;
  const char *fname_native;

  /* APR doesn't like "" directories */
  if (fname[0] == '\0')
    fname = ".";

  SVN_ERR (svn_utf_cstring_from_utf8 (&fname_native, fname, pool));

  status = apr_stat (finfo, fname_native, wanted, pool);

  if (status)
    return svn_error_createf (status, 0, NULL,
                              "svn_io_stat: couldn't stat '%s'...", fname);
  else
    return SVN_NO_ERROR;  
}


svn_error_t *
svn_io_file_rename (const char *from_path, const char *to_path,
                    apr_pool_t *pool)
{
  apr_status_t status;
  const char *from_path_native, *to_path_native;

  SVN_ERR (svn_utf_cstring_from_utf8 (&from_path_native, from_path, pool));
  SVN_ERR (svn_utf_cstring_from_utf8 (&to_path_native, to_path, pool));

  status = apr_file_rename (from_path_native, to_path_native, pool);

  if (status)
    return svn_error_createf (status, 0, NULL,
                              "svn_io_file_rename: can't move '%s' to '%s'",
                              from_path, to_path);
  else
    return SVN_NO_ERROR;  
}


svn_error_t *
svn_io_dir_make (const char *path, apr_fileperms_t perm, apr_pool_t *pool)
{
  apr_status_t status;
  const char *path_native;

  SVN_ERR (svn_utf_cstring_from_utf8 (&path_native, path, pool));

  status = apr_dir_make (path_native, perm, pool);

  if (status)
    return svn_error_createf (status, 0, NULL,
                              "svn_io_dir_make: can't create directory '%s'",
                              path);
  else
    return SVN_NO_ERROR;
}


svn_error_t *
svn_io_dir_open (apr_dir_t **new_dir, const char *dirname, apr_pool_t *pool)
{
  apr_status_t status;
  const char *dirname_native;

  /* APR doesn't like "" directories */
  if (dirname[0] == '\0')
    dirname = ".";

  SVN_ERR (svn_utf_cstring_from_utf8 (&dirname_native, dirname, pool));

  status = apr_dir_open (new_dir, dirname_native, pool);

  if (status)
    return svn_error_createf (status, 0, NULL,
                              "svn_io_dir_open: unable to open directory '%s'",
                              dirname);
  else
    return SVN_NO_ERROR;
}


svn_error_t *
svn_io_dir_remove_nonrecursive (const char *dirname, apr_pool_t *pool)
{
  apr_status_t status;
  const char *dirname_native;

  SVN_ERR (svn_utf_cstring_from_utf8 (&dirname_native, dirname, pool));

  status = apr_dir_remove (dirname_native, pool);

  if (status)
    return svn_error_createf (status, 0, NULL,
                              "svn_io_dir_remove_nonrecursive: "
                              "unable to remove directory '%s'",
                              dirname);
  else
    return SVN_NO_ERROR;
}


svn_error_t *
svn_io_dir_read (apr_finfo_t *finfo,
                 apr_int32_t wanted,
                 apr_dir_t *thedir,
                 apr_pool_t *pool)
{
  apr_status_t status;

  status = apr_dir_read (finfo, wanted, thedir);

  if (status)
    return svn_error_create (status, 0, NULL,
                             "svn_io_dir_read: error reading directory");

  if (finfo->fname)
    SVN_ERR (svn_utf_cstring_to_utf8 (&finfo->fname, finfo->fname,
                                      NULL, pool));

  if (finfo->name)
    SVN_ERR (svn_utf_cstring_to_utf8 (&finfo->name, finfo->name,
                                      NULL, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_file_printf (apr_file_t *fptr, const char *format, ...)
{
  apr_status_t status;
  va_list ap;
  const char *buf, *buf_native;

  va_start (ap, format);
  buf = apr_pvsprintf (apr_file_pool_get (fptr), format, ap); 
  va_end(ap);

  SVN_ERR (svn_utf_cstring_from_utf8 (&buf_native, buf,
                                      apr_file_pool_get (fptr)));

  status = apr_file_puts (buf_native, fptr);
  if (status)
    return svn_error_create (status, 0, NULL,
                             "svn_io_file_printf: unable to print to file");
  else
    return SVN_NO_ERROR;
}
 



/* FIXME: Dirty, ugly, abominable, but works. Beauty comes second for now. */
#include "svn_private_config.h"
#ifdef SVN_WIN32
#include <io.h>

static apr_status_t
close_file_descriptor (void *baton)
{
  int fd = (int) baton;
  _close (fd);
  /* Ignore errors from close, because we can't do anything about them. */
  return APR_SUCCESS;
}
#endif

apr_status_t
svn_io_fd_from_file (int *fd_p, apr_file_t *file)
{
  apr_os_file_t fd;
  apr_status_t status = apr_os_file_get (&fd, file);

  if (status == APR_SUCCESS)
    {
#ifndef SVN_WIN32
      *fd_p = fd;
#else
      *fd_p = _open_osfhandle ((long) fd, _O_RDWR);

      /* We must close the file descriptor when the apr_file_t is
         closed, otherwise we'll run out of them. What happens if the
         underlyig file handle is closed first is anyone's guess, so
         the pool cleanup just ignores errors from the close. I hope
         the RTL frees the FD slot before closing the handle ... */
      if (*fd_p < 0)
        status = APR_EBADF;
      else
        {
          /* FIXME: This bit of code assumes that the first element of
             an apr_file_t on Win32 is a pool. It also assumes an int
             will fit into a void*. Please, let's get rid of this ASAP! */
          apr_pool_t *cntxt = *(apr_pool_t**) file;
          apr_pool_cleanup_register (cntxt, (void*) *fd_p,
                                     close_file_descriptor, NULL);
        }
#endif
    }
  return status;
}


/**
 * Determine if a directory is empty or not.
 * @param Return APR_SUCCESS if the dir is empty, else APR_ENOTEMPTY if not.
 * @param path The directory.
 * @param pool Used for temporary allocation.
 * @remark If path is not a directory, or some other error occurs,
 * then return the appropriate apr status code.
 */                        
static apr_status_t
apr_dir_is_empty (const char *dir, apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_dir_t *dir_handle;
  apr_finfo_t finfo;
  apr_status_t retval = APR_SUCCESS;
  
  /* APR doesn't like "" directories */
  if (dir[0] == '\0')
    dir = ".";

  apr_err = apr_dir_open (&dir_handle, dir, pool);
  if (apr_err != APR_SUCCESS)
    return apr_err;
      
  /* ### What is the gospel on the APR_STATUS_IS_SUCCESS macro these
     days? :-) */

  for (apr_err = apr_dir_read (&finfo, APR_FINFO_NAME, dir_handle);
       apr_err == APR_SUCCESS;
       apr_err = apr_dir_read (&finfo, APR_FINFO_NAME, dir_handle))
    {
      /* Ignore entries for this dir and its parent, robustly.
         (APR promises that they'll come first, so technically
         this guard could be moved outside the loop.  But Ryan Bloom
         says he doesn't believe it, and I believe him. */
      if (! (finfo.name[0] == '.'
             && (finfo.name[1] == '\0'
                 || (finfo.name[1] == '.' && finfo.name[2] == '\0'))))
        {
          retval = APR_ENOTEMPTY;
          break;
        }
    }

  /* Make sure we broke out of the loop for the right reason. */
  if (! APR_STATUS_IS_ENOENT (apr_err))
    return apr_err;

  apr_err = apr_dir_close (dir_handle);
  if (apr_err != APR_SUCCESS)
    return apr_err;

  return retval;
}


svn_error_t *
svn_io_dir_empty (svn_boolean_t *is_empty_p,
                  const char *path,
                  apr_pool_t *pool)
{
  apr_status_t status;
  const char *path_native;

  SVN_ERR (svn_utf_cstring_from_utf8 (&path_native, path, pool));

  status = apr_dir_is_empty (path_native, pool);

  if (APR_STATUS_IS_SUCCESS (status))
    *is_empty_p = TRUE;
  else if (APR_STATUS_IS_ENOTEMPTY (status))
    *is_empty_p = FALSE;
  else
    return svn_error_createf (status, 0, NULL,
                              "svn_io_dir_empty: "
                              "unable to check directory '%s'",
                              path);

  return SVN_NO_ERROR;
}



/*** Version/format files ***/

svn_error_t *
svn_io_write_version_file (const char *path,
                           int version,
                           apr_pool_t *pool)
{
  apr_file_t *format_file = NULL;
  apr_status_t apr_err;
  const char *format_contents = apr_psprintf (pool, "%d\n", version);

  /* We only promise to handle non-negative integers. */
  if (version < 0)
    return svn_error_createf (SVN_ERR_INCORRECT_PARAMS, 0, NULL,
                              "Version %d is not non-negative", version);

  /* Open (or create+open) PATH... */
  SVN_ERR (svn_io_file_open (&format_file, path,
                             APR_WRITE | APR_CREATE, APR_OS_DEFAULT, pool));
  
  /* ...dump out our version number string... */
  apr_err = apr_file_write_full (format_file, format_contents,
                                 strlen (format_contents), NULL);
  if (apr_err)
    return svn_error_createf (apr_err, 0, 0, "writing to `%s'", path);
  
  /* ...and close the file. */
  apr_err = apr_file_close (format_file);
  if (apr_err)
    return svn_error_createf (apr_err, 0, 0, "closing `%s'", path);
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_read_version_file (int *version,
                          const char *path,
                          apr_pool_t *pool)
{
  apr_file_t *format_file;
  svn_stream_t *format_stream;
  svn_stringbuf_t *version_str;
  apr_status_t apr_err;

  /* Read a line from PATH */
  SVN_ERR (svn_io_file_open (&format_file, path, APR_READ, 
                             APR_OS_DEFAULT, pool));
  format_stream = svn_stream_from_aprfile (format_file, pool);
  SVN_ERR (svn_stream_readline (format_stream, &version_str, pool));

  /* If there was no data in PATH, return an error. */
  if (! version_str)
    return svn_error_createf (SVN_ERR_STREAM_UNEXPECTED_EOF, 0, NULL,
                              "reading `%s'", path);

  /* Check that the first line contains only digits. */
  {
    char *c;

    for (c = version_str->data; *c; c++)
      {
        if (! apr_isdigit (*c))
          return svn_error_createf
            (SVN_ERR_BAD_VERSION_FILE_FORMAT, 0, NULL,
             "first line of '%s' contains non-digit", path);
      }
  }

  /* Convert to integer. */
  *version = atoi (version_str->data);

  /* And finally, close the file. */
  apr_err = apr_file_close (format_file);
  if (apr_err)
    return svn_error_createf (apr_err, 0, 0, "closing `%s'", path);

  return SVN_NO_ERROR;
}
