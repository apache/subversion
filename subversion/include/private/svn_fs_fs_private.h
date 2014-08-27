/*
 * svn_fs_fs_private.h: Private declarations for the FSFS back-end internals
 * be consumed by the low-level svnfsfs tool.  A hodgepodge of declarations
 * from various areas.
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

#ifndef SVN_FS_FS_PRIVATE_H
#define SVN_FS_FS_PRIVATE_H

#include "svn_fs.h"

#include "private/svn_atomic.h"
#include "private/svn_cache.h"
#include "private/svn_mutex.h"
#include "private/svn_named_atomic.h"
#include "private/svn_sqlite.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* ---------------------------------------------------------------------
 *
 * Private exports global data structures.
 */

/* Node-revision IDs in FSFS consist of 3 of sub-IDs ("parts") that consist
 * of a creation REVISION number and some revision- / transaction-local
 * counter value (NUMBER).  Old-style ID parts use global counter values.
 *
 * The parts are: node_id, copy_id and txn_id for in-txn IDs as well as
 * node_id, copy_id and rev_offset for in-revision IDs.  This struct the
 * data structure used for each of those parts.
 */
typedef struct svn_fs_fs__id_part_t
{
  /* SVN_INVALID_REVNUM for txns -> not a txn, COUNTER must be 0.
     SVN_INVALID_REVNUM for others -> not assigned to a revision, yet.
     0                  for others -> old-style ID or the root in rev 0. */
  svn_revnum_t revision;

  /* sub-id value relative to REVISION.  Its interpretation depends on
     the part itself.  In rev_item, it is the index_index value, in others
     it represents a unique counter value. */
  apr_uint64_t number;
} svn_fs_fs__id_part_t;


/* Private FSFS-specific data shared between all svn_txn_t objects that
   relate to a particular transaction in a filesystem (as identified
   by transaction id and filesystem UUID).  Objects of this type are
   allocated in their own subpool of the common pool. */
typedef struct fs_fs_shared_txn_data_t
{
  /* The next transaction in the list, or NULL if there is no following
     transaction. */
  struct fs_fs_shared_txn_data_t *next;

  /* ID of this transaction. */
  svn_fs_fs__id_part_t txn_id;

  /* Whether the transaction's prototype revision file is locked for
     writing by any thread in this process (including the current
     thread; recursive locks are not permitted).  This is effectively
     a non-recursive mutex. */
  svn_boolean_t being_written;

  /* The pool in which this object has been allocated; a subpool of the
     common pool. */
  apr_pool_t *pool;
} fs_fs_shared_txn_data_t;

/* Private FSFS-specific data shared between all svn_fs_t objects that
   relate to a particular filesystem, as identified by filesystem UUID.
   Objects of this type are allocated in the common pool. */
typedef struct fs_fs_shared_data_t
{
  /* A list of shared transaction objects for each transaction that is
     currently active, or NULL if none are.  All access to this list,
     including the contents of the objects stored in it, is synchronised
     under TXN_LIST_LOCK. */
  fs_fs_shared_txn_data_t *txns;

  /* A free transaction object, or NULL if there is no free object.
     Access to this object is synchronised under TXN_LIST_LOCK. */
  fs_fs_shared_txn_data_t *free_txn;

  /* The following lock must be taken out in reverse order of their
     declaration here.  Any subset may be acquired and held at any given
     time but their relative acquisition order must not change.

     (lock 'txn-current' before 'pack' before 'write' before 'txn-list') */

  /* A lock for intra-process synchronization when accessing the TXNS list. */
  svn_mutex__t *txn_list_lock;

  /* A lock for intra-process synchronization when grabbing the
     repository write lock. */
  svn_mutex__t *fs_write_lock;

  /* A lock for intra-process synchronization when grabbing the
     repository pack operation lock. */
  svn_mutex__t *fs_pack_lock;

  /* A lock for intra-process synchronization when locking the
     txn-current file. */
  svn_mutex__t *txn_current_lock;

  /* The common pool, under which this object is allocated, subpools
     of which are used to allocate the transaction objects. */
  apr_pool_t *common_pool;
} fs_fs_shared_data_t;

/* Data structure for the 1st level DAG node cache. */
typedef struct fs_fs_dag_cache_t fs_fs_dag_cache_t;

/* Private (non-shared) FSFS-specific data for each svn_fs_t object.
   Any caches in here may be NULL. */
