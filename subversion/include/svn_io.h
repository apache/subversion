/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
 * @endcopyright
 *
 * @file svn_io.h
 * @brief general Subversion I/O definitions
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


/** Determine the @a kind of @a path.
 *
 * If utf8-encoded @a path exists, set @a *kind to the appropriate kind,
 * else set it to @c svn_node_unknown. 
 *
 * If @a path is a file, @a *kind is set to @c svn_node_file.
 *
 * If @a path is a directory, @a *kind is set to @c svn_node_dir.
 *
 * If @a path does not exist in its final component, @a *kind is set to
 * @c svn_node_none.  
 *
 * If intermediate directories on the way to @a path don't exist, an
 * error is returned, and @a *kind's value is undefined.
 */
svn_error_t *svn_io_check_path (const char *path,
                                svn_node_kind_t *kind,
                                apr_pool_t *pool);


/** Open a uniquely named file in a given directory.
 *
 * Open a new file (for writing) with a unique name based on utf-8
 * encoded @a path, in the same directory as @a path.  The file handle is
 * returned in @a *f, and the name, which ends with @a suffix, is returned
 * in @a *unique_name, also utf8-encoded.  If @a delete_on_close is set,
 * then the @c APR_DELONCLOSE flag will be used when opening the file.
 *
 * The name will include as much of @a path as possible, then a dot,
 * then a random portion, then another dot, then an iterated attempt
 * number (00001 for the first try, 00002 for the second, etc), and
 * end with @a suffix.  For example, if @a path is
 *
 *    tests/t1/A/D/G/pi
 *
 * then @c svn_io_open_unique_file(&f, &uniqe_name, @a path, ".tmp", pool) 
 * might pick
 *
 *    tests/t1/A/D/G/pi.3221223676.00001.tmp
 *
 * the first time, then
 *
 *    tests/t1/A/D/G/pi.3221223676.00002.tmp
 *
 * if called again while the first unique file still exists.
 *
 * It doesn't matter if @a path is a file or directory, the unique name will
 * be in @a path's parent either way.
 *
 * @a *unique_name will never be exactly the same as @a path, even if @a path 
 * does not exist.
 * 
 * @a *f and @a *unique_name are allocated in @a pool.
 *
 * If no unique name can be found, @c SVN_ERR_IO_UNIQUE_NAMES_EXHAUSTED is
 * the error returned.
 *
 * Claim of Historical Inevitability: this function was written
 * because 
 *
 *    - @c tmpnam() is not thread-safe.
 *    - @c tempname() tries standard system tmp areas first.
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


/** Copy @a src to @a dst atomically.
 *
 * Copy @a src to @a dst atomically.  Overwrite @a dst if it exists, else
 * create it.  Both @a src and @a dst are utf8-encoded filenames.  If
 * @a copy_perms is true, set @a dst's permissions to match those of @a src.
 */
svn_error_t *svn_io_copy_file (const char *src,
                               const char *dst,
                               svn_boolean_t copy_perms,
                               apr_pool_t *pool);


/** Recursively copy directory @a src into @a dst_parent.
 *
 * Recursively copy directory @a src into @a dst_parent, as a new entry named
 * @a dst_basename.  If @a dst_basename already exists in @a dst_parent, 
 * return error.  @a copy_perms will be passed through to @c svn_io_copy_file 
 * when any files are copied.  @a src, @a dst_parent, and @a dst_basename are 
 * all utf8-encoded.
 */ 
svn_error_t *svn_io_copy_dir_recursively (const char *src,
                                          const char *dst_parent,
                                          const char *dst_basename,
                                          svn_boolean_t copy_perms,
                                          apr_pool_t *pool);



/** Create directory @a path on the file system, along with any needed 
 * intermediate directories.
 *
 * Create directory @a path on the file system, creating intermediate
 * directories as required, like <tt>mkdir -p</tt>.  Report no error if @a 
 * path already exists.  @a path is utf8-encoded.
 *
 * This is essentially a wrapper for @c apr_dir_make_recursive(), passing
 * @c APR_OS_DEFAULT as the permissions.
 */
