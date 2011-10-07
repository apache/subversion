/* 
 * File:   tree.h
 * Author: julianfoad
 *
 * Created on 07 October 2011, 17:39
 */

#ifndef TREE_H
#define	TREE_H

#include <apr_hash.h>
#include "svn_types.h"
#include "svn_io.h"
#include "svn_delta.h"
#include "svn_opt.h"
#include "svn_client.h"


#ifdef	__cplusplus
extern "C" {
#endif


/* Present as a tree:
 *   an unversioned disk tree;
 *   a WC base tree
 *   a WC working tree
 *   a repository tree
 * 
 * The consumer "pulls" parts of the tree and can omit unwanted parts.
 * Consumer can pull any subtree "recursively" for efficient streaming.
 */

/**
 * A readable tree.  This object is used to perform read requests to a
 * repository tree or a working-copy (base or working) tree or any other
 * readable tree.
 *
 * @since New in 1.8.
 */
typedef struct svn_client_tree_t svn_client_tree_t;

/* */
typedef svn_io_dirent2_t svn_client_tree_dirent_t;

/* V-table for #svn_client_tree_t.
 *
 * Paths are relpaths, relative to the tree root.
 * Revision numbers and repository ids are #SVN_INVALID_REVNUM and NULL
 * for an unversioned node (including a node that is a local add/copy/move
 * in a WC working tree).
 */
typedef struct svn_client_tree__vtable_t
{
  /* Fetch the node kind of the node at @a relpath.
   * (### and other metadata? revnum? props?)
   *
   * Set @a *kind to the node kind.
   */
  svn_error_t *(*get_kind)(svn_client_tree_t *tree,
                           svn_node_kind_t *kind,
                           const char *relpath,
                           apr_pool_t *scratch_pool);

  /* Fetch the contents and properties of the file at @a relpath.
   *
   * If @a stream is non-NULL, set @a *stream to a readable stream yielding
   * the contents of the file at @a relpath.  (### ? The stream
   * handlers for @a stream may not perform any operations on @a tree.)
   *
   * If @a props is non-NULL, set @a *props to contain the regular
   * versioned properties of the file (not 'wcprops', 'entryprops', etc.).
   * The hash maps (const char *) names to (#svn_string_t *) values.
   */
  svn_error_t *(*get_file)(svn_client_tree_t *tree,
                           svn_stream_t **stream,
                           apr_hash_t **props,
                           const char *relpath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

  /* Fetch the entries and properties of the directory at @a relpath.
   *
   * If @a dirents is non-NULL, set @a *dirents to contain all the entries
   * of directory @a relpath.  The keys will be (<tt>const char *</tt>)
   * entry names, and the values (#svn_client_tree_dirent_t *) dirents.
   * Only the @c kind and @c filesize fields are filled in.
   * ### @c special would be useful too.
   *
   * If @a props is non-NULL, set @a *props to contain the regular
   * versioned properties of the file (not 'wcprops', 'entryprops', etc.).
   * The hash maps (const char *) names to (#svn_string_t *) values.
   */
  svn_error_t *(*get_dir)(svn_client_tree_t *tree,
                          apr_hash_t **dirents,
                          apr_hash_t **props,
                          const char *relpath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

  /* Push a sub-tree into an editor, as a delta against an empty tree.
   * This is useful for efficiency when streaming a (sub-)tree from a
   * remote source. */
  svn_error_t *(*push_as_delta_edit)(svn_client_tree_t *tree,
                                     const char *relpath,
                                     svn_delta_editor_t *editor,
                                     void *edit_baton,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool);
} svn_client_tree__vtable_t;

/* */
struct svn_client_tree_t
{
  const svn_client_tree__vtable_t *vtable;

  /* Pool used to manage this session. */
  apr_pool_t *pool;

  /* Private data for the tree implementation. */
  void *priv;
};


/*-----------------------------------------------------------------*/


/* */
svn_error_t *
svn_client__disk_tree(svn_client_tree_t **tree_p,
                      const char *abspath,
                      svn_delta_editor_t *editor,
                      apr_pool_t *result_pool);

/* */
svn_error_t *
svn_client__wc_base_tree(svn_client_tree_t **tree_p,
                         const char *path,
                         svn_delta_editor_t *editor,
                         apr_pool_t *result_pool);

/* */
svn_error_t *
svn_client__wc_working_tree(svn_client_tree_t **tree_p,
                            const char *path,
                            svn_delta_editor_t *editor,
                            apr_pool_t *result_pool);

/* */
svn_error_t *
svn_client__repository_tree(svn_client_tree_t **tree_p,
                            const char *path_or_url,
                            const svn_opt_revision_t *peg_revision,
                            const svn_opt_revision_t *revision,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *result_pool);


#ifdef	__cplusplus
}
#endif

#endif	/* TREE_H */

