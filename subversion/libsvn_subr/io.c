/*
 * io.c:   shared file reading, writing, and probing code.
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
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
#include "svn_types.h"
#include "svn_path.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_io.h"



struct svn_stream_t {
  void *baton;
  svn_read_fn_t *read_fn;
  svn_write_fn_t *write_fn;
  svn_close_fn_t *close_fn;
  apr_pool_t *pool;
};



svn_error_t *
svn_io_check_path (const svn_string_t *path,
                   enum svn_node_kind *kind,
                   apr_pool_t *pool)
{
  apr_finfo_t finfo;
  apr_status_t apr_err;

  apr_err = apr_stat (&finfo, path->data, pool);

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
                         svn_string_t **unique_name,
                         const svn_string_t *path,
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
  char *random_portion = apr_psprintf (pool, "%hu%n",
                                       unique_name,
                                       &random_portion_width);

  *unique_name = svn_string_dup (path, pool);

  /* Not sure of a portable PATH_MAX constant to use here, so just
     guessing at 255. */
  if ((*unique_name)->len >= 255)
    {
      int chop_amt = ((*unique_name)->len - 255)
                      + random_portion_width
                      + 3  /* 2 dots */
                      + 5  /* 5 digits of iteration portion */
                      + strlen (suffix);
      svn_string_chop (*unique_name, chop_amt);
    }

  iterating_portion_idx = (*unique_name)->len + random_portion_width + 2;
  svn_string_appendcstr (*unique_name,
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

      apr_err = apr_open (f, (*unique_name)->data,
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


svn_error_t *
svn_io_file_reader (void *filehandle,
                    char *buffer,
                    apr_size_t *len,
                    apr_pool_t *pool)
{
  apr_status_t stat;

  /* Recover our filehandle */
  apr_file_t *the_file = (apr_file_t *) filehandle;

  if (the_file == NULL)
    *len = 0;
  else
    {
      stat = apr_full_read (the_file, buffer,
                            (apr_size_t) *len,
                            (apr_size_t *) len);
      
      if (stat && !APR_STATUS_IS_EOF(stat))
        return
          svn_error_create (stat, 0, NULL, pool,
                            "adm_crawler.c (posix_file_reader): "
                            "file read error");
    }

  return SVN_NO_ERROR;  
}


svn_error_t *
svn_io_file_writer (void *filehandle,
                    const char *buffer,
                    apr_size_t *len,
                    apr_pool_t *pool)
{
  apr_file_t *dst = (apr_file_t *) filehandle;
  apr_status_t stat;
  
  stat = apr_full_write (dst, buffer, (apr_size_t) *len, (apr_size_t *) len);
  
  if (stat && !APR_STATUS_IS_EOF(stat))
    return
      svn_error_create (stat, 0, NULL, pool,
                        "error writing xml delta");
  else 
    return 0;  
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
  apr_err = apr_open (&d, dst, flags, perms, pool);
  if (apr_err)
    {
      apr_close (s);  /* toss */
      return apr_err;
    }
  
  /* Copy bytes till the cows come home. */
  read_err = 0;
  while (!APR_STATUS_IS_EOF(read_err))
    {
      apr_size_t bytes_this_time = sizeof (buf);

      /* Read 'em. */
      read_err = apr_read (s, buf, &bytes_this_time);
      if (read_err && !APR_STATUS_IS_EOF(read_err))
        {
          apr_close (s);  /* toss */
          apr_close (d);  /* toss */
          return read_err;
        }

      /* Write 'em. */
      write_err = apr_full_write (d, buf, bytes_this_time, NULL);
      if (write_err)
        {
          apr_close (s);  /* toss */
          apr_close (d);
          return write_err;
        }

      if (read_err && APR_STATUS_IS_EOF(read_err))
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
svn_io_copy_file (svn_string_t *src, svn_string_t *dst, apr_pool_t *pool)
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
svn_io_append_file (svn_string_t *src, svn_string_t *dst, apr_pool_t *pool)
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
                           svn_string_t *path,
                           apr_pool_t *pool)
{
  apr_finfo_t finfo;
  apr_status_t apr_err;

  apr_err = apr_stat (&finfo, path->data, pool);
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
  stream->pool = pool;
  return stream;
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
  apr_pool_t *subpool;
  svn_error_t *err;

  assert (stream->read_fn != NULL);
  subpool = svn_pool_create (stream->pool);
  err = stream->read_fn (stream->baton, buffer, len, subpool);
  apr_destroy_pool (subpool);
  return err;
}


svn_error_t *
svn_stream_write (svn_stream_t *stream, const char *data, apr_size_t *len)
{
  apr_pool_t *subpool;
  svn_error_t *err;

  assert (stream->write_fn != NULL);
  subpool = svn_pool_create (stream->pool);
  err = stream->write_fn (stream->baton, data, len, subpool);
  apr_destroy_pool (subpool);
  return err;
}


svn_error_t *
svn_stream_close (svn_stream_t *stream)
{
  if (stream->close_fn == NULL)
    return SVN_NO_ERROR;
  return stream->close_fn (stream->baton);
}



/*** Generic stream for APR files ***/
struct baton_apr {
  apr_file_t *file;
  apr_pool_t *pool;
};


static svn_error_t *
read_handler_apr (void *baton, char *buffer, apr_size_t *len, apr_pool_t *pool)
{
  struct baton_apr *btn = baton;
  apr_status_t status;

  if (btn->file == NULL)
    *len = 0;
  else
    {
      status = apr_full_read (btn->file, buffer, *len, len);
      if (!APR_STATUS_IS_SUCCESS(status) && !APR_STATUS_IS_EOF(status))
        return svn_error_create (status, 0, NULL, btn->pool, "reading file");
    }
  return SVN_NO_ERROR;
}


static svn_error_t *
write_handler_apr (void *baton, const char *data, apr_size_t *len,
                   apr_pool_t *pool)
{
  struct baton_apr *btn = baton;
  apr_status_t status;

  status = apr_full_write (btn->file, data, *len, len);
  if (!APR_STATUS_IS_SUCCESS(status))
    return svn_error_create (status, 0, NULL, btn->pool, "writing file");
  else
    return SVN_NO_ERROR;
}


static svn_error_t *
close_handler_apr (void *baton)
{
  struct baton_apr *btn = baton;
  apr_status_t status;

  if (btn->file != NULL)
    {
      status = apr_close (btn->file);
      if (!APR_STATUS_IS_SUCCESS(status))
        return svn_error_create (status, 0, NULL, btn->pool, "closing file");
    }
  return SVN_NO_ERROR;
}


svn_stream_t *
svn_stream_from_aprfile (apr_file_t *file, apr_pool_t *pool)
{
  struct baton_apr *baton = apr_palloc (pool, sizeof (*baton));
  svn_stream_t *stream;

  baton->file = file;
  baton->pool = pool;
  stream = svn_stream_create (baton, pool);
  svn_stream_set_read (stream, read_handler_apr);
  svn_stream_set_write (stream, write_handler_apr);
  svn_stream_set_close (stream, close_handler_apr);
  return stream;
}



/*** Generic stream for stdio files ***/
struct baton_stdio {
  FILE *fp;
  apr_pool_t *pool;
};


static svn_error_t *
read_handler_stdio (void *baton, char *buffer, apr_size_t *len,
                    apr_pool_t *pool)
{
  struct baton_stdio *btn = baton;
  svn_error_t *err = SVN_NO_ERROR;
  apr_size_t count;

  if (btn->fp == NULL)
    *len = 0;
  else
    {
      count = fread (buffer, 1, *len, btn->fp);
      if (count < *len && ferror(btn->fp))
        err = svn_error_create (0, errno, NULL, btn->pool, "reading file");
      *len = count;
    }
  return err;
}


static svn_error_t *
write_handler_stdio (void *baton, const char *data, apr_size_t *len,
                     apr_pool_t *pool)
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


static svn_error_t *
close_handler_stdio (void *baton)
{
  struct baton_stdio *btn = baton;

  if (btn->fp != NULL && fclose (btn->fp) != 0)
    return svn_error_create (0, errno, NULL, btn->pool, "closing file");
  else
    return SVN_NO_ERROR;
}


svn_stream_t *svn_stream_from_stdio (FILE *fp, apr_pool_t *pool)
{
  struct baton_stdio *baton = apr_palloc (pool, sizeof (*baton));
  svn_stream_t *stream;

  baton->fp = fp;
  baton->pool = pool;
  stream = svn_stream_create (baton, pool);
  svn_stream_set_read (stream, read_handler_stdio);
  svn_stream_set_write (stream, write_handler_stdio);
  svn_stream_set_close (stream, close_handler_stdio);
  return stream;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
