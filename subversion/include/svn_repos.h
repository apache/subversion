/* svn_repos.h :  tools built on top of the filesystem.
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


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef SVN_REPOS_H
#define SVN_REPOS_H

#include <apr_pools.h>
#include <apr_hash.h>
#include "svn_fs.h"
#include "svn_delta.h"
#include "svn_types.h"
#include "svn_error.h"


/* ---------------------------------------------------------------*/


/* The repository object. */
typedef struct svn_repos_t svn_repos_t;

/* Opening and creating repositories. */


/* Set *REPOS_P to a repository object for the repository at
   PATH.  Allocate *REPOS_P in POOL.

   Acquires a shared lock on the repository, and attaches a cleanup
   function to POOL to remove the lock.  If no lock can be acquired,
   returns error, with undefined effect on *REPOS_P.  If an exclusive
   lock is present, this blocks until it's gone.  */
svn_error_t *svn_repos_open (svn_repos_t **repos_p,
                             const char *path,
                             apr_pool_t *pool);

/* Create a new Subversion repository at PATH, building the necessary
   directory structure, creating the Berkeley DB filesystem
   environment, and so on.  Return the repository object in *REPOS_P,
   allocated in POOL. */
svn_error_t *svn_repos_create (svn_repos_t **repos_p, 
                               const char *path, 
                               apr_pool_t *pool);

/* Close the Subversion repository object REPOS. */
svn_error_t *svn_repos_close (svn_repos_t *repos);

/* Destroy the Subversion repository found at PATH.  Use POOL for any
   necessary allocations. */
svn_error_t *svn_repos_delete (const char *path, apr_pool_t *pool);

/* Return the filesystem associated with repository object REPOS. */
svn_fs_t *svn_repos_fs (svn_repos_t *repos);


/* Repository Paths */

/* Return the top-level repository path allocated in POOL. */
const char *svn_repos_path (svn_repos_t *repos, apr_pool_t *pool);

/* Return the path to REPOS's Berkeley DB environment, allocated in
   POOL. */
const char *svn_repos_db_env (svn_repos_t *repos, apr_pool_t *pool);

/* Return the path to REPOS's configuration directory, allocated in
   POOL. */
const char *svn_repos_conf_dir (svn_repos_t *repos, apr_pool_t *pool);

/* Return path to REPOS's lock directory or db lockfile, respectively,
   allocated in POOL. */
const char *svn_repos_lock_dir (svn_repos_t *repos, apr_pool_t *pool);
const char *svn_repos_db_lockfile (svn_repos_t *repos, apr_pool_t *pool);

/* Return the path to REPOS's hook directory, allocated in POOL. */
const char *svn_repos_hook_dir (svn_repos_t *repos, apr_pool_t *pool);

/* Return the path to REPOS's start-commit hook, pre-commit hook,
   post-commit hook, read sentinel, and write sentinel programs,
   respectively, allocated in POOL. */
const char *svn_repos_start_commit_hook (svn_repos_t *repos, apr_pool_t *pool);
const char *svn_repos_pre_commit_hook (svn_repos_t *repos, apr_pool_t *pool);
const char *svn_repos_post_commit_hook (svn_repos_t *repos, apr_pool_t *pool);
const char *svn_repos_read_sentinel_hook (svn_repos_t *repos, apr_pool_t *pool);
const char *svn_repos_write_sentinel_hook (svn_repos_t *repos, apr_pool_t *pool);



/* ---------------------------------------------------------------*/

/*** Reporting the state of a working copy, for updates. */