typedef struct fs_fs_data_t
{
  /* The format number of this FS. */
  int format;

  /* The maximum number of files to store per directory (for sharded
     layouts) or zero (for linear layouts). */
  int max_files_per_dir;

  /* The first revision that uses logical addressing.  SVN_INVALID_REVNUM
     if there is no such revision (pre-f7 or non-sharded).  May be a
     future revision if the current shard started with physical addressing
     and is not complete, yet. */
  svn_revnum_t min_log_addressing_rev;

  /* Rev / pack file read granularity in bytes. */
  apr_int64_t block_size;

  /* Capacity in entries of log-to-phys index pages */
  apr_int64_t l2p_page_size;

  /* Rev / pack file granularity (in bytes) covered by a single phys-to-log
   * index page. */
  apr_int64_t p2l_page_size;

  /* If set, parse and cache *all* data of each block that we read
   * (not just the one bit that we need, atm). */
  svn_boolean_t use_block_read;

  /* The revision that was youngest, last time we checked. */
  svn_revnum_t youngest_rev_cache;

  /* Caches of immutable data.  (Note that these may be shared between
     multiple svn_fs_t's for the same filesystem.) */

  /* Access to the configured memcached instances.  May be NULL. */
  svn_memcache_t *memcache;

  /* If TRUE, don't ignore any cache-related errors.  If FALSE, errors from
     e.g. memcached may be ignored as caching is an optional feature. */
  svn_boolean_t fail_stop;

  /* A cache of revision root IDs, mapping from (svn_revnum_t *) to
     (svn_fs_id_t *).  (Not threadsafe.) */
  svn_cache__t *rev_root_id_cache;

  /* Caches native dag_node_t* instances and acts as a 1st level cache */
  fs_fs_dag_cache_t *dag_node_cache;

  /* DAG node cache for immutable nodes.  Maps (revision, fspath)
     to (dag_node_t *). This is the 2nd level cache for DAG nodes. */
  svn_cache__t *rev_node_cache;

  /* A cache of the contents of immutable directories; maps from
     unparsed FS ID to a apr_hash_t * mapping (const char *) dirent
     names to (svn_fs_dirent_t *). */
  svn_cache__t *dir_cache;

  /* Fulltext cache; currently only used with memcached.  Maps from
     rep key (revision/offset) to svn_stringbuf_t. */
  svn_cache__t *fulltext_cache;

  /* Access object to the atomics namespace used by revprop caching.
     Will be NULL until the first access. */
  svn_atomic_namespace__t *revprop_namespace;

  /* Access object to the revprop "generation". Will be NULL until
     the first access. */
  svn_named_atomic__t *revprop_generation;

  /* Access object to the revprop update timeout. Will be NULL until
     the first access. */
  svn_named_atomic__t *revprop_timeout;

  /* Revision property cache.  Maps from (rev,generation) to apr_hash_t. */
  svn_cache__t *revprop_cache;

  /* Node properties cache.  Maps from rep key to apr_hash_t. */
  svn_cache__t *properties_cache;

  /* Pack manifest cache; a cache mapping (svn_revnum_t) shard number to
     a manifest; and a manifest is a mapping from (svn_revnum_t) revision
     number offset within a shard to (apr_off_t) byte-offset in the
     respective pack file. */
  svn_cache__t *packed_offset_cache;

  /* Cache for svn_fs_fs__raw_cached_window_t objects; the key is
     window_cache_key_t. */
  svn_cache__t *raw_window_cache;

  /* Cache for txdelta_window_t objects; the key is window_cache_key_t */
  svn_cache__t *txdelta_window_cache;

  /* Cache for combined windows as svn_stringbuf_t objects;
     the key is window_cache_key_t */
  svn_cache__t *combined_window_cache;

  /* Cache for node_revision_t objects; the key is (revision, item_index) */
  svn_cache__t *node_revision_cache;

  /* Cache for change lists as APR arrays of change_t * objects; the key
     is the revision */
  svn_cache__t *changes_cache;

  /* Cache for svn_fs_fs__rep_header_t objects; the key is a
     (revision, item index) pair */
  svn_cache__t *rep_header_cache;

  /* Cache for svn_mergeinfo_t objects; the key is a combination of
     revision, inheritance flags and path. */
  svn_cache__t *mergeinfo_cache;

  /* Cache for presence of svn_mergeinfo_t on a noderev; the key is a
     combination of revision, inheritance flags and path; value is "1"
     if the node has mergeinfo, "0" if it doesn't. */
  svn_cache__t *mergeinfo_existence_cache;

  /* Cache for l2p_header_t objects; the key is (revision, is-packed).
     Will be NULL for pre-format7 repos */
  svn_cache__t *l2p_header_cache;

  /* Cache for l2p_page_t objects; the key is svn_fs_fs__page_cache_key_t.
     Will be NULL for pre-format7 repos */
  svn_cache__t *l2p_page_cache;

  /* Cache for p2l_header_t objects; the key is (revision, is-packed).
     Will be NULL for pre-format7 repos */
  svn_cache__t *p2l_header_cache;

  /* Cache for apr_array_header_t objects containing svn_fs_fs__p2l_entry_t
     elements; the key is svn_fs_fs__page_cache_key_t.
     Will be NULL for pre-format7 repos */
  svn_cache__t *p2l_page_cache;

  /* TRUE while the we hold a lock on the write lock file. */
  svn_boolean_t has_write_lock;

  /* If set, there are or have been more than one concurrent transaction */
  svn_boolean_t concurrent_transactions;

  /* Temporary cache for changed directories yet to be committed; maps from
     unparsed FS ID to ###x.  NULL outside transactions. */
  svn_cache__t *txn_dir_cache;

  /* Data shared between all svn_fs_t objects for a given filesystem. */
  fs_fs_shared_data_t *shared;

  /* The sqlite database used for rep caching. */
  svn_sqlite__db_t *rep_cache_db;

  /* Thread-safe boolean */
  svn_atomic_t rep_cache_db_opened;

  /* The oldest revision not in a pack file.  It also applies to revprops
   * if revprop packing has been enabled by the FSFS format version. */
  svn_revnum_t min_unpacked_rev;

  /* Whether rep-sharing is supported by the filesystem
   * and allowed by the configuration. */
  svn_boolean_t rep_sharing_allowed;

  /* File size limit in bytes up to which multiple revprops shall be packed
   * into a single file. */
  apr_int64_t revprop_pack_size;

  /* Whether packed revprop files shall be compressed. */
  svn_boolean_t compress_packed_revprops;

  /* Whether directory nodes shall be deltified just like file nodes. */
  svn_boolean_t deltify_directories;

  /* Whether nodes properties shall be deltified. */
  svn_boolean_t deltify_properties;

  /* Restart deltification histories after each multiple of this value */
  apr_int64_t max_deltification_walk;

  /* Maximum number of length of the linear part at the top of the
   * deltification history after which skip deltas will be used. */
  apr_int64_t max_linear_deltification;

  /* Compression level to use with txdelta storage format in new revs. */
  int delta_compression_level;

  /* Pack after every commit. */
  svn_boolean_t pack_after_commit;

  /* Per-instance filesystem ID, which provides an additional level of
     uniqueness for filesystems that share the same UUID, but should
     still be distinguishable (e.g. backups produced by svn_fs_hotcopy()
     or dump / load cycles). */
  const char *instance_id;

  /* Pointer to svn_fs_open. */
  svn_error_t *(*svn_fs_open_)(svn_fs_t **, const char *, apr_hash_t *,
                               apr_pool_t *, apr_pool_t *);
} fs_fs_data_t;