svn_error_t *svn_io_make_dir_recursively (const char *path, apr_pool_t *pool);


/** Check if @a path is empty.
 *
 * Set @a *is_empty_p to @c TRUE if directory @a path is empty, else to 
 * @c FALSE if it is not empty.  @a path must be a directory, and is
 * utf8-encoded.  Use @a pool for temporary allocation.
 */
svn_error_t *
svn_io_dir_empty (svn_boolean_t *is_empty_p,
                  const char *path,
                  apr_pool_t *pool);


/** Append @a src to @a dst.
 *
 * Append @a src to @a dst.  @a dst will be appended to if it exists, else it
 * will be created.  Both @a src and @a dst are utf8-encoded.
 */
svn_error_t *svn_io_append_file (const char *src,
                                 const char *dst,
                                 apr_pool_t *pool);


/** Make a file read-only if the operating system allows it.
 *
 * Make a file as read-only as the operating system allows.
 * @a path is the utf8-encoded path to the file. If @a ignore_enoent is
 * @c TRUE, don't fail if the target file doesn't exist.
 */
svn_error_t *svn_io_set_file_read_only (const char *path,
                                        svn_boolean_t ignore_enoent,
                                        apr_pool_t *pool);


/** Make a file read-write if the operating system allows it.
 *
 * Make a file as writable as the operating system allows.
 * @a path is the utf8-encoded path to the file.  If @a ignore_enoent is
 * @c TRUE, don't fail if the target file doesn't exist.
 */
svn_error_t *svn_io_set_file_read_write (const char *path,
                                         svn_boolean_t ignore_enoent,
                                         apr_pool_t *pool);

/** Toggle a file's "executability" if the operating system allows it.
 *
 * Toggle a file's "executability", as much as the operating system
 * allows.  @a path is the utf8-encoded path to the file.  If @a executable
 * is @c TRUE, then make the file executable.  If @c FALSE, make in
 * non-executable.  If @a ignore_enoent is @c TRUE, don't fail if the target
 * file doesn't exist.
 */
svn_error_t *svn_io_set_file_executable (const char *path,
                                         svn_boolean_t executable,
                                         svn_boolean_t ignore_enoent,
                                         apr_pool_t *pool);

/** Determine whether a file is executable by the current user.
 *
 * Determine whether a file is executable by the current user.  
 * Set @a *executable to @c TRUE if the file @a path is executable by the 
 * current user, otherwise set it to @c FALSE.  
 * 
 * On Windows and on platforms without userids, always returns @c FALSE.
 */
svn_error_t *svn_io_is_file_executable(svn_boolean_t *executable, 
                                       const char *path, 
                                       apr_pool_t *pool);


/** Read a line from @a file into @a buf up to @a *limit bytes long.
 *
 * Read a line from @a file into @a buf, but not exceeding @a *limit bytes.
 * Does not include newline, instead '\\0' is put there.
 * Length (as in strlen) is returned in @a *limit.
 * @a buf should be pre-allocated.
 * @a file should be already opened. 
 *
 * When the file is out of lines, @c APR_EOF will be returned.
 */
apr_status_t
svn_io_read_length_line (apr_file_t *file, char *buf, apr_size_t *limit);


/** Get the time of the last modification to the contents of the file at 
 * @a path.
 *
 * Set @a *apr_time to the time of last modification of the contents of the
 * file @a path.  @a path is utf8-encoded.
 *
 * Note: this is the APR mtime which corresponds to the traditional mtime
 * on Unix, and the last write time on Windows.
 */
svn_error_t *svn_io_file_affected_time (apr_time_t *apr_time,
                                        const char *path,
                                        apr_pool_t *pool);


