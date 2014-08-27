/* fs.h : interface to Subversion filesystem, private to libsvn_fs
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

#ifndef SVN_LIBSVN_FS_FS_H
#define SVN_LIBSVN_FS_FS_H

#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_network_io.h>
#include <apr_md5.h>
#include <apr_sha1.h>

#include "svn_config.h"
#include "private/svn_fs_private.h"
#include "private/svn_fs_fs_private.h"

#include "id.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** The filesystem structure.  ***/

/* Following are defines that specify the textual elements of the
   native filesystem directories and revision files. */

/* Names of special files in the fs_fs filesystem. */
#define PATH_FORMAT           "format"           /* Contains format number */
#define PATH_UUID             "uuid"             /* Contains UUID */
#define PATH_CURRENT          "current"          /* Youngest revision */
#define PATH_LOCK_FILE        "write-lock"       /* Revision lock file */
#define PATH_PACK_LOCK_FILE   "pack-lock"        /* Pack lock file */
#define PATH_REVS_DIR         "revs"             /* Directory of revisions */
#define PATH_REVPROPS_DIR     "revprops"         /* Directory of revprops */
#define PATH_TXNS_DIR         "transactions"     /* Directory of transactions */
#define PATH_NODE_ORIGINS_DIR "node-origins"     /* Lazy node-origin cache */
#define PATH_TXN_PROTOS_DIR   "txn-protorevs"    /* Directory of proto-revs */
#define PATH_TXN_CURRENT      "txn-current"      /* File with next txn key */
#define PATH_TXN_CURRENT_LOCK "txn-current-lock" /* Lock for txn-current */
#define PATH_LOCKS_DIR        "locks"            /* Directory of locks */
#define PATH_MIN_UNPACKED_REV "min-unpacked-rev" /* Oldest revision which
                                                    has not been packed. */
#define PATH_REVPROP_GENERATION "revprop-generation"
                                                 /* Current revprop generation*/
#define PATH_MANIFEST         "manifest"         /* Manifest file name */
#define PATH_PACKED           "pack"             /* Packed revision data file */
#define PATH_EXT_PACKED_SHARD ".pack"            /* Extension for packed
                                                    shards */
#define PATH_EXT_L2P_INDEX    ".l2p"             /* extension of the log-
                                                    to-phys index */
#define PATH_EXT_P2L_INDEX    ".p2l"             /* extension of the phys-
                                                    to-log index */
/* If you change this, look at tests/svn_test_fs.c(maybe_install_fsfs_conf) */
#define PATH_CONFIG           "fsfs.conf"        /* Configuration */

/* Names of special files and file extensions for transactions */
#define PATH_CHANGES       "changes"       /* Records changes made so far */
#define PATH_TXN_PROPS     "props"         /* Transaction properties */
#define PATH_TXN_PROPS_FINAL "props-final" /* Final transaction properties
                                              before moving to revprops */
#define PATH_NEXT_IDS      "next-ids"      /* Next temporary ID assignments */
#define PATH_PREFIX_NODE   "node."         /* Prefix for node filename */
#define PATH_EXT_TXN       ".txn"          /* Extension of txn dir */
#define PATH_EXT_CHILDREN  ".children"     /* Extension for dir contents */
#define PATH_EXT_PROPS     ".props"        /* Extension for node props */
#define PATH_EXT_REV       ".rev"          /* Extension of protorev file */
#define PATH_EXT_REV_LOCK  ".rev-lock"     /* Extension of protorev lock file */
#define PATH_TXN_ITEM_INDEX "itemidx"      /* File containing the current item
                                              index number */
#define PATH_INDEX          "index"        /* name of index files w/o ext */

/* Names of files in legacy FS formats */
#define PATH_REV           "rev"           /* Proto rev file */
#define PATH_REV_LOCK      "rev-lock"      /* Proto rev (write) lock file */

