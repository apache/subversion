/*
 * swigutil_py.c: utility functions for the SWIG Python bindings
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


#include <Python.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_portable.h>

#include "svn_string.h"
#include "svn_opt.h"
#include "svn_delta.h"

#include "swigutil_py.h"


/*** Helper/Conversion Routines ***/

static PyObject *make_pointer(const char *typename, void *ptr)
{
  /* ### cache the swig_type_info at some point? */
  return SWIG_NewPointerObj(ptr, SWIG_TypeQuery(typename), 0);
}

/* for use by the "O&" format specifier */
static PyObject *make_ob_pool(void *ptr)
{
  return make_pointer("apr_pool_t *", ptr);
}

/* for use by the "O&" format specifier */
static PyObject *make_ob_window(void *ptr)
{
  return make_pointer("svn_txdelta_window_t *", ptr);
}

static PyObject *convert_hash(apr_hash_t *hash,
                              PyObject * (*converter_func)(void *value,
                                                           void *ctx),
                              void *ctx)
{
    apr_hash_index_t *hi;
    PyObject *dict = PyDict_New();

    if (dict == NULL)
        return NULL;

    for (hi = apr_hash_first(NULL, hash); hi; hi = apr_hash_next(hi)) {
        const void *key;
        void *val;
        PyObject *value;

        apr_hash_this(hi, &key, NULL, &val);
        value = (*converter_func)(val, ctx);
        if (value == NULL) {
            Py_DECREF(dict);
            return NULL;
        }
        /* ### gotta cast this thing cuz Python doesn't use "const" */
        if (PyDict_SetItemString(dict, (char *)key, value) == -1) {
            Py_DECREF(value);
            Py_DECREF(dict);
            return NULL;
        }
        Py_DECREF(value);
    }

    return dict;
}

static PyObject *convert_to_swigtype(void *value, void *ctx)
{
  /* ctx is a 'swig_type_info *' */
  return SWIG_NewPointerObj(value, ctx, 0);
}

static PyObject *convert_svn_string_t(void *value, void *ctx)
{
  /* ctx is unused */

  const svn_string_t *s = value;

  /* ### borrowing from value in the pool. or should we copy? note
     ### that copying is "safest" */

  /* ### gotta cast this thing cuz Python doesn't use "const" */
  return PyBuffer_FromMemory((void *)s->data, s->len);
}

static PyObject *convert_svn_client_commit_item_t(void *value, void *ctx)
{
  PyObject *list;
  PyObject *path, *kind, *url, *rev, *cf_url, *state;
  svn_client_commit_item_t *item = value;

  /* ctx is unused */

  list = PyList_New(6);

  if (item->path)
    path = PyString_FromString(item->path);
  else
    {
      path = Py_None;
      Py_INCREF(Py_None);
    }

  if (item->url)
    url = PyString_FromString(item->url);
  else
    {
      url = Py_None;
      Py_INCREF(Py_None);
    }
        
  if (item->copyfrom_url)
    cf_url = PyString_FromString(item->copyfrom_url);
  else
    {
      cf_url = Py_None;
      Py_INCREF(Py_None);
    }
        
  kind = PyInt_FromLong(item->kind);
  rev = PyInt_FromLong(item->revision);
  state = PyInt_FromLong(item->state_flags);

  if (! (list && path && kind && url && rev && cf_url && state))
    {
      Py_XDECREF(list);
      Py_XDECREF(path);
      Py_XDECREF(kind);
      Py_XDECREF(url);
      Py_XDECREF(rev);
      Py_XDECREF(cf_url);
      Py_XDECREF(state);
      return NULL;
    }

  PyList_SET_ITEM(list, 0, path);
  PyList_SET_ITEM(list, 1, kind);
  PyList_SET_ITEM(list, 2, url);
  PyList_SET_ITEM(list, 3, rev);
  PyList_SET_ITEM(list, 4, cf_url);
  PyList_SET_ITEM(list, 5, state);
  return list;
}

PyObject *svn_swig_py_prophash_to_dict(apr_hash_t *hash)
{
  return convert_hash(hash, convert_svn_string_t, NULL);
}

PyObject *svn_swig_py_convert_hash(apr_hash_t *hash, swig_type_info *type)
{
  return convert_hash(hash, convert_to_swigtype, type);
}