/** Determine if two files have different sizes.
 *
 * Set @a *different_p to non-zero if @a file1 and @a file2 have different
 * sizes, else set to zero.  Both @a file1 and @a file2 are utf8-encoded.
 *
 * Setting @a *different_p to zero does not mean the files definitely
 * have the same size, it merely means that the sizes are not
 * definitely different.  That is, if the size of one or both files
 * cannot be determined, then the sizes are not known to be different,
 * so @a *different_p is set to 0.
 */
svn_error_t *svn_io_filesizes_different_p (svn_boolean_t *different_p,
                                           const char *file1,
                                           const char *file2,
                                           apr_pool_t *pool);


/** Get the md5 checksum of @a file.
 *
 * Store a base64-encoded md5 checksum of @a file's contents in
 * @a *checksum_p.  @a file is utf8-encoded.  Allocate @a checksum_p in 
 * @a pool, and use @a pool for any temporary allocation.
 */
svn_error_t *svn_io_file_checksum (svn_stringbuf_t **checksum_p,
                                   const char *file,
                                   apr_pool_t *pool);


/** Return a POSIX-like file descriptor from @a file.
 *
 * Return a POSIX-like file descriptor from @a file.
 *
 * We need this because on some platforms, notably Windows, apr_file_t
 * is not based on a file descriptor; but we have to pass an FD to neon.
 *
 * FIXME: This function will hopefully go away if/when neon gets
 *        replaced by apr-serf.
 */
apr_status_t svn_io_fd_from_file (int *fd_p, apr_file_t *file);



/** Generic byte-streams
 *
 * @defgroup svn_io_byte_streams generic byte streams
 * @{
 */

/** An abstract stream of bytes--either incoming or outgoing or both.
 *
 * An abstract stream of bytes--either incoming or outgoing or both.
 *
 * The creator of a stream sets functions to handle read and write.
 * Both of these handlers accept a baton whose value is determined at
 * stream creation time; this baton can point to a structure
 * containing data associated with the stream.  If a caller attempts
 * to invoke a handler which has not been set, it will generate a
 * runtime assertion failure.  The creator can also set a handler for
 * close requests so that it can flush buffered data or whatever;
 * if a close handler is not specified, a close request on the stream
 * will simply be ignored.  Note that svn_stream_close() does not
 * deallocate the memory used to allocate the stream structure; free
 * the pool you created the stream in to free that memory.
 *
 * The read and write handlers accept length arguments via pointer.
 * On entry to the handler, the pointed-to value should be the amount
 * of data which can be read or the amount of data to write.  When the
 * handler returns, the value is reset to the amount of data actually
 * read or written.  Handlers are obliged to complete a read or write
 * to the maximum extent possible; thus, a short read with no
 * associated error implies the end of the input stream, and a short
 * write should never occur without an associated error.
 */
typedef struct svn_stream_t svn_stream_t;



/** Read handler function for a generic stream.  */
typedef svn_error_t *(*svn_read_fn_t) (void *baton,
                                       char *buffer,
                                       apr_size_t *len);

/** Write handler function for a generic stream.  */
typedef svn_error_t *(*svn_write_fn_t) (void *baton,
                                        const char *data,
                                        apr_size_t *len);

/** Close handler function for a generic stream.  */
typedef svn_error_t *(*svn_close_fn_t) (void *baton);


/** Creating a generic stream.  */
svn_stream_t *svn_stream_create (void *baton, apr_pool_t *pool);

/** Duplicate a generic stream. */
svn_stream_t *svn_stream_dup (svn_stream_t *stream, apr_pool_t *pool);

/** Set @a stream's baton to @a baton */
void svn_stream_set_baton (svn_stream_t *stream, void *baton);

/** Set @a stream's read function to @a read_fn */
void svn_stream_set_read (svn_stream_t *stream, svn_read_fn_t read_fn);

/** Set @a stream's write function to @a write_fn */
void svn_stream_set_write (svn_stream_t *stream, svn_write_fn_t write_fn);

/** Set @a stream's close function to @a close_fn */
void svn_stream_set_close (svn_stream_t *stream, svn_close_fn_t close_fn);


