/* util.c --- utility functions for FSFS repo access
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

#include <assert.h>

#include "svn_ctype.h"
#include "svn_dirent_uri.h"

#include "fs_fs.h"
#include "id.h"
#include "util.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

/* Following are defines that specify the textual elements of the
   native filesystem directories and revision files. */

/* Notes:

To avoid opening and closing the rev-files all the time, it would
probably be advantageous to keep each rev-file open for the
lifetime of the transaction object.  I'll leave that as a later
optimization for now.

I didn't keep track of pool lifetimes at all in this code.  There
are likely some errors because of that.

*/

/* Pathname helper functions */

/* Return TRUE is REV is packed in FS, FALSE otherwise. */
svn_boolean_t
is_packed_rev(svn_fs_t *fs, svn_revnum_t rev)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  return (rev < ffd->min_unpacked_rev);
}

/* Return TRUE is REV is packed in FS, FALSE otherwise. */
svn_boolean_t
is_packed_revprop(svn_fs_t *fs, svn_revnum_t rev)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  /* rev 0 will not be packed */
  return (rev < ffd->min_unpacked_rev)
      && (rev != 0)
      && (ffd->format >= SVN_FS_FS__MIN_PACKED_REVPROP_FORMAT);
}

const char *
path_format(svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_FORMAT, pool);
}

const char *
path_uuid(svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_UUID, pool);
}

const char *
svn_fs_fs__path_current(svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_CURRENT, pool);
}

const char *
path_txn_current(svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_TXN_CURRENT, pool);
}

const char *
path_txn_current_lock(svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_TXN_CURRENT_LOCK, pool);
}

const char *
path_lock(svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_LOCK_FILE, pool);
}

const char *
path_revprop_generation(svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_REVPROP_GENERATION, pool);
}

const char *
path_rev_packed(svn_fs_t *fs, svn_revnum_t rev, const char *kind,
                apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  assert(ffd->max_files_per_dir);
  assert(is_packed_rev(fs, rev));

  return svn_dirent_join_many(pool, fs->path, PATH_REVS_DIR,
                              apr_psprintf(pool,
                                           "%ld" PATH_EXT_PACKED_SHARD,
                                           rev / ffd->max_files_per_dir),
                              kind, NULL);
}

const char *
path_rev_shard(svn_fs_t *fs, svn_revnum_t rev, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  assert(ffd->max_files_per_dir);
  return svn_dirent_join_many(pool, fs->path, PATH_REVS_DIR,
                              apr_psprintf(pool, "%ld",
                                                 rev / ffd->max_files_per_dir),
                              NULL);
}

const char *
path_rev(svn_fs_t *fs, svn_revnum_t rev, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  assert(! is_packed_rev(fs, rev));

  if (ffd->max_files_per_dir)
    {
      return svn_dirent_join(path_rev_shard(fs, rev, pool),
                             apr_psprintf(pool, "%ld", rev),
                             pool);
    }

  return svn_dirent_join_many(pool, fs->path, PATH_REVS_DIR,
                              apr_psprintf(pool, "%ld", rev), NULL);
}

svn_error_t *
svn_fs_fs__path_rev_absolute(const char **path,
                             svn_fs_t *fs,
                             svn_revnum_t rev,
                             apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  if (ffd->format < SVN_FS_FS__MIN_PACKED_FORMAT
      || ! is_packed_rev(fs, rev))
    {
      *path = path_rev(fs, rev, pool);
    }
  else
    {
      *path = path_rev_packed(fs, rev, PATH_PACKED, pool);
    }

  return SVN_NO_ERROR;
}

const char *
path_revprops_shard(svn_fs_t *fs, svn_revnum_t rev, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  assert(ffd->max_files_per_dir);
  return svn_dirent_join_many(pool, fs->path, PATH_REVPROPS_DIR,
                              apr_psprintf(pool, "%ld",
                                           rev / ffd->max_files_per_dir),
                              NULL);
}

const char *
path_revprops_pack_shard(svn_fs_t *fs, svn_revnum_t rev, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  assert(ffd->max_files_per_dir);
  return svn_dirent_join_many(pool, fs->path, PATH_REVPROPS_DIR,
                              apr_psprintf(pool, "%ld" PATH_EXT_PACKED_SHARD,
                                           rev / ffd->max_files_per_dir),
                              NULL);
}

const char *
path_revprops(svn_fs_t *fs, svn_revnum_t rev, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  if (ffd->max_files_per_dir)
    {
      return svn_dirent_join(path_revprops_shard(fs, rev, pool),
                             apr_psprintf(pool, "%ld", rev),
                             pool);
    }

  return svn_dirent_join_many(pool, fs->path, PATH_REVPROPS_DIR,
                              apr_psprintf(pool, "%ld", rev), NULL);
}

