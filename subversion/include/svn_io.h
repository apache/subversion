/*
 * svn_io.h :  general Subversion I/O definitions
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

/* ==================================================================== */


#ifndef SVN_IO_H
#define SVN_IO_H

#include <apr.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_thread_proc.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_string.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* ### NOTE: Although all svn_io functions claim to conversion from/to
   UTF-8, in fact they only do this if Subversion was configured with
   the `--enable-utf8' flag.  We expect (?) this flag to go away
   soon, because UTF-8 can't really be optional if it's to be
   depended on for interoperability.  The flag is only there so that
   development of the UTF-8 code doesn't affect others until said
   code is ready for prime time.  */


/* If utf8-encoded PATH exists, set *KIND to the appropriate kind,
 * else set it to svn_node_unknown. 
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
svn_error_t *svn_io_check_path (const char *path,
                                enum svn_node_kind *kind,
                                apr_pool_t *pool);


/* Open a new file (for writing) with a unique name based on utf-8
 * encoded PATH, in the same directory as PATH.  The file handle is
 * returned in *F, and the name, which ends with SUFFIX, is returned
 * in *UNIQUE_NAME, also utf8-encoded.  If DELETE_ON_CLOSE is set,
 * then the APR_DELONCLOSE flag will be used when opening the file.
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
                                      const char **unique_name,
                                      const char *path,
                                      const char *suffix,
                                      svn_boolean_t delete_on_close,
                                      apr_pool_t *pool);


/* Copy SRC to DST atomically.  Overwrite DST if it exists, else
   create it.  Both SRC and DST are utf8-encoded filenames.  If
   COPY_PERMS is true, set DST's permissions to match those of SRC. */
svn_error_t *svn_io_copy_file (const char *src,
                               const char *dst,
                               svn_boolean_t copy_perms,
                               apr_pool_t *pool);


/* Recursively copy directory SRC into DST_PARENT, as a new entry named
   DST_BASENAME.  If DST_BASENAME already exists in DST_PARENT, return
   error.  COPY_PERMS will be passed through to svn_io_copy_file when
   any files are copied.  SRC, DST_PARENT, and DST_BASENAME are all
   utf8-encoded. */ 
svn_error_t *svn_io_copy_dir_recursively (const char *src,
                                          const char *dst_parent,
                                          const char *dst_basename,
                                          svn_boolean_t copy_perms,
                                          apr_pool_t *pool);



/* Create directory PATH on the file system, creating intermediate
 * directories as required, like 'mkdir -p'.  Report no error if PATH
 * already exists.  PATH is utf8-encoded.
 *
 * This is essentially a wrapper for apr_dir_make_recursive(), passing
 * APR_OS_DEFAULT as the permissions.
 */
svn_error_t *svn_io_make_dir_recursively (const char *path, apr_pool_t *pool);


/* Set *IS_EMPTY_P to true if directory PATH is empty, else to false
 * if it is not empty.  PATH must be a directory, and is
 * utf8-encoded.  Use POOL for temporary allocation.
 */
svn_error_t *
svn_io_dir_empty (svn_boolean_t *is_empty_p,
                  const char *path,
                  apr_pool_t *pool);


/* Append SRC to DST.  DST will be appended to if it exists, else it
   will be created.  Both SRC and DST are utf8-encoded. */
svn_error_t *svn_io_append_file (const char *src,
                                 const char *dst,
                                 apr_pool_t *pool);


/* Make a file as read-only as the operating system allows.
   PATH is the utf8-encoded path to the file. If IGNORE_ENOENT is
   TRUE, don't fail if the target file doesn't exist. */
svn_error_t *svn_io_set_file_read_only (const char *path,
                                        svn_boolean_t ignore_enoent,
                                        apr_pool_t *pool);


/* Make a file as writable as the operating system allows.
   PATH is the utf8-encoded path to the file.  If IGNORE_ENOENT is
   TRUE, don't fail if the target file doesn't exist. */
svn_error_t *svn_io_set_file_read_write (const char *path,
                                         svn_boolean_t ignore_enoent,
                                         apr_pool_t *pool);

/* Toggle a file's "executability", as much as the operating system
   allows.  PATH is the utf8-encoded path to the file.  If EXECUTABLE
   is TRUE, then make the file executable.  If FALSE, make in
   non-executable.  If IGNORE_ENOENT is TRUE, don't fail if the target
   file doesn't exist.  */
svn_error_t *svn_io_set_file_executable (const char *path,
                                         svn_boolean_t executable,
                                         svn_boolean_t ignore_enoent,
                                         apr_pool_t *pool);

/* Determine whether a file is executable by the current user.  
 * Set *EXECUTABLE to TRUE if the file PATH is executable by the 
 * current user, otherwise set it to FALSE.  
 * 
 * On Windows and on platforms without userids, always returns FALSE.
 */
svn_error_t *svn_io_is_file_executable(svn_boolean_t *executable, 
                                       const char *path, 
                                       apr_pool_t *pool);


/* Read a line from FILE into BUF, but not exceeding *LIMIT bytes.
 * Does not include newline, instead '\0' is put there.
 * Length (as in strlen) is returned in *LIMIT.
 * BUF should be pre-allocated.
 * FILE should be already opened. 
 *
 * When the file is out of lines, APR_EOF will be returned.
 */
apr_status_t
svn_io_read_length_line (apr_file_t *file, char *buf, apr_size_t *limit);


/* Set *APR_TIME to the later of PATH's (a regular file) mtime or ctime.
 * PATH is utf8-encoded.
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
                                        const char *path,
                                        apr_pool_t *pool);


/* Set *DIFFERENT_P to non-zero if FILE1 and FILE2 have different
 * sizes, else set to zero.  Both FILE1 and FILE2 are utf8-encoded.
 *
 * Setting *DIFFERENT_P to zero does not mean the files definitely
 * have the same size, it merely means that the sizes are not
 * definitely different.  That is, if the size of one or both files
 * cannot be determined, then the sizes are not known to be different,
 * so *DIFFERENT_P is set to 0.
 */
svn_error_t *svn_io_filesizes_different_p (svn_boolean_t *different_p,
                                           const char *file1,
                                           const char *file2,
                                           apr_pool_t *pool);


/* Store a base64-encoded MD5 checksum of FILE's contents in
   *CHECKSUM_P.  FILE is utf8-encoded.  Allocate CHECKSUM_P in POOL,
   *and use POOL for any temporary allocation. */
svn_error_t *svn_io_file_checksum (svn_stringbuf_t **checksum_p,
                                   const char *file,
                                   apr_pool_t *pool);


/* Return a POSIX-like file descriptor from FILE.

   We need this because on some platforms, notably Windows, apr_file_t
   is not based on a file descriptor; but we have to pass an FD to neon.

   FIXME: This function will hopefully go away if/when neon gets
          replaced by apr-serf. */
apr_status_t svn_io_fd_from_file (int *fd_p, apr_file_t *file);



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

svn_stream_t *svn_stream_dup (svn_stream_t *stream, apr_pool_t *pool);

void svn_stream_set_baton (svn_stream_t *stream, void *baton);

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


/* Write to STREAM using a printf-style FMT specifier, passed through
   apr_psprintf using memory from POOL.   */
svn_error_t *svn_stream_printf (svn_stream_t *stream,
                                apr_pool_t *pool,
                                const char *fmt,
                                ...)
       __attribute__ ((format(printf, 3, 4)));

/* Allocate *STRINGBUF in POOL, and read one line from STREAM into it.
   The '\n' is read from the stream, but is not added to the end of
   the stringbuf.  Instead, the stringbuf ends with a usual \0.

   If STREAM runs out of bytes before encountering a '\n', then set
   *STRINGBUF to NULL and return no error.
*/
svn_error_t *
svn_stream_readline (svn_stream_t *stream,
                     svn_stringbuf_t **stringbuf,
                     apr_pool_t *pool);

/* Sets *RESULT to a string containing the contents of FILENAME, a
   utf8-encoded path. 

   If FILENAME is "-", return the error SVN_ERR_UNSUPPORTED_FEATURE
   and don't touch *RESULT.

   ### Someday, "-" will fill *RESULT from stdin.  The problem right
   now is that if the same command invokes the editor, stdin is crap,
   and the editor acts funny or dies outright.  One solution is to
   disallow stdin reading and invoking the editor, but how to do that
   reliably?
*/
svn_error_t *svn_stringbuf_from_file (svn_stringbuf_t **result, 
                                      const char *filename, 
                                      apr_pool_t *pool);

/* Sets *RESULT to a string containing the contents of the already opened
   FILE.  Reads from the current position in file to the end.  Does not
   close the file or reset the cursor position.*/
svn_error_t *svn_stringbuf_from_aprfile (svn_stringbuf_t **result,
                                         apr_file_t *file,
                                         apr_pool_t *pool);

/* Remove file PATH, a utf8-encoded path.  This wraps
   apr_file_remove(), converting any error to a Subversion error. */
svn_error_t *svn_io_remove_file (const char *path, apr_pool_t *pool);

/* Recursively remove directory PATH.  PATH is utf8-encoded. */
svn_error_t *svn_io_remove_dir (const char *path, apr_pool_t *pool);


/* Read all of the disk entries in directory PATH, a utf8-encoded
   path.  Return a DIRENTS hash mapping dirent names (char *) to
   enumerated dirent filetypes (enum svn_node_kind *).

   Note:  the `.' and `..' directories normally returned by
   apr_dir_read are NOT returned in the hash. */