PyObject *svn_swig_py_c_strings_to_list(char **strings)
{
    PyObject *list = PyList_New(0);
    char *s;

    while ((s = *strings++) != NULL) {
        PyObject *ob = PyString_FromString(s);

        if (ob == NULL)
            goto error;
        if (PyList_Append(list, ob) == -1)
            goto error;
    }

    return list;

  error:
    Py_DECREF(list);
    return NULL;
}

const apr_array_header_t *svn_swig_py_strings_to_array(PyObject *source,
                                                       apr_pool_t *pool)
{
    int targlen;
    apr_array_header_t *temp;

    if (!PySequence_Check(source)) {
        PyErr_SetString(PyExc_TypeError, "not a sequence");
        return NULL;
    }
    targlen = PySequence_Length(source);
    temp = apr_array_make(pool, targlen, sizeof(const char *));
    /* APR_ARRAY_IDX doesn't actually increment the array item count
       (like, say, apr_array_push would). */
    temp->nelts = targlen;
    while (targlen--) {
        PyObject *o = PySequence_GetItem(source, targlen);
        if (o == NULL)
            return NULL;
        if (!PyString_Check(o)) {
            Py_DECREF(o);
            PyErr_SetString(PyExc_TypeError, "not a string");
            return NULL;
        }
        APR_ARRAY_IDX(temp, targlen, const char *) = PyString_AS_STRING(o);
        Py_DECREF(o);
    }
    return temp;
}


/*** apr_array_header_t conversions.  To create a new type of
     converter, simply copy-n-paste one of these function and tweak
     the creation of the PyObject *ob.  ***/

PyObject *svn_swig_py_array_to_list(const apr_array_header_t *array)
{
    PyObject *list = PyList_New(array->nelts);
    int i;

    for (i = 0; i < array->nelts; ++i) {
        PyObject *ob = 
          PyString_FromString(APR_ARRAY_IDX(array, i, const char *));
        if (ob == NULL)
          goto error;
        PyList_SET_ITEM(list, i, ob);
    }
    return list;

  error:
    Py_DECREF(list);
    return NULL;
}

PyObject *svn_swig_py_revarray_to_list(const apr_array_header_t *array)
{
    PyObject *list = PyList_New(array->nelts);
    int i;

    for (i = 0; i < array->nelts; ++i) {
        PyObject *ob 
          = PyInt_FromLong(APR_ARRAY_IDX(array, i, svn_revnum_t));
        if (ob == NULL)
          goto error;
        PyList_SET_ITEM(list, i, ob);
    }
    return list;

  error:
    Py_DECREF(list);
    return NULL;
}

static PyObject *
commit_item_array_to_list(const apr_array_header_t *array)
{
    PyObject *list = PyList_New(array->nelts);
    int i;

    for (i = 0; i < array->nelts; ++i) {
        PyObject *ob = convert_svn_client_commit_item_t
          (APR_ARRAY_IDX(array, i, svn_client_commit_item_t *), NULL);
        if (ob == NULL)
          goto error;
        PyList_SET_ITEM(list, i, ob);
    }
    return list;

  error:
    Py_DECREF(list);
    return NULL;
}


static svn_error_t * convert_python_error(apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_SWIG_PY_EXCEPTION_SET, 0, NULL, pool,
                          "the Python callback raised an exception");
}


/*** Editor Wrapping ***/

/* this baton is used for the editor, directory, and file batons. */
typedef struct {
  PyObject *editor;     /* the editor handling the callbacks */
  PyObject *baton;      /* the dir/file baton (or NULL for edit baton) */
  apr_pool_t *pool;     /* pool to use for errors */
} item_baton;

typedef struct {
  PyObject *handler;    /* the window handler (a callable) */
  apr_pool_t *pool;     /* a pool for constructing errors */
} handler_baton;

static item_baton * make_baton(apr_pool_t *pool,
                               PyObject *editor, PyObject *baton)
{
  item_baton *newb = apr_palloc(pool, sizeof(*newb));

  /* one more reference to the editor. */
  Py_INCREF(editor);

  /* note: we take the caller's reference to 'baton' */

  newb->editor = editor;
  newb->baton = baton;
  newb->pool = pool;

  return newb;
}

static svn_error_t * close_baton(void *baton, const char *method)
{
  item_baton *ib = baton;
  PyObject *result;

  /* If there is no baton object, then it is an edit_baton, and we should
     not bother to pass an object. Note that we still shove a NULL onto
     the stack, but the format specified just won't reference it.  */
  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)method,
                                    ib->baton ? (char *)"(O)" : NULL,
                                    ib->baton)) == NULL)
    {
      return convert_python_error(ib->pool);
    }

  /* there is no return value, so just toss this object (probably Py_None) */
  Py_DECREF(result);

  /* We're now done with the baton. Since there isn't really a free, all
     we need to do is note that its objects are no longer referenced by
     the baton.  */
  Py_DECREF(ib->editor);
  Py_XDECREF(ib->baton);

