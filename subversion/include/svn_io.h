/*
 * svn_io.h :  general Subversion I/O definitions
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

/* ==================================================================== */


#ifndef SVN_IO_H
#define SVN_IO_H

#include <apr.h>
#include <apr_pools.h>
#include <apr_file_io.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_string.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* If PATH exists, set *KIND to the appropriate kind, else set it to
 * svn_node_unknown. 
 *
 * If PATH is a file, *KIND is set to svn_node_file.
 *
 * If PATH is a directory, *KIND is set to svn_node_dir.
 *
 * If PATH does not exist in its final component, *KIND is set to
 * svn_node_none.  
 *
 * If intermediate directories on the way to PATH don't exist, an
 * error is returned, and *KIND's value is undefined.
 */
svn_error_t *svn_io_check_path (const svn_stringbuf_t *path,
                                enum svn_node_kind *kind,
                                apr_pool_t *pool);


/* Open a new file (for writing) with a unique name based on PATH, in the
 * same directory as PATH.  The file handle is returned in *F, and the
 * name, which ends with SUFFIX, is returned in *UNIQUE_NAME.
 *
 * The name will include as much of PATH as possible, then a dot,
 * then a random portion, then another dot, then an iterated attempt
 * number (00001 for the first try, 00002 for the second, etc), and
 * end with SUFFIX.  For example, if PATH is
 *
 *    tests/t1/A/D/G/pi
 *
 * then svn_io_open_unique_file(&f, &uniqe_name, PATH, ".tmp", pool) might pick
 *
 *    tests/t1/A/D/G/pi.3221223676.00001.tmp
 *
 * the first time, then
 *
 *    tests/t1/A/D/G/pi.3221223676.00002.tmp
 *
 * if called again while the first unique file still exists.
 *
 * It doesn't matter if PATH is a file or directory, the unique name will
 * be in PATH's parent either way.
 *
 * *UNIQUE_NAME will never be exactly the same as PATH, even if PATH does
 * not exist.
 * 
 * *F and *UNIQUE_NAME are allocated in POOL.
 *
 * If no unique name can be found, SVN_ERR_IO_UNIQUE_NAMES_EXHAUSTED is
 * the error returned.
 *
 * Claim of Historical Inevitability: this function was written
 * because 
 *
 *    tmpnam() is not thread-safe.
 *    tempname() tries standard system tmp areas first.
 *
 * Claim of Historical Evitability: the random portion of the name is
 * there because someday, someone will have a directory full of files
 * whose names match the iterating portion and suffix (say, a
 * database's holding area).  The random portion is a safeguard
 * against that case.
 */
svn_error_t *svn_io_open_unique_file (apr_file_t **f,
                                      svn_stringbuf_t **unique_name,
                                      const svn_stringbuf_t *path,
                                      const char *suffix,
                                      apr_pool_t *pool);


/* Copy SRC to DST.  DST will be overwritten if it exists, else it
   will be created. */
svn_error_t *svn_io_copy_file (svn_stringbuf_t *src,
                               svn_stringbuf_t *dst,
                               apr_pool_t *pool);

/* Append SRC to DST.  DST will be appended to if it exists, else it
   will be created. */
svn_error_t *svn_io_append_file (svn_stringbuf_t *src,
                                 svn_stringbuf_t *dst,
                                 apr_pool_t *pool);


/* Set *APR_TIME to the later of PATH's (a regular file) mtime or ctime.
 *
 * Unix traditionally distinguishes between "mod time", which is when
 * someone last modified the contents of the file, and "change time",
 * when someone changed something else about the file (such as
 * permissions).
 *
 * Since Subversion versions both kinds of information, our timestamp
 * comparisons have to notice either kind of change.  That's why this
 * function gives the time of whichever kind came later.  APR will
 * hopefully make sure that both ctime and mtime always have useful
 * values, even on OS's that do things differently. (?)
 */
svn_error_t *svn_io_file_affected_time (apr_time_t *apr_time,
                                        svn_stringbuf_t *path,
                                        apr_pool_t *pool);



/* Generic byte-streams */

/* svn_stream_t represents an abstract stream of bytes--either
   incoming or outgoing or both.

   The creator of a stream sets functions to handle read and write.
   Both of these handlers accept a baton whose value is determined at
   stream creation time; this baton can point to a structure
   containing data associated with the stream.  If a caller attempts
   to invoke a handler which has not been set, it will generate a
   runtime assertion failure.  The creator can also set a handler for
   close requests so that it can flush buffered data or whatever;
   if a close handler is not specified, a close request on the stream
   will simply be ignored.  Note that svn_stream_close() does not
   deallocate the memory used to allocate the stream structure; free
   the pool you created the stream in to free that memory.

   The read and write handlers accept length arguments via pointer.
   On entry to the handler, the pointed-to value should be the amount
   of data which can be read or the amount of data to write.  When the
   handler returns, the value is reset to the amount of data actually
   read or written.  Handlers are obliged to complete a read or write
   to the maximum extent possible; thus, a short read with no
   associated error implies the end of the input stream, and a short
   write should never occur without an associated error.  */

typedef struct svn_stream_t svn_stream_t;


/* Handler functions to implement the operations on a generic stream.  */

typedef svn_error_t *(*svn_read_fn_t) (void *baton,
                                       char *buffer,
                                       apr_size_t *len);

typedef svn_error_t *(*svn_write_fn_t) (void *baton,
                                        const char *data,
                                        apr_size_t *len);

typedef svn_error_t *(*svn_close_fn_t) (void *baton);


/* Functions for creating generic streams.  */

svn_stream_t *svn_stream_create (void *baton, apr_pool_t *pool);

void svn_stream_set_read (svn_stream_t *stream, svn_read_fn_t read_fn);

void svn_stream_set_write (svn_stream_t *stream, svn_write_fn_t write_fn);

void svn_stream_set_close (svn_stream_t *stream, svn_close_fn_t close_fn);


/* Convenience function to create a readable generic stream which is
   empty.  */

svn_stream_t *svn_stream_empty (apr_pool_t *pool);


/* Convenience functions for creating streams which operate on APR
   files or on stdio files.  For convenience, if FILE or FP is NULL
   then svn_stream_empty(pool) is returned.  Note that the stream
   returned by these operations is not considered to "own" the
   underlying file, meaning that svn_stream_close() on the stream will
   not close the file.  */

svn_stream_t *svn_stream_from_aprfile (apr_file_t *file, apr_pool_t *pool);

svn_stream_t *svn_stream_from_stdio (FILE *fp, apr_pool_t *pool);


/* Functions for operating on generic streams.  */

svn_error_t *svn_stream_read (svn_stream_t *stream, char *buffer,
                              apr_size_t *len);

svn_error_t *svn_stream_write (svn_stream_t *stream, const char *data,
                               apr_size_t *len);

svn_error_t *svn_stream_close (svn_stream_t *stream);

/* Sets *RESULT to a string containing the contents of FILENAME. */
svn_error_t *svn_string_from_file (svn_stringbuf_t **result, 
                                   const char *filename, 
                                   apr_pool_t *pool);

/* Recursively remove directory PATH. */
apr_status_t apr_dir_remove_recursively (const char *path, apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_IO_H */

/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
