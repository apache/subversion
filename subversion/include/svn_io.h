/*
 * svn_io.h :  general Subversion I/O definitions
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

/* ==================================================================== */


#ifndef SVN_IO_H
#define SVN_IO_H

#include "svn_types.h"
#include "svn_error.h"
#include "svn_string.h"



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
svn_error_t *svn_io_check_path (const svn_string_t *path,
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
                                      svn_string_t **unique_name,
                                      const svn_string_t *path,
                                      const char *suffix,
                                      apr_pool_t *pool);



/* A typedef for functions resembling the POSIX `read' system call,
   representing a incoming stream of bytes, in `caller-pulls' form.

   We will need to compute text deltas for data drawn from files,
   memory, sockets, and so on.  We will need to read tree deltas from
   various sources.  The data may be huge --- too large to read into
   memory at one time.  Using a `read'-like function like this to
   represent the input data allows us to process the data as we go.

   BATON is some opaque structure representing what we're reading.
   Whoever provided the function gets to use BATON however they
   please.

   BUFFER is a buffer to hold the data, and *LEN indicates how many
   bytes to read.  Upon return, the function should set *LEN to the
   number of bytes actually read, or zero at the end of the data
   stream.  *LEN should only change when there is a read error or the
   input stream ends before the full count of bytes can be read; the
   generic read function is obligated to perform a full read when
   possible.

   Any necessary temporary allocation should be done in a sub-pool of
   POOL.  (If the read function needs to perform allocations which
   last beyond the lifetime of the function, it must use a different
   pool, e.g. one referenced through BATON.)  */
typedef svn_error_t *svn_read_fn_t (void *baton,
                                    char *buffer,
                                    apr_size_t *len,
                                    apr_pool_t *pool);

/* Similar to svn_read_fn_t, but for writing.  */
typedef svn_error_t *svn_write_fn_t (void *baton,
				     const char *data,
				     apr_size_t *len,
				     apr_pool_t *pool);


/* A posix-like read function of type svn_read_fn_t (see above).
   Given an already-open APR FILEHANDLE, read LEN bytes into BUFFER.  
   (Notice that FILEHANDLE is void *, to match svn_io_read_fn_t).

   As a convenience, if FILEHANDLE is null, then this function will
   set *LEN to 0 and do nothing to BUFFER every time.
*/
svn_error_t *svn_io_file_reader (void *filehandle,
                                 char *buffer,
                                 apr_size_t *len,
                                 apr_pool_t *pool);


/* A posix-like write function of type svn_write_fn_t (see svn_io.h).
   Given an already-open APR FILEHANDLE, write LEN bytes out of BUFFER.
   (Notice that FILEHANDLE is void *, to match svn_io_write_fn_t).
*/
svn_error_t *svn_io_file_writer (void *filehandle,
                                 const char *buffer,
                                 apr_size_t *len,
                                 apr_pool_t *pool);


/* Copy SRC to DST.  DST will be overwritten if it exists, else it
   will be created. */
svn_error_t *svn_io_copy_file (svn_string_t *src,
                               svn_string_t *dst,
                               apr_pool_t *pool);

/* Append SRC to DST.  DST will be appended to if it exists, else it
   will be created. */
svn_error_t *svn_io_append_file (svn_string_t *src,
                                 svn_string_t *dst,
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
                                        svn_string_t *path,
                                        apr_pool_t *pool);



#endif /* SVN_IO_H */