/** Convenience function to create a generic stream which is empty.  */
svn_stream_t *svn_stream_empty (apr_pool_t *pool);


/** Convenience function for creating a stream which operates on an APR file.
 *
 * Convenience function for creating streams which operate on APR
 * files.  For convenience, if @a file is NULL then @c svn_stream_empty(pool) 
 * is returned.  Note that the stream returned by these operations is not 
 * considered to "own" the underlying file, meaning that @c svn_stream_close() 
 * on the stream will not close the file.
 */
svn_stream_t *svn_stream_from_aprfile (apr_file_t *file, apr_pool_t *pool);

/** Convenience function for creating a stream which operates on an stdio file.
 *
 * Convenience function for creating streams which operate on stdio
 * files.  For convenience, if @a fp is NULL then @c svn_stream_empty(pool) 
 * is returned.  Note that the stream returned by these operations is not 
 * considered to "own" the underlying file, meaning that @c svn_stream_close() 
 * on the stream will not close the file.
 */
svn_stream_t *svn_stream_from_stdio (FILE *fp, apr_pool_t *pool);

/** Return a generic stream connected to stringbuf @a str.
 *
 * Return a generic stream connected to stringbuf @a str.  Allocate the
 * stream in @a pool.
 */
svn_stream_t *svn_stream_from_stringbuf (svn_stringbuf_t *str,
                                         apr_pool_t *pool);

/** Read from a generic stream. */
svn_error_t *svn_stream_read (svn_stream_t *stream, char *buffer,
                              apr_size_t *len);

/** Write to a generic stream. */
svn_error_t *svn_stream_write (svn_stream_t *stream, const char *data,
                               apr_size_t *len);

/** Close a generic stream. */
svn_error_t *svn_stream_close (svn_stream_t *stream);


/** Write to @a stream using a printf-style @a fmt specifier, passed through
 * @c apr_psprintf using memory from @a pool.
 */
svn_error_t *svn_stream_printf (svn_stream_t *stream,
                                apr_pool_t *pool,
                                const char *fmt,
                                ...)
       __attribute__ ((format(printf, 3, 4)));

/** Read a line from a stream.
 *
 * Allocate @a *stringbuf in @a pool, and read one line from @a stream 
 * into it. The '\\n' is read from the stream, but is not added to the end 
 * of the stringbuf.  Instead, the stringbuf ends with a usual '\\0'.
 *
 * If @a stream runs out of bytes before encountering a '\\n', then set
 * @a *stringbuf to @c NULL and return no error.
 */
svn_error_t *
svn_stream_readline (svn_stream_t *stream,
                     svn_stringbuf_t **stringbuf,
                     apr_pool_t *pool);

/** @} */

/** Read a file into a stringbuf.
 *
 * Sets @a *result to a string containing the contents of @a filename, a
 * utf8-encoded path. 
 *
 * If @a filename is "-", return the error @c SVN_ERR_UNSUPPORTED_FEATURE
 * and don't touch @a *result.
 *
 * ### Someday, "-" will fill @a *result from stdin.  The problem right
 * now is that if the same command invokes the editor, stdin is crap,
 * and the editor acts funny or dies outright.  One solution is to
 * disallow stdin reading and invoking the editor, but how to do that
 * reliably?
 */
svn_error_t *svn_stringbuf_from_file (svn_stringbuf_t **result, 
                                      const char *filename, 
                                      apr_pool_t *pool);

/** Read the contents of an @c apr_file_t into a stringbuf.
 *
 * Sets @a *result to a string containing the contents of the already opened
 * @a file.  Reads from the current position in file to the end.  Does not
 * close the file or reset the cursor position.
 */
svn_error_t *svn_stringbuf_from_aprfile (svn_stringbuf_t **result,
                                         apr_file_t *file,
                                         apr_pool_t *pool);