/* Construct and return a REPORT_BATON that will be paired with some
   svn_ra_reporter_t table.  The table and baton are used to build a
   transaction in the system;  when the report is finished,
   svn_repos_dir_delta is called on the transaction, driving
   EDITOR/EDIT_BATON. 

   Specifically, the report will create a transaction made by
   USERNAME, relative to FS_BASE in the filesystem.  TARGET is a
   single path component, used to limit the scope of the report to a
   single entry of FS_BASE, or NULL if all of FS_BASE itself is the
   main subject of the report.

   TGT_PATH and REVNUM is the fs path/revision pair that is the
   "target" of dir_delta.  In other words, a tree delta will be
   returned that transforms the transaction into TGT_PATH/REVNUM.
   TGT_PATH may (indeed, should) be NULL when the source and target
   paths of the report are the same.  That is, TGT_PATH should *only*
   be specified when specifying that the resultant editor drive be one
   that tranforms the reported heirarchy into a pristing tree of
   TGT_PATH at revision REVNUM.  Else, a NULL value for TGT_PATH will
   indicate that the editor should be driven in such a way as to
   transform the reported heirarchy to revision REVNUM, preserving the
   reported heirarchy.

   TEXT_DELTAS instructs the driver of the EDITOR to enable to disable
   the generation of text deltas.

   RECURSE instructs the driver of the EDITOR to send a recursive
   delta (or not.)

   All allocation for the context and collected state will occur in
   POOL. */
svn_error_t *
svn_repos_begin_report (void **report_baton,
                        svn_revnum_t revnum,
                        const char *username,
                        svn_repos_t *repos,
                        const char *fs_base,
                        const char *target,
                        const char *tgt_path,
                        svn_boolean_t text_deltas,
                        svn_boolean_t recurse,
                        const svn_delta_edit_fns_t *editor,
                        void *edit_baton,
                        apr_pool_t *pool);


/* Given a REPORT_BATON constructed by svn_repos_begin_report(), this
   routine will build REVISION:PATH into the current transaction.
   This routine is called multiple times to create a transaction that
   is a "mirror" of a working copy.

   The first call of this in a given report usually passes an empty
   PATH; that allows the reporter to set up the correct root revision
   (useful when creating a txn, for example).  */
svn_error_t *svn_repos_set_path (void *report_baton,
                                 const char *path,
                                 svn_revnum_t revision);


/* Given a REPORT_BATON constructed by svn_repos_begin_report(), this
   routine will build REVISION:LINK_PATH into the current transaction
   at PATH.  Note that while PATH is relative to the anchor/target
   used in the creation of the REPORT_BATON, LINK_PATH is an absolute
   filesystem path!  */
svn_error_t *svn_repos_link_path (void *report_baton,
                                  const char *path,
                                  const char *link_path,
                                  svn_revnum_t revision);

/* Given a REPORT_BATON constructed by svn_repos_begin_report(), this
   routine will remove PATH from the current fs transaction. 

   (This allows the reporter's driver to describe missing pieces of a
   working copy, so that 'svn up' can recreate them.) */   
svn_error_t *svn_repos_delete_path (void *report_baton,
                                    const char *path);

/* Make the filesystem compare the transaction to a revision and have
   it drive an update editor (using svn_repos_delta_dirs()).  Then
   abort the transaction. */
svn_error_t *svn_repos_finish_report (void *report_baton);


/* The report-driver is bailing, so abort the fs transaction. */
svn_error_t *svn_repos_abort_report (void *report_baton);


/* ---------------------------------------------------------------*/

/*** The magical dir_delta update routines. */

/* Use the provided EDITOR and EDIT_BATON to describe the changes
   necessary for making a given node (and its descendants, if it a
   directory) under SRC_ROOT look exactly like TGT_PATH under
   TGT_ROOT.  SRC_ENTRY is the node to update, and is either NULL or a
   single path component.  If SRC_ENTRY is NULL, then compute the
   difference between the entire tree anchored at SRC_PARENT_DIR under
   SRC_ROOT and TGT_PATH under TARGET_ROOT.  Else, describe the
   changes needed to update only that entry in SRC_PARENT_DIR.
   Typically, callers of this function will use a TGT_PATH that is the
   concatenation of SRC_PARENT_DIR and SRC_ENTRY.

   SRC_ROOT and TGT_ROOT can both be either revision or transaction
   roots.  If TGT_ROOT is a revision, EDITOR's set_target_revision()
   will be called with the TGT_ROOT's revision number, else it will
   not be called at all.

   SRC_REVS is a hash whose keys are character string paths, and whose
   values are pointers to svn_revnum_t's, which describes the base
   revisions of the items in the SRC_PARENT tree.  This hash need only
   contain the base revision for the top of that tree, and then those
   paths that have a base revision that differs from that of their
   parent directory.

   If TEXT_DELTAS is FALSE, only a single NULL txdelta window will be
   sent to the window handler returned by EDITOR->apply_textdelta().

   If ENTRY_PROPS is TRUE, each open/added entry will be accompanied
   by propchange editor calls that relay special "entry props" (this
   is typically used only for working copy updates).

   USE_COPYFROM_ARGS determines whether or not the editor's add_file
   and add_directory functions will be called with copyfrom_*
   arguments.  That is to say, if a node that needs to be added can be
   optimized by simply copying another node that already exists in the
   source tree, svn_repos_dir_delta might ask that such a copy take
   place.

   Before completing successfully, this function calls EDITOR's
   close_edit(), so the caller should expect its EDIT_BATON to be
   invalid after its use with this function.

   Do any allocation necessary for the delta computation in POOL.
   This function's maximum memory consumption is at most roughly
   proportional to the greatest depth of the tree under TGT_ROOT, not
   the total size of the delta.
*/
svn_error_t *
svn_repos_dir_delta (svn_fs_root_t *src_root,
                     const char *src_parent_dir,
                     const char *src_entry,
                     apr_hash_t *src_revs,
                     svn_fs_root_t *tgt_root,
                     const char *tgt_path,
                     const svn_delta_edit_fns_t *editor,
                     void *edit_baton,
                     svn_boolean_t text_deltas,
                     svn_boolean_t recurse,
                     svn_boolean_t entry_props,
                     svn_boolean_t use_copyfrom_args,
                     apr_pool_t *pool);


