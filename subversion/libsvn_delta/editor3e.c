/*
 * editor3e.c :  editing trees of versioned resources
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

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_props.h"
#include "svn_sorts.h"
#include "svn_iter.h"

#include "private/svn_editor3e.h"
#include "svn_private_config.h"

#ifdef SVN_DEBUG
/* This enables runtime checks of the editor API constraints.  This may
   introduce additional memory and runtime overhead, and should not be used
   in production builds. */
#define ENABLE_ORDERING_CHECK
#endif


struct svn_editor3_t
{
  void *baton;

  /* Standard cancellation function. Called before each callback.  */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  /* The callback functions.  */
  svn_editor3_cb_funcs_t funcs;

  /* This pool is used as the scratch_pool for all callbacks.  */
  apr_pool_t *scratch_pool;

#ifdef ENABLE_ORDERING_CHECK
  svn_boolean_t within_callback;
  svn_boolean_t finished;
  apr_pool_t *state_pool;
#endif
};


#ifdef ENABLE_ORDERING_CHECK

#define START_CALLBACK(editor)                       \
  do {                                               \
    svn_editor3_t *editor__tmp_e = (editor);          \
    SVN_ERR_ASSERT(!editor__tmp_e->within_callback); \
    editor__tmp_e->within_callback = TRUE;           \
  } while (0)
#define END_CALLBACK(editor) ((editor)->within_callback = FALSE)

#define MARK_FINISHED(editor) ((editor)->finished = TRUE)
#define SHOULD_NOT_BE_FINISHED(editor)  SVN_ERR_ASSERT(!(editor)->finished)

#else

#define START_CALLBACK(editor)  /* empty */
#define END_CALLBACK(editor)  /* empty */

#define MARK_FINISHED(editor)  /* empty */
#define SHOULD_NOT_BE_FINISHED(editor)  /* empty */

#endif /* ENABLE_ORDERING_CHECK */


svn_editor3_t *
svn_editor3_create(const svn_editor3_cb_funcs_t *editor_funcs,
                   void *editor_baton,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *result_pool)
{
  svn_editor3_t *editor = apr_pcalloc(result_pool, sizeof(*editor));

  editor->funcs = *editor_funcs;
  editor->baton = editor_baton;
  editor->cancel_func = cancel_func;
  editor->cancel_baton = cancel_baton;
  editor->scratch_pool = svn_pool_create(result_pool);

#ifdef ENABLE_ORDERING_CHECK
  editor->within_callback = FALSE;
  editor->finished = FALSE;
  editor->state_pool = result_pool;
#endif

  return editor;
}


void *
svn_editor3__get_baton(const svn_editor3_t *editor)
{
  return editor->baton;
}


static svn_error_t *
check_cancel(svn_editor3_t *editor)
{
  svn_error_t *err = NULL;

  if (editor->cancel_func)
    {
      START_CALLBACK(editor);
      err = editor->cancel_func(editor->cancel_baton);
      END_CALLBACK(editor);
    }

  return svn_error_trace(err);
}

/* Do everything that is common to calling any callback.
 *
 * CB is the name of the callback method, e.g. "cb_add".
 * ARG_LIST is the callback-specific arguments prefixed by the number of
 * these arguments, in the form "3(arg1, arg2, arg3)".
 */
#define DO_CALLBACK(editor, cb, arg_list)                               \
  {                                                                     \
    SVN_ERR(check_cancel(editor));                                      \
    if ((editor)->funcs.cb)                                             \
      {                                                                 \
        svn_error_t *_do_cb_err;                                        \
        START_CALLBACK(editor);                                         \
        _do_cb_err = (editor)->funcs.cb((editor)->baton,                \
                                        ARGS ## arg_list                \
                                        (editor)->scratch_pool);        \
        END_CALLBACK(editor);                                           \
        svn_pool_clear((editor)->scratch_pool);                         \
        SVN_ERR(_do_cb_err);                                            \
      }                                                                 \
  }
#define ARGS0()
#define ARGS1(a1)                             a1,
#define ARGS2(a1, a2)                         a1, a2,
#define ARGS3(a1, a2, a3)                     a1, a2, a3,
#define ARGS4(a1, a2, a3, a4)                 a1, a2, a3, a4,
#define ARGS5(a1, a2, a3, a4, a5)             a1, a2, a3, a4, a5,
#define ARGS6(a1, a2, a3, a4, a5, a6)         a1, a2, a3, a4, a5, a6,
#define ARGS7(a1, a2, a3, a4, a5, a6, a7)     a1, a2, a3, a4, a5, a6, a7,
#define ARGS8(a1, a2, a3, a4, a5, a6, a7, a8) a1, a2, a3, a4, a5, a6, a7, a8,