/** Remove file @a path, a utf8-encoded path.
 *
 * Remove file @a path, a utf8-encoded path.  This wraps @c apr_file_remove(), 
 * converting any error to a Subversion error.
 */
svn_error_t *svn_io_remove_file (const char *path, apr_pool_t *pool);

/** Recursively remove directory @a path.
 *
 * Recursively remove directory @a path.  @a path is utf8-encoded.
 */
svn_error_t *svn_io_remove_dir (const char *path, apr_pool_t *pool);


/** Read the dirents for a directory into a hash.
 *
 * Read all of the disk entries in directory @a path, a utf8-encoded
 * path.  Return a @a dirents hash mapping dirent names (<tt>char *</tt>) to
 * enumerated dirent filetypes (@c svn_node_kind_t *).
 *
 * Note:  the `.' and `..' directories normally returned by
 * @c apr_dir_read are NOT returned in the hash.
 */
svn_error_t *svn_io_get_dirents (apr_hash_t **dirents,
                                 const char *path,
                                 apr_pool_t *pool);


/** Run a command.
 *
 * Invoke @a cmd with @a args, using utf8-encoded @a path as working directory.
 * Connect @a cmd's stdin, stdout, and stderr to @a infile, @a outfile, and
 * @a errfile, except where they are null.
 *
 * If set, @a exitcode will contain the exit code of the process upon return,
 * and @a exitwhy will indicate why the process terminated. If @a exitwhy is 
 * not set and the exit reason is not @c APR_PROC_CHECK_EXIT(), or if 
 * @a exitcode is not set and the exit code is non-zero, then an 
 * @c SVN_ERR_EXTERNAL_PROGRAM error will be returned.
 *
 * @a args is a list of utf8-encoded (<tt>const char *</tt>)'s, terminated by
 * @c NULL.  @c ARGS[0] is the name of the program, though it need not be
 * the same as @a cmd.
 *
 * @a inherit sets whether the invoked program shall inherit its environment or
 * run "clean".
 */
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

/** Run diff.
 *
 * Invoke @c SVN_CLIENT_DIFF, with @a user_args (an array of utf8-encoded
 * @a num_user_args arguments), if they are specified, or "-u" if they
 * are not.
 *
 * Diff runs in utf8-encoded @a dir, and its exit status is stored in
 * @a exitcode, if it is not @c NULL.  
 *
 * If @a label1 and/or @a label2 are not null they will be passed to the diff
 * process as the arguments of "-L" options.  @a label1 and @a label2 are also 
 * in utf8, and will be converted to native charset along with the other args.
 *
 * @a from is the first file passed to diff, and @a to is the second.  The
 * stdout of diff will be sent to @a outfile, and the stderr to @a errfile.
 *
 * Do all allocation in @a pool.
 */
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


/** Run diff3
 *
 * Invoke @c SVN_CLIENT_DIFF3 in utf8-encoded @a dir like this:
 *
 *          diff3 -Em @a mine @a older @a yours > @a merged
 *
 * (See the diff3 documentation for details.)
 *
 * @a mine, @a older, and @a yours are utf8-encoded paths, relative to @a dir, 
 * to three files that already exist.  @a merged is an open file handle, and
 * is left open after the merge result is written to it. (@a merged
 * should *not* be the same file as @a mine, or nondeterministic things
 * may happen!)
 *
 * @a mine_label, @a older_label, @a yours_label are utf8-encoded label
 * parameters for diff3's -L option.  Any of them may be @c NULL, in
 * which case the corresponding @a mine, @a older, or @a yours parameter is
 * used instead.
 *
 * Set @a *exitcode to diff3's exit status.  If @a *exitcode is anything
 * other than 0 or 1, then return @c SVN_ERR_EXTERNAL_PROGRAM.  (Note the
 * following from the diff3 info pages: "An exit status of 0 means
 * `diff3' was successful, 1 means some conflicts were found, and 2
 * means trouble.")
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


/** Find the mime type of a file.
 *
 * Examine utf8-encoded @a file to determine if it can be described by a
 * known (as in, known by this function) Multipurpose Internet Mail
 * Extension (MIME) type.  If so, set @a mimetype to a character string
 * describing the MIME type, else set it to @c NULL.  Use @a pool for any
 * necessary allocations.
 */
