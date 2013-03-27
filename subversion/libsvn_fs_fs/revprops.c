/* revprops.c --- everything needed to handle revprops in FSFS
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

#include "svn_pools.h"
#include "svn_hash.h"
#include "svn_dirent_uri.h"

#include "fs_fs.h"
#include "revprops.h"
#include "util.h"
#include "transaction.h"

#include "private/svn_subr_private.h"
#include "private/svn_string_private.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

/* Give writing processes 10 seconds to replace an existing revprop
   file with a new one. After that time, we assume that the writing
   process got aborted and that we have re-read revprops. */
#define REVPROP_CHANGE_TIMEOUT (10 * 1000000)

/* The following are names of atomics that will be used to communicate
 * revprop updates across all processes on this machine. */
#define ATOMIC_REVPROP_GENERATION "rev-prop-generation"
#define ATOMIC_REVPROP_TIMEOUT    "rev-prop-timeout"
#define ATOMIC_REVPROP_NAMESPACE  "rev-prop-atomics"

/* In the filesystem FS, pack all revprop shards up to min_unpacked_rev.
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
upgrade_pack_revprops(svn_fs_t *fs,
                      apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *revprops_shard_path;
  const char *revprops_pack_file_dir;
  apr_int64_t shard;
  apr_int64_t first_unpacked_shard
    =  ffd->min_unpacked_rev / ffd->max_files_per_dir;

  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  const char *revsprops_dir = svn_dirent_join(fs->path, PATH_REVPROPS_DIR,
                                              scratch_pool);
  int compression_level = ffd->compress_packed_revprops
                           ? SVN_DELTA_COMPRESSION_LEVEL_DEFAULT
                           : SVN_DELTA_COMPRESSION_LEVEL_NONE;

  /* first, pack all revprops shards to match the packed revision shards */
  for (shard = 0; shard < first_unpacked_shard; ++shard)
    {
      revprops_pack_file_dir = svn_dirent_join(revsprops_dir,
                   apr_psprintf(iterpool,
                                "%" APR_INT64_T_FMT PATH_EXT_PACKED_SHARD,
                                shard),
                   iterpool);
      revprops_shard_path = svn_dirent_join(revsprops_dir,
                       apr_psprintf(iterpool, "%" APR_INT64_T_FMT, shard),
                       iterpool);

      SVN_ERR(pack_revprops_shard(revprops_pack_file_dir, revprops_shard_path,
                                  shard, ffd->max_files_per_dir,
                                  (int)(0.9 * ffd->revprop_pack_size),
                                  compression_level,
                                  NULL, NULL, iterpool));
      svn_pool_clear(iterpool);
    }

  /* delete the non-packed revprops shards afterwards */
  for (shard = 0; shard < first_unpacked_shard; ++shard)
    {
      revprops_shard_path = svn_dirent_join(revsprops_dir,
                       apr_psprintf(iterpool, "%" APR_INT64_T_FMT, shard),
                       iterpool);
      SVN_ERR(delete_revprops_shard(revprops_shard_path,
                                    shard, ffd->max_files_per_dir,
                                    NULL, NULL, iterpool));
      svn_pool_clear(iterpool);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Revprop caching management.
 *
 * Mechanism:
 * ----------
 * 
 * Revprop caching needs to be activated and will be deactivated for the
 * respective FS instance if the necessary infrastructure could not be
 * initialized.  In deactivated mode, there is almost no runtime overhead
 * associated with revprop caching.  As long as no revprops are being read
 * or changed, revprop caching imposes no overhead.
 *
 * When activated, we cache revprops using (revision, generation) pairs
 * as keys with the generation being incremented upon every revprop change.
 * Since the cache is process-local, the generation needs to be tracked
 * for at least as long as the process lives but may be reset afterwards.
 *
 * To track the revprop generation, we use two-layer approach. On the lower
 * level, we use named atomics to have a system-wide consistent value for
 * the current revprop generation.  However, those named atomics will only
 * remain valid for as long as at least one process / thread in the system
 * accesses revprops in the respective repository.  The underlying shared
 * memory gets cleaned up afterwards.
 *
 * On the second level, we will use a persistent file to track the latest
 * revprop generation.  It will be written upon each revprop change but
 * only be read if we are the first process to initialize the named atomics
 * with that value.
 *
 * The overhead for the second and following accesses to revprops is
 * almost zero on most systems.
 *
 *
 * Tech aspects:
 * -------------
 *
 * A problem is that we need to provide a globally available file name to
 * back the SHM implementation on OSes that need it.  We can only assume
 * write access to some file within the respective repositories.  Because
 * a given server process may access thousands of repositories during its
 * lifetime, keeping the SHM data alive for all of them is also not an
 * option.
 *
 * So, we store the new revprop generation on disk as part of each
 * setrevprop call, i.e. this write will be serialized and the write order
 * be guaranteed by the repository write lock.
 *
 * The only racy situation occurs when the data is being read again by two
 * processes concurrently but in that situation, the first process to
 * finish that procedure is guaranteed to be the only one that initializes
 * the SHM data.  Since even writers will first go through that
 * initialization phase, they will never operate on stale data.
 */

/* Read revprop generation as stored on disk for repository FS. The result
 * is returned in *CURRENT. Default to 2 if no such file is available.
 */
static svn_error_t *
read_revprop_generation_file(apr_int64_t *current,
                             svn_fs_t *fs,
                             apr_pool_t *pool)
{
  svn_error_t *err;
  apr_file_t *file;
  char buf[80];
  apr_size_t len;
  const char *path = path_revprop_generation(fs, pool);

  err = svn_io_file_open(&file, path,
                         APR_READ | APR_BUFFERED,
                         APR_OS_DEFAULT, pool);
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      *current = 2;

      return SVN_NO_ERROR;
    }
  SVN_ERR(err);

  len = sizeof(buf);
  SVN_ERR(svn_io_read_length_line(file, buf, &len, pool));

  /* Check that the first line contains only digits. */
  SVN_ERR(check_file_buffer_numeric(buf, 0, path,
                                    "Revprop Generation", pool));
  SVN_ERR(svn_cstring_atoi64(current, buf));

  return svn_io_file_close(file, pool);
}

/* Write the CURRENT revprop generation to disk for repository FS.
 */
svn_error_t *
write_revprop_generation_file(svn_fs_t *fs,
                              apr_int64_t current,
                              apr_pool_t *pool)
{
  apr_file_t *file;
  const char *tmp_path;

  char buf[SVN_INT64_BUFFER_SIZE];
  apr_size_t len = svn__i64toa(buf, current);
  buf[len] = '\n';

  SVN_ERR(svn_io_open_unique_file3(&file, &tmp_path, fs->path,
                                   svn_io_file_del_none, pool, pool));
  SVN_ERR(svn_io_file_write_full(file, buf, len + 1, NULL, pool));
  SVN_ERR(svn_io_file_close(file, pool));

  return move_into_place(tmp_path, path_revprop_generation(fs, pool),
                         tmp_path, pool);
}