/* Names of sections and options in fsfs.conf. */
#define CONFIG_SECTION_CACHES            "caches"
#define CONFIG_OPTION_FAIL_STOP          "fail-stop"
#define CONFIG_SECTION_REP_SHARING       "rep-sharing"
#define CONFIG_OPTION_ENABLE_REP_SHARING "enable-rep-sharing"
#define CONFIG_SECTION_DELTIFICATION     "deltification"
#define CONFIG_OPTION_ENABLE_DIR_DELTIFICATION   "enable-dir-deltification"
#define CONFIG_OPTION_ENABLE_PROPS_DELTIFICATION "enable-props-deltification"
#define CONFIG_OPTION_MAX_DELTIFICATION_WALK     "max-deltification-walk"
#define CONFIG_OPTION_MAX_LINEAR_DELTIFICATION   "max-linear-deltification"
#define CONFIG_OPTION_COMPRESSION_LEVEL  "compression-level"
#define CONFIG_SECTION_PACKED_REVPROPS   "packed-revprops"
#define CONFIG_OPTION_REVPROP_PACK_SIZE  "revprop-pack-size"
#define CONFIG_OPTION_COMPRESS_PACKED_REVPROPS  "compress-packed-revprops"
#define CONFIG_SECTION_IO                "io"
#define CONFIG_OPTION_BLOCK_SIZE         "block-size"
#define CONFIG_OPTION_L2P_PAGE_SIZE      "l2p-page-size"
#define CONFIG_OPTION_P2L_PAGE_SIZE      "p2l-page-size"
#define CONFIG_SECTION_DEBUG             "debug"
#define CONFIG_OPTION_PACK_AFTER_COMMIT  "pack-after-commit"

/* The format number of this filesystem.
   This is independent of the repository format number, and
   independent of any other FS back ends.

   Note: If you bump this, please update the switch statement in
         svn_fs_fs__create() as well.
 */
#define SVN_FS_FS__FORMAT_NUMBER   7

/* The minimum format number that supports svndiff version 1.  */
#define SVN_FS_FS__MIN_SVNDIFF1_FORMAT 2

/* The minimum format number that supports transaction ID generation
   using a transaction sequence in the txn-current file. */
#define SVN_FS_FS__MIN_TXN_CURRENT_FORMAT 3

/* The minimum format number that supports the "layout" filesystem
   format option. */
#define SVN_FS_FS__MIN_LAYOUT_FORMAT_OPTION_FORMAT 3

/* The minimum format number that stores protorevs in a separate directory. */
#define SVN_FS_FS__MIN_PROTOREVS_DIR_FORMAT 3

/* The minimum format number that doesn't keep node and copy ID counters. */
#define SVN_FS_FS__MIN_NO_GLOBAL_IDS_FORMAT 3

/* The minimum format number that maintains minfo-here and minfo-count
   noderev fields. */
#define SVN_FS_FS__MIN_MERGEINFO_FORMAT 3

/* The minimum format number that allows rep sharing. */
#define SVN_FS_FS__MIN_REP_SHARING_FORMAT 4

/* The minimum format number that supports packed shards. */
#define SVN_FS_FS__MIN_PACKED_FORMAT 4

/* The minimum format number that stores node kinds in changed-paths lists. */
#define SVN_FS_FS__MIN_KIND_IN_CHANGED_FORMAT 4

/* 1.8 deltification options should work with any FSFS repo but to avoid
 * issues with very old servers, restrict those options to the 1.6+ format*/
#define SVN_FS_FS__MIN_DELTIFICATION_FORMAT 4

/* The 1.7-dev format, never released, that packed revprops into SQLite
   revprops.db . */
#define SVN_FS_FS__PACKED_REVPROP_SQLITE_DEV_FORMAT 5

/* The minimum format number that supports packed revprops. */
#define SVN_FS_FS__MIN_PACKED_REVPROP_FORMAT 6

/* The minimum format number that supports packed revprops. */
#define SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT 7

/* Minimum format number that providing a separate lock file for pack ops */
#define SVN_FS_FS__MIN_PACK_LOCK_FORMAT 7

/* Minimum format number that stores mergeinfo-mode flag in changed paths */
#define SVN_FS_FS__MIN_MERGEINFO_IN_CHANGES_FORMAT 7

/* Minimum format number that supports per-instance filesystem IDs. */
#define SVN_FS_FS__MIN_INSTANCE_ID_FORMAT 7

/* The minimum format number that supports a configuration file (fsfs.conf) */
#define SVN_FS_FS__MIN_CONFIG_FILE 4

/* Key type for all caches that use revision + offset / counter as key.

   Note: Cache keys should be 16 bytes for best performance and there
         should be no padding. */
typedef struct pair_cache_key_t
{
  /* The object's revision.  Use the 64 data type to prevent padding. */
  apr_int64_t revision;

  /* Sub-address: item index, revprop generation, packed flag, etc. */
  apr_int64_t second;
} pair_cache_key_t;

/* Key type that identifies a txdelta window.

   Note: Cache keys should require no padding. */