svn_error_t *svn_io_detect_mimetype (const char **mimetype,
                                     const char *file,
                                     apr_pool_t *pool);
                                      

/** Wrapper for @c apr_file_open().
 *
 * Wrapper for @c apr_file_open(), which see.  @a fname is utf8-encoded.
 */
svn_error_t *
svn_io_file_open (apr_file_t **new_file, const char *fname,
                  apr_int32_t flag, apr_fileperms_t perm,
                  apr_pool_t *pool);


/* Wrapper for @c apr_stat().
 *
 * Wrapper for @c apr_stat(), which see.  @a fname is utf8-encoded.
 */
svn_error_t *
svn_io_stat (apr_finfo_t *finfo, const char *fname,
             apr_int32_t wanted, apr_pool_t *pool);


/** Wrapper for @c apr_file_rename().
 *
 * Wrapper for @c apr_file_rename(), which see.  @a from_path and @a to_path
 * are utf8-encoded.
 */
svn_error_t *
svn_io_file_rename (const char *from_path, const char *to_path,
                    apr_pool_t *pool);


/** Wrapper for @c apr_dir_make().
 *
 * Wrapper for @c apr_dir_make(), which see.  @a path is utf8-encoded.
 */
svn_error_t *
svn_io_dir_make (const char *path, apr_fileperms_t perm, apr_pool_t *pool);


/** Wrapper for @c apr_dir_open().
 *
 * Wrapper for @c apr_dir_open(), which see.  @a dirname is utf8-encoded.
 */
svn_error_t *
svn_io_dir_open (apr_dir_t **new_dir, const char *dirname, apr_pool_t *pool);


/** Wrapper for @c apr_dir_remove(), which see.
 *
 * Wrapper for @c apr_dir_remove(), which see.  @a dirname is utf8-encoded.
 * Note: this function has this name to avoid confusion with
 * @c svn_io_remove_dir, which is recursive.
 */
svn_error_t *
svn_io_dir_remove_nonrecursive (const char *dirname, apr_pool_t *pool);


/** Wrapper for @c apr_dir_read, which see.
 *
 * Wrapper for @c apr_dir_read, which see.  Ensures that @a finfo->name is
 * utf8-encoded, which means allocating @a finfo->name in @a pool, which may
 * or may not be the same as @a finfo's pool.
 * Use @a pool for error allocation as well.
 */
svn_error_t *
svn_io_dir_read (apr_finfo_t *finfo,
                 apr_int32_t wanted,
                 apr_dir_t *thedir,
                 apr_pool_t *pool);


/** Wrapper for @c apr_file_printf(), which see.
 *
 * Wrapper for @c apr_file_printf(), which see.  @a format is a utf8-encoded
 * string after it is formatted, so this function can convert it to
 * native encoding before printing.
 */
svn_error_t *
svn_io_file_printf (apr_file_t *fptr, const char *format, ...);



/** Version/format files. 
 *
 * @defgroup svn_io_format_files version/format files
 * @{
 */

/** Get the version number from it's file.
 *
 * Set @a *version to the integer that starts the file at @a path.  If the
 * file does not begin with a series of digits followed by a newline,
 * return the error @c SVN_ERR_BAD_VERSION_FILE_FORMAT.  Use @a pool for
 * all allocations.
 */
svn_error_t *
svn_io_read_version_file (int *version, const char *path, apr_pool_t *pool);

/** Write a version number file.
 *
 * Create (or overwrite) the file at @a path with new contents,
 * formatted as a non-negative integer @a version followed by a single
 * newline.  Use @a pool for all allocations.
 */
svn_error_t *
svn_io_write_version_file (const char *path, int version, apr_pool_t *pool);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_IO_H */