/* ---------------------------------------------------------------*/

/*** Finding particular revisions. */

/* Set *REVISION to the revision number in REPOS's filesystem that was
   youngest at time TM. */
svn_error_t *
svn_repos_dated_revision (svn_revnum_t *revision,
                          svn_repos_t *repos,
                          apr_time_t tm,
                          apr_pool_t *pool);
                          

/*  Given a ROOT/PATH within some filesystem, return three pieces of
    information allocated in POOL:

      - set *COMMITTED_REV to the revision in which the object was
        last modified.  (In fs parlance, this is the revision in which
        the particular node-rev-id was 'created'.)
    
      - set *COMMITTED_DATE to the date of said revision.

      - set *LAST_AUTHOR to the author of said revision.    
 */
svn_error_t *
svn_repos_get_committed_info (svn_revnum_t *committed_rev,
                              svn_string_t **committed_date,
                              svn_string_t **last_author,
                              svn_fs_root_t *root,
                              const svn_string_t *path,
                              apr_pool_t *pool);

/* ### other queries we can do someday --

     * fetch the last revision created by <user>
         (once usernames become revision properties!)
     * fetch the last revision where <path> was modified
     
*/


/* ---------------------------------------------------------------*/

/*** Retrieving log messages. */


/* Invoke RECEIVER with RECEIVER_BATON on each log message from START
 * to END in REPOS's fileystem.  START may be greater or less than
 * END; this just controls whether the log messages are processed in
 * descending or ascending revision number order.
 *
 * If START or END is SVN_INVALID_REVNUM, it defaults to youngest.
 *
 * If PATHS is non-null and has one or more elements, then only show
 * revisions in which at least one of PATHS was changed (i.e., if
 * file, text or props changed; if dir, props changed or an entry was
 * added or deleted).  Each path is an (svn_stringbuf_t *)
 * representing an absolute path in the repository.
 *
 * ### todo: need to consider whether the above directory behavior is
 * most useful, or if we should actually treat _any_ node change in a
 * directory as a visible change for purposes of log... i.e., show
 * bubble-up.  The reason this might be useful is so that running log
 * on a directory would give a msg for every change under that dir,
 * no matter how far down.  See the thread started on the dev list by
 * Lars Kellogg-Stedman <lars@larsshack.org> with the subject
 * "Single repository, multiple projects?" for more.  We may simple
 * need to offer a few different semantics for PATHS.
 *
 * If DISCOVER_CHANGED_PATHS, then each call to RECEIVER passes a
 * `const apr_hash_t *' for the receiver's CHANGED_PATHS argument; the
 * hash's keys are all the paths committed in that revision.
 * Otherwise, each call to RECEIVER passes null for CHANGED_PATHS.
 *
 * ### NOTE: PATHS and DISCOVER_CHANGED_PATHS are currently ignored,
 * see http://subversion.tigris.org/issues/show_bug.cgi?id=562 for
 * more information.
 *
 * If any invocation of RECEIVER returns error, return that error
 * immediately and without wrapping it.
 *
 * If START or END is a non-existent revision, return the error
 * SVN_ERR_FS_NO_SUCH_REVISION, without ever invoking RECEIVER.
 *
 * See also the documentation for `svn_log_message_receiver_t'.
 *
 * Use POOL for temporary allocations.  */