/* ---------------------------------------------------------------------
 *
 * Private exports from the svn_fs_fs__revision_file_t interface.
 */

/* In format 7, index files must be read in sync with the respective
 * revision / pack file.  I.e. we must use packed index files for packed
 * rev files and unpacked ones for non-packed rev files.  So, the whole
 * point is to open them with matching "is packed" setting in case some
 * background pack process was run.
 */

/* Opaque index stream type.
 */
typedef struct svn_fs_fs__packed_number_stream_t
  svn_fs_fs__packed_number_stream_t;

/* Data file, including indexes data, and associated properties for
 * START_REVISION.  As the FILE is kept open, background pack operations
 * will not cause access to this file to fail.
 */
typedef struct svn_fs_fs__revision_file_t
{
  /* first (potentially only) revision in the rev / pack file.
   * SVN_INVALID_REVNUM for txn proto-rev files. */
  svn_revnum_t start_revision;

  /* the revision was packed when the first file / stream got opened */
  svn_boolean_t is_packed;

  /* rev / pack file */
  apr_file_t *file;

  /* stream based on FILE and not NULL exactly when FILE is not NULL */
  svn_stream_t *stream;

  /* the opened P2L index stream or NULL.  Always NULL for txns. */
  svn_fs_fs__packed_number_stream_t *p2l_stream;

  /* the opened L2P index stream or NULL.  Always NULL for txns. */
  svn_fs_fs__packed_number_stream_t *l2p_stream;

  /* Copied from FS->FFD->BLOCK_SIZE upon creation.  It allows us to
   * use aligned seek() without having the FS handy. */
  apr_off_t block_size;

  /* Offset within FILE at which the rev data ends and the L2P index
   * data starts. Less than P2L_OFFSET. -1 if svn_fs_fs__auto_read_footer
   * has not been called, yet. */
  apr_off_t l2p_offset;

  /* Offset within FILE at which the L2P index ends and the P2L index
   * data starts. Greater than L2P_OFFSET. -1 if svn_fs_fs__auto_read_footer
   * has not been called, yet. */
  apr_off_t p2l_offset;

  /* Offset within FILE at which the P2L index ends and the footer starts.
   * Greater than P2L_OFFSET. -1 if svn_fs_fs__auto_read_footer has not
   * been called, yet. */
  apr_off_t footer_offset;

  /* pool containing this object */
  apr_pool_t *pool;
} svn_fs_fs__revision_file_t;