/* Make sure the revprop_namespace member in FS is set. */
static svn_error_t *
ensure_revprop_namespace(svn_fs_t *fs)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  return ffd->revprop_namespace == NULL
    ? svn_atomic_namespace__create(&ffd->revprop_namespace,
                                   svn_dirent_join(fs->path,
                                                   ATOMIC_REVPROP_NAMESPACE,
                                                   fs->pool),
                                   fs->pool)
    : SVN_NO_ERROR;
}

/* Make sure the revprop_namespace member in FS is set. */
svn_error_t *
cleanup_revprop_namespace(svn_fs_t *fs)
{
  const char *name = svn_dirent_join(fs->path,
                                     ATOMIC_REVPROP_NAMESPACE,
                                     fs->pool);
  return svn_error_trace(svn_atomic_namespace__cleanup(name, fs->pool));
}

/* Make sure the revprop_generation member in FS is set and, if necessary,
 * initialized with the latest value stored on disk.
 */
static svn_error_t *
ensure_revprop_generation(svn_fs_t *fs, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  SVN_ERR(ensure_revprop_namespace(fs));
  if (ffd->revprop_generation == NULL)
    {
      apr_int64_t current = 0;
      
      SVN_ERR(svn_named_atomic__get(&ffd->revprop_generation,
                                    ffd->revprop_namespace,
                                    ATOMIC_REVPROP_GENERATION,
                                    TRUE));

      /* If the generation is at 0, we just created a new namespace
       * (it would be at least 2 otherwise). Read the latest generation
       * from disk and if we are the first one to initialize the atomic
       * (i.e. is still 0), set it to the value just gotten.
       */
      SVN_ERR(svn_named_atomic__read(&current, ffd->revprop_generation));
      if (current == 0)
        {
          SVN_ERR(read_revprop_generation_file(&current, fs, pool));
          SVN_ERR(svn_named_atomic__cmpxchg(NULL, current, 0,
                                            ffd->revprop_generation));
        }
    }

  return SVN_NO_ERROR;
}

/* Make sure the revprop_timeout member in FS is set. */
static svn_error_t *
ensure_revprop_timeout(svn_fs_t *fs)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  SVN_ERR(ensure_revprop_namespace(fs));
  return ffd->revprop_timeout == NULL
    ? svn_named_atomic__get(&ffd->revprop_timeout,
                            ffd->revprop_namespace,
                            ATOMIC_REVPROP_TIMEOUT,
                            TRUE)
    : SVN_NO_ERROR;
}

/* Create an error object with the given MESSAGE and pass it to the
   WARNING member of FS. */
static void
log_revprop_cache_init_warning(svn_fs_t *fs,
                               svn_error_t *underlying_err,
                               const char *message)
{
  svn_error_t *err = svn_error_createf(SVN_ERR_FS_REPPROP_CACHE_INIT_FAILURE,
                                       underlying_err,
                                       message, fs->path);

  if (fs->warning)
    (fs->warning)(fs->warning_baton, err);
  
  svn_error_clear(err);
}

/* Test whether revprop cache and necessary infrastructure are
   available in FS. */
static svn_boolean_t
has_revprop_cache(svn_fs_t *fs, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_error_t *error;

  /* is the cache (still) enabled? */
  if (ffd->revprop_cache == NULL)
    return FALSE;

  /* is it efficient? */
  if (!svn_named_atomic__is_efficient())
    {
      /* access to it would be quite slow
       * -> disable the revprop cache for good
       */
      ffd->revprop_cache = NULL;
      log_revprop_cache_init_warning(fs, NULL,
                                     "Revprop caching for '%s' disabled"
                                     " because it would be inefficient.");
      
      return FALSE;
    }

  /* try to access our SHM-backed infrastructure */
  error = ensure_revprop_generation(fs, pool);
  if (error)
    {
      /* failure -> disable revprop cache for good */

      ffd->revprop_cache = NULL;
      log_revprop_cache_init_warning(fs, error,
                                     "Revprop caching for '%s' disabled "
                                     "because SHM infrastructure for revprop "
                                     "caching failed to initialize.");

      return FALSE;
    }

  return TRUE;
}

/* Baton structure for revprop_generation_fixup. */
typedef struct revprop_generation_fixup_t
{
  /* revprop generation to read */
  apr_int64_t *generation;

  /* containing the revprop_generation member to query */
  fs_fs_data_t *ffd;
} revprop_generation_upgrade_t;

/* If the revprop generation has an odd value, it means the original writer
   of the revprop got killed. We don't know whether that process as able
   to change the revprop data but we assume that it was. Therefore, we
   increase the generation in that case to basically invalidate everyones
   cache content.
   Execute this onlx while holding the write lock to the repo in baton->FFD.
 */
static svn_error_t *
revprop_generation_fixup(void *void_baton,
                         apr_pool_t *pool)
{
  revprop_generation_upgrade_t *baton = void_baton;
  assert(baton->ffd->has_write_lock);
  
  /* Maybe, either the original revprop writer or some other reader has
     already corrected / bumped the revprop generation.  Thus, we need
     to read it again. */
  SVN_ERR(svn_named_atomic__read(baton->generation,
                                 baton->ffd->revprop_generation));

  /* Cause everyone to re-read revprops upon their next access, if the
     last revprop write did not complete properly. */
  while (*baton->generation % 2)
    SVN_ERR(svn_named_atomic__add(baton->generation,
                                  1,
                                  baton->ffd->revprop_generation));

  return SVN_NO_ERROR;
}

/* Read the current revprop generation and return it in *GENERATION.
   Also, detect aborted / crashed writers and recover from that.
   Use the access object in FS to set the shared mem values. */
static svn_error_t *
read_revprop_generation(apr_int64_t *generation,
                        svn_fs_t *fs,
                        apr_pool_t *pool)
{
  apr_int64_t current = 0;
  fs_fs_data_t *ffd = fs->fsap_data;

  /* read the current revprop generation number */
  SVN_ERR(ensure_revprop_generation(fs, pool));
  SVN_ERR(svn_named_atomic__read(&current, ffd->revprop_generation));

  /* is an unfinished revprop write under the way? */
  if (current % 2)
    {
      apr_int64_t timeout = 0;

      /* read timeout for the write operation */
      SVN_ERR(ensure_revprop_timeout(fs));
      SVN_ERR(svn_named_atomic__read(&timeout, ffd->revprop_timeout));

      /* has the writer process been aborted,
       * i.e. has the timeout been reached?
       */
      if (apr_time_now() > timeout)
        {
          revprop_generation_upgrade_t baton;
          baton.generation = &current;
          baton.ffd = ffd;

          /* Ensure that the original writer process no longer exists by
           * acquiring the write lock to this repository.  Then, fix up
           * the revprop generation.
           */
          if (ffd->has_write_lock)
            SVN_ERR(revprop_generation_fixup(&baton, pool));
          else
            SVN_ERR(svn_fs_fs__with_write_lock(fs, revprop_generation_fixup,
                                               &baton, pool));
        }
    }

  /* return the value we just got */
  *generation = current;
  return SVN_NO_ERROR;
}

/* Set the revprop generation to the next odd number to indicate that
   there is a revprop write process under way. If that times out,
   readers shall recover from that state & re-read revprops.
   Use the access object in FS to set the shared mem value. */
