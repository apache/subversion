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
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_file_info.h>
#include <apr_strings.h>
#include <apr_thread_proc.h>
#include <apr_portable.h>
#include "svn_types.h"
#include "svn_path.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_pools.h"



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
  const char *path_name = (path->len == 0 ? "." : path->data);

  apr_err = apr_stat (&finfo, path_name, APR_FINFO_MIN, pool);

  if (apr_err && !APR_STATUS_IS_ENOENT(apr_err))
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "svn_io_check_path: "
                              "problem checking path \"%s\"",
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
                         svn_boolean_t delete_on_close,
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
      apr_int32_t flag = (APR_READ | APR_WRITE | APR_CREATE | APR_EXCL);

      if (delete_on_close)
        flag |= APR_DELONCLOSE;

      /* Tweak last attempted name to get the next one. */
      sprintf (number_buf, "%05d", i);
      (*unique_name)->data[iterating_portion_idx + 0] = number_buf[0];
      (*unique_name)->data[iterating_portion_idx + 1] = number_buf[1];
      (*unique_name)->data[iterating_portion_idx + 2] = number_buf[2];
      (*unique_name)->data[iterating_portion_idx + 3] = number_buf[3];
      (*unique_name)->data[iterating_portion_idx + 4] = number_buf[4];

      apr_err = apr_file_open (f, (*unique_name)->data, flag,
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
                                     (APR_WRITE | APR_CREATE | APR_TRUNCATE),
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


static svn_error_t *
translate_err (apr_status_t err, 
               const char *verb, 
               const char *path,
               apr_pool_t *pool)
{
  return svn_error_createf (err, 0, NULL, pool,
                            "svn_io_convert_eol: error %s `%s'", verb, path);
}


svn_error_t *
svn_io_copy_and_translate (const char *src,
                           const char *dst,
                           const char *eol_str,
                           svn_boolean_t repair,
                           const char *revision,
                           const char *date,
                           const char *author,
                           const char *url,
                           apr_pool_t *pool)
{
  apr_file_t *s = NULL, *d = NULL;  /* init to null important for APR */
  apr_status_t apr_err;
  svn_error_t *err = SVN_NO_ERROR;
  apr_status_t read_err, write_err;
  char c;

  /*
  char newline_buf[3] = { 0 };
  int  newline_off = 0;
  char keyword_buf[SVN_IO_MAX_KEYWORD_LEN] = { 0 };
  int  keyword_off = 0;
  char src_format[3] = { 0 };
  */

  abort(); /* don't use this function yet, it's not finished! */

  /* Open source file. */
  apr_err = apr_file_open (&s, src, APR_READ, APR_OS_DEFAULT, pool);
  if (apr_err)
    return translate_err (apr_err, "opening", src, pool);
  
  /* Open dest file. */
  apr_err = apr_file_open (&d, dst, APR_WRITE | APR_CREATE, 
                           APR_OS_DEFAULT, pool);
  if (apr_err)
    {
      apr_file_close (s); /* toss */
      return translate_err (apr_err, "opening", dst, pool);
    }

  /*** Any errors after this point require us to close the two files and
       remove DST. */
  
  /* Copy bytes till the cows come home (or until one of them breaks a
     leg, at which point you should trot out to the range with your
     trusty sidearm, put her down, and consider steak dinners for the
     next two weeks). */
  while (err == SVN_NO_ERROR)
    {
      /* Read a byte from SRC */
      read_err = apr_file_getc (&c, s);

      /* Check for read errors.  The docstring for apr_file_read
         states that we cannot *both* read bytes AND get an error
         while doing so (include APR_EOF).  Since apr_file_getc is simply a
         wrapper around apr_file_read, we know that if we get any
         error at all, we haven't read any bytes.  This gives us the
         ability to handle the error here without worrying about
         losing the last character of the file simply because we
         recieved an APR_EOF read error.  */
      if (read_err)
        {
          if (!APR_STATUS_IS_EOF(read_err))
            {
              /* This was some error other than EOF! */
              err = translate_err (read_err, "reading", src, pool);
              goto cleanup;
            }
          else
            {
              /* We've reached the end of the file.  Close up shop.  */
              apr_err = apr_file_close (s);
              if (apr_err)
                {
                  s = NULL;
                  err = translate_err (apr_err, "closing", src, pool);
                  goto cleanup;
                }
              apr_err = apr_file_close (d);
              if (apr_err)
                {
                  d = NULL;
                  err = translate_err (apr_err, "closing", dst, pool);
                  goto cleanup;
                }
            }
        }

      /* Handle the byte.  Is it a part of a known newline string?
         Part of a keyword?  */
      switch (c)
        {
        case '\n':
        case '\r':

        case '$':
          /* If we are currently in the middle of tracking a possible
             keyword expansion, then this is its terminator.  Else,
             this is the beginning of a new possible keyword. */
        default:
        }

      /* ### TODO:  Here's the real meat.  Things to keep in mind:

         - It's not enough to only replace instances of SRC's EOL
           string with the EOL string requested.  We need to scan for
           all known EOL strings, because if we find one that *isn't*
           STR's EOL format, we have to decide whether to repair that
           (by replacing it, too) or bail out (complaining that SRC is
           inconsistent). 
           
         - Don't even *think* about removing the abort() at the top if
           this function until you've written good tests for it! >:-(
      */

      /* Write out our (possibly tweaked) data. */
      write_err = apr_file_putc (c, d);
      if (write_err)
        {
          err = translate_err (write_err, "writing", dst, pool);
          goto cleanup;
        }
    }
  return SVN_NO_ERROR;

 cleanup:
  if (s)
    apr_file_close (s); /* toss */
  if (d)
    apr_file_close (d); /* toss */
  apr_file_remove (dst, pool); /* toss */
  return err;
}


svn_error_t *svn_io_copy_dir_recursively (svn_stringbuf_t *src,
                                          svn_stringbuf_t *dst_parent,
                                          svn_stringbuf_t *dst_basename,
                                          apr_pool_t *pool)
{
  enum svn_node_kind kind;
  apr_status_t status;
  apr_hash_t *dirents;
  apr_hash_index_t *hi;
  svn_stringbuf_t *dst_path, *src_target, *dst_target;

  /* Make a subpool for recursion */
  apr_pool_t *subpool = svn_pool_create (pool);

  /* The 'dst_path' is simply dst_parent/dst_basename */
  dst_path = svn_stringbuf_dup (dst_parent, pool);
  svn_path_add_component (dst_path, dst_basename, svn_path_local_style);

  /* Sanity checks:  SRC and DST_PARENT are directories, and
     DST_BASENAME doesn't already exist in DST_PARENT. */
  SVN_ERR (svn_io_check_path (src, &kind, subpool));
  if (kind != svn_node_dir)
    return svn_error_createf (SVN_ERR_WC_UNEXPECTED_KIND, 0, NULL, subpool,
                              "svn_io_copy_dir: '%s' is not a directory.",
                              src->data);

  SVN_ERR (svn_io_check_path (dst_parent, &kind, subpool));
  if (kind != svn_node_dir)
    return svn_error_createf (SVN_ERR_WC_UNEXPECTED_KIND, 0, NULL, subpool,
                              "svn_io_copy_dir: '%s' is not a directory.",
                              dst_parent->data);

  SVN_ERR (svn_io_check_path (dst_path, &kind, subpool));
  if (kind != svn_node_none)
    return svn_error_createf (SVN_ERR_WC_ENTRY_EXISTS, 0, NULL, subpool,
                              "'%s' already exists.", dst_path->data);
  
  /* Create the new directory. */
  status = apr_dir_make (dst_path->data, APR_OS_DEFAULT, pool);
  if (status)
    return svn_error_createf (status, 0, NULL, pool,
                              "Unable to create directory '%s'",
                              dst_path->data);

  /* Loop over the dirents in SRC.  ('.' and '..' are auto-excluded) */
  SVN_ERR (svn_io_get_dirents (&dirents, src, subpool));

  src_target = svn_stringbuf_dup (src, subpool);
  dst_target = svn_stringbuf_dup (dst_path, subpool);

  for (hi = apr_hash_first (subpool, dirents); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      const char *entryname;
      enum svn_node_kind *entrykind;

      /* Get next entry and its kind */
      apr_hash_this (hi, &key, &klen, &val);
      entryname = (char *) key;
      entrykind = (enum svn_node_kind *) val;

      /* Telescope the entryname onto the source dir. */
      svn_path_add_component_nts (src_target, entryname,
                                  svn_path_local_style);

      /* If it's a file, just copy it over. */
      if (*entrykind == svn_node_file)
        {
          /* Telescope and de-telescope the dst_target in here */
          svn_path_add_component_nts (dst_target, entryname,
                                      svn_path_local_style);
          SVN_ERR (svn_io_copy_file (src_target, dst_target, subpool));
          svn_path_remove_component (dst_target, svn_path_local_style);
        }          

      /* If it's a directory, recurse. */
      else if (*entrykind == svn_node_dir)
        SVN_ERR (svn_io_copy_dir_recursively (src_target,
                                              dst_target,
                                              svn_stringbuf_create (entryname,
                                                                    subpool),
                                              subpool));

      /* ### someday deal with other node kinds? */

      /* De-telescope the source dir for the next iteration. */
      svn_path_remove_component (src_target, svn_path_local_style);
    }
    

  /* Free any memory used by recursion */
  apr_pool_destroy (subpool);
           
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


/* TODO write test for these two functions, then refactor. */

svn_error_t *
svn_string_from_file (svn_stringbuf_t **result,
                      const char *filename,
                      apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_file_t *f = NULL;

  apr_err = apr_file_open (&f, filename, APR_READ, APR_OS_DEFAULT, pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "read_from_file: failed to open '%s'",
                              filename);

  SVN_ERR (svn_string_from_aprfile (result, f, pool));

  apr_err = apr_file_close (f);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "svn_string_from_file: failed to close '%s'",
                              filename);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_string_from_aprfile (svn_stringbuf_t **result,
                         apr_file_t *file,
                         apr_pool_t *pool)
{
  /* ### this function must be fixed to do an apr_stat() for SIZE,
     ### alloc the buffer, then read the file into the buffer. Using
     ### an svn_stringbuf_t means quadratic memory usage: start with
     ### BUFSIZE, allocate 2*BUFSIZE, then alloc 4*BUFSIZE, etc.
     ### The pools keep each of those allocs around. */
  char buf[BUFSIZ];
  apr_size_t len;
  apr_status_t apr_err;
  svn_stringbuf_t *res = svn_stringbuf_create("", pool);

  do 
    {
      apr_err = apr_file_read_full (file, buf, sizeof(buf), &len);
      if (apr_err && !APR_STATUS_IS_EOF (apr_err))
        {
          const char * filename;
          apr_file_name_get (&filename, file);
          return svn_error_createf 
            (apr_err, 0, NULL, pool,
             "svn_string_from_aprfile: failed to read '%s'", filename);
        }
      
      svn_stringbuf_appendbytes (res, buf, len);
    } 
  while (len != 0);

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


/* Invoke PROGRAM with ARGS, using PATH as working directory.
 * Connect PROGRAM's stdin, stdout, and stderr to INFILE, OUTFILE, and
 * ERRFILE, except where they are null.
 *
 * ARGS is a list of (const char *)'s, terminated by NULL.
 * ARGS[0] is the name of the program, though it need not be the same
 * as CMD.
 */
svn_error_t *
svn_io_run_cmd (const char *path,
                const char *cmd,
                const char *const *args,
                int *exitcode,
                apr_exit_why_e *exitwhy,
                apr_file_t *infile,
                apr_file_t *outfile,
                apr_file_t *errfile,
                apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_proc_t cmd_proc;
  apr_procattr_t *cmdproc_attr;

  /* Create the process attributes. */
  apr_err = apr_procattr_create (&cmdproc_attr, pool); 
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf
      (apr_err, 0, NULL, pool,
       "run_cmd_in_directory: error creating %s process attributes",
       cmd);

  /* Make sure we invoke cmd directly, not through a shell. */
  apr_err = apr_procattr_cmdtype_set (cmdproc_attr, APR_PROGRAM);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf 
      (apr_err, 0, NULL, pool,
       "run_cmd_in_directory: error setting %s process cmdtype",
       cmd);

  /* Set the process's working directory. */
  if (path)
    {
      apr_err = apr_procattr_dir_set (cmdproc_attr, path);
      if (! APR_STATUS_IS_SUCCESS (apr_err))
        return svn_error_createf 
          (apr_err, 0, NULL, pool,
           "run_cmd_in_directory: error setting %s process directory",
           cmd);
    }

  /* Set io style. */
  apr_err = apr_procattr_io_set (cmdproc_attr, APR_FULL_BLOCK, 
                                APR_CHILD_BLOCK, APR_CHILD_BLOCK);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf
      (apr_err, 0, NULL, pool,
       "run_cmd_in_directory: error setting %s process io attributes",
       cmd);

  /* Use requested inputs and outputs. */
  if (infile)
    {
      apr_err = apr_procattr_child_in_set (cmdproc_attr, infile, NULL);
      if (! APR_STATUS_IS_SUCCESS (apr_err))
        return svn_error_createf 
          (apr_err, 0, NULL, pool,
           "run_cmd_in_directory: error setting %s process child input",
           cmd);
    }
  if (outfile)
    {
      apr_err = apr_procattr_child_out_set (cmdproc_attr, outfile, NULL);
      if (! APR_STATUS_IS_SUCCESS (apr_err))
        return svn_error_createf 
          (apr_err, 0, NULL, pool,
           "run_cmd_in_directory: error setting %s process child outfile",
           cmd);
    }
  if (errfile)
    {
      apr_err = apr_procattr_child_err_set (cmdproc_attr, errfile, NULL);
      if (! APR_STATUS_IS_SUCCESS (apr_err))
        return svn_error_createf 
          (apr_err, 0, NULL, pool,
           "run_cmd_in_directory: error setting %s process child errfile",
           cmd);
    }

  /* Start the cmd command. */ 
  apr_err = apr_proc_create (&cmd_proc, cmd, args, NULL, cmdproc_attr, pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf 
      (apr_err, 0, NULL, pool,
       "run_cmd_in_directory: error starting %s process",
       cmd);

  /* Wait for the cmd command to finish. */
  apr_err = apr_proc_wait (&cmd_proc, exitcode, exitwhy, APR_WAIT);
  if (APR_STATUS_IS_CHILD_NOTDONE (apr_err))
    return svn_error_createf
      (apr_err, 0, NULL, pool,
       "run_cmd_in_directory: error waiting for %s process",
       cmd);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_detect_mimetype (const char **mimetype,
                        const char *file,
                        apr_pool_t *pool)
{
  enum svn_node_kind kind;
  apr_file_t *fh;
  const char *generic_binary = "application/octet-stream";
  apr_status_t apr_err;
  unsigned char block[1024];
  apr_size_t amt_read = sizeof (block);


  /* Default return value is NULL. */
  *mimetype = NULL;

  /* See if this file even exists, and make sure it really is a file. */
  SVN_ERR (svn_io_check_path (svn_stringbuf_create (file, pool), &kind, pool));
  if (kind != svn_node_file)
    return svn_error_createf (SVN_ERR_BAD_FILENAME, 0, NULL, pool,
                              "Can't detect mimetype of non-file '%s'",
                              file);

  apr_err = apr_file_open (&fh, file, APR_READ, 0, pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "svn_io_detect_mimetype: error opening '%s'",
                              file);

  /* Read a block of data from FILE. */
  apr_err = apr_file_read (fh, block, &amt_read);
  if (apr_err && (apr_err != APR_EOF))
    return svn_error_createf (apr_err, 0, NULL, pool,
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


apr_status_t
apr_check_dir_empty (const char *path, 
                     apr_pool_t *pool)
{
  apr_status_t apr_err, retval;
  apr_dir_t *dir;
  apr_finfo_t finfo;
  
  apr_err = apr_dir_open (&dir, path, pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return apr_err;
      
  /* All systems return "." and ".." as the first two files, so read
     past them unconditionally. */
  apr_err = apr_dir_read (&finfo, APR_FINFO_NAME, dir);
  if (! APR_STATUS_IS_SUCCESS (apr_err)) return apr_err;
  apr_err = apr_dir_read (&finfo, APR_FINFO_NAME, dir);
  if (! APR_STATUS_IS_SUCCESS (apr_err)) return apr_err;

  /* Now, there should be nothing left.  If there is something left,
     return EGENERAL. */
  apr_err = apr_dir_read (&finfo, APR_FINFO_NAME, dir);
  if (APR_STATUS_IS_ENOENT (apr_err))
    retval = APR_SUCCESS;
  else if (APR_STATUS_IS_SUCCESS (apr_err))
    retval = APR_EGENERAL;
  else
    retval = apr_err;

  apr_err = apr_dir_close (dir);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return apr_err;

  return retval;
}





/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