/* Open the correct revision file for REV.  If the filesystem FS has
 * been packed, *FILE will be set to the packed file; otherwise, set *FILE
 * to the revision file for REV.  Return SVN_ERR_FS_NO_SUCH_REVISION if the
 * file doesn't exist.  Allocate *FILE in RESULT_POOL and use SCRATCH_POOL
 * for temporaries. */
svn_error_t *
svn_fs_fs__open_pack_or_rev_file(svn_fs_fs__revision_file_t **file,
                                 svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);

/* Open the correct revision file for REV with read and write access.
 * If necessary, temporarily reset the file's read-only state.  If the
 * filesystem FS has been packed, *FILE will be set to the packed file;
 * otherwise, set *FILE to the revision file for REV.
 *
 * Return SVN_ERR_FS_NO_SUCH_REVISION if the file doesn't exist.
 * Allocate *FILE in RESULT_POOL and use SCRATCH_POOLfor temporaries. */
svn_error_t *
svn_fs_fs__open_pack_or_rev_file_writable(svn_fs_fs__revision_file_t **file,
                                          svn_fs_t *fs,
                                          svn_revnum_t rev,
                                          apr_pool_t *result_pool,
                                          apr_pool_t *scratch_pool);

/* If the footer data in FILE has not been read, yet, do so now.
 * Index locations will only be read upon request as we assume they get
 * cached and the FILE is usually used for REP data access only.
 * Hence, the separate step.
 */
svn_error_t *
svn_fs_fs__auto_read_footer(svn_fs_fs__revision_file_t *file);

/* Close all files and streams in FILE.
 */
svn_error_t *
svn_fs_fs__close_revision_file(svn_fs_fs__revision_file_t *file);

/* ---------------------------------------------------------------------
 *
 * Private exports from the index interface.
 */

/* Per-defined item index values.  They are used to identify empty or
 * mandatory items.
 */
#define SVN_FS_FS__ITEM_INDEX_UNUSED     0  /* invalid / reserved value */
#define SVN_FS_FS__ITEM_INDEX_CHANGES    1  /* list of changed paths */
#define SVN_FS_FS__ITEM_INDEX_ROOT_NODE  2  /* the root noderev */
#define SVN_FS_FS__ITEM_INDEX_FIRST_USER 3  /* first noderev to be freely
                                               assigned */

/* Data / item types as stored in the phys-to-log index.
 */
#define SVN_FS_FS__ITEM_TYPE_UNUSED     0  /* file section not used */
#define SVN_FS_FS__ITEM_TYPE_FILE_REP   1  /* item is a file representation */
#define SVN_FS_FS__ITEM_TYPE_DIR_REP    2  /* item is a directory rep. */
#define SVN_FS_FS__ITEM_TYPE_FILE_PROPS 3  /* item is a file property rep. */
#define SVN_FS_FS__ITEM_TYPE_DIR_PROPS  4  /* item is a directory prop rep */
#define SVN_FS_FS__ITEM_TYPE_NODEREV    5  /* item is a noderev */
#define SVN_FS_FS__ITEM_TYPE_CHANGES    6  /* item is a changed paths list */

#define SVN_FS_FS__ITEM_TYPE_ANY_REP    7  /* item is any representation.
                                              Only used in pre-format7. */

/* (user visible) entry in the phys-to-log index.  It describes a section
 * of some packed / non-packed rev file as containing a specific item.
 * There must be no overlapping / conflicting entries.
 */