svn_error_t *svn_io_get_dirents (apr_hash_t **dirents,
                                 const char *path,
                                 apr_pool_t *pool);


/* Invoke CMD with ARGS, using utf8-encoded PATH as working directory.
   Connect CMD's stdin, stdout, and stderr to INFILE, OUTFILE, and
   ERRFILE, except where they are null.

   If set, EXITCODE will contain the exit code of the process upon return,
   and EXITWHY will indicate why the process terminated. If EXITWHY is not
   set and the exit reason is not APR_PROC_CHECK_EXIT(), or if EXITCODE is
   not set and the exit code is non-zero, then an SVN_ERR_EXTERNAL_PROGRAM
   error will be returned.

   ARGS is a list of utf8-encoded (const char *)'s, terminated by
   NULL.  ARGS[0] is the name of the program, though it need not be
   the same as CMD.

   INHERIT sets whether the invoked program shall inherit its environment or
   run "clean". */
svn_error_t *svn_io_run_cmd (const char *path,
                             const char *cmd,
                             const char *const *args,
                             int *exitcode,
                             apr_exit_why_e *exitwhy,
                             svn_boolean_t inherit,
                             apr_file_t *infile,
                             apr_file_t *outfile,
                             apr_file_t *errfile,
                             apr_pool_t *pool);