static svn_error_t *
begin_revprop_change(svn_fs_t *fs, apr_pool_t *pool)
{
  apr_int64_t current;
  fs_fs_data_t *ffd = fs->fsap_data;

  /* set the timeout for the write operation */
  SVN_ERR(ensure_revprop_timeout(fs));
  SVN_ERR(svn_named_atomic__write(NULL,
                                  apr_time_now() + REVPROP_CHANGE_TIMEOUT,
                                  ffd->revprop_timeout));

  /* set the revprop generation to an odd value to indicate
   * that a write is in progress
   */
  SVN_ERR(ensure_revprop_generation(fs, pool));
  do
    {
      SVN_ERR(svn_named_atomic__add(&current,
                                    1,
                                    ffd->revprop_generation));
    }
  while (current % 2 == 0);

  return SVN_NO_ERROR;
}

/* Set the revprop generation to the next even number to indicate that
   a) readers shall re-read revprops, and
   b) the write process has been completed (no recovery required)
   Use the access object in FS to set the shared mem value. */
static svn_error_t *
end_revprop_change(svn_fs_t *fs, apr_pool_t *pool)
{
  apr_int64_t current = 1;
  fs_fs_data_t *ffd = fs->fsap_data;

  /* set the revprop generation to an even value to indicate
   * that a write has been completed
   */
  SVN_ERR(ensure_revprop_generation(fs, pool));
  do
    {
      SVN_ERR(svn_named_atomic__add(&current,
                                    1,
                                    ffd->revprop_generation));
    }
  while (current % 2);

  /* Save the latest generation to disk. FS is currently in a "locked"
   * state such that we can be sure the be the only ones to write that
   * file.
   */
  return write_revprop_generation_file(fs, current, pool);
}

/* Container for all data required to access the packed revprop file
 * for a given REVISION.  This structure will be filled incrementally
 * by read_pack_revprops() its sub-routines.
 */
typedef struct packed_revprops_t
{
  /* revision number to read (not necessarily the first in the pack) */
  svn_revnum_t revision;

  /* current revprop generation. Used when populating the revprop cache */
  apr_int64_t generation;

  /* the actual revision properties */
  apr_hash_t *properties;

  /* their size when serialized to a single string
   * (as found in PACKED_REVPROPS) */
  apr_size_t serialized_size;


  /* name of the pack file (without folder path) */
  const char *filename;

  /* packed shard folder path */
  const char *folder;

  /* sum of values in SIZES */
  apr_size_t total_size;

  /* first revision in the pack */
  svn_revnum_t start_revision;

  /* size of the revprops in PACKED_REVPROPS */
  apr_array_header_t *sizes;

  /* offset of the revprops in PACKED_REVPROPS */
  apr_array_header_t *offsets;


  /* concatenation of the serialized representation of all revprops 
   * in the pack, i.e. the pack content without header and compression */
  svn_stringbuf_t *packed_revprops;

  /* content of the manifest.
   * Maps long(rev - START_REVISION) to const char* pack file name */
  apr_array_header_t *manifest;
} packed_revprops_t;

/* Parse the serialized revprops in CONTENT and return them in *PROPERTIES.
 * Also, put them into the revprop cache, if activated, for future use.
 * Three more parameters are being used to update the revprop cache: FS is
 * our file system, the revprops belong to REVISION and the global revprop
 * GENERATION is used as well.
 * 
 * The returned hash will be allocated in POOL, SCRATCH_POOL is being used
 * for temporary allocations.
 */