const char *
path_txn_dir(svn_fs_t *fs, const char *txn_id, apr_pool_t *pool)
{
  SVN_ERR_ASSERT_NO_RETURN(txn_id != NULL);
  return svn_dirent_join_many(pool, fs->path, PATH_TXNS_DIR,
                              apr_pstrcat(pool, txn_id, PATH_EXT_TXN,
                                          (char *)NULL),
                              NULL);
}

/* Return the name of the sha1->rep mapping file in transaction TXN_ID
 * within FS for the given SHA1 checksum.  Use POOL for allocations.
 */
const char *
path_txn_sha1(svn_fs_t *fs, const char *txn_id, svn_checksum_t *sha1,
              apr_pool_t *pool)
{
  return svn_dirent_join(path_txn_dir(fs, txn_id, pool),
                         svn_checksum_to_cstring(sha1, pool),
                         pool);
}

const char *
path_txn_changes(svn_fs_t *fs, const char *txn_id, apr_pool_t *pool)
{
  return svn_dirent_join(path_txn_dir(fs, txn_id, pool), PATH_CHANGES, pool);
}

const char *
path_txn_props(svn_fs_t *fs, const char *txn_id, apr_pool_t *pool)
{
  return svn_dirent_join(path_txn_dir(fs, txn_id, pool), PATH_TXN_PROPS, pool);
}

const char *
path_txn_next_ids(svn_fs_t *fs, const char *txn_id, apr_pool_t *pool)
{
  return svn_dirent_join(path_txn_dir(fs, txn_id, pool), PATH_NEXT_IDS, pool);
}

const char *
path_min_unpacked_rev(svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_MIN_UNPACKED_REV, pool);
}


const char *
path_txn_proto_rev(svn_fs_t *fs, const char *txn_id, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  if (ffd->format >= SVN_FS_FS__MIN_PROTOREVS_DIR_FORMAT)
    return svn_dirent_join_many(pool, fs->path, PATH_TXN_PROTOS_DIR,
                                apr_pstrcat(pool, txn_id, PATH_EXT_REV,
                                            (char *)NULL),
                                NULL);
  else
    return svn_dirent_join(path_txn_dir(fs, txn_id, pool), PATH_REV, pool);
}

const char *
path_txn_proto_rev_lock(svn_fs_t *fs, const char *txn_id, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  if (ffd->format >= SVN_FS_FS__MIN_PROTOREVS_DIR_FORMAT)
    return svn_dirent_join_many(pool, fs->path, PATH_TXN_PROTOS_DIR,
                                apr_pstrcat(pool, txn_id, PATH_EXT_REV_LOCK,
                                            (char *)NULL),
                                NULL);
  else
    return svn_dirent_join(path_txn_dir(fs, txn_id, pool), PATH_REV_LOCK,
                           pool);
}

const char *
path_txn_node_rev(svn_fs_t *fs, const svn_fs_id_t *id, apr_pool_t *pool)
{
  const char *txn_id = svn_fs_fs__id_txn_id(id);
  const char *node_id = svn_fs_fs__id_node_id(id);
  const char *copy_id = svn_fs_fs__id_copy_id(id);
  const char *name = apr_psprintf(pool, PATH_PREFIX_NODE "%s.%s",
                                  node_id, copy_id);

  return svn_dirent_join(path_txn_dir(fs, txn_id, pool), name, pool);
}

const char *
path_txn_node_props(svn_fs_t *fs, const svn_fs_id_t *id, apr_pool_t *pool)
{
  return apr_pstrcat(pool, path_txn_node_rev(fs, id, pool), PATH_EXT_PROPS,
                     (char *)NULL);
}

const char *
path_txn_node_children(svn_fs_t *fs, const svn_fs_id_t *id, apr_pool_t *pool)
{
  return apr_pstrcat(pool, path_txn_node_rev(fs, id, pool),
                     PATH_EXT_CHILDREN, (char *)NULL);
}

const char *
path_node_origin(svn_fs_t *fs, const char *node_id, apr_pool_t *pool)
{
  size_t len = strlen(node_id);
  const char *node_id_minus_last_char =
    (len == 1) ? "0" : apr_pstrmemdup(pool, node_id, len - 1);
  return svn_dirent_join_many(pool, fs->path, PATH_NODE_ORIGINS_DIR,
                              node_id_minus_last_char, NULL);
}


/* Check that BUF, a nul-terminated buffer of text from file PATH,
   contains only digits at OFFSET and beyond, raising an error if not.
   TITLE contains a user-visible description of the file, usually the
   short file name.

   Uses POOL for temporary allocation. */
svn_error_t *
check_file_buffer_numeric(const char *buf, apr_off_t offset,
                          const char *path, const char *title,
                          apr_pool_t *pool)
{
  const char *p;

  for (p = buf + offset; *p; p++)
    if (!svn_ctype_isdigit(*p))
      return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
        _("%s file '%s' contains unexpected non-digit '%c' within '%s'"),
        title, svn_dirent_local_style(path, pool), *p, buf);

  return SVN_NO_ERROR;
}

