/*
 * io.c:   shared file reading, writing, and probing code.
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_file_info.h>
#include <apr_strings.h>
#include "svn_types.h"
#include "svn_path.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_io.h"



struct svn_stream_t {
  void *baton;
  svn_read_fn_t read_fn;
  svn_write_fn_t write_fn;
  svn_close_fn_t close_fn;
};



svn_error_t *
svn_io_check_path (const svn_stringbuf_t *path,
                   enum svn_node_kind *kind,
                   apr_pool_t *pool)
{
  apr_finfo_t finfo;
  apr_status_t apr_err;

  apr_err = apr_stat (&finfo, path->data, APR_FINFO_MIN, pool);

  if (apr_err && !APR_STATUS_IS_ENOENT(apr_err))
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "svn_io_check_path: "
                              "problem checking path %s",
                              path->data);
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
                         svn_stringbuf_t **unique_name,
                         const svn_stringbuf_t *path,
                         const char *suffix,
                         apr_pool_t *pool)
{
  char number_buf[6];
  int i;
  apr_size_t iterating_portion_idx;

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

  *unique_name = svn_stringbuf_dup (path, pool);

  /* Not sure of a portable PATH_MAX constant to use here, so just
     guessing at 255. */
  if ((*unique_name)->len >= 255)
    {
      int chop_amt = ((*unique_name)->len - 255)
                      + random_portion_width
                      + 3  /* 2 dots */
                      + 5  /* 5 digits of iteration portion */
                      + strlen (suffix);
      svn_stringbuf_chop (*unique_name, chop_amt);
    }

  iterating_portion_idx = (*unique_name)->len + random_portion_width + 2;
  svn_stringbuf_appendcstr (*unique_name,
                         apr_psprintf (pool, ".%s.00000%s",
                                       random_portion, suffix));

  for (i = 1; i <= 99999; i++)
    {
      apr_status_t apr_err;

      /* Tweak last attempted name to get the next one. */
      sprintf (number_buf, "%05d", i);
      (*unique_name)->data[iterating_portion_idx + 0] = number_buf[0];
      (*unique_name)->data[iterating_portion_idx + 1] = number_buf[1];
      (*unique_name)->data[iterating_portion_idx + 2] = number_buf[2];
      (*unique_name)->data[iterating_portion_idx + 3] = number_buf[3];
      (*unique_name)->data[iterating_portion_idx + 4] = number_buf[4];

      apr_err = apr_file_open (f, (*unique_name)->data,
                          (APR_WRITE | APR_CREATE | APR_EXCL),
                          APR_OS_DEFAULT, pool);

      if (APR_STATUS_IS_EEXIST(apr_err))
        continue;
      else if (apr_err)
        {
          char *filename = (*unique_name)->data;
          *f = NULL;
          *unique_name = NULL;
          return svn_error_createf (apr_err,
                                    0,
                                    NULL,
                                    pool,
                                    "svn_io_open_unique_file: "
                                    "error attempting %s",
                                    filename);
        }
      else
        return SVN_NO_ERROR;
    }

  *f = NULL;
  *unique_name = NULL;
  return svn_error_createf (SVN_ERR_IO_UNIQUE_NAMES_EXHAUSTED,
                            0,
                            NULL,
                            pool,
                            "svn_io_open_unique_file: unable to make name for "
                            "%s", path->data);
}



/*** Copying and appending files. ***/

#ifndef apr_transfer_file_contents
/**
 * copy or append one file to another
 * [This is a helper routine for apr_copy_file() and apr_append_file().]
 * @param from_path The full path to the source file (using / on all systems)
 * @param to_path The full path to the dest file (using / on all systems)
 * @flags the flags with which to open the dest file
 * @param pool The pool to use.
 * @tip The source file will be copied until EOF is reached, not until
 *      its size at the time of opening is reached.
 * @tip The dest file will be created if it does not exist.
 */
apr_status_t apr_transfer_file_contents (const char *src,
                                         const char *dst,
                                         apr_int32_t flags,
                                         apr_pool_t *pool);

apr_status_t
apr_transfer_file_contents (const char *src,
                            const char *dst,
                            apr_int32_t flags,
                            apr_pool_t *pool)
{
  apr_file_t *s = NULL, *d = NULL;  /* init to null important for APR */
  apr_status_t apr_err;
  apr_status_t read_err, write_err;
  apr_finfo_t finfo;
  apr_fileperms_t perms;
  char buf[BUFSIZ];

  /* Open source file. */
  apr_err = apr_file_open (&s, src, APR_READ, APR_OS_DEFAULT, pool);
  if (apr_err)
    return apr_err;
  
  /* Get its size. */
  apr_err = apr_file_info_get (&finfo, APR_FINFO_MIN, s);
  if (apr_err)
    {
      apr_file_close (s);  /* toss any error */
      return apr_err;
    }
  else
    perms = finfo.protection;

  /* Open dest file. */
  apr_err = apr_file_open (&d, dst, flags, perms, pool);
  if (apr_err)
    {
      apr_file_close (s);  /* toss */
      return apr_err;
    }
  
  /* Copy bytes till the cows come home. */
  read_err = 0;
  while (!APR_STATUS_IS_EOF(read_err))
    {
      apr_size_t bytes_this_time = sizeof (buf);

      /* Read 'em. */
      read_err = apr_file_read (s, buf, &bytes_this_time);
      if (read_err && !APR_STATUS_IS_EOF(read_err))
        {
          apr_file_close (s);  /* toss */
          apr_file_close (d);  /* toss */
          return read_err;
        }

      /* Write 'em. */
      write_err = apr_file_write_full (d, buf, bytes_this_time, NULL);
      if (write_err)
        {
          apr_file_close (s);  /* toss */
          apr_file_close (d);
          return write_err;
        }

      if (read_err && APR_STATUS_IS_EOF(read_err))
        {
          apr_err = apr_file_close (s);
          if (apr_err)
            {
              apr_file_close (d);
              return apr_err;
            }
          
          apr_err = apr_file_close (d);
          if (apr_err)
            return apr_err;
        }
    }

  return 0;
}
#endif /* apr_transfer_file_contents */