static svn_error_t *
parse_revprop(apr_hash_t **properties,
              svn_fs_t *fs,
              svn_revnum_t revision,
              apr_int64_t generation,
              svn_string_t *content,
              apr_pool_t *pool,
              apr_pool_t *scratch_pool)
{
  svn_stream_t *stream = svn_stream_from_string(content, scratch_pool);
  *properties = apr_hash_make(pool);

  SVN_ERR(svn_hash_read2(*properties, stream, SVN_HASH_TERMINATOR, pool));
  if (has_revprop_cache(fs, pool))
    {
      fs_fs_data_t *ffd = fs->fsap_data;
      pair_cache_key_t key = { 0 };

      key.revision = revision;
      key.second = generation;
      SVN_ERR(svn_cache__set(ffd->revprop_cache, &key, *properties,
                             scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Read the non-packed revprops for revision REV in FS, put them into the
 * revprop cache if activated and return them in *PROPERTIES.  GENERATION
 * is the current revprop generation.
 *
 * If the data could not be read due to an otherwise recoverable error,
 * leave *PROPERTIES unchanged. No error will be returned in that case.
 *
 * Allocations will be done in POOL.
 */
static svn_error_t *
read_non_packed_revprop(apr_hash_t **properties,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        apr_int64_t generation,
                        apr_pool_t *pool)
{
  svn_stringbuf_t *content = NULL;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_boolean_t missing = FALSE;
  int i;

  for (i = 0; i < RECOVERABLE_RETRY_COUNT && !missing && !content; ++i)
    {
      svn_pool_clear(iterpool);
      SVN_ERR(try_stringbuf_from_file(&content,
                                      &missing,
                                      path_revprops(fs, rev, iterpool),
                                      i + 1 < RECOVERABLE_RETRY_COUNT,
                                      iterpool));
    }

  if (content)
    SVN_ERR(parse_revprop(properties, fs, rev, generation,
                          svn_stringbuf__morph_into_string(content),
                          pool, iterpool));

  svn_pool_clear(iterpool);

  return SVN_NO_ERROR;
}

/* Given FS and REVPROPS->REVISION, fill the FILENAME, FOLDER and MANIFEST
 * members. Use POOL for allocating results and SCRATCH_POOL for temporaries.
 */
static svn_error_t *
get_revprop_packname(svn_fs_t *fs,
                     packed_revprops_t *revprops,
                     apr_pool_t *pool,
                     apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_stringbuf_t *content = NULL;
  const char *manifest_file_path;
  int idx;

  /* read content of the manifest file */
  revprops->folder = path_revprops_pack_shard(fs, revprops->revision, pool);
  manifest_file_path = svn_dirent_join(revprops->folder, PATH_MANIFEST, pool);

  SVN_ERR(read_content(&content, manifest_file_path, pool));

  /* parse the manifest. Every line is a file name */
  revprops->manifest = apr_array_make(pool, ffd->max_files_per_dir,
                                      sizeof(const char*));
  while (content->data)
    {
      APR_ARRAY_PUSH(revprops->manifest, const char*) = content->data;
      content->data = strchr(content->data, '\n');
      if (content->data)
        {
          *content->data = 0;
          content->data++;
        }
    }

  /* Index for our revision. Rev 0 is excluded from the first shard. */
  idx = (int)(revprops->revision % ffd->max_files_per_dir);
  if (revprops->revision < ffd->max_files_per_dir)
    --idx;

  if (revprops->manifest->nelts <= idx)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Packed revprop manifest for rev %ld too "
                               "small"), revprops->revision);

  /* Now get the file name */
  revprops->filename = APR_ARRAY_IDX(revprops->manifest, idx, const char*);

  return SVN_NO_ERROR;
}

/* Given FS and the full packed file content in REVPROPS->PACKED_REVPROPS,
 * fill the START_REVISION, SIZES, OFFSETS members. Also, make
 * PACKED_REVPROPS point to the first serialized revprop.
 *
 * Parse the revprops for REVPROPS->REVISION and set the PROPERTIES as
 * well as the SERIALIZED_SIZE member.  If revprop caching has been
 * enabled, parse all revprops in the pack and cache them.
 */
static svn_error_t *
parse_packed_revprops(svn_fs_t *fs,
                      packed_revprops_t *revprops,
                      apr_pool_t *pool,
                      apr_pool_t *scratch_pool)
{
  svn_stream_t *stream;
  apr_int64_t first_rev, count, i;
  apr_off_t offset;
  const char *header_end;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* decompress (even if the data is only "stored", there is still a
   * length header to remove) */
  svn_stringbuf_t *compressed = revprops->packed_revprops;
  svn_stringbuf_t *uncompressed = svn_stringbuf_create_empty(pool);
  SVN_ERR(svn__decompress(compressed, uncompressed, 0x1000000));

  /* read first revision number and number of revisions in the pack */
  stream = svn_stream_from_stringbuf(uncompressed, scratch_pool);
  SVN_ERR(read_number_from_stream(&first_rev, NULL, stream, iterpool));
  SVN_ERR(read_number_from_stream(&count, NULL, stream, iterpool));

  /* make PACKED_REVPROPS point to the first char after the header.
   * This is where the serialized revprops are. */
  header_end = strstr(uncompressed->data, "\n\n");
  if (header_end == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Header end not found"));

  offset = header_end - uncompressed->data + 2;

  revprops->packed_revprops = svn_stringbuf_create_empty(pool);
  revprops->packed_revprops->data = uncompressed->data + offset;
  revprops->packed_revprops->len = (apr_size_t)(uncompressed->len - offset);
  revprops->packed_revprops->blocksize = (apr_size_t)(uncompressed->blocksize - offset);

  /* STREAM still points to the first entry in the sizes list.
   * Init / construct REVPROPS members. */
  revprops->start_revision = (svn_revnum_t)first_rev;
  revprops->sizes = apr_array_make(pool, (int)count, sizeof(offset));
  revprops->offsets = apr_array_make(pool, (int)count, sizeof(offset));

  /* Now parse, revision by revision, the size and content of each
   * revisions' revprops. */
  for (i = 0, offset = 0, revprops->total_size = 0; i < count; ++i)
    {
      apr_int64_t size;
      svn_string_t serialized;
      apr_hash_t *properties;
      svn_revnum_t revision = (svn_revnum_t)(first_rev + i);

      /* read & check the serialized size */
      SVN_ERR(read_number_from_stream(&size, NULL, stream, iterpool));
      if (size + offset > (apr_int64_t)revprops->packed_revprops->len)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                        _("Packed revprop size exceeds pack file size"));

      /* Parse this revprops list, if necessary */
      serialized.data = revprops->packed_revprops->data + offset;
      serialized.len = (apr_size_t)size;

      if (revision == revprops->revision)
        {
          SVN_ERR(parse_revprop(&revprops->properties, fs, revision,
                                revprops->generation, &serialized,
                                pool, iterpool));
          revprops->serialized_size = serialized.len;
        }
      else
        {
          /* If revprop caching is enabled, parse any revprops.
           * They will get cached as a side-effect of this. */
          if (has_revprop_cache(fs, pool))
            SVN_ERR(parse_revprop(&properties, fs, revision,
                                  revprops->generation, &serialized,
                                  iterpool, iterpool));
        }

      /* fill REVPROPS data structures */
      APR_ARRAY_PUSH(revprops->sizes, apr_off_t) = serialized.len;
      APR_ARRAY_PUSH(revprops->offsets, apr_off_t) = offset;
      revprops->total_size += serialized.len;

      offset += serialized.len;

      svn_pool_clear(iterpool);
    }

  return SVN_NO_ERROR;
}

/* In filesystem FS, read the packed revprops for revision REV into
 * *REVPROPS.  Use GENERATION to populate the revprop cache, if enabled.
 * Allocate data in POOL.
 */
static svn_error_t *
read_pack_revprop(packed_revprops_t **revprops,
                  svn_fs_t *fs,
                  svn_revnum_t rev,
                  apr_int64_t generation,
                  apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_boolean_t missing = FALSE;
  svn_error_t *err;
  packed_revprops_t *result;
  int i;

  /* someone insisted that REV is packed. Double-check if necessary */
  if (!is_packed_revprop(fs, rev))
     SVN_ERR(update_min_unpacked_rev(fs, iterpool));

  if (!is_packed_revprop(fs, rev))
    return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                              _("No such packed revision %ld"), rev);

  /* initialize the result data structure */
  result = apr_pcalloc(pool, sizeof(*result));
  result->revision = rev;
  result->generation = generation;

  /* try to read the packed revprops. This may require retries if we have
   * concurrent writers. */
  for (i = 0; i < RECOVERABLE_RETRY_COUNT && !result->packed_revprops; ++i)
    {
      const char *file_path;

      /* there might have been concurrent writes.
       * Re-read the manifest and the pack file.
       */
      SVN_ERR(get_revprop_packname(fs, result, pool, iterpool));
      file_path  = svn_dirent_join(result->folder,
                                   result->filename,
                                   iterpool);
      SVN_ERR(try_stringbuf_from_file(&result->packed_revprops,
                                      &missing,
                                      file_path,
                                      i + 1 < RECOVERABLE_RETRY_COUNT,
                                      pool));

      /* If we could not find the file, there was a write.
       * So, we should refresh our revprop generation info as well such
       * that others may find data we will put into the cache.  They would
       * consider it outdated, otherwise.
       */
      if (missing && has_revprop_cache(fs, pool))
        SVN_ERR(read_revprop_generation(&result->generation, fs, pool));

      svn_pool_clear(iterpool);
    }

  /* the file content should be available now */
  if (!result->packed_revprops)
    return svn_error_createf(SVN_ERR_FS_PACKED_REPPROP_READ_FAILURE, NULL,
                  _("Failed to read revprop pack file for rev %ld"), rev);

  /* parse it. RESULT will be complete afterwards. */
  err = parse_packed_revprops(fs, result, pool, iterpool);
  svn_pool_destroy(iterpool);
  if (err)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, err,
                  _("Revprop pack file for rev %ld is corrupt"), rev);

  *revprops = result;

  return SVN_NO_ERROR;
}

/* Read the revprops for revision REV in FS and return them in *PROPERTIES_P.
 *
 * Allocations will be done in POOL.
 */