#ifdef SVN_DEBUG
  ib->editor = ib->baton = NULL;
#endif

  return SVN_NO_ERROR;
}

static svn_error_t * thunk_set_target_revision(void *edit_baton,
                                               svn_revnum_t target_revision,
                                               apr_pool_t *pool)
{
  item_baton *ib = edit_baton;
  PyObject *result;

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"set_target_revision",
                                    (char *)"l", target_revision)) == NULL)
    {
      return convert_python_error(ib->pool);
    }

  /* there is no return value, so just toss this object (probably Py_None) */
  Py_DECREF(result);

  return SVN_NO_ERROR;
}

static svn_error_t * thunk_open_root(void *edit_baton,
                                     svn_revnum_t base_revision,
                                     apr_pool_t *dir_pool,
                                     void **root_baton)
{
  item_baton *ib = edit_baton;
  PyObject *result;

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"open_root",
                                    (char *)"lO&", base_revision,
                                    make_ob_pool, dir_pool)) == NULL)
    {
      return convert_python_error(dir_pool);
    }

  /* make_baton takes our 'result' reference */
  *root_baton = make_baton(dir_pool, ib->editor, result);

  return SVN_NO_ERROR;
}

static svn_error_t * thunk_delete_entry(const char *path,
                                        svn_revnum_t revision,
                                        void *parent_baton,
                                        apr_pool_t *pool)
{
  item_baton *ib = parent_baton;
  PyObject *result;

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"delete_entry",
                                    (char *)"slOO&", path, revision, ib->baton,
                                    make_ob_pool, pool)) == NULL)
    {
      return convert_python_error(pool);
    }

  /* there is no return value, so just toss this object (probably Py_None) */
  Py_DECREF(result);

  return SVN_NO_ERROR;
}

static svn_error_t * thunk_add_directory(const char *path,
                                         void *parent_baton,
                                         const char *copyfrom_path,
                                         svn_revnum_t copyfrom_revision,
                                         apr_pool_t *dir_pool,
                                         void **child_baton)
{
  item_baton *ib = parent_baton;
  PyObject *result;

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"add_directory",
                                    (char *)"sOslO&", path, ib->baton,
                                    copyfrom_path, copyfrom_revision,
                                    make_ob_pool, dir_pool)) == NULL)
    {
      return convert_python_error(dir_pool);
    }

  /* make_baton takes our 'result' reference */
  *child_baton = make_baton(dir_pool, ib->editor, result);

  return SVN_NO_ERROR;
}

static svn_error_t * thunk_open_directory(const char *path,
                                          void *parent_baton,
                                          svn_revnum_t base_revision,
                                          apr_pool_t *dir_pool,
                                          void **child_baton)
{
  item_baton *ib = parent_baton;
  PyObject *result;

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"open_directory",
                                    (char *)"sOlO&", path, ib->baton,
                                    base_revision,
                                    make_ob_pool, dir_pool)) == NULL)
    {
      return convert_python_error(dir_pool);
    }

  /* make_baton takes our 'result' reference */
  *child_baton = make_baton(dir_pool, ib->editor, result);

  return SVN_NO_ERROR;
}

static svn_error_t * thunk_change_dir_prop(void *dir_baton,
                                           const char *name,
                                           const svn_string_t *value,
                                           apr_pool_t *pool)
{
  item_baton *ib = dir_baton;
  PyObject *result;

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"change_dir_prop",
                                    (char *)"Oss#O&", ib->baton, name,
                                    value->data, value->len,
                                    make_ob_pool, pool)) == NULL)
    {
      return convert_python_error(pool);
    }

  /* there is no return value, so just toss this object (probably Py_None) */
  Py_DECREF(result);

  return SVN_NO_ERROR;
}

static svn_error_t * thunk_close_directory(void *dir_baton,
                                           apr_pool_t *pool)
{
  return close_baton(dir_baton, "close_directory");
}