typedef struct svn_fs_fs__p2l_entry_t
{
  /* offset of the first byte that belongs to the item */
  apr_off_t offset;
  
  /* length of the item in bytes */
  apr_off_t size;

  /* type of the item (see SVN_FS_FS__ITEM_TYPE_*) defines */
  unsigned type;

  /* modified FNV-1a checksum.  0 if unknown checksum */
  apr_uint32_t fnv1_checksum;

  /* item in that block */
  svn_fs_fs__id_part_t item;
} svn_fs_fs__p2l_entry_t;

/* Use the phys-to-log mapping files in FS to build a list of entries
 * that (at least partly) overlap with the range given by BLOCK_START
 * offset and BLOCK_SIZE in the rep / pack file containing REVISION.
 * Return the array in *ENTRIES with svn_fs_fs__p2l_entry_t as elements.
 * REV_FILE determines whether to access single rev or pack file data.
 * If that is not available anymore (neither in cache nor on disk),
 * return an error.  Use POOL for allocations.
 *
 * Note that (only) the first and the last mapping may cross a cluster
 * boundary.
 */
svn_error_t *
svn_fs_fs__p2l_index_lookup(apr_array_header_t **entries,
                            svn_fs_t *fs,
                            svn_fs_fs__revision_file_t *rev_file,
                            svn_revnum_t revision,
                            apr_off_t block_start,
                            apr_off_t block_size,
                            apr_pool_t *pool);

/* In *OFFSET, return the last OFFSET in the pack / rev file containing.
 * REV_FILE determines whether to access single rev or pack file data.
 * If that is not available anymore (neither in cache nor on disk), re-open
 * the rev / pack file and retry to open the index file.
 * Use POOL for allocations.
 */
svn_error_t *
svn_fs_fs__p2l_get_max_offset(apr_off_t *offset,
                              svn_fs_t *fs,
                              svn_fs_fs__revision_file_t *rev_file,
                              svn_revnum_t revision,
                              apr_pool_t *pool);

/* Index (re-)creation utilities.
 */

/* For FS, create a new L2P auto-deleting proto index file in POOL and return
 * its name in *PROTONAME.  All entries to write are given in ENTRIES and
 * entries are of type svn_fs_fs__p2l_entry_t* (sic!).  The ENTRIES array
 * will be reordered.  Give the proto index file the lifetime of RESULT_POOL
 * and use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_fs__l2p_index_from_p2l_entries(const char **protoname,
                                      svn_fs_t *fs,
                                      apr_array_header_t *entries,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool);

/* For FS, create a new P2L auto-deleting proto index file in POOL and return
 * its name in *PROTONAME.  All entries to write are given in ENTRIES and
 * of type svn_fs_fs__p2l_entry_t*.  The FVN1 checksums are not taken from
 * ENTRIES but are begin calculated from the current contents of REV_FILE
 * as we go.  Give the proto index file the lifetime of RESULT_POOL and use
 * SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_fs__p2l_index_from_p2l_entries(const char **protoname,
                                      svn_fs_t *fs,
                                      svn_fs_fs__revision_file_t *rev_file,
                                      apr_array_header_t *entries,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool);

/* Append the L2P and P2L indexes given by their proto index file names
 * L2P_PROTO_INDEX and P2L_PROTO_INDEX to the revision / pack FILE.
 * The latter contains revision(s) starting at REVISION in FS.
 * Use POOL for temporary allocations.  */
svn_error_t *
svn_fs_fs__add_index_data(svn_fs_t *fs,
                          apr_file_t *file,
                          const char *l2p_proto_index,
                          const char *p2l_proto_index,
                          svn_revnum_t revision,
                          apr_pool_t *pool);

/* ---------------------------------------------------------------------
 *
 * Private exports from the pack interface.
 */

/**
 * For the packed revision @a rev in @a fs,  determine the offset within
 * the revision pack file and return it in @a rev_offset.  Use @a pool for
 * allocations.
 */
svn_error_t *
svn_fs_fs__get_packed_offset(apr_off_t *rev_offset,
                             svn_fs_t *fs,
                             svn_revnum_t rev,
                             apr_pool_t *pool);

/* ---------------------------------------------------------------------
 *
 * Private exports from the utilities section.
 */

/* Return TRUE, iff revision REV in FS requires logical addressing. */
svn_boolean_t
svn_fs_fs__use_log_addressing(svn_fs_t *fs,
                              svn_revnum_t rev);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_FS_FS_PRIVATE_H */