svn_error_t *
get_revision_proplist(apr_hash_t **proplist_p,
                      svn_fs_t *fs,
                      svn_revnum_t rev,
                      apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_int64_t generation = 0;

  /* not found, yet */
  *proplist_p = NULL;

  /* should they be available at all? */
  SVN_ERR(svn_fs_fs__ensure_revision_exists(rev, fs, pool));

  /* Try cache lookup first. */
  if (has_revprop_cache(fs, pool))
    {
      svn_boolean_t is_cached;
      pair_cache_key_t key = { 0 };

      SVN_ERR(read_revprop_generation(&generation, fs, pool));

      key.revision = rev;
      key.second = generation;
      SVN_ERR(svn_cache__get((void **) proplist_p, &is_cached,
                             ffd->revprop_cache, &key, pool));
      if (is_cached)
        return SVN_NO_ERROR;
    }

  /* if REV had not been packed when we began, try reading it from the
   * non-packed shard.  If that fails, we will fall through to packed
   * shard reads. */
  if (!is_packed_revprop(fs, rev))
    {
      svn_error_t *err = read_non_packed_revprop(proplist_p, fs, rev,
                                                 generation, pool);
      if (err)
        {
          if (!APR_STATUS_IS_ENOENT(err->apr_err)
              || ffd->format < SVN_FS_FS__MIN_PACKED_REVPROP_FORMAT)
            return svn_error_trace(err);

          svn_error_clear(err);
          *proplist_p = NULL; /* in case read_non_packed_revprop changed it */
        }
    }

  /* if revprop packing is available and we have not read the revprops, yet,
   * try reading them from a packed shard.  If that fails, REV is most
   * likely invalid (or its revprops highly contested). */
  if (ffd->format >= SVN_FS_FS__MIN_PACKED_REVPROP_FORMAT && !*proplist_p)
    {
      packed_revprops_t *packed_revprops;
      SVN_ERR(read_pack_revprop(&packed_revprops, fs, rev, generation, pool));
      *proplist_p = packed_revprops->properties;
    }

  /* The revprops should have been there. Did we get them? */
  if (!*proplist_p)
    return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                             _("Could not read revprops for revision %ld"),
                             rev);

  return SVN_NO_ERROR;
}

/* Serialize the revision property list PROPLIST of revision REV in
 * filesystem FS to a non-packed file.  Return the name of that temporary
 * file in *TMP_PATH and the file path that it must be moved to in
 * *FINAL_PATH.
 * 
 * Use POOL for allocations.
 */
static svn_error_t *
write_non_packed_revprop(const char **final_path,
                         const char **tmp_path,
                         svn_fs_t *fs,
                         svn_revnum_t rev,
                         apr_hash_t *proplist,
                         apr_pool_t *pool)
{
  svn_stream_t *stream;
  *final_path = path_revprops(fs, rev, pool);

  /* ### do we have a directory sitting around already? we really shouldn't
     ### have to get the dirname here. */
  SVN_ERR(svn_stream_open_unique(&stream, tmp_path,
                                 svn_dirent_dirname(*final_path, pool),
                                 svn_io_file_del_none, pool, pool));
  SVN_ERR(svn_hash_write2(proplist, stream, SVN_HASH_TERMINATOR, pool));
  SVN_ERR(svn_stream_close(stream));

  return SVN_NO_ERROR;
}

/* After writing the new revprop file(s), call this function to move the
 * file at TMP_PATH to FINAL_PATH and give it the permissions from
 * PERMS_REFERENCE.
 *
 * If indicated in BUMP_GENERATION, increase FS' revprop generation.
 * Finally, delete all the temporary files given in FILES_TO_DELETE.
 * The latter may be NULL.
 * 
 * Use POOL for temporary allocations.
 */
static svn_error_t *
switch_to_new_revprop(svn_fs_t *fs,
                      const char *final_path,
                      const char *tmp_path,
                      const char *perms_reference,
                      apr_array_header_t *files_to_delete,
                      svn_boolean_t bump_generation,
                      apr_pool_t *pool)
{
  /* Now, we may actually be replacing revprops. Make sure that all other
     threads and processes will know about this. */
  if (bump_generation)
    SVN_ERR(begin_revprop_change(fs, pool));

  SVN_ERR(move_into_place(tmp_path, final_path, perms_reference, pool));

  /* Indicate that the update (if relevant) has been completed. */
  if (bump_generation)
    SVN_ERR(end_revprop_change(fs, pool));

  /* Clean up temporary files, if necessary. */
  if (files_to_delete)
    {
      apr_pool_t *iterpool = svn_pool_create(pool);
      int i;
      
      for (i = 0; i < files_to_delete->nelts; ++i)
        {
          const char *path = APR_ARRAY_IDX(files_to_delete, i, const char*);
          SVN_ERR(svn_io_remove_file2(path, TRUE, iterpool));
          svn_pool_clear(iterpool);
        }

      svn_pool_destroy(iterpool);
    }
  return SVN_NO_ERROR;
}

/* Write a pack file header to STREAM that starts at revision START_REVISION
 * and contains the indexes [START,END) of SIZES.
 */
static svn_error_t *
serialize_revprops_header(svn_stream_t *stream,
                          svn_revnum_t start_revision,
                          apr_array_header_t *sizes,
                          int start,
                          int end,
                          apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  int i;

  SVN_ERR_ASSERT(start < end);

  /* start revision and entry count */
  SVN_ERR(svn_stream_printf(stream, pool, "%ld\n", start_revision));
  SVN_ERR(svn_stream_printf(stream, pool, "%d\n", end - start));

  /* the sizes array */
  for (i = start; i < end; ++i)
    {
      apr_off_t size = APR_ARRAY_IDX(sizes, i, apr_off_t);
      SVN_ERR(svn_stream_printf(stream, iterpool, "%" APR_OFF_T_FMT "\n",
                                size));
    }

  /* the double newline char indicates the end of the header */
  SVN_ERR(svn_stream_printf(stream, iterpool, "\n"));

  svn_pool_clear(iterpool);
  return SVN_NO_ERROR;
}

/* Writes the a pack file to FILE_STREAM.  It copies the serialized data
 * from REVPROPS for the indexes [START,END) except for index CHANGED_INDEX.
 * 
 * The data for the latter is taken from NEW_SERIALIZED.  Note, that
 * CHANGED_INDEX may be outside the [START,END) range, i.e. no new data is
 * taken in that case but only a subset of the old data will be copied.
 *
 * NEW_TOTAL_SIZE is a hint for pre-allocating buffers of appropriate size.
 * POOL is used for temporary allocations.
 */
static svn_error_t *
repack_revprops(svn_fs_t *fs,
                packed_revprops_t *revprops,
                int start,
                int end,
                int changed_index,
                svn_stringbuf_t *new_serialized,
                apr_off_t new_total_size,
                svn_stream_t *file_stream,
                apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_stream_t *stream;
  int i;

  /* create data empty buffers and the stream object */
  svn_stringbuf_t *uncompressed
    = svn_stringbuf_create_ensure((apr_size_t)new_total_size, pool);
  svn_stringbuf_t *compressed
    = svn_stringbuf_create_empty(pool);
  stream = svn_stream_from_stringbuf(uncompressed, pool);

  /* write the header*/
  SVN_ERR(serialize_revprops_header(stream, revprops->start_revision + start,
                                    revprops->sizes, start, end, pool));

  /* append the serialized revprops */
  for (i = start; i < end; ++i)
    if (i == changed_index)
      {
        SVN_ERR(svn_stream_write(stream,
                                 new_serialized->data,
                                 &new_serialized->len));
      }
    else
      {
        apr_size_t size 
            = (apr_size_t)APR_ARRAY_IDX(revprops->sizes, i, apr_off_t);
        apr_size_t offset 
            = (apr_size_t)APR_ARRAY_IDX(revprops->offsets, i, apr_off_t);

        SVN_ERR(svn_stream_write(stream,
                                 revprops->packed_revprops->data + offset,
                                 &size));
      }

  /* flush the stream buffer (if any) to our underlying data buffer */
  SVN_ERR(svn_stream_close(stream));

  /* compress / store the data */
  SVN_ERR(svn__compress(uncompressed,
                        compressed,
                        ffd->compress_packed_revprops
                          ? SVN_DELTA_COMPRESSION_LEVEL_DEFAULT
                          : SVN_DELTA_COMPRESSION_LEVEL_NONE));

  /* finally, write the content to the target stream and close it */
  SVN_ERR(svn_stream_write(file_stream, compressed->data, &compressed->len));
  SVN_ERR(svn_stream_close(file_stream));

  return SVN_NO_ERROR;
}