/* Invoke SVN_CLIENT_DIFF, with USER_ARGS (an array of utf8-encoded
   NUM_USER_ARGS arguments), if they are specified, or "-u" if they
   are not.

   Diff runs in utf8-encoded DIR, and its exit status is stored in
   EXITCODE, if it is not NULL.  

   If LABEL1 and/or LABEL2 are not null they will be passed to the diff
   process as the arguments of "-L" options.  LABEL1 and LABEL2 are also in
   utf8, and will be converted to native charset along with the other args.

   FROM is the first file passed to diff, and TO is the second.  The
   stdout of diff will be sent to OUTFILE, and the stderr to ERRFILE.

   Do all allocation in POOL.  */
svn_error_t *svn_io_run_diff (const char *dir,
                              const char *const *user_args,
                              const int num_user_args,
                              const char *label1,
                              const char *label2,
                              const char *from,
                              const char *to,
                              int *exitcode,
                              apr_file_t *outfile,
                              apr_file_t *errfile,
                              apr_pool_t *pool);


/*  Invoke SVN_CLIENT_DIFF3 in utf8-encoded DIR like this:

            diff3 -Em MINE OLDER YOURS > MERGED

   (See the diff3 documentation for details.)

   MINE, OLDER, and YOURS are utf8-encoded paths, relative to DIR, to
   three files that already exist.  MERGED is an open file handle, and
   is left open after the merge result is written to it. (MERGED
   should *not* be the same file as MINE, or nondeterministic things
   may happen!)

   MINE_LABEL, OLDER_LABEL, YOURS_LABEL are utf8-encoded label
   parameters for diff3's -L option.  Any of them may be NULL, in
   which case the corresponding MINE, OLDER, or YOURS parameter is
   used instead.

   Set *EXITCODE to diff3's exit status.  If *EXITCODE is anything
   other than 0 or 1, then return SVN_ERR_EXTERNAL_PROGRAM.  (Note the
   following from the diff3 info pages: "An exit status of 0 means
   `diff3' was successful, 1 means some conflicts were found, and 2
   means trouble.")
*/
svn_error_t *svn_io_run_diff3 (const char *dir,
                               const char *mine,
                               const char *older,
                               const char *yours,
                               const char *mine_label,
                               const char *older_label,
                               const char *yours_label,
                               apr_file_t *merged,
                               int *exitcode,
                               apr_pool_t *pool);


