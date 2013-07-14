/* util.h --- utility functions for FSFS repo access
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#ifndef SVN_LIBSVN_FS__UTIL_H
#define SVN_LIBSVN_FS__UTIL_H

#include "svn_fs.h"
#include "id.h"

/* Functions for dealing with recoverable errors on mutable files
 *
 * Revprops, current, and txn-current files are mutable; that is, they
 * change as part of normal fsfs operation, in constrat to revs files, or
 * the format file, which are written once at create (or upgrade) time.
 * When more than one host writes to the same repository, we will
 * sometimes see these recoverable errors when accesssing these files.
 *
 * These errors all relate to NFS, and thus we only use this retry code if
 * ESTALE is defined.
 *
 ** ESTALE
 *
 * In NFS v3 and under, the server doesn't track opened files.  If you
 * unlink(2) or rename(2) a file held open by another process *on the
 * same host*, that host's kernel typically renames the file to
 * .nfsXXXX and automatically deletes that when it's no longer open,
 * but this behavior is not required.
 *
 * For obvious reasons, this does not work *across hosts*.  No one
 * knows about the opened file; not the server, and not the deleting
 * client.  So the file vanishes, and the reader gets stale NFS file
 * handle.
 *
 ** EIO, ENOENT
 *
 * Some client implementations (at least the 2.6.18.5 kernel that ships
 * with Ubuntu Dapper) sometimes give spurious ENOENT (only on open) or
 * even EIO errors when trying to read these files that have been renamed
 * over on some other host.
 *
 ** Solution
 *
 * Try open and read of such files in try_stringbuf_from_file().  Call
 * this function within a loop of SVN_FS_FS__RECOVERABLE_RETRY_COUNT
 * iterations (though, realistically, the second try will succeed).
 */

#define SVN_FS_FS__RECOVERABLE_RETRY_COUNT 10

/* Return TRUE is REV is packed in FS, FALSE otherwise. */
svn_boolean_t
svn_fs_fs__is_packed_rev(svn_fs_t *fs,
                         svn_revnum_t rev);

/* Return TRUE is REV's props have been packed in FS, FALSE otherwise. */
svn_boolean_t
svn_fs_fs__is_packed_revprop(svn_fs_t *fs,
                             svn_revnum_t rev);

/* Return the full path of the rev shard directory that will contain
 * revision REV in FS.  Allocate the result in POOL.
 */
const char *
svn_fs_fs__path_rev_shard(svn_fs_t *fs,
                          svn_revnum_t rev,
                          apr_pool_t *pool);

/* Return the full path of the non-packed rev file containing revision REV
 * in FS.  Allocate the result in POOL.
 */
const char *
svn_fs_fs__path_rev(svn_fs_t *fs,
                    svn_revnum_t rev,
                    apr_pool_t *pool);

/* Return the path of the pack-related file that for revision REV in FS.
 * KIND specifies the file name base, e.g. "manifest" or "pack".
 * The result will be allocated in POOL.
 */
const char *
svn_fs_fs__path_rev_packed(svn_fs_t *fs,
                           svn_revnum_t rev,
                           const char *kind,
                           apr_pool_t *pool);

/* Return the full path of the revprop generation file in FS.
 * Allocate the result in POOL.
 */
const char *
svn_fs_fs__path_revprop_generation(svn_fs_t *fs,
                                   apr_pool_t *pool);

/* Return the full path of the revision properties pack shard directory
 * that will contain the packed properties of revision REV in FS.
 * Allocate the result in POOL.
 */
const char *
svn_fs_fs__path_revprops_pack_shard(svn_fs_t *fs,
                                    svn_revnum_t rev,
                                    apr_pool_t *pool);

/* Set *PATH to the path of REV in FS, whether in a pack file or not.
   Allocate *PATH in POOL.

   Note: If the caller does not have the write lock on FS, then the path is
   not guaranteed to be correct or to remain correct after the function
   returns, because the revision might become packed before or after this
   call.  If a file exists at that path, then it is correct; if not, then
   the caller should call update_min_unpacked_rev() and re-try once. */
const char *
svn_fs_fs__path_rev_absolute(svn_fs_t *fs,
                             svn_revnum_t rev,
                             apr_pool_t *pool);

/* Return the full path of the revision properties shard directory that
 * will contain the properties of revision REV in FS.
 * Allocate the result in POOL.
 */
const char *
svn_fs_fs__path_revprops_shard(svn_fs_t *fs,
                               svn_revnum_t rev,
                               apr_pool_t *pool);

/* Return the full path of the non-packed revision properties file that
 * contains the props for revision REV in FS.  Allocate the result in POOL.
 */
const char *
svn_fs_fs__path_revprops(svn_fs_t *fs,
                         svn_revnum_t rev,
                         apr_pool_t *pool);

/* Return the path of the file storing the oldest non-packed revision in FS.
 * The result will be allocated in POOL.
 */
const char *
svn_fs_fs__path_min_unpacked_rev(svn_fs_t *fs,
                                 apr_pool_t *pool);

/* Set *MIN_UNPACKED_REV to the integer value read from the file returned
 * by #svn_fs_fs__path_min_unpacked_rev() for FS.
 * Use POOL for temporary allocations.
 */
svn_error_t *
svn_fs_fs__read_min_unpacked_rev(svn_revnum_t *min_unpacked_rev,
                                 svn_fs_t *fs,
                                 apr_pool_t *pool);

/* Check that BUF, a nul-terminated buffer of text from file PATH,
   contains only digits at OFFSET and beyond, raising an error if not.
   TITLE contains a user-visible description of the file, usually the
   short file name.

   Uses POOL for temporary allocation. */
svn_error_t *
svn_fs_fs__check_file_buffer_numeric(const char *buf,
                                     apr_off_t offset,
                                     const char *path,
                                     const char *title,
                                     apr_pool_t *pool);

/* Re-read the MIN_UNPACKED_REV member of FS from disk.
 * Use POOL for temporary allocations.
 */
svn_error_t *
svn_fs_fs__update_min_unpacked_rev(svn_fs_t *fs,
                                   apr_pool_t *pool);

/* Write a file FILENAME in directory FS_PATH, containing a single line
 * with the number REVNUM in ASCII decimal.  Move the file into place
 * atomically, overwriting any existing file.
 *
 * Similar to write_current(). */
svn_error_t *
svn_fs_fs__write_revnum_file(svn_fs_t *fs,
                             svn_revnum_t revnum,
                             apr_pool_t *scratch_pool);

/* Read the file at PATH and return its content in *CONTENT. *CONTENT will
 * not be modified unless the whole file was read successfully.
 *
 * ESTALE, EIO and ENOENT will not cause this function to return an error
 * unless LAST_ATTEMPT has been set.  If MISSING is not NULL, indicate
 * missing files (ENOENT) there.
 *
 * Use POOL for allocations.
 */
svn_error_t *
svn_fs_fs__try_stringbuf_from_file(svn_stringbuf_t **content,
                                   svn_boolean_t *missing,
                                   const char *path,
                                   svn_boolean_t last_attempt,
                                   apr_pool_t *pool);

/* Read the file FNAME and store the contents in *BUF.
   Allocations are performed in POOL. */
svn_error_t *
svn_fs_fs__read_content(svn_stringbuf_t **content,
                        const char *fname,
                        apr_pool_t *pool);

/* Reads a line from STREAM and converts it to a 64 bit integer to be
 * returned in *RESULT.  If we encounter eof, set *HIT_EOF and leave
 * *RESULT unchanged.  If HIT_EOF is NULL, EOF causes an "corrupt FS"
 * error return.
 * SCRATCH_POOL is used for temporary allocations.
 */
svn_error_t *
svn_fs_fs__read_number_from_stream(apr_int64_t *result,
                                   svn_boolean_t *hit_eof,
                                   svn_stream_t *stream,
                                   apr_pool_t *scratch_pool);

/* Move a file into place from OLD_FILENAME in the transactions
   directory to its final location NEW_FILENAME in the repository.  On
   Unix, match the permissions of the new file to the permissions of
   PERMS_REFERENCE.  Temporary allocations are from POOL.

   This function almost duplicates svn_io_file_move(), but it tries to
   guarantee a flush. */
svn_error_t *
svn_fs_fs__move_into_place(const char *old_filename,
                           const char *new_filename,
                           const char *perms_reference,
                           apr_pool_t *pool);

#endif