/* Allocate a new pack file name for the revisions at index [START,END)
 * of REVPROPS->MANIFEST.  Add the name of old file to FILES_TO_DELETE,
 * auto-create that array if necessary.  Return an open file stream to
 * the new file in *STREAM allocated in POOL.
 */
static svn_error_t *
repack_stream_open(svn_stream_t **stream,
                   svn_fs_t *fs,
                   packed_revprops_t *revprops,
                   int start,
                   int end,
                   apr_array_header_t **files_to_delete,
                   apr_pool_t *pool)
{
  apr_int64_t tag;
  const char *tag_string;
  svn_string_t *new_filename;
  int i;
  apr_file_t *file;

  /* get the old (= current) file name and enlist it for later deletion */
  const char *old_filename
    = APR_ARRAY_IDX(revprops->manifest, start, const char*);

  if (*files_to_delete == NULL)
    *files_to_delete = apr_array_make(pool, 3, sizeof(const char*));

  APR_ARRAY_PUSH(*files_to_delete, const char*)
    = svn_dirent_join(revprops->folder, old_filename, pool);

  /* increase the tag part, i.e. the counter after the dot */
  tag_string = strchr(old_filename, '.');
  if (tag_string == NULL)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Packed file '%s' misses a tag"),
                             old_filename);
    
  SVN_ERR(svn_cstring_atoi64(&tag, tag_string + 1));
  new_filename = svn_string_createf(pool, "%ld.%" APR_INT64_T_FMT,
                                    revprops->start_revision + start,
                                    ++tag);

  /* update the manifest to point to the new file */
  for (i = start; i < end; ++i)
    APR_ARRAY_IDX(revprops->manifest, i, const char*) = new_filename->data;

  /* create a file stream for the new file */
  SVN_ERR(svn_io_file_open(&file, svn_dirent_join(revprops->folder,
                                                  new_filename->data,
                                                  pool),
                           APR_WRITE | APR_CREATE, APR_OS_DEFAULT, pool));
  *stream = svn_stream_from_aprfile2(file, FALSE, pool);

  return SVN_NO_ERROR;
}

/* For revision REV in filesystem FS, set the revision properties to
 * PROPLIST.  Return a new file in *TMP_PATH that the caller shall move
 * to *FINAL_PATH to make the change visible.  Files to be deleted will
 * be listed in *FILES_TO_DELETE which may remain unchanged / unallocated.
 * Use POOL for allocations.
 */
static svn_error_t *
write_packed_revprop(const char **final_path,
                     const char **tmp_path,
                     apr_array_header_t **files_to_delete,
                     svn_fs_t *fs,
                     svn_revnum_t rev,
                     apr_hash_t *proplist,
                     apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  packed_revprops_t *revprops;
  apr_int64_t generation = 0;
  svn_stream_t *stream;
  svn_stringbuf_t *serialized;
  apr_off_t new_total_size;
  int changed_index;

  /* read the current revprop generation. This value will not change
   * while we hold the global write lock to this FS. */
  if (has_revprop_cache(fs, pool))
    SVN_ERR(read_revprop_generation(&generation, fs, pool));

  /* read contents of the current pack file */
  SVN_ERR(read_pack_revprop(&revprops, fs, rev, generation, pool));

  /* serialize the new revprops */
  serialized = svn_stringbuf_create_empty(pool);
  stream = svn_stream_from_stringbuf(serialized, pool);
  SVN_ERR(svn_hash_write2(proplist, stream, SVN_HASH_TERMINATOR, pool));
  SVN_ERR(svn_stream_close(stream));

  /* calculate the size of the new data */
  changed_index = (int)(rev - revprops->start_revision);
  new_total_size = revprops->total_size - revprops->serialized_size
                 + serialized->len
                 + (revprops->offsets->nelts + 2) * SVN_INT64_BUFFER_SIZE;

  APR_ARRAY_IDX(revprops->sizes, changed_index, apr_off_t) = serialized->len;

  /* can we put the new data into the same pack as the before? */
  if (   new_total_size < ffd->revprop_pack_size
      || revprops->sizes->nelts == 1)
    {
      /* simply replace the old pack file with new content as we do it
       * in the non-packed case */

      *final_path = svn_dirent_join(revprops->folder, revprops->filename,
                                    pool);
      SVN_ERR(svn_stream_open_unique(&stream, tmp_path, revprops->folder,
                                     svn_io_file_del_none, pool, pool));
      SVN_ERR(repack_revprops(fs, revprops, 0, revprops->sizes->nelts,
                              changed_index, serialized, new_total_size,
                              stream, pool));
    }
  else
    {
      /* split the pack file into two of roughly equal size */
      int right_count, left_count, i;
          
      int left = 0;
      int right = revprops->sizes->nelts - 1;
      apr_off_t left_size = 2 * SVN_INT64_BUFFER_SIZE;
      apr_off_t right_size = 2 * SVN_INT64_BUFFER_SIZE;

      /* let left and right side grow such that their size difference
       * is minimal after each step. */
      while (left <= right)
        if (  left_size + APR_ARRAY_IDX(revprops->sizes, left, apr_off_t)
            < right_size + APR_ARRAY_IDX(revprops->sizes, right, apr_off_t))
          {
            left_size += APR_ARRAY_IDX(revprops->sizes, left, apr_off_t)
                      + SVN_INT64_BUFFER_SIZE;
            ++left;
          }
        else
          {
            right_size += APR_ARRAY_IDX(revprops->sizes, right, apr_off_t)
                        + SVN_INT64_BUFFER_SIZE;
            --right;
          }

       /* since the items need much less than SVN_INT64_BUFFER_SIZE
        * bytes to represent their length, the split may not be optimal */
      left_count = left;
      right_count = revprops->sizes->nelts - left;

      /* if new_size is large, one side may exceed the pack size limit.
       * In that case, split before and after the modified revprop.*/
      if (   left_size > ffd->revprop_pack_size
          || right_size > ffd->revprop_pack_size)
        {
          left_count = changed_index;
          right_count = revprops->sizes->nelts - left_count - 1;
        }

      /* write the new, split files */
      if (left_count)
        {
          SVN_ERR(repack_stream_open(&stream, fs, revprops, 0,
                                     left_count, files_to_delete, pool));
          SVN_ERR(repack_revprops(fs, revprops, 0, left_count,
                                  changed_index, serialized, new_total_size,
                                  stream, pool));
        }

      if (left_count + right_count < revprops->sizes->nelts)
        {
          SVN_ERR(repack_stream_open(&stream, fs, revprops, changed_index,
                                     changed_index + 1, files_to_delete,
                                     pool));
          SVN_ERR(repack_revprops(fs, revprops, changed_index,
                                  changed_index + 1,
                                  changed_index, serialized, new_total_size,
                                  stream, pool));
        }

      if (right_count)
        {
          SVN_ERR(repack_stream_open(&stream, fs, revprops,
                                     revprops->sizes->nelts - right_count,
                                     revprops->sizes->nelts,
                                     files_to_delete, pool));
          SVN_ERR(repack_revprops(fs, revprops,
                                  revprops->sizes->nelts - right_count,
                                  revprops->sizes->nelts, changed_index,
                                  serialized, new_total_size, stream,
                                  pool));
        }

      /* write the new manifest */
      *final_path = svn_dirent_join(revprops->folder, PATH_MANIFEST, pool);
      SVN_ERR(svn_stream_open_unique(&stream, tmp_path, revprops->folder,
                                     svn_io_file_del_none, pool, pool));

      for (i = 0; i < revprops->manifest->nelts; ++i)
        {
          const char *filename = APR_ARRAY_IDX(revprops->manifest, i,
                                               const char*);
          SVN_ERR(svn_stream_printf(stream, pool, "%s\n", filename));
        }

      SVN_ERR(svn_stream_close(stream));
    }
  
  return SVN_NO_ERROR;
}