/* Examine utf8-encoded FILE to determine if it can be described by a
   known (as in, known by this function) Multipurpose Internet Mail
   Extension (MIME) type.  If so, set MIMETYPE to a character string
   describing the MIME type, else set it to NULL.  Use POOL for any
   necessary allocations.  */
svn_error_t *svn_io_detect_mimetype (const char **mimetype,
                                     const char *file,
                                     apr_pool_t *pool);
                                      

/* Wrapper for apr_file_open(), which see.  FNAME is utf8-encoded. */
svn_error_t *
svn_io_file_open (apr_file_t **new_file, const char *fname,
                  apr_int32_t flag, apr_fileperms_t perm,
                  apr_pool_t *pool);


/* Wrapper for apr_stat(), which see.  FNAME is utf8-encoded. */
svn_error_t *
svn_io_stat (apr_finfo_t *finfo, const char *fname,
             apr_int32_t wanted, apr_pool_t *pool);


/* Wrapper for apr_file_rename(), which see.  FROM_PATH and TO_PATH
   are utf8-encoded. */
svn_error_t *
svn_io_file_rename (const char *from_path, const char *to_path,
                    apr_pool_t *pool);


/* Wrapper for apr_dir_make(), which see.  PATH is utf8-encoded. */
svn_error_t *
svn_io_dir_make (const char *path, apr_fileperms_t perm, apr_pool_t *pool);


/* Wrapper for apr_dir_open(), which see.  DIRNAME is utf8-encoded. */
svn_error_t *
svn_io_dir_open (apr_dir_t **new_dir, const char *dirname, apr_pool_t *pool);


/* Wrapper for apr_dir_remove(), which see.  DIRNAME is utf8-encoded.
   Note: this function has this name to avoid confusion with
   svn_io_remove_dir, which is recursive. */
svn_error_t *
svn_io_dir_remove_nonrecursive (const char *dirname, apr_pool_t *pool);


/* Wrapper for apr_dir_read, which see.  Ensures that FINFO->name is
   utf8-encoded, which means allocating FINFO->name in POOL, which may
   or may not be the same as FINFO's pool.
   Use POOL for error allocation as well. */
svn_error_t *
svn_io_dir_read (apr_finfo_t *finfo,
                 apr_int32_t wanted,
                 apr_dir_t *thedir,
                 apr_pool_t *pool);


/* Wrapper for apr_file_printf(), which see.  FORMAT is a utf8-encoded
   string after it is formatted, so this function can convert it to
   native encoding before printing. */
svn_error_t *
svn_io_file_printf (apr_file_t *fptr, const char *format, ...);



/*** Version/format files. ***/

/* Set *VERSION to the integer that starts the file at PATH.  If the
   file does not begin with a series of digits followed by a newline,
   return the error SVN_ERR_BAD_VERSION_FILE_FORMAT.  Use POOL for
   all allocations. */
svn_error_t *
svn_io_read_version_file (int *version, const char *path, apr_pool_t *pool);

/* Create (or overwrite) the file at PATH with new contents,
   formatted as a non-negative integer VERSION followed by a single
   newline.  Use POOL for all allocations. */
svn_error_t *
svn_io_write_version_file (const char *path, int version, apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_IO_H */

/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