static svn_error_t * thunk_add_file(const char *path,
                                    void *parent_baton,
                                    const char *copyfrom_path,
                                    svn_revnum_t copyfrom_revision,
                                    apr_pool_t *file_pool,
                                    void **file_baton)
{
  item_baton *ib = parent_baton;
  PyObject *result;

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"add_file",
                                    (char *)"sOslO&", path, ib->baton,
                                    copyfrom_path, copyfrom_revision,
                                    make_ob_pool, file_pool)) == NULL)
    {
      return convert_python_error(file_pool);
    }

  /* make_baton takes our 'result' reference */
  *file_baton = make_baton(file_pool, ib->editor, result);

  return SVN_NO_ERROR;
}

static svn_error_t * thunk_open_file(const char *path,
                                     void *parent_baton,
                                     svn_revnum_t base_revision,
                                     apr_pool_t *file_pool,
                                     void **file_baton)
{
  item_baton *ib = parent_baton;
  PyObject *result;

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"open_file",
                                    (char *)"sOlO&", path, ib->baton,
                                    base_revision,
                                    make_ob_pool, file_pool)) == NULL)
    {
      return convert_python_error(file_pool);
    }

  /* make_baton takes our 'result' reference */
  *file_baton = make_baton(file_pool, ib->editor, result);

  return SVN_NO_ERROR;
}

static svn_error_t * thunk_window_handler(svn_txdelta_window_t *window,
                                          void *baton)
{
  handler_baton *hb = baton;
  PyObject *result;

  if (window == NULL)
    {
      /* the last call; it closes the handler */

      /* invoke the handler with None for the window */
      /* ### python doesn't have 'const' on the format */
      result = PyObject_CallFunction(hb->handler, (char *)"O", Py_None);

      /* we no longer need to refer to the handler object */
      Py_DECREF(hb->handler);
    }
  else
    {
      /* invoke the handler with the window */
      /* ### python doesn't have 'const' on the format */
      result = PyObject_CallFunction(hb->handler,
                                     (char *)"O&", make_ob_window, window);
    }

  if (result == NULL)
    return convert_python_error(hb->pool);

  /* there is no return value, so just toss this object (probably Py_None) */
  Py_DECREF(result);

  return SVN_NO_ERROR;
}

static svn_error_t *
thunk_apply_textdelta(void *file_baton, 
                      apr_pool_t *pool,
                      svn_txdelta_window_handler_t *handler,
                      void **h_baton)
{
  item_baton *ib = file_baton;
  PyObject *result;

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"apply_textdelta",
                                    (char *)"(O)", ib->baton)) == NULL)
    {
      return convert_python_error(ib->pool);
    }

  if (result == Py_None)
    {
      Py_DECREF(result);
      *handler = NULL;
      *h_baton = NULL;
    }
  else
    {
      handler_baton *hb = apr_palloc(ib->pool, sizeof(*hb));

      /* return the thunk for invoking the handler. the baton takes our
         'result' reference. */
      hb->handler = result;
      hb->pool = ib->pool;

      *handler = thunk_window_handler;
      *h_baton = hb;
    }

  return SVN_NO_ERROR;
}

static svn_error_t * thunk_change_file_prop(void *file_baton,
                                            const char *name,
                                            const svn_string_t *value,
                                            apr_pool_t *pool)
{
  item_baton *ib = file_baton;
  PyObject *result;

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"change_file_prop",
                                    (char *)"Oss#O&", ib->baton, name,
                                    value->data, value->len,
                                    make_ob_pool, pool)) == NULL)
    {
      return convert_python_error(pool);
    }

  /* there is no return value, so just toss this object (probably Py_None) */
  Py_DECREF(result);

  return SVN_NO_ERROR;
}

static svn_error_t * thunk_close_file(void *file_baton,
                                      apr_pool_t *pool)
{
  return close_baton(file_baton, "close_file");
}

static svn_error_t * thunk_close_edit(void *edit_baton,
                                      apr_pool_t *pool)
{
  return close_baton(edit_baton, "close_edit");
}

static svn_error_t * thunk_abort_edit(void *edit_baton,
                                      apr_pool_t *pool)
{
  return close_baton(edit_baton, "abort_edit");
}

static const svn_delta_editor_t thunk_editor = {
  thunk_set_target_revision,
  thunk_open_root,
  thunk_delete_entry,
  thunk_add_directory,
  thunk_open_directory,
  thunk_change_dir_prop,
  thunk_close_directory,
  thunk_add_file,
  thunk_open_file,
  thunk_apply_textdelta,
  thunk_change_file_prop,
  thunk_close_file,
  thunk_close_edit,
  thunk_abort_edit
};