/* Set the revision property list of revision REV in filesystem FS to
   PROPLIST.  Use POOL for temporary allocations. */
svn_error_t *
set_revision_proplist(svn_fs_t *fs,
                      svn_revnum_t rev,
                      apr_hash_t *proplist,
                      apr_pool_t *pool)
{
  svn_boolean_t is_packed;
  svn_boolean_t bump_generation = FALSE;
  const char *final_path;
  const char *tmp_path;
  const char *perms_reference;
  apr_array_header_t *files_to_delete = NULL;

  SVN_ERR(svn_fs_fs__ensure_revision_exists(rev, fs, pool));

  /* this info will not change while we hold the global FS write lock */
  is_packed = is_packed_revprop(fs, rev);
  
  /* Test whether revprops already exist for this revision.
   * Only then will we need to bump the revprop generation. */
  if (has_revprop_cache(fs, pool))
    {
      if (is_packed)
        {
          bump_generation = TRUE;
        }
      else
        {
          svn_node_kind_t kind;
          SVN_ERR(svn_io_check_path(path_revprops(fs, rev, pool), &kind,
                                    pool));
          bump_generation = kind != svn_node_none;
        }
    }

  /* Serialize the new revprop data */
  if (is_packed)
    SVN_ERR(write_packed_revprop(&final_path, &tmp_path, &files_to_delete,
                                 fs, rev, proplist, pool));
  else
    SVN_ERR(write_non_packed_revprop(&final_path, &tmp_path,
                                     fs, rev, proplist, pool));

  /* We use the rev file of this revision as the perms reference,
   * because when setting revprops for the first time, the revprop
   * file won't exist and therefore can't serve as its own reference.
   * (Whereas the rev file should already exist at this point.)
   */
  perms_reference = svn_fs_fs__path_rev_absolute(fs, rev, pool);

  /* Now, switch to the new revprop data. */
  SVN_ERR(switch_to_new_revprop(fs, final_path, tmp_path, perms_reference,
                                files_to_delete, bump_generation, pool));

  return SVN_NO_ERROR;
}

/* Return TRUE, if for REVISION in FS, we can find the revprop pack file.
 * Use POOL for temporary allocations.
 * Set *MISSING, if the reason is a missing manifest or pack file. 
 */
svn_boolean_t
packed_revprop_available(svn_boolean_t *missing,
                         svn_fs_t *fs,
                         svn_revnum_t revision,
                         apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_stringbuf_t *content = NULL;

  /* try to read the manifest file */
  const char *folder = path_revprops_pack_shard(fs, revision, pool);
  const char *manifest_path = svn_dirent_join(folder, PATH_MANIFEST, pool);

  svn_error_t *err = try_stringbuf_from_file(&content,
                                             missing,
                                             manifest_path,
                                             FALSE,
                                             pool);

  /* if the manifest cannot be read, consider the pack files inaccessible
   * even if the file itself exists. */
  if (err)
    {
      svn_error_clear(err);
      return FALSE;
    }

  if (*missing)
    return FALSE;

  /* parse manifest content until we find the entry for REVISION.
   * Revision 0 is never packed. */
  revision = revision < ffd->max_files_per_dir
           ? revision - 1
           : revision % ffd->max_files_per_dir;
  while (content->data)
    {
      char *next = strchr(content->data, '\n');
      if (next)
        {
          *next = 0;
          ++next;
        }

      if (revision-- == 0)
        {
          /* the respective pack file must exist (and be a file) */
          svn_node_kind_t kind;
          err = svn_io_check_path(svn_dirent_join(folder, content->data,
                                                  pool),
                                  &kind, pool);
          if (err)
            {
              svn_error_clear(err);
              return FALSE;
            }

          *missing = kind == svn_node_none;
          return kind == svn_node_file;
        }

      content->data = next;
    }

  return FALSE;
}


/****** Packing FSFS shards *********/

/* Copy revprop files for revisions [START_REV, END_REV) from SHARD_PATH
 * to the pack file at PACK_FILE_NAME in PACK_FILE_DIR.
 *
 * The file sizes have already been determined and written to SIZES.
 * Please note that this function will be executed while the filesystem
 * has been locked and that revprops files will therefore not be modified
 * while the pack is in progress.
 *
 * COMPRESSION_LEVEL defines how well the resulting pack file shall be
 * compressed or whether is shall be compressed at all.  TOTAL_SIZE is
 * a hint on which initial buffer size we should use to hold the pack file
 * content.
 *
 * CANCEL_FUNC and CANCEL_BATON are used as usual. Temporary allocations
 * are done in SCRATCH_POOL.
 */