svn_error_t *
svn_repos_get_logs (svn_repos_t *repos,
                    const apr_array_header_t *paths,
                    svn_revnum_t start,
                    svn_revnum_t end,
                    svn_boolean_t discover_changed_paths,
                    svn_log_message_receiver_t receiver,
                    void *receiver_baton,
                    apr_pool_t *pool);


/* ---------------------------------------------------------------*/

/*** Hook-sensitive wrappers for libsvn_fs routines. ***/


/* Like svn_fs_commit_txn(), but invoke the REPOS's pre- and
 * post-commit hooks around the commit.  Use TXN's pool for temporary
 * allocations.
 *
 * CONFLICT_P, NEW_REV, and TXN are as in svn_fs_commit_txn().  */
svn_error_t *svn_repos_fs_commit_txn (const char **conflict_p,
                                      svn_repos_t *repos,
                                      svn_revnum_t *new_rev,
                                      svn_fs_txn_t *txn);

/* Like svn_fs_begin_txn(), but use AUTHOR and LOG_MSG to set the
 * corresponding properties on transaction *TXN_P.  REPOS is the
 * repository object which contains the filesystem.  REV, *TXN_P, and
 * POOL are as in svn_fs_begin_txn().
 *
 * Before a txn is created, the repository's start-commit hooks are
 * run; if any of them fail, no txn is created, *TXN_P is
 * unaffected, and SVN_ERR_REPOS_HOOK_FAILURE is returned.
 *
 * LOG_MSG may be NULL to indicate the message is not (yet) available.
 * The caller will need to attach it to the transaction at a later time.  */
svn_error_t *svn_repos_fs_begin_txn_for_commit (svn_fs_txn_t **txn_p,
                                                svn_repos_t *repos,
                                                svn_revnum_t rev,
                                                const char *author,
                                                svn_string_t *log_msg,
                                                apr_pool_t *pool);


/* Like svn_fs_begin_txn(), but use AUTHOR to set the corresponding
 * property on transaction *TXN_P.  REPOS is the repository object
 * which contains the filesystem.  REV, *TXN_P, and POOL are as in
 * svn_fs_begin_txn().
 *
 * ### Someday: before a txn is created, some kind of read-hook could
 *              be called here. */
svn_error_t *svn_repos_fs_begin_txn_for_update (svn_fs_txn_t **txn_p,
                                                svn_repos_t *repos,
                                                svn_revnum_t rev,
                                                const char *author,
                                                apr_pool_t *pool);



/* ---------------------------------------------------------------*/

/*** Data structures and editor things for repository inspection. ***/

/* As it turns out, the svn_repos_dir_delta() interface can be
 * extremely useful for examining the repository, or more exactly,
 * changes to the repository.  svn_repos_dir_delta() allows for
 * differences between two trees to be described using an editor.
 *
 * By using the specific editor found below in conjunction with
 * svn_repos_dir_delta(), the description of how to transform one tree
 * into another can be used to build an in-memory linked-list tree,
 * which each node representing a repository node that was changed as a
 * result of having svn_repos_dir_delta() drive that editor.
 */

typedef struct svn_repos_node_t
{
  /* Node type (file, dir, etc.) */
  enum svn_node_kind kind;

  /* How this node entered the node tree: 'A'dd, 'D'elete, 'R'eplace */
  char action; 

  /* Were there any textual mods? (files only) */
  svn_boolean_t text_mod;

  /* Where there any property mods? */
  svn_boolean_t prop_mod;

  /* The name of this node as it appears in its parent's entries list */
  const char *name;

  /* The filesystem revision and path where this was copied from (if any) */
  svn_revnum_t copyfrom_rev;
  const char *copyfrom_path;

  /* Pointer to the next sibling of this node */
  struct svn_repos_node_t *sibling;

  /* Pointer to the first child of this node */
  struct svn_repos_node_t *child;

} svn_repos_node_t;