void svn_swig_py_make_editor(const svn_delta_editor_t **editor,
                             void **edit_baton,
                             PyObject *py_editor,
                             apr_pool_t *pool)
{
  *editor = &thunk_editor;
  *edit_baton = make_baton(pool, py_editor, NULL);
}

/* This is very hacky and gross.  And did I mention broken? */
apr_file_t *svn_swig_py_make_file (PyObject *py_file,
                                   apr_pool_t *pool)
{
  apr_file_t *apr_file;
  apr_status_t status;
  FILE *file;
  int fd = -1;

  if (py_file == NULL || py_file == Py_None)
    return NULL;

  if (PyString_Check (py_file))
    {
      fd = open (PyString_AS_STRING (py_file),
                 O_CREAT | O_RDWR,
                 S_IRUSR | S_IWUSR);
    }
  else if (PyFile_Check (py_file))
    {
      file = PyFile_AsFile (py_file);
      fd = fileno (file);
    }
  else if (PyInt_Check (py_file))
    {
      fd = PyInt_AsLong (py_file);
    }

  if (fd >= 0) 
    {  
      status = apr_os_file_put (&apr_file, &fd, O_CREAT | O_WRONLY, pool);
    }

  /* FIXME: We shouldn't just silently fail. */

  return apr_file;
}

void svn_swig_py_notify_func(void *baton,
                             const char *path,
                             svn_wc_notify_action_t action,
                             svn_node_kind_t kind,
                             const char *mime_type,
                             svn_wc_notify_state_t content_state,
                             svn_wc_notify_state_t prop_state,
                             svn_revnum_t revision)
{
  PyObject *function = baton;
  PyObject *result;

  if (function != NULL && function != Py_None)
    {
      if ((result = PyObject_CallFunction(function, 
                                          (char *)"(siisiii)", 
                                          path, action, kind,
                                          mime_type,
                                          content_state, prop_state, 
                                          revision)) != NULL)
        {
          Py_XDECREF(result);
        }
    }
}

svn_error_t *
svn_swig_py_get_commit_log_func(const char **log_msg,
                                apr_array_header_t *commit_items,
                                void *baton,
                                apr_pool_t *pool)
{
  PyObject *function = baton;
  PyObject *result;
  PyObject *cmt_items;

  if ((function == NULL) || (function == Py_None))
    return SVN_NO_ERROR;

  if (commit_items)
    {
      cmt_items = commit_item_array_to_list(commit_items);
    }
  else
    {
      cmt_items = Py_None;
      Py_INCREF(Py_None);
    }

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallFunction(function, 
                                      (char *)"OO&",
                                      cmt_items,
                                      make_ob_pool, pool)) == NULL)
    {
      Py_DECREF(cmt_items);
      return convert_python_error(pool);
    }

  Py_DECREF(cmt_items);

  if (result == Py_None)
    {
      Py_DECREF(result);
      *log_msg = NULL;
      return SVN_NO_ERROR;
    }
  else if (PyString_Check(result)) 
    {
      *log_msg = apr_pstrdup(pool, PyString_AS_STRING(result));
      Py_DECREF(result);
      return SVN_NO_ERROR;
    }
     
  Py_DECREF(result);
  PyErr_SetString(PyExc_TypeError, "not a string");
  return convert_python_error(pool);
}



/*** Other Wrappers for SVN Functions ***/

svn_error_t * svn_swig_py_thunk_log_receiver(void *baton,
                                             apr_hash_t *changed_paths,
                                             svn_revnum_t rev,
                                             const char *author,
                                             const char *date,
                                             const char *msg,
                                             apr_pool_t *pool)
{
  PyObject *receiver = baton;
  PyObject *result;
  swig_type_info *tinfo = SWIG_TypeQuery("SWIGTYPE_p_svn_log_changed_path_t");
  PyObject *chpaths;
 
  if ((receiver == NULL) || (receiver == Py_None))
    return SVN_NO_ERROR;

  if (changed_paths)
    {
      chpaths = svn_swig_py_convert_hash (changed_paths, tinfo);
    }
  else
    {
      chpaths = Py_None;
      Py_INCREF(Py_None);
    }

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallFunction(receiver, 
                                      (char *)"OlsssO&", 
                                      chpaths, rev, author, date, msg, 
                                      make_ob_pool, pool)) == NULL)
    {
      Py_DECREF(chpaths);
      return convert_python_error(pool);
    }

  /* there is no return value, so just toss this object (probably Py_None) */
  Py_DECREF(result);
  Py_DECREF(chpaths);
  return SVN_NO_ERROR;
}

/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */
