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
 * this function within a loop of RECOVERABLE_RETRY_COUNT iterations
 * (though, realistically, the second try will succeed).
 */

#define RECOVERABLE_RETRY_COUNT 10

/* Pathname helper functions */

/* Return TRUE is REV is packed in FS, FALSE otherwise. */
svn_boolean_t
is_packed_rev(svn_fs_t *fs,
              svn_revnum_t rev);

/* Return TRUE is REV is packed in FS, FALSE otherwise. */
svn_boolean_t
is_packed_revprop(svn_fs_t *fs,
                  svn_revnum_t rev);

const char *
path_format(svn_fs_t *fs,
            apr_pool_t *pool);

const char *
path_uuid(svn_fs_t *fs,
          apr_pool_t *pool);

const char *
path_txn_current(svn_fs_t *fs,
                 apr_pool_t *pool);

const char *
path_txn_current_lock(svn_fs_t *fs,
                      apr_pool_t *pool);

const char *
path_lock(svn_fs_t *fs,
          apr_pool_t *pool);

const char *
path_revprop_generation(svn_fs_t *fs,
                        apr_pool_t *pool);

const char *
path_rev_packed(svn_fs_t *fs,
                svn_revnum_t rev,
                const char *kind,
                apr_pool_t *pool);

const char *
path_rev_shard(svn_fs_t *fs,
               svn_revnum_t rev,
               apr_pool_t *pool);

const char *
path_rev(svn_fs_t *fs,
         svn_revnum_t rev,
         apr_pool_t *pool);

const char *
path_revprops_shard(svn_fs_t *fs,
                    svn_revnum_t rev,
                    apr_pool_t *pool);

const char *
path_revprops_pack_shard(svn_fs_t *fs,
                         svn_revnum_t rev,
                         apr_pool_t *pool);

const char *
path_revprops(svn_fs_t *fs,
              svn_revnum_t rev,
              apr_pool_t *pool);

const char *
path_txn_dir(svn_fs_t *fs,
             const char *txn_id,
             apr_pool_t *pool);

/* Return the name of the sha1->rep mapping file in transaction TXN_ID
 * within FS for the given SHA1 checksum.  Use POOL for allocations.
 */
const char *
path_txn_sha1(svn_fs_t *fs,
              const char *txn_id,
              svn_checksum_t *sha1,
              apr_pool_t *pool);

const char *
path_txn_changes(svn_fs_t *fs,
                 const char *txn_id,
                 apr_pool_t *pool);

const char *
path_txn_props(svn_fs_t *fs,
               const char *txn_id,
               apr_pool_t *pool);

const char *
path_txn_next_ids(svn_fs_t *fs,
                  const char *txn_id,
                  apr_pool_t *pool);

const char *
path_min_unpacked_rev(svn_fs_t *fs,
                      apr_pool_t *pool);


const char *
path_txn_proto_rev(svn_fs_t *fs,
                   const char *txn_id,
                   apr_pool_t *pool);

const char *
path_txn_proto_rev_lock(svn_fs_t *fs,
                        const char *txn_id,
                        apr_pool_t *pool);

const char *
path_txn_node_rev(svn_fs_t *fs,
                  const svn_fs_id_t *id,
                  apr_pool_t *pool);

const char *
path_txn_node_props(svn_fs_t *fs,
                    const svn_fs_id_t *id,
                    apr_pool_t *pool);

const char *
path_txn_node_children(svn_fs_t *fs,
                       const svn_fs_id_t *id,
                       apr_pool_t *pool);

const char *
path_node_origin(svn_fs_t *fs,
                 const char *node_id,
                 apr_pool_t *pool);

/* Check that BUF, a nul-terminated buffer of text from file PATH,
   contains only digits at OFFSET and beyond, raising an error if not.
   TITLE contains a user-visible description of the file, usually the
   short file name.

   Uses POOL for temporary allocation. */
svn_error_t *
check_file_buffer_numeric(const char *buf,
                          apr_off_t offset,
                          const char *path,
                          const char *title,
                          apr_pool_t *pool);

svn_error_t *
read_min_unpacked_rev(svn_revnum_t *min_unpacked_rev,
                      svn_fs_t *fs,
                      apr_pool_t *pool);

svn_error_t *
update_min_unpacked_rev(svn_fs_t *fs,
                        apr_pool_t *pool);

/* Write a file FILENAME in directory FS_PATH, containing a single line
 * with the number REVNUM in ASCII decimal.  Move the file into place
 * atomically, overwriting any existing file.
 *
 * Similar to write_current(). */
svn_error_t *
write_revnum_file(svn_fs_t *fs,
                  svn_revnum_t revnum,
                  apr_pool_t *scratch_pool);

/* Atomically update the 'current' file to hold the specifed REV,
   NEXT_NODE_ID, and NEXT_COPY_ID.  (The two next-ID parameters are
   ignored and may be NULL if the FS format does not use them.)
   Perform temporary allocations in POOL. */
svn_error_t *
write_current(svn_fs_t *fs,
              svn_revnum_t rev,
              const char *next_node_id,
              const char *next_copy_id,
              apr_pool_t *pool);

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
try_stringbuf_from_file(svn_stringbuf_t **content,
                        svn_boolean_t *missing,
                        const char *path,
                        svn_boolean_t last_attempt,
                        apr_pool_t *pool);

/* Fetch the current offset of FILE into *OFFSET_P. */
svn_error_t *
get_file_offset(apr_off_t *offset_p,
                apr_file_t *file,
                apr_pool_t *pool);

/* Read the 'current' file FNAME and store the contents in *BUF.
   Allocations are performed in POOL. */
svn_error_t *
read_content(svn_stringbuf_t **content,
             const char *fname,
             apr_pool_t *pool);

/* Reads a line from STREAM and converts it to a 64 bit integer to be
 * returned in *RESULT.  If we encounter eof, set *HIT_EOF and leave
 * *RESULT unchanged.  If HIT_EOF is NULL, EOF causes an "corrupt FS"
 * error return.
 * SCRATCH_POOL is used for temporary allocations.
 */
svn_error_t *
read_number_from_stream(apr_int64_t *result,
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
move_into_place(const char *old_filename,
                const char *new_filename,
                const char *perms_reference,
                apr_pool_t *pool);

#endif