svn_error_t *
read_min_unpacked_rev(svn_revnum_t *min_unpacked_rev,
                      const char *path,
                      apr_pool_t *pool)
{
  char buf[80];
  apr_file_t *file;
  apr_size_t len;

  SVN_ERR(svn_io_file_open(&file, path, APR_READ | APR_BUFFERED,
                           APR_OS_DEFAULT, pool));
  len = sizeof(buf);
  SVN_ERR(svn_io_read_length_line(file, buf, &len, pool));
  SVN_ERR(svn_io_file_close(file, pool));

  *min_unpacked_rev = SVN_STR_TO_REV(buf);
  return SVN_NO_ERROR;
}

svn_error_t *
update_min_unpacked_rev(svn_fs_t *fs, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  SVN_ERR_ASSERT(ffd->format >= SVN_FS_FS__MIN_PACKED_FORMAT);

  return read_min_unpacked_rev(&ffd->min_unpacked_rev,
                               path_min_unpacked_rev(fs, pool),
                               pool);
}



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
                        apr_pool_t *pool)
{
  svn_error_t *err = svn_stringbuf_from_file2(content, path, pool);
  if (missing)
    *missing = FALSE;

  if (err)
    {
      *content = NULL;

      if (APR_STATUS_IS_ENOENT(err->apr_err))
        {
          if (!last_attempt)
            {
              svn_error_clear(err);
              if (missing)
                *missing = TRUE;
              return SVN_NO_ERROR;
            }
        }
#ifdef ESTALE
      else if (APR_TO_OS_ERROR(err->apr_err) == ESTALE
                || APR_TO_OS_ERROR(err->apr_err) == EIO)
        {
          if (!last_attempt)
            {
              svn_error_clear(err);
              return SVN_NO_ERROR;
            }
        }
#endif
    }

  return svn_error_trace(err);
}

/* Read the 'current' file FNAME and store the contents in *BUF.
   Allocations are performed in POOL. */
svn_error_t *
read_content(svn_stringbuf_t **content, const char *fname, apr_pool_t *pool)
{
  int i;
  *content = NULL;

  for (i = 0; !*content && (i < RECOVERABLE_RETRY_COUNT); ++i)
    SVN_ERR(try_stringbuf_from_file(content, NULL,
                                    fname, i + 1 < RECOVERABLE_RETRY_COUNT,
                                    pool));

  if (!*content)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Can't read '%s'"),
                             svn_dirent_local_style(fname, pool));

  return SVN_NO_ERROR;
}

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
                        apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *sb;
  svn_boolean_t eof;
  svn_error_t *err;

  SVN_ERR(svn_stream_readline(stream, &sb, "\n", &eof, scratch_pool));
  if (hit_eof)
    *hit_eof = eof;
  else
    if (eof)
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL, _("Unexpected EOF"));

  if (!eof)
    {
      err = svn_cstring_atoi64(result, sb->data);
      if (err)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, err,
                                 _("Number '%s' invalid or too large"),
                                 sb->data);
    }

  return SVN_NO_ERROR;
}


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
                apr_pool_t *pool)
{
  svn_error_t *err;

  SVN_ERR(svn_io_copy_perms(perms_reference, old_filename, pool));

  /* Move the file into place. */
  err = svn_io_file_rename(old_filename, new_filename, pool);
  if (err && APR_STATUS_IS_EXDEV(err->apr_err))
    {
      apr_file_t *file;

      /* Can't rename across devices; fall back to copying. */
      svn_error_clear(err);
      err = SVN_NO_ERROR;
      SVN_ERR(svn_io_copy_file(old_filename, new_filename, TRUE, pool));

      /* Flush the target of the copy to disk. */
      SVN_ERR(svn_io_file_open(&file, new_filename, APR_READ,
                               APR_OS_DEFAULT, pool));
      /* ### BH: Does this really guarantee a flush of the data written
         ### via a completely different handle on all operating systems?
         ###
         ### Maybe we should perform the copy ourselves instead of making
         ### apr do that and flush the real handle? */
      SVN_ERR(svn_io_file_flush_to_disk(file, pool));
      SVN_ERR(svn_io_file_close(file, pool));
    }
  if (err)
    return svn_error_trace(err);

#ifdef __linux__
  {
    /* Linux has the unusual feature that fsync() on a file is not
       enough to ensure that a file's directory entries have been
       flushed to disk; you have to fsync the directory as well.
       On other operating systems, we'd only be asking for trouble
       by trying to open and fsync a directory. */
    const char *dirname;
    apr_file_t *file;

    dirname = svn_dirent_dirname(new_filename, pool);
    SVN_ERR(svn_io_file_open(&file, dirname, APR_READ, APR_OS_DEFAULT,
                             pool));
    SVN_ERR(svn_io_file_flush_to_disk(file, pool));
    SVN_ERR(svn_io_file_close(file, pool));
  }
#endif

  return SVN_NO_ERROR;
}