/*
 * ========================================================================
 * Editor for Commit (independent per-node changes; node-id addressing)
 * ========================================================================
 */

#define VALID_NODE_KIND(kind) ((kind) != svn_node_unknown && (kind) != svn_node_none)
#define VALID_EID(eid) ((eid) >= 0)
#define VALID_NAME(name) ((name) && (name)[0] && svn_relpath_is_canonical(name))
#define VALID_CONTENT(content) ((content) && VALID_NODE_KIND((content)->kind))
#define VALID_EL_REV_ID(el_rev) (el_rev && el_rev->branch && VALID_EID(el_rev->eid))

#define VERIFY(method, expr) \
  if (! (expr)) \
    return svn_error_createf(SVN_ERR_BRANCHING, NULL, \
                             _("svn_editor3_%s: validation (%s) failed"), \
                             #method, #expr)

svn_error_t *
svn_editor3_add(svn_editor3_t *editor,
                svn_branch_eid_t *local_eid_p,
                svn_node_kind_t new_kind,
                svn_branch_instance_t *branch,
                svn_branch_eid_t new_parent_eid,
                const char *new_name,
                const svn_element_content_t *new_content)
{
  int eid = -1;

  SVN_ERR_ASSERT(VALID_NODE_KIND(new_kind));
  SVN_ERR_ASSERT(VALID_EID(new_parent_eid));
  SVN_ERR_ASSERT(VALID_NAME(new_name));
  SVN_ERR_ASSERT(VALID_CONTENT(new_content));
  SVN_ERR_ASSERT(new_content->kind == new_kind);

  DO_CALLBACK(editor, cb_add,
              6(&eid, new_kind,
                branch, new_parent_eid, new_name,
                new_content));

  SVN_ERR_ASSERT(VALID_EID(eid));

  /* We allow the output pointer to be null, here, so that implementations
     may assume their output pointer is non-null. */
  if (local_eid_p)
    *local_eid_p = eid;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3_instantiate(svn_editor3_t *editor,
                        svn_branch_instance_t *branch,
                        svn_branch_eid_t local_eid,
                        svn_branch_eid_t new_parent_eid,
                        const char *new_name,
                        const svn_element_content_t *new_content)
{
  SVN_ERR_ASSERT(VALID_EID(local_eid));
  SVN_ERR_ASSERT(VALID_EID(new_parent_eid));
  SVN_ERR_ASSERT(VALID_NAME(new_name));
  SVN_ERR_ASSERT(VALID_CONTENT(new_content));
  VERIFY(instantiate, new_parent_eid != local_eid);
  /* TODO: verify this element does not exist (in initial state) */

  DO_CALLBACK(editor, cb_instantiate,
              5(branch, local_eid,
                new_parent_eid, new_name,
                new_content));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3_copy_one(svn_editor3_t *editor,
                     const svn_branch_el_rev_id_t *src_el_rev,
                     svn_branch_instance_t *branch,
                     svn_branch_eid_t local_eid,
                     svn_branch_eid_t new_parent_eid,
                     const char *new_name,
                     const svn_element_content_t *new_content)
{
  SVN_ERR_ASSERT(VALID_EID(local_eid));
  SVN_ERR_ASSERT(VALID_EL_REV_ID(src_el_rev));
  SVN_ERR_ASSERT(VALID_EID(new_parent_eid));
  SVN_ERR_ASSERT(VALID_NAME(new_name));
  SVN_ERR_ASSERT(! new_content || VALID_CONTENT(new_content));
  /* TODO: verify source element exists (in a committed rev) */

  DO_CALLBACK(editor, cb_copy_one,
              6(src_el_rev,
                branch, local_eid,
                new_parent_eid, new_name,
                new_content));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3_copy_tree(svn_editor3_t *editor,
                      const svn_branch_el_rev_id_t *src_el_rev,
                      svn_branch_instance_t *branch,
                      svn_branch_eid_t new_parent_eid,
                      const char *new_name)
{
  SVN_ERR_ASSERT(VALID_EL_REV_ID(src_el_rev));
  SVN_ERR_ASSERT(VALID_EID(new_parent_eid));
  SVN_ERR_ASSERT(VALID_NAME(new_name));
  /* TODO: verify source element exists (in a committed rev) */

  DO_CALLBACK(editor, cb_copy_tree,
              4(src_el_rev,
                branch, new_parent_eid, new_name));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3_delete(svn_editor3_t *editor,
                   svn_revnum_t since_rev,
                   svn_branch_instance_t *branch,
                   svn_branch_eid_t eid)
{
  SVN_ERR_ASSERT(VALID_EID(eid));
  SVN_ERR_ASSERT(eid != branch->sibling_defn->root_eid);
  /* TODO: verify this element exists (in initial state) */

  DO_CALLBACK(editor, cb_delete,
              3(since_rev, branch, eid));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3_alter(svn_editor3_t *editor,
                  svn_revnum_t since_rev,
                  svn_branch_instance_t *branch,
                  svn_branch_eid_t eid,
                  svn_branch_eid_t new_parent_eid,
                  const char *new_name,
                  const svn_element_content_t *new_content)
{
  SVN_ERR_ASSERT(VALID_EID(eid));
  SVN_ERR_ASSERT(VALID_EID(new_parent_eid));
  SVN_ERR_ASSERT(VALID_NAME(new_name));
  SVN_ERR_ASSERT(! new_content || VALID_CONTENT(new_content));
  VERIFY(alter, new_parent_eid != eid);
  /* TODO: verify this element exists (in initial state) */

  DO_CALLBACK(editor, cb_alter,
              6(since_rev, branch, eid,
                new_parent_eid, new_name,
                new_content));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3_sequence_point(svn_editor3_t *editor)
{
  SHOULD_NOT_BE_FINISHED(editor);

  DO_CALLBACK(editor, cb_sequence_point,
              0());

  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3_complete(svn_editor3_t *editor)
{
  SHOULD_NOT_BE_FINISHED(editor);

  DO_CALLBACK(editor, cb_complete,
              0());

  MARK_FINISHED(editor);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3_abort(svn_editor3_t *editor)
{
  SHOULD_NOT_BE_FINISHED(editor);

  DO_CALLBACK(editor, cb_abort,
              0());

  MARK_FINISHED(editor);

  return SVN_NO_ERROR;
}


/*
 * ===================================================================
 * Minor data types
 * ===================================================================
 */

svn_editor3_txn_path_t
svn_editor3_txn_path_dup(svn_editor3_txn_path_t p,
                         apr_pool_t *result_pool)
{
  /* The object P is passed by value so we can modify it in place */
  p.peg = svn_pathrev_dup(p.peg, result_pool);
  p.relpath = apr_pstrdup(result_pool, p.relpath);
  return p;
}


#ifdef SVN_DEBUG

/*
 * ===================================================================
 * A wrapper editor that forwards calls through to a wrapped editor
 * while printing a diagnostic trace of the calls.
 * ===================================================================
 */

typedef struct wrapper_baton_t
{
  svn_editor3_t *wrapped_editor;

  /* debug printing stream */
  svn_stream_t *debug_stream;
  /* debug printing prefix*/
  const char *prefix;

} wrapper_baton_t;

/* Print the variable arguments, formatted with FMT like with 'printf',
 * to the stream EB->debug_stream, prefixed with EB->prefix. */
static void
dbg(wrapper_baton_t *eb,
    apr_pool_t *scratch_pool,
    const char *fmt,
    ...)
{
  const char *message;
  va_list ap;

  va_start(ap, fmt);
  message = apr_pvsprintf(scratch_pool, fmt, ap);
  va_end(ap);

  if (eb->prefix)
    svn_error_clear(svn_stream_puts(eb->debug_stream, eb->prefix));
  svn_error_clear(svn_stream_puts(eb->debug_stream, message));
  svn_error_clear(svn_stream_puts(eb->debug_stream, "\n"));
}

/* Return a human-readable string representation of EL_REV. */
static const char *
el_rev_str(const svn_branch_el_rev_id_t *el_rev,
           apr_pool_t *result_pool)
{
  return apr_psprintf(result_pool, "r%ldb%de%d",
                      el_rev->rev, el_rev->branch->sibling_defn->bid, el_rev->eid);
}

/* Return a human-readable string representation of EID. */
static const char *
eid_str(svn_branch_eid_t eid,
         apr_pool_t *result_pool)
{
  return apr_psprintf(result_pool, "%d", eid);
}

static svn_error_t *
wrap_add(void *baton,
         svn_branch_eid_t *local_eid,
         svn_node_kind_t new_kind,
         svn_branch_instance_t *branch,
         svn_branch_eid_t new_parent_eid,
         const char *new_name,
         const svn_element_content_t *new_content,
         apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "... : add(k=%s, p=%s, n=%s, c=...)",
      svn_node_kind_to_word(new_kind),
      eid_str(new_parent_eid, scratch_pool), new_name);
  SVN_ERR(svn_editor3_add(eb->wrapped_editor,
                          local_eid, new_kind,
                          branch, new_parent_eid, new_name, new_content));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_instantiate(void *baton,
                 svn_branch_instance_t *branch,
                 svn_branch_eid_t local_eid,
                 svn_branch_eid_t new_parent_eid,
                 const char *new_name,
                 const svn_element_content_t *new_content,
                 apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "%s : instantiate(p=%s, n=%s, c=...)",
      eid_str(local_eid, scratch_pool),
      eid_str(new_parent_eid, scratch_pool), new_name);
  SVN_ERR(svn_editor3_instantiate(eb->wrapped_editor,
                                  branch, local_eid,
                                  new_parent_eid, new_name, new_content));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_copy_one(void *baton,
              const svn_branch_el_rev_id_t *src_el_rev,
              svn_branch_instance_t *branch,
              svn_branch_eid_t local_eid,
              svn_branch_eid_t new_parent_eid,
              const char *new_name,
              const svn_element_content_t *new_content,
              apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "%s : copy_one(f=%s, p=%s, n=%s, c=...)",
      eid_str(local_eid, scratch_pool), el_rev_str(src_el_rev, scratch_pool),
      eid_str(new_parent_eid, scratch_pool), new_name);
  SVN_ERR(svn_editor3_copy_one(eb->wrapped_editor,
                               src_el_rev,
                               branch, local_eid,
                               new_parent_eid, new_name, new_content));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_copy_tree(void *baton,
               const svn_branch_el_rev_id_t *src_el_rev,
               svn_branch_instance_t *branch,
               svn_branch_eid_t new_parent_eid,
               const char *new_name,
               apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "... : copy_tree(f=%s, p=%s, n=%s)",
      el_rev_str(src_el_rev, scratch_pool),
      eid_str(new_parent_eid, scratch_pool), new_name);
  SVN_ERR(svn_editor3_copy_tree(eb->wrapped_editor,
                                src_el_rev,
                                branch, new_parent_eid, new_name));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_delete(void *baton,
            svn_revnum_t since_rev,
            svn_branch_instance_t *branch,
            svn_branch_eid_t eid,
            apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "%s : delete()",
      eid_str(eid, scratch_pool));
  SVN_ERR(svn_editor3_delete(eb->wrapped_editor,
                             since_rev, branch, eid));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_alter(void *baton,
           svn_revnum_t since_rev,
           svn_branch_instance_t *branch,
           svn_branch_eid_t eid,
           svn_branch_eid_t new_parent_eid,
           const char *new_name,
           const svn_element_content_t *new_content,
           apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "%s : alter(p=%s, n=%s, c=...)",
      eid_str(eid, scratch_pool), eid_str(eid, scratch_pool),
      eid_str(new_parent_eid, scratch_pool), new_name);
  SVN_ERR(svn_editor3_alter(eb->wrapped_editor,
                            since_rev, branch, eid,
                            new_parent_eid, new_name, new_content));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_sequence_point(void *baton,
                    apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "sequence_point()");
  SVN_ERR(svn_editor3_sequence_point(eb->wrapped_editor));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_complete(void *baton,
              apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "complete()");
  SVN_ERR(svn_editor3_complete(eb->wrapped_editor));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_abort(void *baton,
           apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "abort()");
  SVN_ERR(svn_editor3_abort(eb->wrapped_editor));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3__get_debug_editor(svn_editor3_t **editor_p,
                              svn_editor3_t *wrapped_editor,
                              apr_pool_t *result_pool)
{
  static const svn_editor3_cb_funcs_t wrapper_funcs = {
    wrap_add,
    wrap_instantiate,
    wrap_copy_one,
    wrap_copy_tree,
    wrap_delete,
    wrap_alter,
    wrap_sequence_point,
    wrap_complete,
    wrap_abort
  };
  wrapper_baton_t *eb = apr_palloc(result_pool, sizeof(*eb));

  eb->wrapped_editor = wrapped_editor;

  /* set up for diagnostic printing */
  {
    apr_file_t *errfp;
    apr_status_t apr_err = apr_file_open_stdout(&errfp, result_pool);

    if (apr_err)
      return svn_error_wrap_apr(apr_err, "Failed to open debug output stream");

    eb->debug_stream = svn_stream_from_aprfile2(errfp, TRUE, result_pool);
    eb->prefix = apr_pstrdup(result_pool, "DBG: ");
  }

  *editor_p = svn_editor3_create(&wrapper_funcs, eb,
                                 NULL, NULL, /* cancellation */
                                 result_pool);

  return SVN_NO_ERROR;
}
#endif


/*
 * ===================================================================
 * Branch functionality
 * ===================================================================
 */

#define FAMILY_HAS_ELEMENT(family, eid) \
  ((eid) >= (family)->first_eid && (eid) < (family)->next_eid)

#define BRANCH_FAMILY_HAS_ELEMENT(branch, eid) \
   FAMILY_HAS_ELEMENT((branch)->sibling_defn->family, (eid))

#define BRANCHES_IN_SAME_FAMILY(branch1, branch2) \
  ((branch1)->sibling_defn->family->fid \
   == (branch2)->sibling_defn->family->fid)

/* Return the relative path to element EID within SUBTREE.
 *
 * Assumes the mapping is "complete" (has complete paths to SUBTREE and to EID).
 */
static const char *
element_relpath_in_subtree(const svn_branch_el_rev_id_t *subtree,
                           int eid,
                           apr_pool_t *scratch_pool)
{
  const char *subtree_path;
  const char *element_path;
  const char *relpath = NULL;

  SVN_ERR_ASSERT_NO_RETURN(BRANCH_FAMILY_HAS_ELEMENT(subtree->branch, subtree->eid));
  SVN_ERR_ASSERT_NO_RETURN(BRANCH_FAMILY_HAS_ELEMENT(subtree->branch, eid));

  subtree_path = svn_branch_get_path_by_eid(subtree->branch, subtree->eid,
                                            scratch_pool);
  element_path = svn_branch_get_path_by_eid(subtree->branch, eid,
                                            scratch_pool);

  SVN_ERR_ASSERT_NO_RETURN(subtree_path);

  if (element_path)
    relpath = svn_relpath_skip_ancestor(subtree_path, element_path);

  return relpath;
}

svn_error_t *
svn_branch_subtree_differences(apr_hash_t **diff_p,
                               svn_editor3_t *editor,
                               const svn_branch_el_rev_id_t *left,
                               const svn_branch_el_rev_id_t *right,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  apr_hash_t *diff = apr_hash_make(result_pool);
  int first_eid, next_eid;
  int e;

  /*SVN_DBG(("branch_element_differences(b%d r%ld, b%d r%ld, e%d)",
           left->branch->sibling->bid, left->rev,
           right->branch->sibling->bid, right->rev, right->eid));*/
  SVN_ERR_ASSERT(BRANCHES_IN_SAME_FAMILY(left->branch, right->branch));
  SVN_ERR_ASSERT(BRANCH_FAMILY_HAS_ELEMENT(left->branch, left->eid));
  SVN_ERR_ASSERT(BRANCH_FAMILY_HAS_ELEMENT(left->branch, right->eid));

  first_eid = left->branch->sibling_defn->family->first_eid;
  next_eid = MAX(left->branch->sibling_defn->family->next_eid,
                 right->branch->sibling_defn->family->next_eid);

  for (e = first_eid; e < next_eid; e++)
    {
      svn_branch_el_rev_content_t *content_left = NULL;
      svn_branch_el_rev_content_t *content_right = NULL;

      if (e < left->branch->sibling_defn->family->next_eid
          && element_relpath_in_subtree(left, e, scratch_pool))
        {
          SVN_ERR(svn_editor3_el_rev_get(&content_left, editor,
                                         left->branch, e,
                                         result_pool, scratch_pool));
        }
      if (e < right->branch->sibling_defn->family->next_eid
          && element_relpath_in_subtree(right, e, scratch_pool))
        {
          SVN_ERR(svn_editor3_el_rev_get(&content_right, editor,
                                         right->branch, e,
                                         result_pool, scratch_pool));
        }

      if (! svn_branch_el_rev_content_equal(content_left, content_right,
                                            scratch_pool))
        {
          svn_branch_el_rev_content_t **contents
            = apr_palloc(result_pool, 2 * sizeof(void *));

          contents[0] = content_left;
          contents[1] = content_right;
          svn_int_hash_set(diff, e, contents);
        }
    }

  *diff_p = diff;
  return SVN_NO_ERROR;
}

