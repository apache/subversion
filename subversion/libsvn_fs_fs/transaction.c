/* transaction.c --- transaction-related functions of FSFS
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

#include "transaction.h"

#include <assert.h>
#include <apr_sha1.h>

#include "svn_hash.h"
#include "svn_props.h"
#include "svn_sorts.h"
#include "svn_time.h"
#include "svn_dirent_uri.h"

#include "fs_fs.h"
#include "tree.h"
#include "util.h"
#include "id.h"
#include "low_level.h"
#include "temp_serializer.h"
#include "cached_data.h"
#include "lock.h"
#include "rep-cache.h"
#include "index.h"

#include "private/svn_fs_util.h"
#include "private/svn_subr_private.h"
#include "private/svn_string_private.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

/* The vtable associated with an open transaction object. */
static txn_vtable_t txn_vtable = {
  svn_fs_fs__commit_txn,
  svn_fs_fs__abort_txn,
  svn_fs_fs__txn_prop,
  svn_fs_fs__txn_proplist,
  svn_fs_fs__change_txn_prop,
  svn_fs_fs__txn_root,
  svn_fs_fs__change_txn_props
};

/* FSFS-specific data being attached to svn_fs_txn_t.
 */
typedef struct fs_txn_data_t
{
  /* Strongly typed representation of the TXN's ID member. */
  svn_fs_fs__id_part_t txn_id;
} fs_txn_data_t;

const svn_fs_fs__id_part_t *
svn_fs_fs__txn_get_id(svn_fs_txn_t *txn)
{
  fs_txn_data_t *ftd = txn->fsap_data;
  return &ftd->txn_id;
}

/* Functions for working with shared transaction data. */

/* Return the transaction object for transaction TXN_ID from the
   transaction list of filesystem FS (which must already be locked via the
   txn_list_lock mutex).  If the transaction does not exist in the list,
   then create a new transaction object and return it (if CREATE_NEW is
   true) or return NULL (otherwise). */
static fs_fs_shared_txn_data_t *
get_shared_txn(svn_fs_t *fs,
               const svn_fs_fs__id_part_t *txn_id,
               svn_boolean_t create_new)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  fs_fs_shared_data_t *ffsd = ffd->shared;
  fs_fs_shared_txn_data_t *txn;

  for (txn = ffsd->txns; txn; txn = txn->next)
    if (svn_fs_fs__id_part_eq(&txn->txn_id, txn_id))
      break;

  if (txn || !create_new)
    return txn;

  /* Use the transaction object from the (single-object) freelist,
     if one is available, or otherwise create a new object. */
  if (ffsd->free_txn)
    {
      txn = ffsd->free_txn;
      ffsd->free_txn = NULL;
    }
  else
    {
      apr_pool_t *subpool = svn_pool_create(ffsd->common_pool);
      txn = apr_palloc(subpool, sizeof(*txn));
      txn->pool = subpool;
    }

  txn->txn_id = *txn_id;
  txn->being_written = FALSE;

  /* Link this transaction into the head of the list.  We will typically
     be dealing with only one active transaction at a time, so it makes
     sense for searches through the transaction list to look at the
     newest transactions first.  */
  txn->next = ffsd->txns;
  ffsd->txns = txn;

  return txn;
}

/* Free the transaction object for transaction TXN_ID, and remove it
   from the transaction list of filesystem FS (which must already be
   locked via the txn_list_lock mutex).  Do nothing if the transaction
   does not exist. */
static void
free_shared_txn(svn_fs_t *fs, const svn_fs_fs__id_part_t *txn_id)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  fs_fs_shared_data_t *ffsd = ffd->shared;
  fs_fs_shared_txn_data_t *txn, *prev = NULL;

  for (txn = ffsd->txns; txn; prev = txn, txn = txn->next)
    if (svn_fs_fs__id_part_eq(&txn->txn_id, txn_id))
      break;

  if (!txn)
    return;

  if (prev)
    prev->next = txn->next;
  else
    ffsd->txns = txn->next;

  /* As we typically will be dealing with one transaction after another,
     we will maintain a single-object free list so that we can hopefully
     keep reusing the same transaction object. */
  if (!ffsd->free_txn)
    ffsd->free_txn = txn;
  else
    svn_pool_destroy(txn->pool);
}


/* Obtain a lock on the transaction list of filesystem FS, call BODY
   with FS, BATON, and POOL, and then unlock the transaction list.
   Return what BODY returned. */
static svn_error_t *
with_txnlist_lock(svn_fs_t *fs,
                  svn_error_t *(*body)(svn_fs_t *fs,
                                       const void *baton,
                                       apr_pool_t *pool),
                  const void *baton,
                  apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  fs_fs_shared_data_t *ffsd = ffd->shared;

  SVN_MUTEX__WITH_LOCK(ffsd->txn_list_lock,
                       body(fs, baton, pool));

  return SVN_NO_ERROR;
}


/* Get a lock on empty file LOCK_FILENAME, creating it in POOL. */
static svn_error_t *
get_lock_on_filesystem(const char *lock_filename,
                       apr_pool_t *pool)
{
  svn_error_t *err = svn_io_file_lock2(lock_filename, TRUE, FALSE, pool);

  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      /* No lock file?  No big deal; these are just empty files
         anyway.  Create it and try again. */
      svn_error_clear(err);
      err = NULL;

      SVN_ERR(svn_io_file_create_empty(lock_filename, pool));
      SVN_ERR(svn_io_file_lock2(lock_filename, TRUE, FALSE, pool));
    }

  return svn_error_trace(err);
}

/* Reset the HAS_WRITE_LOCK member in the FFD given as BATON_VOID.
   When registered with the pool holding the lock on the lock file,
   this makes sure the flag gets reset just before we release the lock. */
static apr_status_t
reset_lock_flag(void *baton_void)
{
  fs_fs_data_t *ffd = baton_void;
  ffd->has_write_lock = FALSE;
  return APR_SUCCESS;
}

/* Obtain a write lock on the file LOCK_FILENAME (protecting with
   LOCK_MUTEX if APR is threaded) in a subpool of POOL, call BODY with
   BATON and that subpool, destroy the subpool (releasing the write
   lock) and return what BODY returned.  If IS_GLOBAL_LOCK is set,
   set the HAS_WRITE_LOCK flag while we keep the write lock. */
static svn_error_t *
with_some_lock_file(svn_fs_t *fs,
                    svn_error_t *(*body)(void *baton,
                                         apr_pool_t *pool),
                    void *baton,
                    const char *lock_filename,
                    svn_boolean_t is_global_lock,
                    apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_error_t *err = get_lock_on_filesystem(lock_filename, subpool);

  if (!err)
    {
      fs_fs_data_t *ffd = fs->fsap_data;

      if (is_global_lock)
        {
          /* set the "got the lock" flag and register reset function */
          apr_pool_cleanup_register(subpool,
                                    ffd,
                                    reset_lock_flag,
                                    apr_pool_cleanup_null);
          ffd->has_write_lock = TRUE;
        }

      /* nobody else will modify the repo state
         => read HEAD & pack info once */
      if (ffd->format >= SVN_FS_FS__MIN_PACKED_FORMAT)
        SVN_ERR(update_min_unpacked_rev(fs, pool));
      SVN_ERR(svn_fs_fs__youngest_rev(&ffd->youngest_rev_cache, fs, pool));
      err = body(baton, subpool);
    }

  svn_pool_destroy(subpool);

  return svn_error_trace(err);
}

svn_error_t *
svn_fs_fs__with_write_lock(svn_fs_t *fs,
                           svn_error_t *(*body)(void *baton,
                                                apr_pool_t *pool),
                           void *baton,
                           apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  fs_fs_shared_data_t *ffsd = ffd->shared;

  SVN_MUTEX__WITH_LOCK(ffsd->fs_write_lock,
                       with_some_lock_file(fs, body, baton,
                                           path_lock(fs, pool),
                                           TRUE,
                                           pool));

  return SVN_NO_ERROR;
}

/* Run BODY (with BATON and POOL) while the txn-current file
   of FS is locked. */
static svn_error_t *
with_txn_current_lock(svn_fs_t *fs,
                      svn_error_t *(*body)(void *baton,
                                           apr_pool_t *pool),
                      void *baton,
                      apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  fs_fs_shared_data_t *ffsd = ffd->shared;

  SVN_MUTEX__WITH_LOCK(ffsd->txn_current_lock,
                       with_some_lock_file(fs, body, baton,
                                           path_txn_current_lock(fs, pool),
                                           FALSE,
                                           pool));

  return SVN_NO_ERROR;
}

/* A structure used by unlock_proto_rev() and unlock_proto_rev_body(),
   which see. */
struct unlock_proto_rev_baton
{
  svn_fs_fs__id_part_t txn_id;
  void *lockcookie;
};

/* Callback used in the implementation of unlock_proto_rev(). */
static svn_error_t *
unlock_proto_rev_body(svn_fs_t *fs, const void *baton, apr_pool_t *pool)
{
  const struct unlock_proto_rev_baton *b = baton;
  apr_file_t *lockfile = b->lockcookie;
  fs_fs_shared_txn_data_t *txn = get_shared_txn(fs, &b->txn_id, FALSE);
  apr_status_t apr_err;

  if (!txn)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Can't unlock unknown transaction '%s'"),
                             svn_fs_fs__id_txn_unparse(&b->txn_id, pool));
  if (!txn->being_written)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Can't unlock nonlocked transaction '%s'"),
                             svn_fs_fs__id_txn_unparse(&b->txn_id, pool));

  apr_err = apr_file_unlock(lockfile);
  if (apr_err)
    return svn_error_wrap_apr
      (apr_err,
       _("Can't unlock prototype revision lockfile for transaction '%s'"),
       svn_fs_fs__id_txn_unparse(&b->txn_id, pool));
  apr_err = apr_file_close(lockfile);
  if (apr_err)
    return svn_error_wrap_apr
      (apr_err,
       _("Can't close prototype revision lockfile for transaction '%s'"),
       svn_fs_fs__id_txn_unparse(&b->txn_id, pool));

  txn->being_written = FALSE;

  return SVN_NO_ERROR;
}

/* Unlock the prototype revision file for transaction TXN_ID in filesystem
   FS using cookie LOCKCOOKIE.  The original prototype revision file must
   have been closed _before_ calling this function.

   Perform temporary allocations in POOL. */
static svn_error_t *
unlock_proto_rev(svn_fs_t *fs,
                 const svn_fs_fs__id_part_t *txn_id,
                 void *lockcookie,
                 apr_pool_t *pool)
{
  struct unlock_proto_rev_baton b;

  b.txn_id = *txn_id;
  b.lockcookie = lockcookie;
  return with_txnlist_lock(fs, unlock_proto_rev_body, &b, pool);
}

/* Same as unlock_proto_rev(), but requires that the transaction list
   lock is already held. */
static svn_error_t *
unlock_proto_rev_list_locked(svn_fs_t *fs,
                             const svn_fs_fs__id_part_t *txn_id,
                             void *lockcookie,
                             apr_pool_t *pool)
{
  struct unlock_proto_rev_baton b;

  b.txn_id = *txn_id;
  b.lockcookie = lockcookie;
  return unlock_proto_rev_body(fs, &b, pool);
}

/* A structure used by get_writable_proto_rev() and
   get_writable_proto_rev_body(), which see. */
struct get_writable_proto_rev_baton
{
  apr_file_t **file;
  void **lockcookie;
  svn_fs_fs__id_part_t txn_id;
};

/* Callback used in the implementation of get_writable_proto_rev(). */
static svn_error_t *
get_writable_proto_rev_body(svn_fs_t *fs, const void *baton, apr_pool_t *pool)
{
  const struct get_writable_proto_rev_baton *b = baton;
  apr_file_t **file = b->file;
  void **lockcookie = b->lockcookie;
  svn_error_t *err;
  fs_fs_shared_txn_data_t *txn = get_shared_txn(fs, &b->txn_id, TRUE);

  /* First, ensure that no thread in this process (including this one)
     is currently writing to this transaction's proto-rev file. */
  if (txn->being_written)
    return svn_error_createf(SVN_ERR_FS_REP_BEING_WRITTEN, NULL,
                             _("Cannot write to the prototype revision file "
                               "of transaction '%s' because a previous "
                               "representation is currently being written by "
                               "this process"),
                             svn_fs_fs__id_txn_unparse(&b->txn_id, pool));


  /* We know that no thread in this process is writing to the proto-rev
     file, and by extension, that no thread in this process is holding a
     lock on the prototype revision lock file.  It is therefore safe
     for us to attempt to lock this file, to see if any other process
     is holding a lock. */

  {
    apr_file_t *lockfile;
    apr_status_t apr_err;
    const char *lockfile_path = path_txn_proto_rev_lock(fs, &b->txn_id, pool);

    /* Open the proto-rev lockfile, creating it if necessary, as it may
       not exist if the transaction dates from before the lockfiles were
       introduced.

       ### We'd also like to use something like svn_io_file_lock2(), but
           that forces us to create a subpool just to be able to unlock
           the file, which seems a waste. */
    SVN_ERR(svn_io_file_open(&lockfile, lockfile_path,
                             APR_WRITE | APR_CREATE, APR_OS_DEFAULT, pool));

    apr_err = apr_file_lock(lockfile,
                            APR_FLOCK_EXCLUSIVE | APR_FLOCK_NONBLOCK);
    if (apr_err)
      {
        svn_error_clear(svn_io_file_close(lockfile, pool));

        if (APR_STATUS_IS_EAGAIN(apr_err))
          return svn_error_createf(SVN_ERR_FS_REP_BEING_WRITTEN, NULL,
                                   _("Cannot write to the prototype revision "
                                     "file of transaction '%s' because a "
                                     "previous representation is currently "
                                     "being written by another process"),
                                   svn_fs_fs__id_txn_unparse(&b->txn_id,
                                                             pool));

        return svn_error_wrap_apr(apr_err,
                                  _("Can't get exclusive lock on file '%s'"),
                                  svn_dirent_local_style(lockfile_path, pool));
      }

    *lockcookie = lockfile;
  }

  /* We've successfully locked the transaction; mark it as such. */
  txn->being_written = TRUE;


  /* Now open the prototype revision file and seek to the end. */
  err = svn_io_file_open(file, path_txn_proto_rev(fs, &b->txn_id, pool),
                         APR_WRITE | APR_BUFFERED, APR_OS_DEFAULT, pool);

  /* You might expect that we could dispense with the following seek
     and achieve the same thing by opening the file using APR_APPEND.
     Unfortunately, APR's buffered file implementation unconditionally
     places its initial file pointer at the start of the file (even for
     files opened with APR_APPEND), so we need this seek to reconcile
     the APR file pointer to the OS file pointer (since we need to be
     able to read the current file position later). */
  if (!err)
    {
      apr_off_t offset = 0;
      err = svn_io_file_seek(*file, APR_END, &offset, pool);
    }

  if (err)
    {
      err = svn_error_compose_create(
              err,
              unlock_proto_rev_list_locked(fs, &b->txn_id, *lockcookie, pool));

      *lockcookie = NULL;
    }

  return svn_error_trace(err);
}

/* Get a handle to the prototype revision file for transaction TXN_ID in
   filesystem FS, and lock it for writing.  Return FILE, a file handle
   positioned at the end of the file, and LOCKCOOKIE, a cookie that
   should be passed to unlock_proto_rev() to unlock the file once FILE
   has been closed.

   If the prototype revision file is already locked, return error
   SVN_ERR_FS_REP_BEING_WRITTEN.

   Perform all allocations in POOL. */