svn_error_t *
copy_revprops(const char *pack_file_dir,
              const char *pack_filename,
              const char *shard_path,
              svn_revnum_t start_rev,
              svn_revnum_t end_rev,
              apr_array_header_t *sizes,
              apr_size_t total_size,
              int compression_level,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *scratch_pool)
{
  svn_stream_t *pack_stream;
  apr_file_t *pack_file;
  svn_revnum_t rev;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_stream_t *stream;

  /* create empty data buffer and a write stream on top of it */
  svn_stringbuf_t *uncompressed
    = svn_stringbuf_create_ensure(total_size, scratch_pool);
  svn_stringbuf_t *compressed
    = svn_stringbuf_create_empty(scratch_pool);
  pack_stream = svn_stream_from_stringbuf(uncompressed, scratch_pool);

  /* write the pack file header */
  SVN_ERR(serialize_revprops_header(pack_stream, start_rev, sizes, 0,
                                    sizes->nelts, iterpool));

  /* Some useful paths. */
  SVN_ERR(svn_io_file_open(&pack_file, svn_dirent_join(pack_file_dir,
                                                       pack_filename,
                                                       scratch_pool),
                           APR_WRITE | APR_CREATE, APR_OS_DEFAULT,
                           scratch_pool));

  /* Iterate over the revisions in this shard, squashing them together. */
  for (rev = start_rev; rev <= end_rev; rev++)
    {
      const char *path;

      svn_pool_clear(iterpool);

      /* Construct the file name. */
      path = svn_dirent_join(shard_path, apr_psprintf(iterpool, "%ld", rev),
                             iterpool);

      /* Copy all the bits from the non-packed revprop file to the end of
       * the pack file. */
      SVN_ERR(svn_stream_open_readonly(&stream, path, iterpool, iterpool));
      SVN_ERR(svn_stream_copy3(stream, pack_stream,
                               cancel_func, cancel_baton, iterpool));
    }

  /* flush stream buffers to content buffer */
  SVN_ERR(svn_stream_close(pack_stream));

  /* compress the content (or just store it for COMPRESSION_LEVEL 0) */
  SVN_ERR(svn__compress(uncompressed, compressed, compression_level));

  /* write the pack file content to disk */
  stream = svn_stream_from_aprfile2(pack_file, FALSE, scratch_pool);
  SVN_ERR(svn_stream_write(stream, compressed->data, &compressed->len));
  SVN_ERR(svn_stream_close(stream));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* For the revprop SHARD at SHARD_PATH with exactly MAX_FILES_PER_DIR
 * revprop files in it, create a packed shared at PACK_FILE_DIR.
 *
 * COMPRESSION_LEVEL defines how well the resulting pack file shall be
 * compressed or whether is shall be compressed at all.  Individual pack
 * file containing more than one revision will be limited to a size of
 * MAX_PACK_SIZE bytes before compression.
 *
 * CANCEL_FUNC and CANCEL_BATON are used in the usual way.  Temporary
 * allocations are done in SCRATCH_POOL.
 */
svn_error_t *
pack_revprops_shard(const char *pack_file_dir,
                    const char *shard_path,
                    apr_int64_t shard,
                    int max_files_per_dir,
                    apr_off_t max_pack_size,
                    int compression_level,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *scratch_pool)
{
  const char *manifest_file_path, *pack_filename = NULL;
  svn_stream_t *manifest_stream;
  svn_revnum_t start_rev, end_rev, rev;
  apr_off_t total_size;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *sizes;

  /* Some useful paths. */
  manifest_file_path = svn_dirent_join(pack_file_dir, PATH_MANIFEST,
                                       scratch_pool);

  /* Remove any existing pack file for this shard, since it is incomplete. */
  SVN_ERR(svn_io_remove_dir2(pack_file_dir, TRUE, cancel_func, cancel_baton,
                             scratch_pool));

  /* Create the new directory and manifest file stream. */
  SVN_ERR(svn_io_dir_make(pack_file_dir, APR_OS_DEFAULT, scratch_pool));
  SVN_ERR(svn_stream_open_writable(&manifest_stream, manifest_file_path,
                                   scratch_pool, scratch_pool));

  /* revisions to handle. Special case: revision 0 */
  start_rev = (svn_revnum_t) (shard * max_files_per_dir);
  end_rev = (svn_revnum_t) ((shard + 1) * (max_files_per_dir) - 1);
  if (start_rev == 0)
    ++start_rev;

  /* initialize the revprop size info */
  sizes = apr_array_make(scratch_pool, max_files_per_dir, sizeof(apr_off_t));
  total_size = 2 * SVN_INT64_BUFFER_SIZE;

  /* Iterate over the revisions in this shard, determine their size and
   * squashing them together into pack files. */
  for (rev = start_rev; rev <= end_rev; rev++)
    {
      apr_finfo_t finfo;
      const char *path;

      svn_pool_clear(iterpool);

      /* Get the size of the file. */
      path = svn_dirent_join(shard_path, apr_psprintf(iterpool, "%ld", rev),
                             iterpool);
      SVN_ERR(svn_io_stat(&finfo, path, APR_FINFO_SIZE, iterpool));

      /* if we already have started a pack file and this revprop cannot be
       * appended to it, write the previous pack file. */
      if (sizes->nelts != 0 &&
          total_size + SVN_INT64_BUFFER_SIZE + finfo.size > max_pack_size)
        {
          SVN_ERR(copy_revprops(pack_file_dir, pack_filename, shard_path,
                                start_rev, rev-1, sizes, (apr_size_t)total_size,
                                compression_level, cancel_func, cancel_baton,
                                iterpool));

          /* next pack file starts empty again */
          apr_array_clear(sizes);
          total_size = 2 * SVN_INT64_BUFFER_SIZE;
          start_rev = rev;
        }

      /* Update the manifest. Allocate a file name for the current pack
       * file if it is a new one */
      if (sizes->nelts == 0)
        pack_filename = apr_psprintf(scratch_pool, "%ld.0", rev);

      SVN_ERR(svn_stream_printf(manifest_stream, iterpool, "%s\n",
                                pack_filename));

      /* add to list of files to put into the current pack file */
      APR_ARRAY_PUSH(sizes, apr_off_t) = finfo.size;
      total_size += SVN_INT64_BUFFER_SIZE + finfo.size;
    }

  /* write the last pack file */
  if (sizes->nelts != 0)
    SVN_ERR(copy_revprops(pack_file_dir, pack_filename, shard_path,
                          start_rev, rev-1, sizes, (apr_size_t)total_size,
                          compression_level, cancel_func, cancel_baton,
                          iterpool));

  /* flush the manifest file and update permissions */
  SVN_ERR(svn_stream_close(manifest_stream));
  SVN_ERR(svn_io_copy_perms(shard_path, pack_file_dir, iterpool));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Delete the non-packed revprop SHARD at SHARD_PATH with exactly
 * MAX_FILES_PER_DIR revprop files in it.  If this is shard 0, keep the
 * revprop file for revision 0.
 *
 * CANCEL_FUNC and CANCEL_BATON are used in the usual way.  Temporary
 * allocations are done in SCRATCH_POOL.
 */
svn_error_t *
delete_revprops_shard(const char *shard_path,
                      apr_int64_t shard,
                      int max_files_per_dir,
                      svn_cancel_func_t cancel_func,
                      void *cancel_baton,
                      apr_pool_t *scratch_pool)
{
  if (shard == 0)
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      int i;

      /* delete all files except the one for revision 0 */
      for (i = 1; i < max_files_per_dir; ++i)
        {
          const char *path = svn_dirent_join(shard_path,
                                       apr_psprintf(iterpool, "%d", i),
                                       iterpool);
          if (cancel_func)
            SVN_ERR((*cancel_func)(cancel_baton));

          SVN_ERR(svn_io_remove_file2(path, TRUE, iterpool));
          svn_pool_clear(iterpool);
        }

      svn_pool_destroy(iterpool);
    }
  else
    SVN_ERR(svn_io_remove_dir2(shard_path, TRUE,
                               cancel_func, cancel_baton, scratch_pool));

  return SVN_NO_ERROR;
}