#ifndef apr_copy_file
/**
 * copy one file to another
 * @param from_path The full path to the source file (using / on all systems)
 * @param to_path The full path to the dest file (using / on all systems)
 * @param pool The pool to use.
 * @tip If a file exists at the new location, then it will be
 *      overwritten, else it will be created.
 * @tip The source file will be copied until EOF is reached, not until
 *      its size at the time of opening is reached.
 */
apr_status_t
apr_copy_file (const char *src, const char *dst, apr_pool_t *pool);

apr_status_t
apr_copy_file (const char *src, const char *dst, apr_pool_t *pool)
{
  return apr_transfer_file_contents (src, dst,
                                     (APR_WRITE | APR_CREATE),
                                     pool);
}
#endif /* apr_copy_file */


#ifndef apr_append_file
/**
 * append src file's onto dest file
 * @param from_path The full path to the source file (using / on all systems)
 * @param to_path The full path to the dest file (using / on all systems)
 * @param pool The pool to use.
 * @tip If a file exists at the new location, then it will be appended
 *      to, else it will be created.
 * @tip The source file will be copied until EOF is reached, not until
 *      its size at the time of opening is reached.
 */
apr_status_t
apr_append_file (const char *src, const char *dst, apr_pool_t *pool);

apr_status_t
apr_append_file (const char *src, const char *dst, apr_pool_t *pool)
{
  return apr_transfer_file_contents (src, dst,
                                     (APR_WRITE | APR_APPEND | APR_CREATE),
                                     pool);
}
#endif /* apr_append_file */


