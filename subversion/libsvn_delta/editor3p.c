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
#include "private/svn_editor3paths.h"
#include "svn_private_config.h"

#ifdef SVN_DEBUG
/* This enables runtime checks of the editor API constraints.  This may
   introduce additional memory and runtime overhead, and should not be used
   in production builds. */
#define ENABLE_ORDERING_CHECK
#endif


struct svn_editor3p_t
{
  void *baton;

  /* Standard cancellation function. Called before each callback.  */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  /* The callback functions.  */
  svn_editor3p_cb_funcs_t funcs;

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
    svn_editor3p_t *editor__tmp_e = (editor);          \
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


svn_editor3p_t *
svn_editor3p_create(const svn_editor3p_cb_funcs_t *editor_funcs,
                    void *editor_baton,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *result_pool)
{
  svn_editor3p_t *editor = apr_pcalloc(result_pool, sizeof(*editor));

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
svn_editor3p__get_baton(const svn_editor3p_t *editor)
{
  return editor->baton;
}


static svn_error_t *
check_cancel(svn_editor3p_t *editor)
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
svn_editor3p_mk(svn_editor3p_t *editor,
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
svn_editor3p_cp(svn_editor3p_t *editor,
#ifdef SVN_EDITOR3_WITH_COPY_FROM_THIS_REV
                svn_editor3_txn_path_t from_loc,
#else
                svn_pathrev_t from_loc,
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
svn_editor3p_mv(svn_editor3p_t *editor,
                svn_pathrev_t from_loc,
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
svn_editor3p_res(svn_editor3p_t *editor,
                 svn_pathrev_t from_loc,
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
svn_editor3p_rm(svn_editor3p_t *editor,
                svn_editor3_txn_path_t loc)
{
  /* SVN_ERR_ASSERT(...); */

  DO_CALLBACK(editor, cb_rm,
              1(loc));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3p_put(svn_editor3p_t *editor,
                 svn_editor3_txn_path_t loc,
                 const svn_element_content_t *new_content)
{
  /* SVN_ERR_ASSERT(...); */

  DO_CALLBACK(editor, cb_put,
              2(loc, new_content));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor3p_complete(svn_editor3p_t *editor)
{
  SHOULD_NOT_BE_FINISHED(editor);

  DO_CALLBACK(editor, cb_complete,
              0());

  MARK_FINISHED(editor);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3p_abort(svn_editor3p_t *editor)
{
  SHOULD_NOT_BE_FINISHED(editor);

  DO_CALLBACK(editor, cb_abort,
              0());

  MARK_FINISHED(editor);

  return SVN_NO_ERROR;
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
  svn_editor3p_t *wrapped_editor;

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
peg_path_str(svn_pathrev_t loc,
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
  SVN_ERR(svn_editor3p_mk(eb->wrapped_editor,
                          new_kind, parent_loc, new_name));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_cp(void *baton,
#ifdef SVN_EDITOR3_WITH_COPY_FROM_THIS_REV
        svn_editor3_txn_path_t from_loc,
#else
        svn_pathrev_t from_loc,
#endif
        svn_editor3_txn_path_t parent_loc,
        const char *new_name,
        apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "cp(f=%s, p=%s, n=%s)",
      peg_path_str(from_loc, scratch_pool),
      txn_path_str(parent_loc, scratch_pool), new_name);
  SVN_ERR(svn_editor3p_cp(eb->wrapped_editor,
                          from_loc, parent_loc, new_name));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_mv(void *baton,
        svn_pathrev_t from_loc,
        svn_editor3_txn_path_t new_parent_loc,
        const char *new_name,
        apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "mv(f=%s, p=%s, n=%s)",
      peg_path_str(from_loc, scratch_pool),
      txn_path_str(new_parent_loc, scratch_pool), new_name);
  SVN_ERR(svn_editor3p_mv(eb->wrapped_editor,
                          from_loc, new_parent_loc, new_name));
  return SVN_NO_ERROR;
}

#ifdef SVN_EDITOR3_WITH_RESURRECTION
static svn_error_t *
wrap_res(void *baton,
         svn_pathrev_t from_loc,
         svn_editor3_txn_path_t parent_loc,
         const char *new_name,
         apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "res(f=%s, p=%s, n=%s)",
      peg_path_str(from_loc, scratch_pool),
      txn_path_str(parent_loc, scratch_pool), new_name);
  SVN_ERR(svn_editor3p_res(eb->wrapped_editor,
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
  SVN_ERR(svn_editor3p_rm(eb->wrapped_editor,
                          loc));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_put(void *baton,
         svn_editor3_txn_path_t loc,
         const svn_element_content_t *new_content,
         apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "put(%s)",
      txn_path_str(loc, scratch_pool));
  SVN_ERR(svn_editor3p_put(eb->wrapped_editor,
                           loc, new_content));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_complete(void *baton,
              apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "complete()");
  SVN_ERR(svn_editor3p_complete(eb->wrapped_editor));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_abort(void *baton,
           apr_pool_t *scratch_pool)
{
  wrapper_baton_t *eb = baton;

  dbg(eb, scratch_pool, "abort()");
  SVN_ERR(svn_editor3p_abort(eb->wrapped_editor));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3p__get_debug_editor(svn_editor3p_t **editor_p,
                               svn_editor3p_t *wrapped_editor,
                               apr_pool_t *result_pool)
{
  static const svn_editor3p_cb_funcs_t wrapper_funcs = {
    wrap_mk,
    wrap_cp,
    wrap_mv,
#ifdef SVN_EDITOR3_WITH_RESURRECTION
    wrap_res,
#endif
    wrap_rm,
    wrap_put,
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

  *editor_p = svn_editor3p_create(&wrapper_funcs, eb,
                                  NULL, NULL, /* cancellation */
                                  result_pool);

  return SVN_NO_ERROR;
}
#endif
