/*
 * editor.c :  editing trees of versioned resources
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

#include "private/svn_editor3.h"

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


svn_error_t *
svn_editor3_create(svn_editor3_t **editor,
                   const svn_editor3_cb_funcs_t *editor_funcs,
                   void *editor_baton,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  *editor = apr_pcalloc(result_pool, sizeof(**editor));

  (*editor)->funcs = *editor_funcs;
  (*editor)->baton = editor_baton;
  (*editor)->cancel_func = cancel_func;
  (*editor)->cancel_baton = cancel_baton;
  (*editor)->scratch_pool = svn_pool_create(result_pool);

#ifdef ENABLE_ORDERING_CHECK
  (*editor)->within_callback = FALSE;
  (*editor)->finished = FALSE;
  (*editor)->state_pool = result_pool;
#endif

  return SVN_NO_ERROR;
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
 * ===================================================================
 * Editor for Commit (incremental tree changes; path-based addressing)
 * ===================================================================
 */

svn_error_t *
svn_editor3_mk(svn_editor3_t *editor,
               svn_node_kind_t new_kind,
               svn_editor3_txn_path_t parent_loc,
               const char *new_name)
{
  /* SVN_ERR_ASSERT(...); */

  DO_CALLBACK(editor, cb_mk,
              3(new_kind, parent_loc, new_name));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3_cp(svn_editor3_t *editor,
#ifdef SVN_EDITOR3_WITH_COPY_FROM_THIS_REV
               svn_editor3_txn_path_t from_loc,
#else
               svn_editor3_peg_path_t from_loc,
#endif
               svn_editor3_txn_path_t parent_loc,
               const char *new_name)
{
  /* SVN_ERR_ASSERT(...); */

  DO_CALLBACK(editor, cb_cp,
              3(from_loc, parent_loc, new_name));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3_mv(svn_editor3_t *editor,
               svn_editor3_peg_path_t from_loc,
               svn_editor3_txn_path_t new_parent_loc,
               const char *new_name)
{
  /* SVN_ERR_ASSERT(...); */

  DO_CALLBACK(editor, cb_mv,
              3(from_loc, new_parent_loc, new_name));

  return SVN_NO_ERROR;
}

#ifdef SVN_EDITOR3_WITH_RESURRECTION
svn_error_t *
svn_editor3_res(svn_editor3_t *editor,
                svn_editor3_peg_path_t from_loc,
                svn_editor3_txn_path_t parent_loc,
                const char *new_name)
{
  /* SVN_ERR_ASSERT(...); */

  DO_CALLBACK(editor, cb_res,
              3(from_loc, parent_loc, new_name));

  return SVN_NO_ERROR;
}
#endif

svn_error_t *
svn_editor3_rm(svn_editor3_t *editor,
               svn_editor3_txn_path_t loc)
{
  /* SVN_ERR_ASSERT(...); */

  DO_CALLBACK(editor, cb_rm,
              1(loc));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3_put(svn_editor3_t *editor,
                svn_editor3_txn_path_t loc,
                const svn_editor3_node_content_t *new_content)
{
  /* SVN_ERR_ASSERT(...); */

  DO_CALLBACK(editor, cb_put,
              2(loc, new_content));

  return SVN_NO_ERROR;
}


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

svn_error_t *
svn_editor3_add(svn_editor3_t *editor,
                svn_editor3_eid_t *local_eid_p,
                svn_node_kind_t new_kind,
                svn_editor3_eid_t new_parent_eid,
                const char *new_name,
                const svn_editor3_node_content_t *new_content)
{
  int eid = -1;

  SVN_ERR_ASSERT(VALID_NODE_KIND(new_kind));
  SVN_ERR_ASSERT(VALID_EID(new_parent_eid));
  SVN_ERR_ASSERT(VALID_NAME(new_name));
  SVN_ERR_ASSERT(VALID_CONTENT(new_content));
  SVN_ERR_ASSERT(new_content->kind == new_kind);

  DO_CALLBACK(editor, cb_add,
              5(&eid, new_kind,
                new_parent_eid, new_name,
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
                        svn_editor3_eid_t local_eid,
                        svn_editor3_eid_t new_parent_eid,
                        const char *new_name,
                        const svn_editor3_node_content_t *new_content)
{
  SVN_ERR_ASSERT(VALID_EID(local_eid));
  SVN_ERR_ASSERT(VALID_EID(new_parent_eid));
  SVN_ERR_ASSERT(VALID_NAME(new_name));
  SVN_ERR_ASSERT(VALID_CONTENT(new_content));

  DO_CALLBACK(editor, cb_instantiate,
              4(local_eid,
                new_parent_eid, new_name,
                new_content));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3_copy_one(svn_editor3_t *editor,
                     svn_editor3_eid_t local_eid,
                     const struct svn_branch_el_rev_id_t *src_el_rev,
                     svn_editor3_eid_t new_parent_eid,
                     const char *new_name,
                     const svn_editor3_node_content_t *new_content)
{
  SVN_ERR_ASSERT(VALID_EID(local_eid));
  SVN_ERR_ASSERT(VALID_EL_REV_ID(src_el_rev));
  SVN_ERR_ASSERT(VALID_EID(new_parent_eid));
  SVN_ERR_ASSERT(VALID_NAME(new_name));
  SVN_ERR_ASSERT(! new_content || VALID_CONTENT(new_content));

  DO_CALLBACK(editor, cb_copy_one,
              5(local_eid,
                src_el_rev,
                new_parent_eid, new_name,
                new_content));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3_copy_tree(svn_editor3_t *editor,
                      const svn_branch_el_rev_id_t *src_el_rev,
                      svn_editor3_eid_t new_parent_eid,
                      const char *new_name)
{
  SVN_ERR_ASSERT(VALID_EL_REV_ID(src_el_rev));
  SVN_ERR_ASSERT(VALID_EID(new_parent_eid));
  SVN_ERR_ASSERT(VALID_NAME(new_name));

  DO_CALLBACK(editor, cb_copy_tree,
              3(src_el_rev,
                new_parent_eid, new_name));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3_delete(svn_editor3_t *editor,
                   svn_revnum_t since_rev,
                   svn_editor3_eid_t eid)
{
  SVN_ERR_ASSERT(VALID_EID(eid));

  DO_CALLBACK(editor, cb_delete,
              2(since_rev, eid));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3_alter(svn_editor3_t *editor,
                  svn_revnum_t since_rev,
                  svn_editor3_eid_t eid,
                  svn_editor3_eid_t new_parent_eid,
                  const char *new_name,
                  const svn_editor3_node_content_t *new_content)
{
  SVN_ERR_ASSERT(VALID_EID(eid));
  SVN_ERR_ASSERT(VALID_EID(new_parent_eid));
  SVN_ERR_ASSERT(VALID_NAME(new_name));
  SVN_ERR_ASSERT(! new_content || VALID_CONTENT(new_content));

  DO_CALLBACK(editor, cb_alter,
              5(since_rev, eid,
                new_parent_eid, new_name,
                new_content));

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
 * Node content
 * ===================================================================
 */

svn_editor3_node_content_t *
svn_editor3_node_content_dup(const svn_editor3_node_content_t *old,
                             apr_pool_t *result_pool)
{
  svn_editor3_node_content_t *new_content;

  if (old == NULL)
    return NULL;

  new_content = apr_pmemdup(result_pool, old, sizeof(*new_content));
  if (old->ref.relpath)
    new_content->ref = svn_editor3_peg_path_dup(old->ref, result_pool);
  if (old->props)
    new_content->props = svn_prop_hash_dup(old->props, result_pool);
  if (old->kind == svn_node_file && old->text)
    new_content->text = svn_stringbuf_dup(old->text, result_pool);
  if (old->kind == svn_node_symlink && old->target)
    new_content->target = apr_pstrdup(result_pool, old->target);
  return new_content;
}

svn_boolean_t
svn_editor3_node_content_equal(const svn_editor3_node_content_t *left,
                               const svn_editor3_node_content_t *right,
                               apr_pool_t *scratch_pool)
{
  apr_array_header_t *prop_diffs;

  /* references are not supported */
  SVN_ERR_ASSERT_NO_RETURN(! left->ref.relpath && ! right->ref.relpath);
  SVN_ERR_ASSERT_NO_RETURN(left->kind != svn_node_unknown
                           && right->kind != svn_node_unknown);

  if (left->kind != right->kind)
    {
      return FALSE;
    }

  svn_error_clear(svn_prop_diffs(&prop_diffs,
                                 left->props, right->props,
                                 scratch_pool));

  if (prop_diffs->nelts != 0)
    {
      return FALSE;
    }
  switch (left->kind)
    {
    case svn_node_dir:
      break;
    case svn_node_file:
      if (! svn_stringbuf_compare(left->text, right->text))
        {
          return FALSE;
        }
      break;
    case svn_node_symlink:
      if (strcmp(left->target, right->target) != 0)
        {
          return FALSE;
        }
      break;
    default:
      break;
    }

  return TRUE;
}

svn_editor3_node_content_t *
svn_editor3_node_content_create_ref(svn_editor3_peg_path_t ref,
                                    apr_pool_t *result_pool)
{
  svn_editor3_node_content_t *new_content
    = apr_pcalloc(result_pool, sizeof(*new_content));

  new_content->kind = svn_node_unknown;
  new_content->ref = svn_editor3_peg_path_dup(ref, result_pool);
  return new_content;
}

svn_editor3_node_content_t *
svn_editor3_node_content_create_dir(apr_hash_t *props,
                                    apr_pool_t *result_pool)
{
  svn_editor3_node_content_t *new_content
    = apr_pcalloc(result_pool, sizeof(*new_content));

  new_content->kind = svn_node_dir;
  new_content->props = props ? svn_prop_hash_dup(props, result_pool) : NULL;
  return new_content;
}

svn_editor3_node_content_t *
svn_editor3_node_content_create_file(apr_hash_t *props,
                                     svn_stringbuf_t *text,
                                     apr_pool_t *result_pool)
{
  svn_editor3_node_content_t *new_content
    = apr_pcalloc(result_pool, sizeof(*new_content));

  SVN_ERR_ASSERT_NO_RETURN(text);

  new_content->kind = svn_node_file;
  new_content->props = props ? svn_prop_hash_dup(props, result_pool) : NULL;
  new_content->text = svn_stringbuf_dup(text, result_pool);
  return new_content;
}

svn_editor3_node_content_t *
svn_editor3_node_content_create_symlink(apr_hash_t *props,
                                        const char *target,
                                        apr_pool_t *result_pool)
{
  svn_editor3_node_content_t *new_content
    = apr_pcalloc(result_pool, sizeof(*new_content));

  SVN_ERR_ASSERT_NO_RETURN(target);

  new_content->kind = svn_node_symlink;
  new_content->props = props ? svn_prop_hash_dup(props, result_pool) : NULL;
  new_content->target = apr_pstrdup(result_pool, target);
  return new_content;
}


/*
 * ===================================================================
 * Minor data types
 * ===================================================================
 */

svn_editor3_peg_path_t
svn_editor3_peg_path_dup(svn_editor3_peg_path_t p,
                         apr_pool_t *result_pool)
{
  /* The object P is passed by value so we can modify it in place */
  p.relpath = apr_pstrdup(result_pool, p.relpath);
  return p;
}

svn_boolean_t
svn_editor3_peg_path_equal(svn_editor3_peg_path_t *peg_path1,
                           svn_editor3_peg_path_t *peg_path2)
{
  if (peg_path1->rev != peg_path2->rev)
    return FALSE;
  if (strcmp(peg_path1->relpath, peg_path2->relpath) != 0)
    return FALSE;

  return TRUE;
}

svn_editor3_txn_path_t
svn_editor3_txn_path_dup(svn_editor3_txn_path_t p,
                         apr_pool_t *result_pool)
{
  /* The object P is passed by value so we can modify it in place */
  p.peg = svn_editor3_peg_path_dup(p.peg, result_pool);
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

/* Return a human-readable string representation of LOC. */
static const char *
peg_path_str(svn_editor3_peg_path_t loc,
             apr_pool_t *result_pool)
{
  return apr_psprintf(result_pool, "%s@%ld",
                      loc.relpath, loc.rev);
}

/* Return a human-readable string representation of LOC. */
static const char *
txn_path_str(svn_editor3_txn_path_t loc,
             apr_pool_t *result_pool)
{
  return apr_psprintf(result_pool, "%s//%s",
                      peg_path_str(loc.peg, result_pool), loc.relpath);
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
eid_str(svn_editor3_eid_t eid,
         apr_pool_t *result_pool)
{
  return apr_psprintf(result_pool, "%d", eid);
}

static svn_error_t *
wrap_mk(void *baton,
        svn_node_kind_t new_kind,
        svn_editor3_txn_path_t parent_loc,
        const char *new_name,
        apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "mk(k=%s, p=%s, n=%s)",
      svn_node_kind_to_word(new_kind),
      txn_path_str(parent_loc, scratch_pool), new_name);
  SVN_ERR(svn_editor3_mk(eb->wrapped_editor,
                         new_kind, parent_loc, new_name));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_cp(void *baton,
#ifdef SVN_EDITOR3_WITH_COPY_FROM_THIS_REV
        svn_editor3_txn_path_t from_loc,
#else
        svn_editor3_peg_path_t from_loc,
#endif
        svn_editor3_txn_path_t parent_loc,
        const char *new_name,
        apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "cp(f=%s, p=%s, n=%s)",
      peg_path_str(from_loc, scratch_pool),
      txn_path_str(parent_loc, scratch_pool), new_name);
  SVN_ERR(svn_editor3_cp(eb->wrapped_editor,
                         from_loc, parent_loc, new_name));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_mv(void *baton,
        svn_editor3_peg_path_t from_loc,
        svn_editor3_txn_path_t new_parent_loc,
        const char *new_name,
        apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "mv(f=%s, p=%s, n=%s)",
      peg_path_str(from_loc, scratch_pool),
      txn_path_str(new_parent_loc, scratch_pool), new_name);
  SVN_ERR(svn_editor3_mv(eb->wrapped_editor,
                         from_loc, new_parent_loc, new_name));
  return SVN_NO_ERROR;
}

#ifdef SVN_EDITOR3_WITH_RESURRECTION
static svn_error_t *
wrap_res(void *baton,
         svn_editor3_peg_path_t from_loc,
         svn_editor3_txn_path_t parent_loc,
         const char *new_name,
         apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "res(f=%s, p=%s, n=%s)",
      peg_path_str(from_loc, scratch_pool),
      txn_path_str(parent_loc, scratch_pool), new_name);
  SVN_ERR(svn_editor3_res(eb->wrapped_editor,
                          from_loc, parent_loc, new_name));
  return SVN_NO_ERROR;
}
#endif

static svn_error_t *
wrap_rm(void *baton,
        svn_editor3_txn_path_t loc,
        apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "rm(%s)",
      txn_path_str(loc, scratch_pool));
  SVN_ERR(svn_editor3_rm(eb->wrapped_editor,
                         loc));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_put(void *baton,
         svn_editor3_txn_path_t loc,
         const svn_editor3_node_content_t *new_content,
         apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "put(%s)",
      txn_path_str(loc, scratch_pool));
  SVN_ERR(svn_editor3_put(eb->wrapped_editor,
                          loc, new_content));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_add(void *baton,
         svn_editor3_eid_t *local_eid,
         svn_node_kind_t new_kind,
         svn_editor3_eid_t new_parent_eid,
         const char *new_name,
         const svn_editor3_node_content_t *new_content,
         apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "... : add(k=%s, p=%s, n=%s, c=...)",
      svn_node_kind_to_word(new_kind),
      eid_str(new_parent_eid, scratch_pool), new_name);
  SVN_ERR(svn_editor3_add(eb->wrapped_editor,
                          local_eid, new_kind,
                          new_parent_eid, new_name, new_content));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_instantiate(void *baton,
         svn_editor3_eid_t local_eid,
         svn_editor3_eid_t new_parent_eid,
         const char *new_name,
         const svn_editor3_node_content_t *new_content,
         apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "%s : instantiate(p=%s, n=%s, c=...)",
      eid_str(local_eid, scratch_pool),
      eid_str(new_parent_eid, scratch_pool), new_name);
  SVN_ERR(svn_editor3_instantiate(eb->wrapped_editor,
                                  local_eid,
                                  new_parent_eid, new_name, new_content));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_copy_one(void *baton,
              svn_editor3_eid_t local_eid,
              const struct svn_branch_el_rev_id_t *src_el_rev,
              svn_editor3_eid_t new_parent_eid,
              const char *new_name,
              const svn_editor3_node_content_t *new_content,
              apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "%s : copy_one(f=%s, p=%s, n=%s, c=...)",
      eid_str(local_eid, scratch_pool), el_rev_str(src_el_rev, scratch_pool),
      eid_str(new_parent_eid, scratch_pool), new_name);
  SVN_ERR(svn_editor3_copy_one(eb->wrapped_editor,
                               local_eid, src_el_rev,
                               new_parent_eid, new_name, new_content));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_copy_tree(void *baton,
               const svn_branch_el_rev_id_t *src_el_rev,
               svn_editor3_eid_t new_parent_eid,
               const char *new_name,
               apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "... : copy_tree(f=%s, p=%s, n=%s)",
      el_rev_str(src_el_rev, scratch_pool),
      eid_str(new_parent_eid, scratch_pool), new_name);
  SVN_ERR(svn_editor3_copy_tree(eb->wrapped_editor,
                                src_el_rev,
                                new_parent_eid, new_name));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_delete(void *baton,
            svn_revnum_t since_rev,
            svn_editor3_eid_t eid,
            apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "%s : delete()",
      eid_str(eid, scratch_pool));
  SVN_ERR(svn_editor3_delete(eb->wrapped_editor,
                             since_rev, eid));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_alter(void *baton,
           svn_revnum_t since_rev,
           svn_editor3_eid_t eid,
           svn_editor3_eid_t new_parent_eid,
           const char *new_name,
           const svn_editor3_node_content_t *new_content,
           apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "%s : alter(p=%s, n=%s, c=...)",
      eid_str(eid, scratch_pool), eid_str(eid, scratch_pool),
      eid_str(new_parent_eid, scratch_pool), new_name);
  SVN_ERR(svn_editor3_alter(eb->wrapped_editor,
                            since_rev, eid,
                            new_parent_eid, new_name, new_content));
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
  apr_pool_t *scratch_pool = result_pool;
  static const svn_editor3_cb_funcs_t wrapper_funcs = {
    wrap_mk,
    wrap_cp,
    wrap_mv,
#ifdef SVN_EDITOR3_WITH_RESURRECTION
    wrap_res,
#endif
    wrap_rm,
    wrap_put,
    wrap_add,
    wrap_instantiate,
    wrap_copy_one,
    wrap_copy_tree,
    wrap_delete,
    wrap_alter,
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

  SVN_ERR(svn_editor3_create(editor_p, &wrapper_funcs, eb,
                             NULL, NULL, /* cancellation */
                             result_pool, scratch_pool));

  return SVN_NO_ERROR;
}
#endif