svn_error_t *
svn_io_copy_file (svn_stringbuf_t *src, svn_stringbuf_t *dst, apr_pool_t *pool)
{
  apr_status_t apr_err;

  apr_err = apr_copy_file (src->data, dst->data, pool);
  if (apr_err)
    {
      const char *msg
        = apr_psprintf (pool, "svn_io_copy_file: copying %s to %s",
                        src->data, dst->data);
      return svn_error_create (apr_err, 0, NULL, pool, msg);
    }
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_append_file (svn_stringbuf_t *src, svn_stringbuf_t *dst, apr_pool_t *pool)
{
  apr_status_t apr_err;

  apr_err = apr_append_file (src->data, dst->data, pool);
  if (apr_err)
    {
      const char *msg
        = apr_psprintf (pool, "svn_io_append_file: appending %s to %s",
                        src->data, dst->data);
      return svn_error_create (apr_err, 0, NULL, pool, msg);
    }
  
  return SVN_NO_ERROR;
}



/*** Modtime checking. ***/

svn_error_t *
svn_io_file_affected_time (apr_time_t *apr_time,
                           svn_stringbuf_t *path,
                           apr_pool_t *pool)
{
  apr_finfo_t finfo;
  apr_status_t apr_err;

  apr_err = apr_stat (&finfo, path->data, APR_FINFO_MIN, pool);
  if (apr_err)
    return svn_error_createf
      (apr_err, 0, NULL, pool,
       "svn_io_file_affected_time: cannot stat %s", path->data);

  if (finfo.mtime > finfo.ctime)
    *apr_time = finfo.mtime;
  else
    *apr_time = finfo.ctime;

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
  if (!APR_STATUS_IS_SUCCESS(status) && !APR_STATUS_IS_EOF(status))
    return svn_error_create (status, 0, NULL, btn->pool, "reading file");
  else
    return SVN_NO_ERROR;
}


static svn_error_t *
write_handler_apr (void *baton, const char *data, apr_size_t *len)
{
  struct baton_apr *btn = baton;
  apr_status_t status;

  status = apr_file_write_full (btn->file, data, *len, len);
  if (!APR_STATUS_IS_SUCCESS(status))
    return svn_error_create (status, 0, NULL, btn->pool, "writing file");
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
    err = svn_error_create (0, errno, NULL, btn->pool, "reading file");
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
    err = svn_error_create (0, errno, NULL, btn->pool, "reading file");
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


/* TODO write test for this, then refactor. */
svn_error_t *
svn_string_from_file (svn_stringbuf_t **result, const char *filename, apr_pool_t *pool)
{
  /* ### this function must be fixed to do an apr_stat() for SIZE,
     ### alloc the buffer, then read the file into the buffer. Using
     ### an svn_stringbuf_t means quadratic memory usage: start with
     ### BUFSIZE, allocate 2*BUFSIZE, then alloc 4*BUFSIZE, etc.
     ### The pools keep each of those allocs around. */

  svn_stringbuf_t *res;
  apr_status_t apr_err;
  char buf[BUFSIZ];
  apr_size_t len;
  apr_file_t *f = NULL;

  res = svn_stringbuf_create ("", pool);

  apr_err = apr_file_open (&f, filename, APR_READ, APR_OS_DEFAULT, pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "read_from_file: failed to open '%s'",
                              filename);
      
  do {
    apr_err = apr_file_read_full (f, buf, sizeof(buf), &len);
    if (apr_err && !APR_STATUS_IS_EOF (apr_err))
      return svn_error_createf (apr_err, 0, NULL, pool,
                                "read_from_file: failed to read '%s'",
                                filename);
    
    svn_stringbuf_appendbytes (res, buf, len);
  } while (len != 0);

  apr_err = apr_file_close (f);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "read_from_file: failed to close '%s'",
                              filename);
  
  *result = res;
  return SVN_NO_ERROR;
}





/* Recursive directory deletion. */

/* Neither windows nor unix allows us to delete a non-empty
   directory.  

   This is a function to perform the equivalent of 'rm -rf'. */

apr_status_t
apr_dir_remove_recursively (const char *path, apr_pool_t *pool)
{
  apr_status_t status;
  apr_dir_t *this_dir;
  apr_finfo_t this_entry;
  apr_pool_t *subpool;
  apr_int32_t flags = APR_FINFO_TYPE | APR_FINFO_NAME;

  status = apr_pool_create (&subpool, pool);
  if (! (APR_STATUS_IS_SUCCESS (status))) return status;

  status = apr_dir_open (&this_dir, path, subpool);
  if (! (APR_STATUS_IS_SUCCESS (status))) return status;

  for (status = apr_dir_read (&this_entry, flags, this_dir);
       APR_STATUS_IS_SUCCESS (status);
       status = apr_dir_read (&this_entry, flags, this_dir))
    {
      char *fullpath = apr_pstrcat (subpool, path, "/", this_entry.name, NULL);

      if (this_entry.filetype == APR_DIR)
        {
          if ((strcmp (this_entry.name, ".") == 0)
              || (strcmp (this_entry.name, "..") == 0))
            continue;

          status = apr_dir_remove_recursively (fullpath, subpool);
          if (! (APR_STATUS_IS_SUCCESS (status))) return status;
        }
      else if (this_entry.filetype == APR_REG)
        {
          status = apr_file_remove (fullpath, subpool);
          if (! (APR_STATUS_IS_SUCCESS (status))) return status;
        }
    }

  if (! (APR_STATUS_IS_ENOENT (status)))
    return status;

  else
    {
      status = apr_dir_close (this_dir);
      if (! (APR_STATUS_IS_SUCCESS (status))) return status;
    }

  status = apr_dir_remove (path, subpool);
  if (! (APR_STATUS_IS_SUCCESS (status))) return status;

  apr_pool_destroy (subpool);

  return APR_SUCCESS;
}



svn_error_t *
svn_io_get_dirents (apr_hash_t **dirents,
                    svn_stringbuf_t *path,
                    apr_pool_t *pool)
{
  apr_status_t status; 
  apr_dir_t *this_dir;
  apr_finfo_t this_entry;
  apr_int32_t flags = APR_FINFO_TYPE | APR_FINFO_NAME;

  /* These exist so we can use their addresses as hash values! */
  static const enum svn_node_kind static_svn_node_file = svn_node_file;
  static const enum svn_node_kind static_svn_node_dir = svn_node_dir;
  static const enum svn_node_kind static_svn_node_unknown = svn_node_unknown;

  *dirents = apr_hash_make (pool);
  
  status = apr_dir_open (&this_dir, path->data, pool);
  if (status) 
    return
      svn_error_createf (status, 0, NULL, pool,
                         "svn_io_get_dirents:  failed to open dir '%s'",
                         path->data);

  for (status = apr_dir_read (&this_entry, flags, this_dir);
       APR_STATUS_IS_SUCCESS (status);
       status = apr_dir_read (&this_entry, flags, this_dir))
    {
      if ((strcmp (this_entry.name, "..") == 0)
          || (strcmp (this_entry.name, ".") == 0))
        continue;
      else
        {
          const char *name = apr_pstrdup (pool, this_entry.name);
          
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
      svn_error_createf (status, 0, NULL, pool,
                         "svn_io_get_dirents:  error while reading dir '%s'",
                         path->data);

  status = apr_dir_close (this_dir);
  if (status) 
    return
      svn_error_createf (status, 0, NULL, pool,
                         "svn_io_get_dirents:  failed to close dir '%s'",
                         path->data);
  
  return SVN_NO_ERROR;
}







/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