typedef struct window_cache_key_t
{
  /* The object's revision.  Use the 64 data type to prevent padding. */
  apr_int64_t revision;

  /* Window number within that representation. */
  apr_int64_t chunk_index;

  /* Item index of the representation */
  apr_uint64_t item_index;
} window_cache_key_t;

/*** Filesystem Transaction ***/
typedef struct transaction_t
{
  /* property list (const char * name, svn_string_t * value).
     may be NULL if there are no properties.  */
  apr_hash_t *proplist;

  /* node revision id of the root node.  */
  const svn_fs_id_t *root_id;

  /* node revision id of the node which is the root of the revision
     upon which this txn is base.  (unfinished only) */
  const svn_fs_id_t *base_id;

  /* copies list (const char * copy_ids), or NULL if there have been
     no copies in this transaction.  */
  apr_array_header_t *copies;

} transaction_t;


/*** Representation ***/
/* If you add fields to this, check to see if you need to change
 * svn_fs_fs__rep_copy. */
typedef struct representation_t
{
  /* Checksums digests for the contents produced by this representation.
     This checksum is for the contents the rep shows to consumers,
     regardless of how the rep stores the data under the hood.  It is
     independent of the storage (fulltext, delta, whatever).

     If has_sha1 is FALSE, then for compatibility behave as though this
     checksum matches the expected checksum.

     The md5 checksum is always filled, unless this is rep which was
     retrieved from the rep-cache.  The sha1 checksum is only computed on
     a write, for use with rep-sharing. */
  svn_boolean_t has_sha1;
  unsigned char sha1_digest[APR_SHA1_DIGESTSIZE];
  unsigned char md5_digest[APR_MD5_DIGESTSIZE];

  /* Revision where this representation is located. */
  svn_revnum_t revision;

  /* Item index with the the revision. */
  apr_uint64_t item_index;

  /* The size of the representation in bytes as seen in the revision
     file. */
  svn_filesize_t size;

  /* The size of the fulltext of the representation. If this is 0,
   * the fulltext size is equal to representation size in the rev file, */
  svn_filesize_t expanded_size;

  /* Is this a representation (still) within a transaction? */
  svn_fs_fs__id_part_t txn_id;

  /* For rep-sharing, we need a way of uniquifying node-revs which share the
     same representation (see svn_fs_fs__noderev_same_rep_key() ).  So, we
     store the original txn of the node rev (not the rep!), along with some
     intra-node uniqification content.

     This is no longer used by the 1.9 code but we have to keep
     reading and writing it for old formats to remain compatible with
     1.8, and earlier, that require it.  We also read/write it in
     format 7 even though it is not currently required by any code
     that handles that format. */
  struct
    {
      /* unique context, i.e. txn ID, in which the noderev (!) got created */
      svn_fs_fs__id_part_t noderev_txn_id;

      /* unique value within that txn */
      apr_uint64_t number;
    } uniquifier;
} representation_t;


/*** Node-Revision ***/
/* If you add fields to this, check to see if you need to change
 * copy_node_revision in dag.c. */
typedef struct node_revision_t
{
  /* node kind */
  svn_node_kind_t kind;

  /* The node-id for this node-rev. */
  const svn_fs_id_t *id;

  /* predecessor node revision id, or NULL if there is no predecessor
     for this node revision */
  const svn_fs_id_t *predecessor_id;

  /* If this node-rev is a copy, where was it copied from? */
  const char *copyfrom_path;
  svn_revnum_t copyfrom_rev;

  /* Helper for history tracing, root of the parent tree from whence
     this node-rev was copied. */
  svn_revnum_t copyroot_rev;
  const char *copyroot_path;

  /* number of predecessors this node revision has (recursively), or
     -1 if not known (for backward compatibility). */
  int predecessor_count;

  /* representation key for this node's properties.  may be NULL if
     there are no properties.  */
  representation_t *prop_rep;

  /* representation for this node's data.  may be NULL if there is
     no data. */
  representation_t *data_rep;

  /* path at which this node first came into existence.  */
  const char *created_path;

  /* is this the unmodified root of a transaction? */
  svn_boolean_t is_fresh_txn_root;

  /* Number of nodes with svn:mergeinfo properties that are
     descendants of this node (including it itself) */
  apr_int64_t mergeinfo_count;

  /* Does this node itself have svn:mergeinfo? */
  svn_boolean_t has_mergeinfo;

} node_revision_t;


/*** Change ***/
typedef struct change_t
{
  /* Path of the change. */
  svn_string_t path;

  /* API compatible change description */
  svn_fs_path_change2_t info;
} change_t;


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_FS_H */