static svn_error_t *
get_writable_proto_rev(apr_file_t **file,
                       void **lockcookie,
                       svn_fs_t *fs,
                       const svn_fs_fs__id_part_t *txn_id,
                       apr_pool_t *pool)
{
  struct get_writable_proto_rev_baton b;

  b.file = file;
  b.lockcookie = lockcookie;
  b.txn_id = *txn_id;

  return with_txnlist_lock(fs, get_writable_proto_rev_body, &b, pool);
}

/* Callback used in the implementation of purge_shared_txn(). */
static svn_error_t *
purge_shared_txn_body(svn_fs_t *fs, const void *baton, apr_pool_t *pool)
{
  const svn_fs_fs__id_part_t *txn_id = baton;

  free_shared_txn(fs, txn_id);
  svn_fs_fs__reset_txn_caches(fs);

  return SVN_NO_ERROR;
}

/* Purge the shared data for transaction TXN_ID in filesystem FS.
   Perform all allocations in POOL. */
static svn_error_t *
purge_shared_txn(svn_fs_t *fs,
                 const svn_fs_fs__id_part_t *txn_id,
                 apr_pool_t *pool)
{
  return with_txnlist_lock(fs, purge_shared_txn_body, txn_id, pool);
}


svn_error_t *
svn_fs_fs__put_node_revision(svn_fs_t *fs,
                             const svn_fs_id_t *id,
                             node_revision_t *noderev,
                             svn_boolean_t fresh_txn_root,
                             apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_file_t *noderev_file;

  noderev->is_fresh_txn_root = fresh_txn_root;

  if (! svn_fs_fs__id_is_txn(id))
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Attempted to write to non-transaction '%s'"),
                             svn_fs_fs__id_unparse(id, pool)->data);

  SVN_ERR(svn_io_file_open(&noderev_file, path_txn_node_rev(fs, id, pool),
                           APR_WRITE | APR_CREATE | APR_TRUNCATE
                           | APR_BUFFERED, APR_OS_DEFAULT, pool));

  SVN_ERR(svn_fs_fs__write_noderev(svn_stream_from_aprfile2(noderev_file, TRUE,
                                                            pool),
                                   noderev, ffd->format,
                                   svn_fs_fs__fs_supports_mergeinfo(fs),
                                   pool));

  SVN_ERR(svn_io_file_close(noderev_file, pool));

  return SVN_NO_ERROR;
}

/* For the in-transaction NODEREV within FS, write the sha1->rep mapping
 * file in the respective transaction, if rep sharing has been enabled etc.
 * Use POOL for temporary allocations.
 */
static svn_error_t *
store_sha1_rep_mapping(svn_fs_t *fs,
                       node_revision_t *noderev,
                       apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  /* if rep sharing has been enabled and the noderev has a data rep and
   * its SHA-1 is known, store the rep struct under its SHA1. */
  if (   ffd->rep_sharing_allowed
      && noderev->data_rep
      && noderev->data_rep->sha1_checksum)
    {
      apr_file_t *rep_file;
      const char *file_name = path_txn_sha1(fs,
                                            &noderev->data_rep->txn_id,
                                            noderev->data_rep->sha1_checksum,
                                            pool);
      svn_stringbuf_t *rep_string
        = svn_fs_fs__unparse_representation(noderev->data_rep,
                                            ffd->format,
                                            (noderev->kind == svn_node_dir),
                                            FALSE,
                                            pool);
      SVN_ERR(svn_io_file_open(&rep_file, file_name,
                               APR_WRITE | APR_CREATE | APR_TRUNCATE
                               | APR_BUFFERED, APR_OS_DEFAULT, pool));

      SVN_ERR(svn_io_file_write_full(rep_file, rep_string->data,
                                     rep_string->len, NULL, pool));

      SVN_ERR(svn_io_file_close(rep_file, pool));
    }

  return SVN_NO_ERROR;
}


static const char *
unparse_dir_entry(svn_node_kind_t kind, const svn_fs_id_t *id,
                  apr_pool_t *pool)
{
  return apr_psprintf(pool, "%s %s",
                      (kind == svn_node_file) ? SVN_FS_FS__KIND_FILE
                                              : SVN_FS_FS__KIND_DIR,
                      svn_fs_fs__id_unparse(id, pool)->data);
}

/* Given a hash ENTRIES of dirent structions, return a hash in
   *STR_ENTRIES_P, that has svn_string_t as the values in the format
   specified by the fs_fs directory contents file.  Perform
   allocations in POOL. */
static svn_error_t *
unparse_dir_entries(apr_hash_t **str_entries_p,
                    apr_hash_t *entries,
                    apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  /* For now, we use a our own hash function to ensure that we get a
   * (largely) stable order when serializing the data.  It also gives
   * us some performance improvement.
   *
   * ### TODO ###
   * Use some sorted or other fixed order data container.
   */
  *str_entries_p = svn_hash__make(pool);

  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      svn_fs_dirent_t *dirent = svn__apr_hash_index_val(hi);
      const char *new_val;

      apr_hash_this(hi, &key, &klen, NULL);
      new_val = unparse_dir_entry(dirent->kind, dirent->id, pool);
      apr_hash_set(*str_entries_p, key, klen,
                   svn_string_create(new_val, pool));
    }

  return SVN_NO_ERROR;
}

/* Merge the internal-use-only CHANGE into a hash of public-FS
   svn_fs_path_change2_t CHANGES, collapsing multiple changes into a
   single summarical (is that real word?) change per path.  Also keep
   the COPYFROM_CACHE up to date with new adds and replaces.  */
static svn_error_t *
fold_change(apr_hash_t *changes,
            const change_t *change,
            apr_hash_t *copyfrom_cache)
{
  apr_pool_t *pool = apr_hash_pool_get(changes);
  svn_fs_path_change2_t *old_change, *new_change;
  const char *path;
  apr_size_t path_len = strlen(change->path);

  if ((old_change = apr_hash_get(changes, change->path, path_len)))
    {
      /* This path already exists in the hash, so we have to merge
         this change into the already existing one. */

      /* Sanity check:  only allow NULL node revision ID in the
         `reset' case. */
      if ((! change->noderev_id) && (change->kind != svn_fs_path_change_reset))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Missing required node revision ID"));

      /* Sanity check: we should be talking about the same node
         revision ID as our last change except where the last change
         was a deletion. */
      if (change->noderev_id
          && (! svn_fs_fs__id_eq(old_change->node_rev_id, change->noderev_id))
          && (old_change->change_kind != svn_fs_path_change_delete))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Invalid change ordering: new node revision ID "
             "without delete"));

      /* Sanity check: an add, replacement, or reset must be the first
         thing to follow a deletion. */
      if ((old_change->change_kind == svn_fs_path_change_delete)
          && (! ((change->kind == svn_fs_path_change_replace)
                 || (change->kind == svn_fs_path_change_reset)
                 || (change->kind == svn_fs_path_change_add))))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Invalid change ordering: non-add change on deleted path"));

      /* Sanity check: an add can't follow anything except
         a delete or reset.  */
      if ((change->kind == svn_fs_path_change_add)
          && (old_change->change_kind != svn_fs_path_change_delete)
          && (old_change->change_kind != svn_fs_path_change_reset))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Invalid change ordering: add change on preexisting path"));

      /* Now, merge that change in. */
      switch (change->kind)
        {
        case svn_fs_path_change_reset:
          /* A reset here will simply remove the path change from the
             hash. */
          old_change = NULL;
          break;

        case svn_fs_path_change_delete:
          if (old_change->change_kind == svn_fs_path_change_add)
            {
              /* If the path was introduced in this transaction via an
                 add, and we are deleting it, just remove the path
                 altogether. */
              old_change = NULL;
            }
          else
            {
              /* A deletion overrules all previous changes. */
              old_change->change_kind = svn_fs_path_change_delete;
              old_change->text_mod = change->text_mod;
              old_change->prop_mod = change->prop_mod;
              old_change->copyfrom_rev = SVN_INVALID_REVNUM;
              old_change->copyfrom_path = NULL;
            }
          break;

        case svn_fs_path_change_add:
        case svn_fs_path_change_replace:
          /* An add at this point must be following a previous delete,
             so treat it just like a replace. */
          old_change->change_kind = svn_fs_path_change_replace;
          old_change->node_rev_id = svn_fs_fs__id_copy(change->noderev_id,
                                                       pool);
          old_change->text_mod = change->text_mod;
          old_change->prop_mod = change->prop_mod;
          if (change->copyfrom_rev == SVN_INVALID_REVNUM)
            {
              old_change->copyfrom_rev = SVN_INVALID_REVNUM;
              old_change->copyfrom_path = NULL;
            }
          else
            {
              old_change->copyfrom_rev = change->copyfrom_rev;
              old_change->copyfrom_path = apr_pstrdup(pool,
                                                      change->copyfrom_path);
            }
          break;

        case svn_fs_path_change_modify:
        default:
          if (change->text_mod)
            old_change->text_mod = TRUE;
          if (change->prop_mod)
            old_change->prop_mod = TRUE;
          break;
        }

      /* Point our new_change to our (possibly modified) old_change. */
      new_change = old_change;
    }
  else
    {
      /* This change is new to the hash, so make a new public change
         structure from the internal one (in the hash's pool), and dup
         the path into the hash's pool, too. */
      new_change = apr_pcalloc(pool, sizeof(*new_change));
      new_change->node_rev_id = svn_fs_fs__id_copy(change->noderev_id, pool);
      new_change->change_kind = change->kind;
      new_change->text_mod = change->text_mod;
      new_change->prop_mod = change->prop_mod;
      /* In FSFS, copyfrom_known is *always* true, since we've always
       * stored copyfroms in changed paths lists. */
      new_change->copyfrom_known = TRUE;
      if (change->copyfrom_rev != SVN_INVALID_REVNUM)
        {
          new_change->copyfrom_rev = change->copyfrom_rev;
          new_change->copyfrom_path = apr_pstrdup(pool, change->copyfrom_path);
        }
      else
        {
          new_change->copyfrom_rev = SVN_INVALID_REVNUM;
          new_change->copyfrom_path = NULL;
        }
    }

  if (new_change)
    new_change->node_kind = change->node_kind;

  /* Add (or update) this path.

     Note: this key might already be present, and it would be nice to
     re-use its value, but there is no way to fetch it. The API makes no
     guarantees that this (new) key will not be retained. Thus, we (again)
     copy the key into the target pool to ensure a proper lifetime.  */
  path = apr_pstrmemdup(pool, change->path, path_len);
  apr_hash_set(changes, path, path_len, new_change);

  /* Update the copyfrom cache, if any. */
  if (copyfrom_cache)
    {
      apr_pool_t *copyfrom_pool = apr_hash_pool_get(copyfrom_cache);
      const char *copyfrom_string = NULL, *copyfrom_key = path;
      if (new_change)
        {
          if (SVN_IS_VALID_REVNUM(new_change->copyfrom_rev))
            copyfrom_string = apr_psprintf(copyfrom_pool, "%ld %s",
                                           new_change->copyfrom_rev,
                                           new_change->copyfrom_path);
          else
            copyfrom_string = "";
        }
      /* We need to allocate a copy of the key in the copyfrom_pool if
       * we're not doing a deletion and if it isn't already there. */
      if (   copyfrom_string
          && (   ! apr_hash_count(copyfrom_cache)
              || ! apr_hash_get(copyfrom_cache, copyfrom_key, path_len)))
        copyfrom_key = apr_pstrmemdup(copyfrom_pool, copyfrom_key, path_len);

      apr_hash_set(copyfrom_cache, copyfrom_key, path_len,
                   copyfrom_string);
    }

  return SVN_NO_ERROR;
}


/* Examine all the changed path entries in CHANGES and store them in
   *CHANGED_PATHS.  Folding is done to remove redundant or unnecessary
   *data.  Store a hash of paths to copyfrom "REV PATH" strings in
   COPYFROM_HASH if it is non-NULL.  If PREFOLDED is true, assume that
   the changed-path entries have already been folded (by
   write_final_changed_path_info) and may be out of order, so we shouldn't
   remove children of replaced or deleted directories.  Do all
   allocations in POOL. */
static svn_error_t *
process_changes(apr_hash_t *changed_paths,
                apr_hash_t *copyfrom_cache,
                apr_array_header_t *changes,
                svn_boolean_t prefolded,
                apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  int i;

  /* Read in the changes one by one, folding them into our local hash
     as necessary. */

  for (i = 0; i < changes->nelts; ++i)
    {
      change_t *change = APR_ARRAY_IDX(changes, i, change_t *);
      
      SVN_ERR(fold_change(changed_paths, change, copyfrom_cache));

      /* Now, if our change was a deletion or replacement, we have to
         blow away any changes thus far on paths that are (or, were)
         children of this path.
         ### i won't bother with another iteration pool here -- at
         most we talking about a few extra dups of paths into what
         is already a temporary subpool.
      */

      if (((change->kind == svn_fs_path_change_delete)
           || (change->kind == svn_fs_path_change_replace))
          && ! prefolded)
        {
          apr_hash_index_t *hi;

          /* a potential child path must contain at least 2 more chars
             (the path separator plus at least one char for the name).
             Also, we should not assume that all paths have been normalized
             i.e. some might have trailing path separators.
          */
          apr_ssize_t change_path_len = strlen(change->path);
          apr_ssize_t min_child_len = change_path_len == 0
                                    ? 1
                                    : change->path[change_path_len-1] == '/'
                                        ? change_path_len + 1
                                        : change_path_len + 2;

          /* CAUTION: This is the inner loop of an O(n^2) algorithm.
             The number of changes to process may be >> 1000.
             Therefore, keep the inner loop as tight as possible.
          */
          for (hi = apr_hash_first(iterpool, changed_paths);
               hi;
               hi = apr_hash_next(hi))
            {
              /* KEY is the path. */
              const void *path;
              apr_ssize_t klen;
              apr_hash_this(hi, &path, &klen, NULL);

              /* If we come across a child of our path, remove it.
                 Call svn_dirent_is_child only if there is a chance that
                 this is actually a sub-path.
               */
              if (   klen >= min_child_len
                  && svn_dirent_is_child(change->path, path, iterpool))
                apr_hash_set(changed_paths, path, klen, NULL);
            }
        }

      /* Clear the per-iteration subpool. */
      svn_pool_clear(iterpool);
    }

  /* Destroy the per-iteration subpool. */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__txn_changes_fetch(apr_hash_t **changed_paths_p,
                             svn_fs_t *fs,
                             const svn_fs_fs__id_part_t *txn_id,
                             apr_pool_t *pool)
{
  apr_file_t *file;
  apr_hash_t *changed_paths = apr_hash_make(pool);
  apr_array_header_t *changes;
  apr_pool_t *scratch_pool = svn_pool_create(pool);

  SVN_ERR(svn_io_file_open(&file, path_txn_changes(fs, txn_id, scratch_pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT,
                           scratch_pool));

  SVN_ERR(svn_fs_fs__read_changes(&changes,
                                  svn_stream_from_aprfile2(file, TRUE,
                                                           scratch_pool),
                                  scratch_pool));
  SVN_ERR(process_changes(changed_paths, NULL, changes, FALSE, pool));
  svn_pool_destroy(scratch_pool);

  *changed_paths_p = changed_paths;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__paths_changed(apr_hash_t **changed_paths_p,
                         svn_fs_t *fs,
                         svn_revnum_t rev,
                         apr_hash_t *copyfrom_cache,
                         apr_pool_t *pool)
{
  apr_hash_t *changed_paths;
  apr_array_header_t *changes;
  apr_pool_t *scratch_pool = svn_pool_create(pool);

  SVN_ERR(svn_fs_fs__get_changes(&changes, fs, rev, scratch_pool));

  changed_paths = svn_hash__make(pool);

  SVN_ERR(process_changes(changed_paths, copyfrom_cache, changes,
                          TRUE, pool));
  svn_pool_destroy(scratch_pool);

  *changed_paths_p = changed_paths;

  return SVN_NO_ERROR;
}

/* Copy a revision node-rev SRC into the current transaction TXN_ID in
   the filesystem FS.  This is only used to create the root of a transaction.
   Allocations are from POOL.  */
static svn_error_t *
create_new_txn_noderev_from_rev(svn_fs_t *fs,
                                const svn_fs_fs__id_part_t *txn_id,
                                svn_fs_id_t *src,
                                apr_pool_t *pool)
{
  node_revision_t *noderev;
  const svn_fs_fs__id_part_t *node_id, *copy_id;

  SVN_ERR(svn_fs_fs__get_node_revision(&noderev, fs, src, pool));

  if (svn_fs_fs__id_is_txn(noderev->id))
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Copying from transactions not allowed"));

  noderev->predecessor_id = noderev->id;
  noderev->predecessor_count++;
  noderev->copyfrom_path = NULL;
  noderev->copyfrom_rev = SVN_INVALID_REVNUM;

  /* For the transaction root, the copyroot never changes. */

  node_id = svn_fs_fs__id_node_id(noderev->id);
  copy_id = svn_fs_fs__id_copy_id(noderev->id);
  noderev->id = svn_fs_fs__id_txn_create(node_id, copy_id, txn_id, pool);

  return svn_fs_fs__put_node_revision(fs, noderev->id, noderev, TRUE, pool);
}

/* A structure used by get_and_increment_txn_key_body(). */
struct get_and_increment_txn_key_baton {
  svn_fs_t *fs;
  apr_uint64_t txn_number;
  apr_pool_t *pool;
};

/* Callback used in the implementation of create_txn_dir().  This gets
   the current base 36 value in PATH_TXN_CURRENT and increments it.
   It returns the original value by the baton. */
static svn_error_t *
get_and_increment_txn_key_body(void *baton, apr_pool_t *pool)
{
  struct get_and_increment_txn_key_baton *cb = baton;
  const char *txn_current_filename = path_txn_current(cb->fs, pool);
  const char *tmp_filename;
  char new_id_str[SVN_INT64_BUFFER_SIZE];

  svn_stringbuf_t *buf;
  SVN_ERR(read_content(&buf, txn_current_filename, cb->pool));

  /* remove trailing newlines */
  cb->txn_number = svn__base36toui64(NULL, buf->data);

  /* Increment the key and add a trailing \n to the string so the
     txn-current file has a newline in it. */
  SVN_ERR(svn_io_write_unique(&tmp_filename,
                              svn_dirent_dirname(txn_current_filename, pool),
                              new_id_str,
                              svn__ui64tobase36(new_id_str, cb->txn_number+1),
                              svn_io_file_del_none, pool));
  SVN_ERR(move_into_place(tmp_filename, txn_current_filename,
                          txn_current_filename, pool));

  return SVN_NO_ERROR;
}

/* Create a unique directory for a transaction in FS based on revision REV.
   Return the ID for this transaction in *ID_P and *TXN_ID.  Use a sequence
   value in the transaction ID to prevent reuse of transaction IDs. */
static svn_error_t *
create_txn_dir(const char **id_p,
               svn_fs_fs__id_part_t *txn_id,
               svn_fs_t *fs,
               svn_revnum_t rev,
               apr_pool_t *pool)
{
  struct get_and_increment_txn_key_baton cb;
  const char *txn_dir;

  /* Get the current transaction sequence value, which is a base-36
     number, from the txn-current file, and write an
     incremented value back out to the file.  Place the revision
     number the transaction is based off into the transaction id. */
  cb.pool = pool;
  cb.fs = fs;
  SVN_ERR(with_txn_current_lock(fs,
                                get_and_increment_txn_key_body,
                                &cb,
                                pool));
  txn_id->revision = rev;
  txn_id->number = cb.txn_number;

  *id_p = svn_fs_fs__id_txn_unparse(txn_id, pool);
  txn_dir = svn_dirent_join_many(pool,
                                 fs->path,
                                 PATH_TXNS_DIR,
                                 apr_pstrcat(pool, *id_p, PATH_EXT_TXN,
                                             (char *)NULL),
                                 NULL);

  return svn_io_dir_make(txn_dir, APR_OS_DEFAULT, pool);
}

/* Create a unique directory for a transaction in FS based on revision
   REV.  Return the ID for this transaction in *ID_P and *TXN_ID.  This
   implementation is used in svn 1.4 and earlier repositories and is
   kept in 1.5 and greater to support the --pre-1.4-compatible and
   --pre-1.5-compatible repository creation options.  Reused
   transaction IDs are possible with this implementation. */
static svn_error_t *
create_txn_dir_pre_1_5(const char **id_p,
                       svn_fs_fs__id_part_t *txn_id,
                       svn_fs_t *fs,
                       svn_revnum_t rev,
                       apr_pool_t *pool)
{
  unsigned int i;
  apr_pool_t *subpool;
  const char *unique_path, *prefix;

  /* Try to create directories named "<txndir>/<rev>-<uniqueifier>.txn". */
  prefix = svn_dirent_join_many(pool, fs->path, PATH_TXNS_DIR,
                                apr_psprintf(pool, "%ld", rev), NULL);

  subpool = svn_pool_create(pool);
  for (i = 1; i <= 99999; i++)
    {
      svn_error_t *err;

      svn_pool_clear(subpool);
      unique_path = apr_psprintf(subpool, "%s-%u" PATH_EXT_TXN, prefix, i);
      err = svn_io_dir_make(unique_path, APR_OS_DEFAULT, subpool);
      if (! err)
        {
          /* We succeeded.  Return the basename minus the ".txn" extension. */
          const char *name = svn_dirent_basename(unique_path, subpool);
          *id_p = apr_pstrndup(pool, name,
                               strlen(name) - strlen(PATH_EXT_TXN));
          SVN_ERR(svn_fs_fs__id_txn_parse(txn_id, *id_p));
          svn_pool_destroy(subpool);
          return SVN_NO_ERROR;
        }
      if (! APR_STATUS_IS_EEXIST(err->apr_err))
        return svn_error_trace(err);
      svn_error_clear(err);
    }

  return svn_error_createf(SVN_ERR_IO_UNIQUE_NAMES_EXHAUSTED,
                           NULL,
                           _("Unable to create transaction directory "
                             "in '%s' for revision %ld"),
                           svn_dirent_local_style(fs->path, pool),
                           rev);
}

svn_error_t *
svn_fs_fs__create_txn(svn_fs_txn_t **txn_p,
                      svn_fs_t *fs,
                      svn_revnum_t rev,
                      apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_fs_txn_t *txn;
  fs_txn_data_t *ftd;
  svn_fs_id_t *root_id;

  txn = apr_pcalloc(pool, sizeof(*txn));
  ftd = apr_pcalloc(pool, sizeof(*ftd));

  /* Get the txn_id. */
  if (ffd->format >= SVN_FS_FS__MIN_TXN_CURRENT_FORMAT)
    SVN_ERR(create_txn_dir(&txn->id, &ftd->txn_id, fs, rev, pool));
  else
    SVN_ERR(create_txn_dir_pre_1_5(&txn->id, &ftd->txn_id, fs, rev, pool));

  txn->fs = fs;
  txn->base_rev = rev;

  txn->vtable = &txn_vtable;
  txn->fsap_data = ftd;
  *txn_p = txn;

  /* Create a new root node for this transaction. */
  SVN_ERR(svn_fs_fs__rev_get_root(&root_id, fs, rev, pool));
  SVN_ERR(create_new_txn_noderev_from_rev(fs, &ftd->txn_id, root_id, pool));

  /* Create an empty rev file. */
  SVN_ERR(svn_io_file_create_empty(path_txn_proto_rev(fs, &ftd->txn_id, pool),
                                   pool));

  /* Create an empty rev-lock file. */
  SVN_ERR(svn_io_file_create_empty(path_txn_proto_rev_lock(fs, &ftd->txn_id,
                                                           pool),
                                   pool));

  /* Create an empty changes file. */
  SVN_ERR(svn_io_file_create_empty(path_txn_changes(fs, &ftd->txn_id, pool),
                                   pool));

  /* Create the next-ids file. */
  return svn_io_file_create(path_txn_next_ids(fs, &ftd->txn_id, pool),
                            "0 0\n", pool);
}

/* Store the property list for transaction TXN_ID in PROPLIST.
   Perform temporary allocations in POOL. */
static svn_error_t *
get_txn_proplist(apr_hash_t *proplist,
                 svn_fs_t *fs,
                 const svn_fs_fs__id_part_t *txn_id,
                 apr_pool_t *pool)
{
  svn_stream_t *stream;

  /* Check for issue #3696. (When we find and fix the cause, we can change
   * this to an assertion.) */
  if (!txn_id || !svn_fs_fs__id_txn_used(txn_id))
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("Internal error: a null transaction id was "
                              "passed to get_txn_proplist()"));

  /* Open the transaction properties file. */
  SVN_ERR(svn_stream_open_readonly(&stream, path_txn_props(fs, txn_id, pool),
                                   pool, pool));

  /* Read in the property list. */
  SVN_ERR(svn_hash_read2(proplist, stream, SVN_HASH_TERMINATOR, pool));

  return svn_stream_close(stream);
}

svn_error_t *
svn_fs_fs__change_txn_prop(svn_fs_txn_t *txn,
                           const char *name,
                           const svn_string_t *value,
                           apr_pool_t *pool)
{
  apr_array_header_t *props = apr_array_make(pool, 1, sizeof(svn_prop_t));
  svn_prop_t prop;

  prop.name = name;
  prop.value = value;
  APR_ARRAY_PUSH(props, svn_prop_t) = prop;

  return svn_fs_fs__change_txn_props(txn, props, pool);
}

svn_error_t *
svn_fs_fs__change_txn_props(svn_fs_txn_t *txn,
                            const apr_array_header_t *props,
                            apr_pool_t *pool)
{
  fs_txn_data_t *ftd = txn->fsap_data;
  const char *txn_prop_filename;
  svn_stringbuf_t *buf;
  svn_stream_t *stream;
  apr_hash_t *txn_prop = apr_hash_make(pool);
  int i;
  svn_error_t *err;

  err = get_txn_proplist(txn_prop, txn->fs, &ftd->txn_id, pool);
  /* Here - and here only - we need to deal with the possibility that the
     transaction property file doesn't yet exist.  The rest of the
     implementation assumes that the file exists, but we're called to set the
     initial transaction properties as the transaction is being created. */
  if (err && (APR_STATUS_IS_ENOENT(err->apr_err)))
    svn_error_clear(err);
  else if (err)
    return svn_error_trace(err);

  for (i = 0; i < props->nelts; i++)
    {
      svn_prop_t *prop = &APR_ARRAY_IDX(props, i, svn_prop_t);

      apr_hash_set(txn_prop, prop->name, APR_HASH_KEY_STRING, prop->value);
    }

  /* Create a new version of the file and write out the new props. */
  /* Open the transaction properties file. */
  buf = svn_stringbuf_create_ensure(1024, pool);
  stream = svn_stream_from_stringbuf(buf, pool);
  SVN_ERR(svn_hash_write2(txn_prop, stream, SVN_HASH_TERMINATOR, pool));
  SVN_ERR(svn_stream_close(stream));
  SVN_ERR(svn_io_write_unique(&txn_prop_filename,
                              path_txn_dir(txn->fs, &ftd->txn_id, pool),
                              buf->data,
                              buf->len,
                              svn_io_file_del_none,
                              pool));
  return svn_io_file_rename(txn_prop_filename,
                            path_txn_props(txn->fs, &ftd->txn_id, pool),
                            pool);
}

svn_error_t *
svn_fs_fs__get_txn(transaction_t **txn_p,
                   svn_fs_t *fs,
                   const svn_fs_fs__id_part_t *txn_id,
                   apr_pool_t *pool)
{
  transaction_t *txn;
  node_revision_t *noderev;
  svn_fs_id_t *root_id;

  txn = apr_pcalloc(pool, sizeof(*txn));
  txn->proplist = apr_hash_make(pool);

  SVN_ERR(get_txn_proplist(txn->proplist, fs, txn_id, pool));
  root_id = svn_fs_fs__id_txn_create_root(txn_id, pool);

  SVN_ERR(svn_fs_fs__get_node_revision(&noderev, fs, root_id, pool));

  txn->root_id = svn_fs_fs__id_copy(noderev->id, pool);
  txn->base_id = svn_fs_fs__id_copy(noderev->predecessor_id, pool);
  txn->copies = NULL;

  *txn_p = txn;

  return SVN_NO_ERROR;
}

/* Write out the currently available next node_id NODE_ID and copy_id
   COPY_ID for transaction TXN_ID in filesystem FS.  The next node-id is
   used both for creating new unique nodes for the given transaction, as
   well as uniquifying representations.  Perform temporary allocations in
   POOL. */
static svn_error_t *
write_next_ids(svn_fs_t *fs,
               const svn_fs_fs__id_part_t *txn_id,
               apr_uint64_t node_id,
               apr_uint64_t copy_id,
               apr_pool_t *pool)
{
  apr_file_t *file;
  char buffer[2 * SVN_INT64_BUFFER_SIZE + 2];
  char *p = buffer;
  
  p += svn__ui64tobase36(p, node_id);
  *(p++) = ' ';
  p += svn__ui64tobase36(p, copy_id);
  *(p++) = '\n';
  *(p++) = '\0';

  SVN_ERR(svn_io_file_open(&file, path_txn_next_ids(fs, txn_id, pool),
                           APR_WRITE | APR_TRUNCATE,
                           APR_OS_DEFAULT, pool));
  SVN_ERR(svn_io_file_write_full(file, buffer, p - buffer, NULL, pool));
  return svn_io_file_close(file, pool);
}

/* Find out what the next unique node-id and copy-id are for
   transaction TXN_ID in filesystem FS.  Store the results in *NODE_ID
   and *COPY_ID.  The next node-id is used both for creating new unique
   nodes for the given transaction, as well as uniquifying representations.
   Perform all allocations in POOL. */
static svn_error_t *
read_next_ids(apr_uint64_t *node_id,
              apr_uint64_t *copy_id,
              svn_fs_t *fs,
              const svn_fs_fs__id_part_t *txn_id,
              apr_pool_t *pool)
{
  svn_stringbuf_t *buf;
  const char *str;
  SVN_ERR(read_content(&buf, path_txn_next_ids(fs, txn_id, pool), pool));

  /* Parse this into two separate strings. */

  str = buf->data;
  *node_id = svn__base36toui64(&str, str);
  if (*str != ' ')
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("next-id file corrupt"));

  *copy_id = svn__base36toui64(&str, ++str);
  if (*str != '\n')
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("next-id file corrupt"));

  return SVN_NO_ERROR;
}

/* Get a new and unique to this transaction node-id for transaction
   TXN_ID in filesystem FS.  Store the new node-id in *NODE_ID_P.
   Node-ids are guaranteed to be unique to this transction, but may
   not necessarily be sequential.  Perform all allocations in POOL. */
static svn_error_t *
get_new_txn_node_id(svn_fs_fs__id_part_t *node_id_p,
                    svn_fs_t *fs,
                    const svn_fs_fs__id_part_t *txn_id,
                    apr_pool_t *pool)
{
  apr_uint64_t node_id, copy_id;

  /* First read in the current next-ids file. */
  SVN_ERR(read_next_ids(&node_id, &copy_id, fs, txn_id, pool));

  node_id_p->revision = SVN_INVALID_REVNUM;
  node_id_p->number = node_id;

  SVN_ERR(write_next_ids(fs, txn_id, ++node_id, copy_id, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__reserve_copy_id(svn_fs_fs__id_part_t *copy_id_p,
                           svn_fs_t *fs,
                           const svn_fs_fs__id_part_t *txn_id,
                           apr_pool_t *pool)
{
  apr_uint64_t node_id, copy_id;

  /* First read in the current next-ids file. */
  SVN_ERR(read_next_ids(&node_id, &copy_id, fs, txn_id, pool));

  copy_id_p->revision = SVN_INVALID_REVNUM;
  copy_id_p->number = copy_id;

  SVN_ERR(write_next_ids(fs, txn_id, node_id, ++copy_id, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__create_node(const svn_fs_id_t **id_p,
                       svn_fs_t *fs,
                       node_revision_t *noderev,
                       const svn_fs_fs__id_part_t *copy_id,
                       const svn_fs_fs__id_part_t *txn_id,
                       apr_pool_t *pool)
{
  svn_fs_fs__id_part_t node_id;
  const svn_fs_id_t *id;

  /* Get a new node-id for this node. */
  SVN_ERR(get_new_txn_node_id(&node_id, fs, txn_id, pool));

  id = svn_fs_fs__id_txn_create(&node_id, copy_id, txn_id, pool);

  noderev->id = id;

  SVN_ERR(svn_fs_fs__put_node_revision(fs, noderev->id, noderev, FALSE, pool));

  *id_p = id;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__purge_txn(svn_fs_t *fs,
                     const char *txn_id_str,
                     apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_fs_fs__id_part_t txn_id;
  SVN_ERR(svn_fs_fs__id_txn_parse(&txn_id, txn_id_str));

  /* Remove the shared transaction object associated with this transaction. */
  SVN_ERR(purge_shared_txn(fs, &txn_id, pool));
  /* Remove the directory associated with this transaction. */
  SVN_ERR(svn_io_remove_dir2(path_txn_dir(fs, &txn_id, pool), FALSE,
                             NULL, NULL, pool));
  if (ffd->format >= SVN_FS_FS__MIN_PROTOREVS_DIR_FORMAT)
    {
      /* Delete protorev and its lock, which aren't in the txn
         directory.  It's OK if they don't exist (for example, if this
         is post-commit and the proto-rev has been moved into
         place). */
      SVN_ERR(svn_io_remove_file2(path_txn_proto_rev(fs, &txn_id, pool),
                                  TRUE, pool));
      SVN_ERR(svn_io_remove_file2(path_txn_proto_rev_lock(fs, &txn_id, pool),
                                  TRUE, pool));
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__abort_txn(svn_fs_txn_t *txn,
                     apr_pool_t *pool)
{
  SVN_ERR(svn_fs__check_fs(txn->fs, TRUE));

  /* Now, purge the transaction. */
  SVN_ERR_W(svn_fs_fs__purge_txn(txn->fs, txn->id, pool),
            apr_psprintf(pool, _("Transaction '%s' cleanup failed"),
                         txn->id));

  return SVN_NO_ERROR;
}

/* Assign the UNIQUIFIER member of REP based on the current state of TXN_ID
 * in FS.  Allocate the uniquifier in POOL.
 */
static svn_error_t *
set_uniquifier(svn_fs_t *fs,
               representation_t *rep,
               apr_pool_t *pool)
{
  svn_fs_fs__id_part_t temp;

  SVN_ERR(get_new_txn_node_id(&temp, fs, &rep->txn_id, pool));
  rep->uniquifier.txn_id = rep->txn_id;
  rep->uniquifier.number = temp.number;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__set_entry(svn_fs_t *fs,
                     const svn_fs_fs__id_part_t *txn_id,
                     node_revision_t *parent_noderev,
                     const char *name,
                     const svn_fs_id_t *id,
                     svn_node_kind_t kind,
                     apr_pool_t *pool)
{
  representation_t *rep = parent_noderev->data_rep;
  const char *filename = path_txn_node_children(fs, parent_noderev->id, pool);
  apr_file_t *file;
  svn_stream_t *out;
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_pool_t *subpool = svn_pool_create(pool);

  if (!rep || !svn_fs_fs__id_txn_used(&rep->txn_id))
    {
      apr_hash_t *entries;

      /* Before we can modify the directory, we need to dump its old
         contents into a mutable representation file. */
      SVN_ERR(svn_fs_fs__rep_contents_dir(&entries, fs, parent_noderev,
                                          subpool));
      SVN_ERR(unparse_dir_entries(&entries, entries, subpool));
      SVN_ERR(svn_io_file_open(&file, filename,
                               APR_WRITE | APR_CREATE | APR_BUFFERED,
                               APR_OS_DEFAULT, pool));
      out = svn_stream_from_aprfile2(file, TRUE, pool);
      SVN_ERR(svn_hash_write2(entries, out, SVN_HASH_TERMINATOR, subpool));

      svn_pool_clear(subpool);

      /* Mark the node-rev's data rep as mutable. */
      rep = apr_pcalloc(pool, sizeof(*rep));
      rep->revision = SVN_INVALID_REVNUM;
      rep->txn_id = *txn_id;
      SVN_ERR(set_uniquifier(fs, rep, pool));
      parent_noderev->data_rep = rep;
      SVN_ERR(svn_fs_fs__put_node_revision(fs, parent_noderev->id,
                                           parent_noderev, FALSE, pool));
    }
  else
    {
      /* The directory rep is already mutable, so just open it for append. */
      SVN_ERR(svn_io_file_open(&file, filename, APR_WRITE | APR_APPEND,
                               APR_OS_DEFAULT, pool));
      out = svn_stream_from_aprfile2(file, TRUE, pool);
    }

  /* if we have a directory cache for this transaction, update it */
  if (ffd->txn_dir_cache)
    {
      /* build parameters: (name, new entry) pair */
      const char *key =
          svn_fs_fs__id_unparse(parent_noderev->id, subpool)->data;
      replace_baton_t baton;

      baton.name = name;
      baton.new_entry = NULL;

      if (id)
        {
          baton.new_entry = apr_pcalloc(subpool, sizeof(*baton.new_entry));
          baton.new_entry->name = name;
          baton.new_entry->kind = kind;
          baton.new_entry->id = id;
        }

      /* actually update the cached directory (if cached) */
      SVN_ERR(svn_cache__set_partial(ffd->txn_dir_cache, key,
                                     svn_fs_fs__replace_dir_entry, &baton,
                                     subpool));
    }
  svn_pool_clear(subpool);

  /* Append an incremental hash entry for the entry change. */
  if (id)
    {
      const char *val = unparse_dir_entry(kind, id, subpool);

      SVN_ERR(svn_stream_printf(out, subpool, "K %" APR_SIZE_T_FMT "\n%s\n"
                                "V %" APR_SIZE_T_FMT "\n%s\n",
                                strlen(name), name,
                                strlen(val), val));
    }
  else
    {
      SVN_ERR(svn_stream_printf(out, subpool, "D %" APR_SIZE_T_FMT "\n%s\n",
                                strlen(name), name));
    }

  SVN_ERR(svn_io_file_close(file, subpool));
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__add_change(svn_fs_t *fs,
                      const svn_fs_fs__id_part_t *txn_id,
                      const char *path,
                      const svn_fs_id_t *id,
                      svn_fs_path_change_kind_t change_kind,
                      svn_boolean_t text_mod,
                      svn_boolean_t prop_mod,
                      svn_node_kind_t node_kind,
                      svn_revnum_t copyfrom_rev,
                      const char *copyfrom_path,
                      apr_pool_t *pool)
{
  apr_file_t *file;
  svn_fs_path_change2_t *change;
  apr_hash_t *changes = apr_hash_make(pool);

  SVN_ERR(svn_io_file_open(&file, path_txn_changes(fs, txn_id, pool),
                           APR_APPEND | APR_WRITE | APR_CREATE
                           | APR_BUFFERED, APR_OS_DEFAULT, pool));

  change = svn_fs__path_change_create_internal(id, change_kind, pool);
  change->text_mod = text_mod;
  change->prop_mod = prop_mod;
  change->node_kind = node_kind;
  change->copyfrom_rev = copyfrom_rev;
  change->copyfrom_path = apr_pstrdup(pool, copyfrom_path);

  apr_hash_set(changes, path, APR_HASH_KEY_STRING, change);
  SVN_ERR(svn_fs_fs__write_changes(svn_stream_from_aprfile2(file, TRUE, pool),
                                   fs, changes, FALSE, pool));

  return svn_io_file_close(file, pool);
}

/* If it is supported by the format of file system FS, store the (ITEM_INDEX,
 * OFFSET) pair in the log-to-phys proto index file of transaction TXN_ID.
 * Use POOL for allocations.
 */
static svn_error_t *
store_l2p_index_entry(svn_fs_t *fs,
                      const svn_fs_fs__id_part_t *txn_id,
                      apr_off_t offset,
                      apr_uint64_t item_index,
                      apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  if (ffd->format >= SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT)
    {
      const char *path = path_l2p_proto_index(fs, txn_id, pool);
      apr_file_t *file;
      SVN_ERR(svn_fs_fs__l2p_proto_index_open(&file, path, pool));
      SVN_ERR(svn_fs_fs__l2p_proto_index_add_entry(file, offset, 0,
                                                   item_index, pool));
      SVN_ERR(svn_io_file_close(file, pool));
    }

  return SVN_NO_ERROR;
}

/* If it is supported by the format of file system FS, store ENTRY in the
 * phys-to-log proto index file of transaction TXN_ID.
 * Use POOL for allocations.
 */
static svn_error_t *
store_p2l_index_entry(svn_fs_t *fs,
                      const svn_fs_fs__id_part_t *txn_id,
                      svn_fs_fs__p2l_entry_t *entry,
                      apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  if (ffd->format >= SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT)
    {
      const char *path = path_p2l_proto_index(fs, txn_id, pool);
      apr_file_t *file;
      SVN_ERR(svn_fs_fs__p2l_proto_index_open(&file, path, pool));
      SVN_ERR(svn_fs_fs__p2l_proto_index_add_entry(file, entry, pool));
      SVN_ERR(svn_io_file_close(file, pool));
    }

  return SVN_NO_ERROR;
}

/* Allocate an item index for the given MY_OFFSET in the transaction TXN_ID
 * of file system FS and return it in *ITEM_INDEX.  For old formats, it
 * will simply return the offset as item index; in new formats, it will
 * increment the txn's item index counter file and store the mapping in
 * the proto index file.
 * Use POOL for allocations.
 */
static svn_error_t *
allocate_item_index(apr_uint64_t *item_index,
                    svn_fs_t *fs,
                    const svn_fs_fs__id_part_t *txn_id,
                    apr_off_t my_offset,
                    apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  if (ffd->format < SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT)
    {
      *item_index = (apr_uint64_t)my_offset;
    }
  else
    {
      apr_file_t *file;
      char buffer[SVN_INT64_BUFFER_SIZE] = { 0 };
      svn_boolean_t eof = FALSE;
      apr_size_t to_write;
      apr_size_t read;
      apr_off_t offset = 0;

      /* read number, increment it and write it back to disk */
      SVN_ERR(svn_io_file_open(&file, path_txn_item_index(fs, txn_id, pool),
                               APR_READ | APR_WRITE
                               | APR_CREATE | APR_BUFFERED,
                               APR_OS_DEFAULT, pool));
      SVN_ERR(svn_io_file_read_full2(file, buffer, sizeof(buffer)-1,
                                     &read, &eof, pool));
      if (read)
        SVN_ERR(svn_cstring_atoui64(item_index, buffer));
      else
        *item_index = SVN_FS_FS__ITEM_INDEX_FIRST_USER;

      to_write = svn__ui64toa(buffer, *item_index + 1);
      SVN_ERR(svn_io_file_seek(file, SEEK_SET, &offset, pool));
      SVN_ERR(svn_io_file_write_full(file, buffer, to_write, NULL, pool));
      SVN_ERR(svn_io_file_close(file, pool));

      /* write log-to-phys index */
      SVN_ERR(store_l2p_index_entry(fs, txn_id, my_offset, *item_index,
                                    pool));
    }

  return SVN_NO_ERROR;
}

/* This baton is used by the representation writing streams.  It keeps
   track of the checksum information as well as the total size of the
   representation so far. */
struct rep_write_baton
{
  /* The FS we are writing to. */
  svn_fs_t *fs;

  /* Actual file to which we are writing. */
  svn_stream_t *rep_stream;

  /* A stream from the delta combiner.  Data written here gets
     deltified, then eventually written to rep_stream. */
  svn_stream_t *delta_stream;

  /* Where is this representation header stored. */
  apr_off_t rep_offset;

  /* Start of the actual data. */
  apr_off_t delta_start;

  /* How many bytes have been written to this rep already. */
  svn_filesize_t rep_size;

  /* The node revision for which we're writing out info. */
  node_revision_t *noderev;

  /* Actual output file. */
  apr_file_t *file;
  /* Lock 'cookie' used to unlock the output file once we've finished
     writing to it. */
  void *lockcookie;

  svn_checksum_ctx_t *md5_checksum_ctx;
  svn_checksum_ctx_t *sha1_checksum_ctx;

  apr_pool_t *pool;

  apr_pool_t *parent_pool;
};

/* Handler for the write method of the representation writable stream.
   BATON is a rep_write_baton, DATA is the data to write, and *LEN is
   the length of this data. */
static svn_error_t *
rep_write_contents(void *baton,
                   const char *data,
                   apr_size_t *len)
{
  struct rep_write_baton *b = baton;

  SVN_ERR(svn_checksum_update(b->md5_checksum_ctx, data, *len));
  SVN_ERR(svn_checksum_update(b->sha1_checksum_ctx, data, *len));
  b->rep_size += *len;

  /* If we are writing a delta, use that stream. */
  if (b->delta_stream)
    return svn_stream_write(b->delta_stream, data, len);
  else
    return svn_stream_write(b->rep_stream, data, len);
}

/* Given a node-revision NODEREV in filesystem FS, return the
   representation in *REP to use as the base for a text representation
   delta if PROPS is FALSE.  If PROPS has been set, a suitable props
   base representation will be returned.  Perform temporary allocations
   in *POOL. */
static svn_error_t *
choose_delta_base(representation_t **rep,
                  svn_fs_t *fs,
                  node_revision_t *noderev,
                  svn_boolean_t props,
                  apr_pool_t *pool)
{
  int count;
  int walk;
  node_revision_t *base;
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_boolean_t maybe_shared_rep = FALSE;

  /* If we have no predecessors, then use the empty stream as a
     base. */
  if (! noderev->predecessor_count)
    {
      *rep = NULL;
      return SVN_NO_ERROR;
    }

  /* Flip the rightmost '1' bit of the predecessor count to determine
     which file rev (counting from 0) we want to use.  (To see why
     count & (count - 1) unsets the rightmost set bit, think about how
     you decrement a binary number.) */
  count = noderev->predecessor_count;
  count = count & (count - 1);

  /* We use skip delta for limiting the number of delta operations
     along very long node histories.  Close to HEAD however, we create
     a linear history to minimize delta size.  */
  walk = noderev->predecessor_count - count;
  if (walk < (int)ffd->max_linear_deltification)
    count = noderev->predecessor_count - 1;

  /* Finding the delta base over a very long distance can become extremely
     expensive for very deep histories, possibly causing client timeouts etc.
     OTOH, this is a rare operation and its gains are minimal. Lets simply
     start deltification anew close every other 1000 changes or so.  */
  if (walk > (int)ffd->max_deltification_walk)
    {
      *rep = NULL;
      return SVN_NO_ERROR;
    }

  /* Walk back a number of predecessors equal to the difference
     between count and the original predecessor count.  (For example,
     if noderev has ten predecessors and we want the eighth file rev,
     walk back two predecessors.) */
  base = noderev;
  while ((count++) < noderev->predecessor_count)
    {
      svn_revnum_t base_revision;
      SVN_ERR(svn_fs_fs__get_node_revision(&base, fs,
                                           base->predecessor_id, pool));

      /* If there is a shared rep along the way, we need to limit the
       * length of the deltification chain.
       * 
       * Please note that copied nodes - such as branch directories - will
       * look the same (false positive) while reps shared within the same
       * revision will not be caught (false negative).
       */
      base_revision = svn_fs_fs__id_rev(base->id);
      if (props)
        {
          if (base->prop_rep && base_revision > base->prop_rep->revision)
            maybe_shared_rep = TRUE;
        }
      else
        {
          if (base->data_rep && base_revision > base->data_rep->revision)
            maybe_shared_rep = TRUE;
        }
    }

  /* return a suitable base representation */
  *rep = props ? base->prop_rep : base->data_rep;

  /* if we encountered a shared rep, it's parent chain may be different
   * from the node-rev parent chain. */
  if (*rep && maybe_shared_rep)
    {
      int chain_length = 0;
      SVN_ERR(svn_fs_fs__rep_chain_length(&chain_length, *rep, fs, pool));
      
      /* Some reasonable limit, depending on how acceptable longer linear
       * chains are in this repo.  Also, allow for some minimal chain. */
      if (chain_length >= 2 * (int)ffd->max_linear_deltification + 2)
        *rep = NULL;
    }

  /* verify that the reps don't form a degenerated '*/
  return SVN_NO_ERROR;
}

/* Something went wrong and the pool for the rep write is being
   cleared before we've finished writing the rep.  So we need
   to remove the rep from the protorevfile and we need to unlock
   the protorevfile. */
static apr_status_t
rep_write_cleanup(void *data)
{
  svn_error_t *err;
  struct rep_write_baton *b = data;
  
  /* Truncate and close the protorevfile. */
  err = svn_io_file_trunc(b->file, b->rep_offset, b->pool);
  err = svn_error_compose_create(err, svn_io_file_close(b->file, b->pool));

  /* Remove our lock regardless of any preceeding errors so that the 
     being_written flag is always removed and stays consistent with the
     file lock which will be removed no matter what since the pool is
     going away. */
  err = svn_error_compose_create(err,
                                 unlock_proto_rev(b->fs,
                                                  svn_fs_fs__id_txn_id(b->noderev->id),
                                                  b->lockcookie, b->pool));
  if (err)
    {
      apr_status_t rc = err->apr_err;
      svn_error_clear(err);
      return rc;
    }

  return APR_SUCCESS;
}


/* Get a rep_write_baton and store it in *WB_P for the representation
   indicated by NODEREV in filesystem FS.  Perform allocations in
   POOL.  Only appropriate for file contents, not for props or
   directory contents. */
static svn_error_t *
rep_write_get_baton(struct rep_write_baton **wb_p,
                    svn_fs_t *fs,
                    node_revision_t *noderev,
                    apr_pool_t *pool)
{
  struct rep_write_baton *b;
  apr_file_t *file;
  representation_t *base_rep;
  svn_stream_t *source;
  svn_txdelta_window_handler_t wh;
  void *whb;
  fs_fs_data_t *ffd = fs->fsap_data;
  int diff_version = ffd->format >= SVN_FS_FS__MIN_SVNDIFF1_FORMAT ? 1 : 0;
  svn_fs_fs__rep_header_t header = { TRUE };

  b = apr_pcalloc(pool, sizeof(*b));

  b->sha1_checksum_ctx = svn_checksum_ctx_create(svn_checksum_sha1, pool);
  b->md5_checksum_ctx = svn_checksum_ctx_create(svn_checksum_md5, pool);

  b->fs = fs;
  b->parent_pool = pool;
  b->pool = svn_pool_create(pool);
  b->rep_size = 0;
  b->noderev = noderev;

  /* Open the prototype rev file and seek to its end. */
  SVN_ERR(get_writable_proto_rev(&file, &b->lockcookie,
                                 fs, svn_fs_fs__id_txn_id(noderev->id),
                                 b->pool));

  b->file = file;
  b->rep_stream = svn_stream_from_aprfile2(file, TRUE, b->pool);

  SVN_ERR(get_file_offset(&b->rep_offset, file, b->pool));

  /* Get the base for this delta. */
  SVN_ERR(choose_delta_base(&base_rep, fs, noderev, FALSE, b->pool));
  SVN_ERR(svn_fs_fs__get_contents(&source, fs, base_rep, b->pool));

  /* Write out the rep header. */
  if (base_rep)
    {
      header.base_revision = base_rep->revision;
      header.base_item_index = base_rep->item_index;
      header.base_length = base_rep->size;
    }
  else
    {
      header.is_delta_vs_empty = TRUE;
    }
  SVN_ERR(svn_fs_fs__write_rep_header(&header, b->rep_stream, b->pool));

  /* Now determine the offset of the actual svndiff data. */
  SVN_ERR(get_file_offset(&b->delta_start, file, b->pool));

  /* Cleanup in case something goes wrong. */
  apr_pool_cleanup_register(b->pool, b, rep_write_cleanup,
                            apr_pool_cleanup_null);

  /* Prepare to write the svndiff data. */
  svn_txdelta_to_svndiff3(&wh,
                          &whb,
                          b->rep_stream,
                          diff_version,
                          SVN_DELTA_COMPRESSION_LEVEL_DEFAULT,
                          pool);

  b->delta_stream = svn_txdelta_target_push(wh, whb, source, b->pool);

  *wb_p = b;

  return SVN_NO_ERROR;
}

/* For the hash REP->SHA1, try to find an already existing representation
   in FS and return it in *OUT_REP.  If no such representation exists or
   if rep sharing has been disabled for FS, NULL will be returned.  Since
   there may be new duplicate representations within the same uncommitted
   revision, those can be passed in REPS_HASH (maps a sha1 digest onto
   representation_t*), otherwise pass in NULL for REPS_HASH.
   POOL will be used for allocations. The lifetime of the returned rep is
   limited by both, POOL and REP lifetime.
 */
static svn_error_t *
get_shared_rep(representation_t **old_rep,
               svn_fs_t *fs,
               representation_t *rep,
               apr_hash_t *reps_hash,
               apr_pool_t *pool)
{
  svn_error_t *err;
  fs_fs_data_t *ffd = fs->fsap_data;

  /* Return NULL, if rep sharing has been disabled. */
  *old_rep = NULL;
  if (!ffd->rep_sharing_allowed)
    return SVN_NO_ERROR;

  /* Check and see if we already have a representation somewhere that's
     identical to the one we just wrote out.  Start with the hash lookup
     because it is cheepest. */
  if (reps_hash)
    *old_rep = apr_hash_get(reps_hash,
                            rep->sha1_checksum->digest,
                            APR_SHA1_DIGESTSIZE);

  /* If we haven't found anything yet, try harder and consult our DB. */
  if (*old_rep == NULL)
    {
      err = svn_fs_fs__get_rep_reference(old_rep, fs, rep->sha1_checksum,
                                         pool);
      /* ### Other error codes that we shouldn't mask out? */
      if (err == SVN_NO_ERROR)
        {
          if (*old_rep)
            SVN_ERR(svn_fs_fs__check_rep(*old_rep, fs, NULL, pool));
        }
      else if (err->apr_err == SVN_ERR_FS_CORRUPT
               || SVN_ERROR_IN_CATEGORY(err->apr_err,
                                        SVN_ERR_MALFUNC_CATEGORY_START))
        {
          /* Fatal error; don't mask it.

             In particular, this block is triggered when the rep-cache refers
             to revisions in the future.  We signal that as a corruption situation
             since, once those revisions are less than youngest (because of more
             commits), the rep-cache would be invalid.
           */
          SVN_ERR(err);
        }
      else
        {
          /* Something's wrong with the rep-sharing index.  We can continue
             without rep-sharing, but warn.
           */
          (fs->warning)(fs->warning_baton, err);
          svn_error_clear(err);
          *old_rep = NULL;
        }
    }

  /* look for intra-revision matches (usually data reps but not limited
     to them in case props happen to look like some data rep)
   */
  if (*old_rep == NULL && svn_fs_fs__id_txn_used(&rep->txn_id))
    {
      svn_node_kind_t kind;
      const char *file_name
        = path_txn_sha1(fs, &rep->txn_id, rep->sha1_checksum, pool);

      /* in our txn, is there a rep file named with the wanted SHA1?
         If so, read it and use that rep.
       */
      SVN_ERR(svn_io_check_path(file_name, &kind, pool));
      if (kind == svn_node_file)
        {
          svn_stringbuf_t *rep_string;
          SVN_ERR(svn_stringbuf_from_file2(&rep_string, file_name, pool));
          SVN_ERR(svn_fs_fs__parse_representation(old_rep, rep_string, pool));
        }
    }

  /* Add information that is missing in the cached data. */
  if (*old_rep)
    {
      /* Use the old rep for this content. */
      (*old_rep)->md5_checksum = rep->md5_checksum;
      (*old_rep)->uniquifier = rep->uniquifier;
    }

  return SVN_NO_ERROR;
}

/* Copy the hash sum calculation results from MD5_CTX, SHA1_CTX into REP.
 * Use POOL for allocations.
 */
static svn_error_t *
digests_final(representation_t *rep,
              const svn_checksum_ctx_t *md5_ctx,
              const svn_checksum_ctx_t *sha1_ctx,
              apr_pool_t *pool)
{
  SVN_ERR(svn_checksum_final(&rep->md5_checksum, md5_ctx, pool));
  SVN_ERR(svn_checksum_final(&rep->sha1_checksum, sha1_ctx, pool));

  return SVN_NO_ERROR;
}

/* Close handler for the representation write stream.  BATON is a
   rep_write_baton.  Writes out a new node-rev that correctly
   references the representation we just finished writing. */
static svn_error_t *
rep_write_contents_close(void *baton)
{
  struct rep_write_baton *b = baton;
  representation_t *rep;
  representation_t *old_rep;
  apr_off_t offset;

  rep = apr_pcalloc(b->parent_pool, sizeof(*rep));

  /* Close our delta stream so the last bits of svndiff are written
     out. */
  if (b->delta_stream)
    SVN_ERR(svn_stream_close(b->delta_stream));

  /* Determine the length of the svndiff data. */
  SVN_ERR(get_file_offset(&offset, b->file, b->pool));
  rep->size = offset - b->delta_start;

  /* Fill in the rest of the representation field. */
  rep->expanded_size = b->rep_size;
  rep->txn_id = *svn_fs_fs__id_txn_id(b->noderev->id);
  SVN_ERR(set_uniquifier(b->fs, rep, b->pool));
  rep->revision = SVN_INVALID_REVNUM;

  /* Finalize the checksum. */
  SVN_ERR(digests_final(rep, b->md5_checksum_ctx, b->sha1_checksum_ctx,
                        b->parent_pool));

  /* Check and see if we already have a representation somewhere that's
     identical to the one we just wrote out. */
  SVN_ERR(get_shared_rep(&old_rep, b->fs, rep, NULL, b->parent_pool));

  if (old_rep)
    {
      /* We need to erase from the protorev the data we just wrote. */
      SVN_ERR(svn_io_file_trunc(b->file, b->rep_offset, b->pool));

      /* Use the old rep for this content. */
      b->noderev->data_rep = old_rep;
    }
  else
    {
      /* Write out our cosmetic end marker. */
      SVN_ERR(svn_stream_puts(b->rep_stream, "ENDREP\n"));
      SVN_ERR(allocate_item_index(&rep->item_index, b->fs, &rep->txn_id,
                                  b->rep_offset, b->pool));

      b->noderev->data_rep = rep;
    }

  /* Remove cleanup callback. */
  apr_pool_cleanup_kill(b->pool, b, rep_write_cleanup);

  /* Write out the new node-rev information. */
  SVN_ERR(svn_fs_fs__put_node_revision(b->fs, b->noderev->id, b->noderev,
                                       FALSE, b->pool));
  if (!old_rep)
    {
      svn_fs_fs__p2l_entry_t entry;
      svn_fs_fs__id_part_t rev_item;
      rev_item.revision = SVN_INVALID_REVNUM;
      rev_item.number = rep->item_index;

      entry.offset = b->rep_offset;
      SVN_ERR(get_file_offset(&offset, b->file, b->pool));
      entry.size = offset - b->rep_offset;
      entry.type = SVN_FS_FS__ITEM_TYPE_FILE_REP;
      entry.item_count = 1;
      entry.items = &rev_item;

      SVN_ERR(store_sha1_rep_mapping(b->fs, b->noderev, b->pool));
      SVN_ERR(store_p2l_index_entry(b->fs, &rep->txn_id, &entry, b->pool));
    }

  SVN_ERR(svn_io_file_close(b->file, b->pool));
  SVN_ERR(unlock_proto_rev(b->fs, &rep->txn_id, b->lockcookie, b->pool));
  svn_pool_destroy(b->pool);

  return SVN_NO_ERROR;
}

/* Store a writable stream in *CONTENTS_P that will receive all data
   written and store it as the file data representation referenced by
   NODEREV in filesystem FS.  Perform temporary allocations in
   POOL.  Only appropriate for file data, not props or directory
   contents. */
static svn_error_t *
set_representation(svn_stream_t **contents_p,
                   svn_fs_t *fs,
                   node_revision_t *noderev,
                   apr_pool_t *pool)
{
  struct rep_write_baton *wb;

  if (! svn_fs_fs__id_is_txn(noderev->id))
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Attempted to write to non-transaction '%s'"),
                             svn_fs_fs__id_unparse(noderev->id, pool)->data);

  SVN_ERR(rep_write_get_baton(&wb, fs, noderev, pool));

  *contents_p = svn_stream_create(wb, pool);
  svn_stream_set_write(*contents_p, rep_write_contents);
  svn_stream_set_close(*contents_p, rep_write_contents_close);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__set_contents(svn_stream_t **stream,
                        svn_fs_t *fs,
                        node_revision_t *noderev,
                        apr_pool_t *pool)
{
  if (noderev->kind != svn_node_file)
    return svn_error_create(SVN_ERR_FS_NOT_FILE, NULL,
                            _("Can't set text contents of a directory"));

  return set_representation(stream, fs, noderev, pool);
}

svn_error_t *
svn_fs_fs__create_successor(const svn_fs_id_t **new_id_p,
                            svn_fs_t *fs,
                            const svn_fs_id_t *old_idp,
                            node_revision_t *new_noderev,
                            const svn_fs_fs__id_part_t *copy_id,
                            const svn_fs_fs__id_part_t *txn_id,
                            apr_pool_t *pool)
{
  const svn_fs_id_t *id;

  if (! copy_id)
    copy_id = svn_fs_fs__id_copy_id(old_idp);
  id = svn_fs_fs__id_txn_create(svn_fs_fs__id_node_id(old_idp), copy_id,
                                txn_id, pool);

  new_noderev->id = id;

  if (! new_noderev->copyroot_path)
    {
      new_noderev->copyroot_path = apr_pstrdup(pool,
                                               new_noderev->created_path);
      new_noderev->copyroot_rev = svn_fs_fs__id_rev(new_noderev->id);
    }

  SVN_ERR(svn_fs_fs__put_node_revision(fs, new_noderev->id, new_noderev, FALSE,
                                       pool));

  *new_id_p = id;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__set_proplist(svn_fs_t *fs,
                        node_revision_t *noderev,
                        apr_hash_t *proplist,
                        apr_pool_t *pool)
{
  const char *filename = path_txn_node_props(fs, noderev->id, pool);
  apr_file_t *file;
  svn_stream_t *out;

  /* Dump the property list to the mutable property file. */
  SVN_ERR(svn_io_file_open(&file, filename,
                           APR_WRITE | APR_CREATE | APR_TRUNCATE
                           | APR_BUFFERED, APR_OS_DEFAULT, pool));
  out = svn_stream_from_aprfile2(file, TRUE, pool);
  SVN_ERR(svn_hash_write2(proplist, out, SVN_HASH_TERMINATOR, pool));
  SVN_ERR(svn_io_file_close(file, pool));

  /* Mark the node-rev's prop rep as mutable, if not already done. */
  if (!noderev->prop_rep
      || !svn_fs_fs__id_txn_used(&noderev->prop_rep->txn_id))
    {
      noderev->prop_rep = apr_pcalloc(pool, sizeof(*noderev->prop_rep));
      noderev->prop_rep->txn_id = *svn_fs_fs__id_txn_id(noderev->id);
      SVN_ERR(svn_fs_fs__put_node_revision(fs, noderev->id, noderev, FALSE,
                                           pool));
    }

  return SVN_NO_ERROR;
}

/* Read the 'current' file for filesystem FS and store the next
   available node id in *NODE_ID, and the next available copy id in
   *COPY_ID.  Allocations are performed from POOL. */
static svn_error_t *
get_next_revision_ids(apr_uint64_t *node_id,
                      apr_uint64_t *copy_id,
                      svn_fs_t *fs,
                      apr_pool_t *pool)
{
  char *buf;
  char *str;
  svn_stringbuf_t *content;

  SVN_ERR(read_content(&content, svn_fs_fs__path_current(fs, pool), pool));
  buf = content->data;

  str = svn_cstring_tokenize(" ", &buf);
  if (! str)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Corrupt 'current' file"));

  str = svn_cstring_tokenize(" ", &buf);
  if (! str)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Corrupt 'current' file"));

  *node_id = svn__base36toui64(NULL, str);

  str = svn_cstring_tokenize(" \n", &buf);
  if (! str)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Corrupt 'current' file"));

  *copy_id = svn__base36toui64(NULL, str);

  return SVN_NO_ERROR;
}

/* This baton is used by the stream created for write_hash_rep. */
struct write_hash_baton
{
  svn_stream_t *stream;

  apr_size_t size;

  svn_checksum_ctx_t *md5_ctx;
  svn_checksum_ctx_t *sha1_ctx;
};

/* The handler for the write_hash_rep stream.  BATON is a
   write_hash_baton, DATA has the data to write and *LEN is the number
   of bytes to write. */
static svn_error_t *
write_hash_handler(void *baton,
                   const char *data,
                   apr_size_t *len)
{
  struct write_hash_baton *whb = baton;

  SVN_ERR(svn_checksum_update(whb->md5_ctx, data, *len));
  SVN_ERR(svn_checksum_update(whb->sha1_ctx, data, *len));

  SVN_ERR(svn_stream_write(whb->stream, data, len));
  whb->size += *len;

  return SVN_NO_ERROR;
}

/* Write out the hash HASH as a text representation to file FILE.  In
   the process, record position, the total size of the dump and MD5 as
   well as SHA1 in REP.   If rep sharing has been enabled and REPS_HASH
   is not NULL, it will be used in addition to the on-disk cache to find
   earlier reps with the same content.  When such existing reps can be
   found, we will truncate the one just written from the file and return
   the existing rep.  Perform temporary allocations in POOL. */
static svn_error_t *
write_hash_rep(representation_t *rep,
               apr_file_t *file,
               apr_hash_t *hash,
               svn_fs_t *fs,
               const svn_fs_fs__id_part_t *txn_id,
               apr_hash_t *reps_hash,
               int item_type,
               apr_pool_t *pool)
{
  svn_stream_t *stream;
  struct write_hash_baton *whb;
  representation_t *old_rep;
  apr_off_t offset = 0;

  SVN_ERR(get_file_offset(&offset, file, pool));

  whb = apr_pcalloc(pool, sizeof(*whb));

  whb->stream = svn_stream_from_aprfile2(file, TRUE, pool);
  whb->size = 0;
  whb->md5_ctx = svn_checksum_ctx_create(svn_checksum_md5, pool);
  whb->sha1_ctx = svn_checksum_ctx_create(svn_checksum_sha1, pool);

  stream = svn_stream_create(whb, pool);
  svn_stream_set_write(stream, write_hash_handler);

  SVN_ERR(svn_stream_puts(whb->stream, "PLAIN\n"));

  SVN_ERR(svn_hash_write2(hash, stream, SVN_HASH_TERMINATOR, pool));

  /* Store the results. */
  SVN_ERR(digests_final(rep, whb->md5_ctx, whb->sha1_ctx, pool));

  /* Check and see if we already have a representation somewhere that's
     identical to the one we just wrote out. */
  SVN_ERR(get_shared_rep(&old_rep, fs, rep, reps_hash, pool));

  if (old_rep)
    {
      /* We need to erase from the protorev the data we just wrote. */
      SVN_ERR(svn_io_file_trunc(file, offset, pool));

      /* Use the old rep for this content. */
      memcpy(rep, old_rep, sizeof (*rep));
    }
  else
    {
      svn_fs_fs__p2l_entry_t entry;
      svn_fs_fs__id_part_t rev_item;

      /* Write out our cosmetic end marker. */
      SVN_ERR(svn_stream_puts(whb->stream, "ENDREP\n"));

      SVN_ERR(allocate_item_index(&rep->item_index, fs, txn_id, offset,
                                  pool));
      
      rev_item.revision = SVN_INVALID_REVNUM;
      rev_item.number = rep->item_index;

      entry.offset = offset;
      SVN_ERR(get_file_offset(&offset, file, pool));
      entry.size = offset - entry.offset;
      entry.type = item_type;
      entry.item_count = 1;
      entry.items = &rev_item;
      SVN_ERR(store_p2l_index_entry(fs, txn_id, &entry, pool));

      /* update the representation */
      rep->size = whb->size;
      rep->expanded_size = 0;
    }

  return SVN_NO_ERROR;
}

/* Write out the hash HASH pertaining to the NODEREV in FS as a deltified
   text representation to file FILE.  In the process, record the total size
   and the md5 digest in REP.  If rep sharing has been enabled and REPS_HASH
   is not NULL, it will be used in addition to the on-disk cache to find
   earlier reps with the same content.  When such existing reps can be found,
   we will truncate the one just written from the file and return the existing
   rep.  If PROPS is set, assume that we want to a props representation as
   the base for our delta.  Perform temporary allocations in POOL. */
static svn_error_t *
write_hash_delta_rep(representation_t *rep,
                     apr_file_t *file,
                     apr_hash_t *hash,
                     svn_fs_t *fs,
                     const svn_fs_fs__id_part_t *txn_id,
                     node_revision_t *noderev,
                     apr_hash_t *reps_hash,
                     int item_type,
                     apr_pool_t *pool)
{
  svn_txdelta_window_handler_t diff_wh;
  void *diff_whb;

  svn_stream_t *file_stream;
  svn_stream_t *stream;
  representation_t *base_rep;
  representation_t *old_rep;
  svn_stream_t *source;
  svn_fs_fs__rep_header_t header = { TRUE };

  apr_off_t rep_end = 0;
  apr_off_t delta_start = 0;
  apr_off_t offset = 0;

  struct write_hash_baton *whb;
  fs_fs_data_t *ffd = fs->fsap_data;
  int diff_version = ffd->format >= SVN_FS_FS__MIN_SVNDIFF1_FORMAT ? 1 : 0;
  svn_boolean_t is_props = (item_type == SVN_FS_FS__ITEM_TYPE_FILE_PROPS)
                        || (item_type == SVN_FS_FS__ITEM_TYPE_DIR_PROPS);

  /* Get the base for this delta. */
  SVN_ERR(choose_delta_base(&base_rep, fs, noderev, is_props, pool));
  SVN_ERR(svn_fs_fs__get_contents(&source, fs, base_rep, pool));

  SVN_ERR(get_file_offset(&offset, file, pool));

  /* Write out the rep header. */
  if (base_rep)
    {
      header.base_revision = base_rep->revision;
      header.base_item_index = base_rep->item_index;
      header.base_length = base_rep->size;
    }
  else
    {
      header.is_delta_vs_empty = TRUE;
    }

  file_stream = svn_stream_from_aprfile2(file, TRUE, pool);
  SVN_ERR(svn_fs_fs__write_rep_header(&header, file_stream, pool));
  SVN_ERR(get_file_offset(&delta_start, file, pool));

  /* Prepare to write the svndiff data. */
  svn_txdelta_to_svndiff3(&diff_wh,
                          &diff_whb,
                          file_stream,
                          diff_version,
                          SVN_DELTA_COMPRESSION_LEVEL_DEFAULT,
                          pool);

  whb = apr_pcalloc(pool, sizeof(*whb));
  whb->stream = svn_txdelta_target_push(diff_wh, diff_whb, source, pool);
  whb->size = 0;
  whb->md5_ctx = svn_checksum_ctx_create(svn_checksum_md5, pool);
  whb->sha1_ctx = svn_checksum_ctx_create(svn_checksum_sha1, pool);

  /* serialize the hash */
  stream = svn_stream_create(whb, pool);
  svn_stream_set_write(stream, write_hash_handler);

  SVN_ERR(svn_hash_write2(hash, stream, SVN_HASH_TERMINATOR, pool));
  SVN_ERR(svn_stream_close(whb->stream));

  /* Store the results. */
  SVN_ERR(digests_final(rep, whb->md5_ctx, whb->sha1_ctx, pool));

  /* Check and see if we already have a representation somewhere that's
     identical to the one we just wrote out. */
  SVN_ERR(get_shared_rep(&old_rep, fs, rep, reps_hash, pool));

  if (old_rep)
    {
      /* We need to erase from the protorev the data we just wrote. */
      SVN_ERR(svn_io_file_trunc(file, offset, pool));

      /* Use the old rep for this content. */
      memcpy(rep, old_rep, sizeof (*rep));
    }
  else
    {
      svn_fs_fs__p2l_entry_t entry;
      svn_fs_fs__id_part_t rev_item;

      /* Write out our cosmetic end marker. */
      SVN_ERR(get_file_offset(&rep_end, file, pool));
      SVN_ERR(svn_stream_puts(file_stream, "ENDREP\n"));

      SVN_ERR(allocate_item_index(&rep->item_index, fs, txn_id, offset,
                                  pool));

      rev_item.revision = SVN_INVALID_REVNUM;
      rev_item.number = rep->item_index;

      entry.offset = offset;
      SVN_ERR(get_file_offset(&offset, file, pool));
      entry.size = offset - entry.offset;
      entry.type = item_type;
      entry.item_count = 1;
      entry.items = &rev_item;

      SVN_ERR(store_p2l_index_entry(fs, txn_id, &entry, pool));

      /* update the representation */
      rep->expanded_size = whb->size;
      rep->size = rep_end - delta_start;
    }

  return SVN_NO_ERROR;
}

/* Sanity check ROOT_NODEREV, a candidate for being the root node-revision
   of (not yet committed) revision REV in FS.  Use POOL for temporary
   allocations.

   If you change this function, consider updating svn_fs_fs__verify() too.
 */
static svn_error_t *
validate_root_noderev(svn_fs_t *fs,
                      node_revision_t *root_noderev,
                      svn_revnum_t rev,
                      apr_pool_t *pool)
{
  svn_revnum_t head_revnum = rev-1;
  int head_predecessor_count;

  SVN_ERR_ASSERT(rev > 0);

  /* Compute HEAD_PREDECESSOR_COUNT. */
  {
    svn_fs_root_t *head_revision;
    const svn_fs_id_t *head_root_id;
    node_revision_t *head_root_noderev;

    /* Get /@HEAD's noderev. */
    SVN_ERR(svn_fs_fs__revision_root(&head_revision, fs, head_revnum, pool));
    SVN_ERR(svn_fs_fs__node_id(&head_root_id, head_revision, "/", pool));
    SVN_ERR(svn_fs_fs__get_node_revision(&head_root_noderev, fs, head_root_id,
                                         pool));

    head_predecessor_count = head_root_noderev->predecessor_count;
  }

  /* Check that the root noderev's predecessor count equals REV.

     This kind of corruption was seen on svn.apache.org (both on
     the root noderev and on other fspaths' noderevs); see
     issue #4129.

     Normally (rev == root_noderev->predecessor_count), but here we
     use a more roundabout check that should only trigger on new instances
     of the corruption, rather then trigger on each and every new commit
     to a repository that has triggered the bug somewhere in its root
     noderev's history.
   */
  if (root_noderev->predecessor_count != -1
      && (root_noderev->predecessor_count - head_predecessor_count)
         != (rev - head_revnum))
    {
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               _("predecessor count for "
                                 "the root node-revision is wrong: "
                                 "found (%d+%ld != %d), committing r%ld"),
                                 head_predecessor_count,
                                 rev - head_revnum, /* This is equal to 1. */
                                 root_noderev->predecessor_count,
                                 rev);
    }

  return SVN_NO_ERROR;
}

/* Given the potentially txn-local id PART, update that to a permanent ID
 * based on the REVISION currently being written and the START_ID for that
 * revision.  Use the repo FORMAT to decide which implementation to use.
 */
static void
get_final_id(svn_fs_fs__id_part_t *part,
             svn_revnum_t revision,
             apr_uint64_t start_id,
             int format)
{
  if (part->revision == SVN_INVALID_REVNUM)
    {
      if (format >= SVN_FS_FS__MIN_NO_GLOBAL_IDS_FORMAT)
        {
          part->revision = revision;
        }
      else
        {
          part->revision = 0;
          part->number += start_id;
        }
    }
}

/* Copy a node-revision specified by id ID in fileystem FS from a
   transaction into the proto-rev-file FILE.  Set *NEW_ID_P to a
   pointer to the new node-id which will be allocated in POOL.
   If this is a directory, copy all children as well.

   START_NODE_ID and START_COPY_ID are
   the first available node and copy ids for this filesystem, for older
   FS formats.

   REV is the revision number that this proto-rev-file will represent.

   INITIAL_OFFSET is the offset of the proto-rev-file on entry to
   commit_body.

   If REPS_TO_CACHE is not NULL, append to it a copy (allocated in
   REPS_POOL) of each data rep that is new in this revision.

   If REPS_HASH is not NULL, append copies (allocated in REPS_POOL)
   of the representations of each property rep that is new in this
   revision.

   AT_ROOT is true if the node revision being written is the root
   node-revision.  It is only controls additional sanity checking
   logic.

   Temporary allocations are also from POOL. */
static svn_error_t *
write_final_rev(const svn_fs_id_t **new_id_p,
                apr_file_t *file,
                svn_revnum_t rev,
                svn_fs_t *fs,
                const svn_fs_id_t *id,
                apr_uint64_t start_node_id,
                apr_uint64_t start_copy_id,
                apr_off_t initial_offset,
                apr_array_header_t *reps_to_cache,
                apr_hash_t *reps_hash,
                apr_pool_t *reps_pool,
                svn_boolean_t at_root,
                apr_pool_t *pool)
{
  node_revision_t *noderev;
  apr_off_t my_offset;
  const svn_fs_id_t *new_id;
  svn_fs_fs__id_part_t node_id, copy_id, rev_item;
  fs_fs_data_t *ffd = fs->fsap_data;
  const svn_fs_fs__id_part_t *txn_id = svn_fs_fs__id_txn_id(id);
  *new_id_p = NULL;

  /* Check to see if this is a transaction node. */
  if (! svn_fs_fs__id_is_txn(id))
    return SVN_NO_ERROR;

  SVN_ERR(svn_fs_fs__get_node_revision(&noderev, fs, id, pool));

  if (noderev->kind == svn_node_dir)
    {
      apr_pool_t *subpool;
      apr_hash_t *entries, *str_entries;
      apr_array_header_t *sorted_entries;
      int i;

      /* This is a directory.  Write out all the children first. */
      subpool = svn_pool_create(pool);

      SVN_ERR(svn_fs_fs__rep_contents_dir(&entries, fs, noderev, pool));
      /* For the sake of the repository administrator sort the entries
         so that the final file is deterministic and repeatable,
         however the rest of the FSFS code doesn't require any
         particular order here. */
      sorted_entries = svn_sort__hash(entries, svn_sort_compare_items_lexically,
                                      pool);
      for (i = 0; i < sorted_entries->nelts; ++i)
        {
          svn_fs_dirent_t *dirent = APR_ARRAY_IDX(sorted_entries, i,
                                                  svn_sort__item_t).value;

          svn_pool_clear(subpool);
          SVN_ERR(write_final_rev(&new_id, file, rev, fs, dirent->id,
                                  start_node_id, start_copy_id,
                                  initial_offset, reps_to_cache, reps_hash,
                                  reps_pool, FALSE, subpool));
          if (new_id && (svn_fs_fs__id_rev(new_id) == rev))
            dirent->id = svn_fs_fs__id_copy(new_id, pool);
        }
      svn_pool_destroy(subpool);

      if (noderev->data_rep
          && svn_fs_fs__id_txn_used(&noderev->data_rep->txn_id))
        {
          /* Write out the contents of this directory as a text rep. */
          SVN_ERR(unparse_dir_entries(&str_entries, entries, pool));
          noderev->data_rep->revision = rev;

          if (ffd->deltify_directories)
            SVN_ERR(write_hash_delta_rep(noderev->data_rep, file,
                                         str_entries, fs, txn_id, noderev,
                                         NULL, SVN_FS_FS__ITEM_TYPE_DIR_REP,
                                         pool));
          else
            SVN_ERR(write_hash_rep(noderev->data_rep, file, str_entries,
                                   fs, txn_id, NULL,
                                   SVN_FS_FS__ITEM_TYPE_DIR_REP, pool));

          svn_fs_fs__id_txn_reset(&noderev->data_rep->txn_id);
        }
    }
  else
    {
      /* This is a file.  We should make sure the data rep, if it
         exists in a "this" state, gets rewritten to our new revision
         num. */

      if (noderev->data_rep
          && svn_fs_fs__id_txn_used(&noderev->data_rep->txn_id))
        {
          svn_fs_fs__id_txn_reset(&noderev->data_rep->txn_id);
          noderev->data_rep->revision = rev;

          if (ffd->format < SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT)
            {
              /* See issue 3845.  Some unknown mechanism caused the
                 protorev file to get truncated, so check for that
                 here.  */
              if (noderev->data_rep->item_index + noderev->data_rep->size
                  > initial_offset)
                return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                        _("Truncated protorev file detected"));
            }
        }
    }

  /* Fix up the property reps. */
  if (noderev->prop_rep
      && svn_fs_fs__id_txn_used(&noderev->prop_rep->txn_id))
    {
      apr_hash_t *proplist;
      int item_type = noderev->kind == svn_node_dir
                    ? SVN_FS_FS__ITEM_TYPE_DIR_PROPS
                    : SVN_FS_FS__ITEM_TYPE_FILE_PROPS;
      SVN_ERR(svn_fs_fs__get_proplist(&proplist, fs, noderev, pool));

      svn_fs_fs__id_txn_reset(&noderev->prop_rep->txn_id);
      noderev->prop_rep->revision = rev;

      if (ffd->deltify_properties)
        SVN_ERR(write_hash_delta_rep(noderev->prop_rep, file,
                                     proplist, fs, txn_id, noderev,
                                     reps_hash, item_type, pool));
      else
        SVN_ERR(write_hash_rep(noderev->prop_rep, file, proplist,
                               fs, txn_id, reps_hash, item_type, pool));
    }

  /* Convert our temporary ID into a permanent revision one. */
  node_id = *svn_fs_fs__id_node_id(noderev->id);
  get_final_id(&node_id, rev, start_node_id, ffd->format);
  copy_id = *svn_fs_fs__id_copy_id(noderev->id);
  get_final_id(&copy_id, rev, start_copy_id, ffd->format);

  if (noderev->copyroot_rev == SVN_INVALID_REVNUM)
    noderev->copyroot_rev = rev;

  SVN_ERR(get_file_offset(&my_offset, file, pool));
  if (ffd->format >= SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT && at_root)
    {
      /* reference the root noderev from the log-to-phys index */
      rev_item.number = SVN_FS_FS__ITEM_INDEX_ROOT_NODE;
      SVN_ERR(store_l2p_index_entry(fs, txn_id, my_offset, rev_item.number,
                                    pool));
    }
  else
    SVN_ERR(allocate_item_index(&rev_item.number, fs, txn_id, my_offset,
                                pool));

  rev_item.revision = rev;
  new_id = svn_fs_fs__id_rev_create(&node_id, &copy_id, &rev_item, pool);

  noderev->id = new_id;

  if (ffd->rep_sharing_allowed)
    {
      /* Save the data representation's hash in the rep cache. */
      if (   noderev->data_rep && noderev->kind == svn_node_file
          && noderev->data_rep->revision == rev)
        {
          SVN_ERR_ASSERT(reps_to_cache && reps_pool);
          APR_ARRAY_PUSH(reps_to_cache, representation_t *)
            = svn_fs_fs__rep_copy(noderev->data_rep, reps_pool);
        }

      if (noderev->prop_rep && noderev->prop_rep->revision == rev)
        {
          /* Add new property reps to hash and on-disk cache. */
          representation_t *copy
            = svn_fs_fs__rep_copy(noderev->prop_rep, reps_pool);

          SVN_ERR_ASSERT(reps_to_cache && reps_pool);
          APR_ARRAY_PUSH(reps_to_cache, representation_t *) = copy;

          apr_hash_set(reps_hash,
                        copy->sha1_checksum->digest,
                        APR_SHA1_DIGESTSIZE,
                        copy);
        }
    }

  /* don't serialize SHA1 for dirs to disk (waste of space) */
  if (noderev->data_rep && noderev->kind == svn_node_dir)
    noderev->data_rep->sha1_checksum = NULL;

  /* don't serialize SHA1 for props to disk (waste of space) */
  if (noderev->prop_rep)
    noderev->prop_rep->sha1_checksum = NULL;

  /* Workaround issue #4031: is-fresh-txn-root in revision files. */
  noderev->is_fresh_txn_root = FALSE;

  /* Write out our new node-revision. */
  if (at_root)
    SVN_ERR(validate_root_noderev(fs, noderev, rev, pool));

  SVN_ERR(svn_fs_fs__write_noderev(svn_stream_from_aprfile2(file, TRUE, pool),
                                   noderev, ffd->format,
                                   svn_fs_fs__fs_supports_mergeinfo(fs),
                                   pool));

  /* reference the root noderev from the log-to-phys index */
  if (ffd->format >= SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT)
    {
      svn_fs_fs__p2l_entry_t entry;
      rev_item.revision = SVN_INVALID_REVNUM;

      entry.offset = my_offset;
      SVN_ERR(get_file_offset(&my_offset, file, pool));
      entry.size = my_offset - entry.offset;
      entry.type = SVN_FS_FS__ITEM_TYPE_NODEREV;
      entry.item_count = 1;
      entry.items = &rev_item;

      SVN_ERR(store_p2l_index_entry(fs, txn_id, &entry, pool));
    }

  /* Return our ID that references the revision file. */
  *new_id_p = noderev->id;

  return SVN_NO_ERROR;
}

/* Write the changed path info from transaction TXN_ID in filesystem
   FS to the permanent rev-file FILE.  *OFFSET_P is set the to offset
   in the file of the beginning of this information.  Perform
   temporary allocations in POOL. */
static svn_error_t *
write_final_changed_path_info(apr_off_t *offset_p,
                              apr_file_t *file,
                              svn_fs_t *fs,
                              const svn_fs_fs__id_part_t *txn_id,
                              apr_pool_t *pool)
{
  apr_hash_t *changed_paths;
  apr_off_t offset;
  fs_fs_data_t *ffd = fs->fsap_data;

  SVN_ERR(get_file_offset(&offset, file, pool));

  SVN_ERR(svn_fs_fs__txn_changes_fetch(&changed_paths, fs, txn_id, pool));

  SVN_ERR(svn_fs_fs__write_changes(svn_stream_from_aprfile2(file, TRUE, pool),
                                   fs, changed_paths, TRUE, pool));

  *offset_p = offset;

  /* reference changes from the indexes */
  if (ffd->format >= SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT)
    {
      svn_fs_fs__p2l_entry_t entry;
      svn_fs_fs__id_part_t rev_item
        = {SVN_INVALID_REVNUM, SVN_FS_FS__ITEM_INDEX_CHANGES};

      entry.offset = offset;
      SVN_ERR(get_file_offset(&offset, file, pool));
      entry.size = offset - entry.offset;
      entry.type = SVN_FS_FS__ITEM_TYPE_CHANGES;
      entry.item_count = 1;
      entry.items = &rev_item;

      SVN_ERR(store_p2l_index_entry(fs, txn_id, &entry, pool));
      SVN_ERR(store_l2p_index_entry(fs, txn_id, entry.offset,
                                    SVN_FS_FS__ITEM_INDEX_CHANGES, pool));
    }

  return SVN_NO_ERROR;
}

/* Update the 'current' file to hold the correct next node and copy_ids
   from transaction TXN_ID in filesystem FS.  The current revision is
   set to REV.  Perform temporary allocations in POOL. */
static svn_error_t *
write_final_current(svn_fs_t *fs,
                    const svn_fs_fs__id_part_t *txn_id,
                    svn_revnum_t rev,
                    apr_uint64_t start_node_id,
                    apr_uint64_t start_copy_id,
                    apr_pool_t *pool)
{
  apr_uint64_t txn_node_id;
  apr_uint64_t txn_copy_id;
  fs_fs_data_t *ffd = fs->fsap_data;

  if (ffd->format >= SVN_FS_FS__MIN_NO_GLOBAL_IDS_FORMAT)
    return write_current(fs, rev, 0, 0, pool);

  /* To find the next available ids, we add the id that used to be in
     the 'current' file, to the next ids from the transaction file. */
  SVN_ERR(read_next_ids(&txn_node_id, &txn_copy_id, fs, txn_id, pool));

  start_node_id += txn_node_id;
  start_copy_id += txn_copy_id;

  return write_current(fs, rev, start_node_id, start_copy_id, pool);
}

/* Verify that the user registed with FS has all the locks necessary to
   permit all the changes associate with TXN_NAME.
   The FS write lock is assumed to be held by the caller. */
static svn_error_t *
verify_locks(svn_fs_t *fs,
             const svn_fs_fs__id_part_t *txn_id,
             apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_t *changes;
  apr_hash_index_t *hi;
  apr_array_header_t *changed_paths;
  svn_stringbuf_t *last_recursed = NULL;
  int i;

  /* Fetch the changes for this transaction. */
  SVN_ERR(svn_fs_fs__txn_changes_fetch(&changes, fs, txn_id, pool));

  /* Make an array of the changed paths, and sort them depth-first-ily.  */
  changed_paths = apr_array_make(pool, apr_hash_count(changes) + 1,
                                 sizeof(const char *));
  for (hi = apr_hash_first(pool, changes); hi; hi = apr_hash_next(hi))
    APR_ARRAY_PUSH(changed_paths, const char *) = svn__apr_hash_index_key(hi);
  qsort(changed_paths->elts, changed_paths->nelts,
        changed_paths->elt_size, svn_sort_compare_paths);

  /* Now, traverse the array of changed paths, verify locks.  Note
     that if we need to do a recursive verification a path, we'll skip
     over children of that path when we get to them. */
  for (i = 0; i < changed_paths->nelts; i++)
    {
      const char *path;
      svn_fs_path_change2_t *change;
      svn_boolean_t recurse = TRUE;

      svn_pool_clear(subpool);
      path = APR_ARRAY_IDX(changed_paths, i, const char *);

      /* If this path has already been verified as part of a recursive
         check of one of its parents, no need to do it again.  */
      if (last_recursed
          && svn_dirent_is_child(last_recursed->data, path, subpool))
        continue;

      /* Fetch the change associated with our path.  */
      change = apr_hash_get(changes, path, APR_HASH_KEY_STRING);

      /* What does it mean to succeed at lock verification for a given
         path?  For an existing file or directory getting modified
         (text, props), it means we hold the lock on the file or
         directory.  For paths being added or removed, we need to hold
         the locks for that path and any children of that path.

         WHEW!  We have no reliable way to determine the node kind
         of deleted items, but fortunately we are going to do a
         recursive check on deleted paths regardless of their kind.  */
      if (change->change_kind == svn_fs_path_change_modify)
        recurse = FALSE;
      SVN_ERR(svn_fs_fs__allow_locked_operation(path, fs, recurse, TRUE,
                                                subpool));

      /* If we just did a recursive check, remember the path we
         checked (so children can be skipped).  */
      if (recurse)
        {
          if (! last_recursed)
            last_recursed = svn_stringbuf_create(path, pool);
          else
            svn_stringbuf_set(last_recursed, path);
        }
    }
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Baton used for commit_body below. */
struct commit_baton {
  svn_revnum_t *new_rev_p;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  apr_array_header_t *reps_to_cache;
  apr_hash_t *reps_hash;
  apr_pool_t *reps_pool;
};

/* The work-horse for svn_fs_fs__commit, called with the FS write lock.
   This implements the svn_fs_fs__with_write_lock() 'body' callback
   type.  BATON is a 'struct commit_baton *'. */
static svn_error_t *
commit_body(void *baton, apr_pool_t *pool)
{
  struct commit_baton *cb = baton;
  fs_fs_data_t *ffd = cb->fs->fsap_data;
  const char *old_rev_filename, *rev_filename, *proto_filename;
  const char *revprop_filename, *final_revprop;
  const svn_fs_id_t *root_id, *new_root_id;
  apr_uint64_t start_node_id = 0;
  apr_uint64_t start_copy_id = 0;
  svn_revnum_t old_rev, new_rev;
  apr_file_t *proto_file;
  void *proto_file_lockcookie;
  apr_off_t initial_offset, changed_path_offset;
  apr_hash_t *txnprops;
  apr_array_header_t *txnprop_list;
  svn_prop_t prop;
  svn_string_t date;
  const svn_fs_fs__id_part_t *txn_id = svn_fs_fs__txn_get_id(cb->txn);

  /* Get the current youngest revision. */
  SVN_ERR(svn_fs_fs__youngest_rev(&old_rev, cb->fs, pool));

  /* Check to make sure this transaction is based off the most recent
     revision. */
  if (cb->txn->base_rev != old_rev)
    return svn_error_create(SVN_ERR_FS_TXN_OUT_OF_DATE, NULL,
                            _("Transaction out of date"));

  /* Locks may have been added (or stolen) between the calling of
     previous svn_fs.h functions and svn_fs_commit_txn(), so we need
     to re-examine every changed-path in the txn and re-verify all
     discovered locks. */
  SVN_ERR(verify_locks(cb->fs, txn_id, pool));

  /* Get the next node_id and copy_id to use. */
  if (ffd->format < SVN_FS_FS__MIN_NO_GLOBAL_IDS_FORMAT)
    SVN_ERR(get_next_revision_ids(&start_node_id, &start_copy_id, cb->fs,
                                  pool));

  /* We are going to be one better than this puny old revision. */
  new_rev = old_rev + 1;

  /* Get a write handle on the proto revision file. */
  SVN_ERR(get_writable_proto_rev(&proto_file, &proto_file_lockcookie,
                                 cb->fs, txn_id, pool));
  SVN_ERR(get_file_offset(&initial_offset, proto_file, pool));

  /* Write out all the node-revisions and directory contents. */
  root_id = svn_fs_fs__id_txn_create_root(txn_id, pool);
  SVN_ERR(write_final_rev(&new_root_id, proto_file, new_rev, cb->fs, root_id,
                          start_node_id, start_copy_id, initial_offset,
                          cb->reps_to_cache, cb->reps_hash, cb->reps_pool,
                          TRUE, pool));

  /* Write the changed-path information. */
  SVN_ERR(write_final_changed_path_info(&changed_path_offset, proto_file,
                                        cb->fs, txn_id, pool));

  if (ffd->format < SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT)
    {
      /* Write the final line. */

      svn_stringbuf_t *trailer;
      apr_off_t root_offset;
      apr_uint32_t sub_item;
      SVN_ERR(svn_fs_fs__item_offset(&root_offset,
                                     &sub_item,
                                     cb->fs,
                                     svn_fs_fs__id_rev(new_root_id),
                                     NULL,
                                     svn_fs_fs__id_item(new_root_id),
                                     pool));
      SVN_ERR_ASSERT(sub_item == 0);
      trailer = svn_fs_fs__unparse_revision_trailer
                  (root_offset,
                   changed_path_offset,
                   pool);
      SVN_ERR(svn_io_file_write_full(proto_file, trailer->data, trailer->len,
                                     NULL, pool));
    }

  SVN_ERR(svn_io_file_flush_to_disk(proto_file, pool));
  SVN_ERR(svn_io_file_close(proto_file, pool));

  /* We don't unlock the prototype revision file immediately to avoid a
     race with another caller writing to the prototype revision file
     before we commit it. */

  /* Remove any temporary txn props representing 'flags'. */
  SVN_ERR(svn_fs_fs__txn_proplist(&txnprops, cb->txn, pool));
  txnprop_list = apr_array_make(pool, 3, sizeof(svn_prop_t));
  prop.value = NULL;

  if (apr_hash_get(txnprops, SVN_FS__PROP_TXN_CHECK_OOD, APR_HASH_KEY_STRING))
    {
      prop.name = SVN_FS__PROP_TXN_CHECK_OOD;
      APR_ARRAY_PUSH(txnprop_list, svn_prop_t) = prop;
    }

  if (apr_hash_get(txnprops, SVN_FS__PROP_TXN_CHECK_LOCKS,
                   APR_HASH_KEY_STRING))
    {
      prop.name = SVN_FS__PROP_TXN_CHECK_LOCKS;
      APR_ARRAY_PUSH(txnprop_list, svn_prop_t) = prop;
    }

  if (! apr_is_empty_array(txnprop_list))
    SVN_ERR(svn_fs_fs__change_txn_props(cb->txn, txnprop_list, pool));

  /* Create the shard for the rev and revprop file, if we're sharding and
     this is the first revision of a new shard.  We don't care if this
     fails because the shard already existed for some reason. */
  if (ffd->max_files_per_dir && new_rev % ffd->max_files_per_dir == 0)
    {
      /* Create the revs shard. */
        {
          const char *new_dir = path_rev_shard(cb->fs, new_rev, pool);
          svn_error_t *err = svn_io_dir_make(new_dir, APR_OS_DEFAULT, pool);
          if (err && !APR_STATUS_IS_EEXIST(err->apr_err))
            return svn_error_trace(err);
          svn_error_clear(err);
          SVN_ERR(svn_io_copy_perms(svn_dirent_join(cb->fs->path,
                                                    PATH_REVS_DIR,
                                                    pool),
                                    new_dir, pool));
        }

      /* Create the revprops shard. */
      SVN_ERR_ASSERT(! is_packed_revprop(cb->fs, new_rev));
        {
          const char *new_dir = path_revprops_shard(cb->fs, new_rev, pool);
          svn_error_t *err = svn_io_dir_make(new_dir, APR_OS_DEFAULT, pool);
          if (err && !APR_STATUS_IS_EEXIST(err->apr_err))
            return svn_error_trace(err);
          svn_error_clear(err);
          SVN_ERR(svn_io_copy_perms(svn_dirent_join(cb->fs->path,
                                                    PATH_REVPROPS_DIR,
                                                    pool),
                                    new_dir, pool));
        }
    }

  if (ffd->format >= SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT)
    {
      /* Convert the index files from the proto format into their form
         in their final location */
      SVN_ERR(svn_fs_fs__l2p_index_create(cb->fs,
                                          path_l2p_index(cb->fs, new_rev,
                                                         pool),
                                          path_l2p_proto_index(cb->fs,
                                                               txn_id,
                                                               pool),
                                          new_rev, pool));
      SVN_ERR(svn_fs_fs__p2l_index_create(cb->fs,
                                          path_p2l_index(cb->fs, new_rev,
                                                         pool),
                                          path_p2l_proto_index(cb->fs,
                                                               txn_id,
                                                               pool),
                                          new_rev, pool));
    }

  /* Move the finished rev file into place. */
  old_rev_filename = svn_fs_fs__path_rev_absolute(cb->fs, old_rev, pool);

  rev_filename = path_rev(cb->fs, new_rev, pool);
  proto_filename = path_txn_proto_rev(cb->fs, txn_id, pool);
  SVN_ERR(move_into_place(proto_filename, rev_filename, old_rev_filename,
                          pool));

  /* Now that we've moved the prototype revision file out of the way,
     we can unlock it (since further attempts to write to the file
     will fail as it no longer exists).  We must do this so that we can
     remove the transaction directory later. */
  SVN_ERR(unlock_proto_rev(cb->fs, txn_id, proto_file_lockcookie, pool));

  /* Update commit time to ensure that svn:date revprops remain ordered. */
  date.data = svn_time_to_cstring(apr_time_now(), pool);
  date.len = strlen(date.data);

  SVN_ERR(svn_fs_fs__change_txn_prop(cb->txn, SVN_PROP_REVISION_DATE,
                                     &date, pool));

  /* Move the revprops file into place. */
  SVN_ERR_ASSERT(! is_packed_revprop(cb->fs, new_rev));
  revprop_filename = path_txn_props(cb->fs, txn_id, pool);
  final_revprop = path_revprops(cb->fs, new_rev, pool);
  SVN_ERR(move_into_place(revprop_filename, final_revprop,
                          old_rev_filename, pool));

  /* Update the 'current' file. */
  SVN_ERR(write_final_current(cb->fs, txn_id, new_rev, start_node_id,
                              start_copy_id, pool));

  /* At this point the new revision is committed and globally visible
     so let the caller know it succeeded by giving it the new revision
     number, which fulfills svn_fs_commit_txn() contract.  Any errors
     after this point do not change the fact that a new revision was
     created. */
  *cb->new_rev_p = new_rev;

  ffd->youngest_rev_cache = new_rev;

  /* Remove this transaction directory. */
  SVN_ERR(svn_fs_fs__purge_txn(cb->fs, cb->txn->id, pool));

  return SVN_NO_ERROR;
}

/* Add the representations in REPS_TO_CACHE (an array of representation_t *)
 * to the rep-cache database of FS. */
static svn_error_t *
write_reps_to_cache(svn_fs_t *fs,
                    const apr_array_header_t *reps_to_cache,
                    apr_pool_t *scratch_pool)
{
  int i;

  for (i = 0; i < reps_to_cache->nelts; i++)
    {
      representation_t *rep = APR_ARRAY_IDX(reps_to_cache, i, representation_t *);

      /* FALSE because we don't care if another parallel commit happened to
       * collide with us.  (Non-parallel collisions will not be detected.) */
      SVN_ERR(svn_fs_fs__set_rep_reference(fs, rep, FALSE, scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__commit(svn_revnum_t *new_rev_p,
                  svn_fs_t *fs,
                  svn_fs_txn_t *txn,
                  apr_pool_t *pool)
{
  struct commit_baton cb;
  fs_fs_data_t *ffd = fs->fsap_data;

  cb.new_rev_p = new_rev_p;
  cb.fs = fs;
  cb.txn = txn;

  if (ffd->rep_sharing_allowed)
    {
      cb.reps_to_cache = apr_array_make(pool, 5, sizeof(representation_t *));
      cb.reps_hash = apr_hash_make(pool);
      cb.reps_pool = pool;
    }
  else
    {
      cb.reps_to_cache = NULL;
      cb.reps_hash = NULL;
      cb.reps_pool = NULL;
    }

  SVN_ERR(svn_fs_fs__with_write_lock(fs, commit_body, &cb, pool));

  /* At this point, *NEW_REV_P has been set, so errors below won't affect
     the success of the commit.  (See svn_fs_commit_txn().)  */

  if (ffd->rep_sharing_allowed)
    {
      SVN_ERR(svn_fs_fs__open_rep_cache(fs, pool));

      /* Write new entries to the rep-sharing database.
       *
       * We use an sqlite transaction to speed things up;
       * see <http://www.sqlite.org/faq.html#q19>.
       */
      SVN_SQLITE__WITH_TXN(
        write_reps_to_cache(fs, cb.reps_to_cache, pool),
        ffd->rep_cache_db);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__list_transactions(apr_array_header_t **names_p,
                             svn_fs_t *fs,
                             apr_pool_t *pool)
{
  const char *txn_dir;
  apr_hash_t *dirents;
  apr_hash_index_t *hi;
  apr_array_header_t *names;
  apr_size_t ext_len = strlen(PATH_EXT_TXN);

  names = apr_array_make(pool, 1, sizeof(const char *));

  /* Get the transactions directory. */
  txn_dir = svn_dirent_join(fs->path, PATH_TXNS_DIR, pool);

  /* Now find a listing of this directory. */
  SVN_ERR(svn_io_get_dirents3(&dirents, txn_dir, TRUE, pool, pool));

  /* Loop through all the entries and return anything that ends with '.txn'. */
  for (hi = apr_hash_first(pool, dirents); hi; hi = apr_hash_next(hi))
    {
      const char *name = svn__apr_hash_index_key(hi);
      apr_ssize_t klen = svn__apr_hash_index_klen(hi);
      const char *id;

      /* The name must end with ".txn" to be considered a transaction. */
      if ((apr_size_t) klen <= ext_len
          || (strcmp(name + klen - ext_len, PATH_EXT_TXN)) != 0)
        continue;

      /* Truncate the ".txn" extension and store the ID. */
      id = apr_pstrndup(pool, name, strlen(name) - ext_len);
      APR_ARRAY_PUSH(names, const char *) = id;
    }

  *names_p = names;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__open_txn(svn_fs_txn_t **txn_p,
                    svn_fs_t *fs,
                    const char *name,
                    apr_pool_t *pool)
{
  svn_fs_txn_t *txn;
  fs_txn_data_t *ftd;
  svn_node_kind_t kind;
  transaction_t *local_txn;
  svn_fs_fs__id_part_t txn_id;

  SVN_ERR(svn_fs_fs__id_txn_parse(&txn_id, name));

  /* First check to see if the directory exists. */
  SVN_ERR(svn_io_check_path(path_txn_dir(fs, &txn_id, pool), &kind, pool));

  /* Did we find it? */
  if (kind != svn_node_dir)
    return svn_error_createf(SVN_ERR_FS_NO_SUCH_TRANSACTION, NULL,
                             _("No such transaction '%s'"),
                             name);

  txn = apr_pcalloc(pool, sizeof(*txn));
  ftd = apr_pcalloc(pool, sizeof(*ftd));
  ftd->txn_id = txn_id;

  /* Read in the root node of this transaction. */
  txn->id = apr_pstrdup(pool, name);
  txn->fs = fs;

  SVN_ERR(svn_fs_fs__get_txn(&local_txn, fs, &txn_id, pool));

  txn->base_rev = svn_fs_fs__id_rev(local_txn->base_id);

  txn->vtable = &txn_vtable;
  txn->fsap_data = ftd;
  *txn_p = txn;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__txn_proplist(apr_hash_t **table_p,
                        svn_fs_txn_t *txn,
                        apr_pool_t *pool)
{
  apr_hash_t *proplist = apr_hash_make(pool);
  SVN_ERR(get_txn_proplist(proplist, txn->fs, svn_fs_fs__txn_get_id(txn),
                           pool));
  *table_p = proplist;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__delete_node_revision(svn_fs_t *fs,
                                const svn_fs_id_t *id,
                                apr_pool_t *pool)
{
  node_revision_t *noderev;

  SVN_ERR(svn_fs_fs__get_node_revision(&noderev, fs, id, pool));

  /* Delete any mutable property representation. */
  if (noderev->prop_rep
      && svn_fs_fs__id_txn_used(&noderev->prop_rep->txn_id))
    SVN_ERR(svn_io_remove_file2(path_txn_node_props(fs, id, pool), FALSE,
                                pool));

  /* Delete any mutable data representation. */
  if (noderev->data_rep
      && svn_fs_fs__id_txn_used(&noderev->data_rep->txn_id)
      && noderev->kind == svn_node_dir)
    {
      fs_fs_data_t *ffd = fs->fsap_data;
      SVN_ERR(svn_io_remove_file2(path_txn_node_children(fs, id, pool), FALSE,
                                  pool));

      /* remove the corresponding entry from the cache, if such exists */
      if (ffd->txn_dir_cache)
        {
          const char *key = svn_fs_fs__id_unparse(id, pool)->data;
          SVN_ERR(svn_cache__set(ffd->txn_dir_cache, key, NULL, pool));
        }
    }

  return svn_io_remove_file2(path_txn_node_rev(fs, id, pool), FALSE, pool);
}



/*** Transactions ***/

svn_error_t *
svn_fs_fs__get_txn_ids(const svn_fs_id_t **root_id_p,
                       const svn_fs_id_t **base_root_id_p,
                       svn_fs_t *fs,
                       const svn_fs_fs__id_part_t *txn_id,
                       apr_pool_t *pool)
{
  transaction_t *txn;
  SVN_ERR(svn_fs_fs__get_txn(&txn, fs, txn_id, pool));
  *root_id_p = txn->root_id;
  *base_root_id_p = txn->base_id;
  return SVN_NO_ERROR;
}


/* Generic transaction operations.  */

svn_error_t *
svn_fs_fs__txn_prop(svn_string_t **value_p,
                    svn_fs_txn_t *txn,
                    const char *propname,
                    apr_pool_t *pool)
{
  apr_hash_t *table;
  svn_fs_t *fs = txn->fs;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));
  SVN_ERR(svn_fs_fs__txn_proplist(&table, txn, pool));

  *value_p = apr_hash_get(table, propname, APR_HASH_KEY_STRING);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__begin_txn(svn_fs_txn_t **txn_p,
                     svn_fs_t *fs,
                     svn_revnum_t rev,
                     apr_uint32_t flags,
                     apr_pool_t *pool)
{
  svn_string_t date;
  svn_prop_t prop;
  apr_array_header_t *props = apr_array_make(pool, 3, sizeof(svn_prop_t));

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  SVN_ERR(svn_fs_fs__create_txn(txn_p, fs, rev, pool));

  /* Put a datestamp on the newly created txn, so we always know
     exactly how old it is.  (This will help sysadmins identify
     long-abandoned txns that may need to be manually removed.)  When
     a txn is promoted to a revision, this property will be
     automatically overwritten with a revision datestamp. */
  date.data = svn_time_to_cstring(apr_time_now(), pool);
  date.len = strlen(date.data);

  prop.name = SVN_PROP_REVISION_DATE;
  prop.value = &date;
  APR_ARRAY_PUSH(props, svn_prop_t) = prop;

  /* Set temporary txn props that represent the requested 'flags'
     behaviors. */
  if (flags & SVN_FS_TXN_CHECK_OOD)
    {
      prop.name = SVN_FS__PROP_TXN_CHECK_OOD;
      prop.value = svn_string_create("true", pool);
      APR_ARRAY_PUSH(props, svn_prop_t) = prop;
    }

  if (flags & SVN_FS_TXN_CHECK_LOCKS)
    {
      prop.name = SVN_FS__PROP_TXN_CHECK_LOCKS;
      prop.value = svn_string_create("true", pool);
      APR_ARRAY_PUSH(props, svn_prop_t) = prop;
    }

  return svn_fs_fs__change_txn_props(*txn_p, props, pool);
}