/* Set *EDITOR and *EDIT_BATON to an editor that, when driven by
   svn_repos_dir_delta(), builds an `svn_repos_node_t *' tree
   representing the delta from BASE_ROOT to ROOT in REPOS's filesystem.
   
   Invoke svn_repos_node_from_baton() on EDIT_BATON to obtain the root
   node afterwards.

   Note that the delta includes "bubbled-up" directories; that is,
   many of the directory nodes will have no prop_mods.

   Allocate the tree and its contents in NODE_POOL; do all other
   allocation in POOL.  */
svn_error_t *svn_repos_node_editor (const svn_delta_edit_fns_t **editor,
                                    void **edit_baton,
                                    svn_repos_t *repos,
                                    svn_fs_root_t *base_root,
                                    svn_fs_root_t *root,
                                    apr_pool_t *node_pool,
                                    apr_pool_t *pool);

/* Return the root node of the linked-list tree generated by driving
   the editor created by svn_repos_node_editor() with
   svn_repos_dir_delta(), which is stored in EDIT_BATON.  This is only
   really useful if used *after* the editor drive is completed. */
svn_repos_node_t *svn_repos_node_from_baton (void *edit_baton);



/* ---------------------------------------------------------------*/

/*** Dumping and loading filesystem data ***/

/*  The filesystem 'dump' format contains nothing but the abstract
    structure of the filesystem -- independent of any internal node-id
    schema or database back-end.  All of the data in the dumpfile is
    acquired by public function calls into svn_fs.h.  Similarly, the
    parser which reads the dumpfile is able to reconstruct the
    filesystem using only public svn_fs.h routines.

    Thus the dump/load feature's main purpose is for *migrating* data
    from one svn filesystem to another -- presumably two filesystems
    which have different internal implementations.

    If you simply want to backup your filesystem, you're probably
    better off using the built-in facilities of the DB backend (using
    Berkeley DB's hot-backup feature, for example.)
    
    For a description of the dumpfile format, see
    /trunk/notes/fs_dumprestore.txt.
*/

/* The RFC822-style headers in our dumpfile format. */
#define SVN_REPOS_DUMPFILE_MAGIC_HEADER            "SVN-fs-dump-format-version"
#define SVN_REPOS_DUMPFILE_FORMAT_VERSION           1

#define SVN_REPOS_DUMPFILE_REVISION_NUMBER           "Revision-number"
#define SVN_REPOS_DUMPFILE_REVISION_CONTENT_CHECKSUM "Revision-content-md5"
#define SVN_REPOS_DUMPFILE_CONTENT_LENGTH            "Content-length"

#define SVN_REPOS_DUMPFILE_NODE_PATH                 "Node-path"
#define SVN_REPOS_DUMPFILE_NODE_KIND                 "Node-kind"
#define SVN_REPOS_DUMPFILE_NODE_ACTION               "Node-action"
#define SVN_REPOS_DUMPFILE_NODE_COPYFROM_PATH        "Node-copyfrom-path"
#define SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV         "Node-copyfrom-rev"
#define SVN_REPOS_DUMPFILE_NODE_COPY_SOURCE_CHECKSUM "Node-copy-source-md5"
#define SVN_REPOS_DUMPFILE_NODE_CONTENT_CHECKSUM     "Node-content-md5"


/* The different "actions" attached to nodes in the dumpfile. */
enum svn_node_action
{
  svn_node_action_change,
  svn_node_action_add,
  svn_node_action_delete,
  svn_node_action_replace
};


/* Dump the contents of the filesystem within already-open REPOS into
   writable STREAM.  Begin at revision START_REV, and dump every
   revision up through END_REV.  Use POOL for all allocation.

   If START_REV is SVN_INVALID_REVNUM, then start dumping at revision 0.
   If END_REV is SVN_INVALID_REVNUM, then dump through the HEAD revision.
*/
svn_error_t *svn_repos_dump_fs (svn_repos_t *repos,
                                svn_stream_t *stream,
                                svn_revnum_t start_rev,
                                svn_revnum_t end_rev,
                                apr_pool_t *pool);


/* Read and parse dumpfile-formatted DUMPSTREAM, reconstructing
   filesystem revisions in already-open REPOS.  Use POOL for all
   allocation.  If non-NULL, the parser will send feedback to
   FEEDBACK_STREAM.

   ### Describe a policy/interface for adding revisions to a non-empty
       repository.  Also, someday create an interface for adding
       revisions to a -subdir- of existing repository?
 */
svn_error_t *svn_repos_load_fs (svn_repos_t *repos,
                                svn_stream_t *dumpstream,
                                svn_stream_t *feedback_stream,
                                apr_pool_t *pool);


/* A vtable that is driven by svn_repos_parse_dumpstream. */
typedef struct svn_repos_parse_fns_t
{
  /* The parser has discovered a new revision record within the
     parsing session represented by PARSE_BATON.  All the headers are
     placed in HEADERS (allocated in POOL), which maps (const char *)
     header-name ==> (const char *) header-value.  The REVISION_BATON
     received back (also allocated in POOL) represents the revision. */
  svn_error_t *(*new_revision_record) (void **revision_baton,
                                       apr_hash_t *headers,
                                       void *parse_baton,
                                       apr_pool_t *pool);

  /* The parser has discovered a new node record within the current
     revision represented by REVISION_BATON.  All the headers are
     placed in HEADERS as above, allocated in POOL.  The NODE_BATON
     received back is allocated in POOL and represents the node. */
  svn_error_t *(*new_node_record) (void **node_baton,
                                   apr_hash_t *headers,
                                   void *revision_baton,
                                   apr_pool_t *pool);

  /* For a given REVISION_BATON, set a property NAME to VALUE. */
  svn_error_t *(*set_revision_property) (void *revision_baton,
                                         const char *name,
                                         const svn_string_t *value);

  /* For a given NODE_BATON, set a property NAME to VALUE. */
  svn_error_t *(*set_node_property) (void *node_baton,
                                     const char *name,
                                     const svn_string_t *value);

  /* For a given NODE_BATON, receive a writable STREAM capable of
     receiving the node's fulltext.  After writing the fulltext, call
     the stream's close() function.

     If a NULL is returned instead of a stream, the vtable is
     indicating that no text is desired, and the parser will not
     attempt to send it.  */
  svn_error_t *(*set_fulltext) (svn_stream_t **stream,
                                void *node_baton);

  /* The parser has reached the end of the current node represented by
     NODE_BATON, it can be freed. */
  svn_error_t *(*close_node) (void *node_baton);

  /* The parser has reached the end of the current revision
     represented by REVISION_BATON.  In other words, there are no more
     changed nodes within the revision.  The baton can be freed. */
  svn_error_t *(*close_revision) (void *revision_baton);

} svn_repos_parser_fns_t;



/* Read and parse dumpfile-formatted STREAM, calling callbacks in
   PARSE_FNS/PARSE_BATON, and using POOL for allocations.

   This parser has built-in knowledge of the dumpfile format, but only
   in a general sense:

      * it recognizes revision and node records by looking for either
        a REVISION_NUMBER or NODE_PATH headers.

      * it recognizes the CONTENT-LENGTH headers, so it knows if and
        how to suck up the content body.

      * it knows how to parse a content body into two parts:  props
        and text, and pass the pieces to the vtable.

   This is enough knowledge to make it easy on vtable implementors,
   but still allow expansion of the format:  most headers are ignored.
*/
svn_error_t *
svn_repos_parse_dumpstream (svn_stream_t *stream,
                            const svn_repos_parser_fns_t *parse_fns,
                            void *parse_baton,
                            apr_pool_t *pool);


/* Set *PARSER and *PARSE_BATON to a vtable parser which commits new
   revisions to the fs in REPOS.  Use POOL to operate on the fs.

   If USE_HISTORY is set, then the parser will require relative
   'copyfrom' history to exist in the repository when it encounters
   nodes that are added-with-history.

   Print all parsing feedback to OUTSTREAM (if non-NULL).
*/
svn_error_t *
svn_repos_get_fs_build_parser (const svn_repos_parser_fns_t **parser,
                               void **parse_baton,
                               svn_repos_t *repos,
                               svn_boolean_t use_history,
                               svn_stream_t *outstream,
                               apr_pool_t *pool);


#endif /* SVN_REPOS_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
