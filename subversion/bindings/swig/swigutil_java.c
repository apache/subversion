/*
 * swigutil_java.c: utility functions for the SWIG Java bindings
 *
 * ====================================================================
 * Copyright (c) 2002 CollabNet.  All rights reserved.
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


#include <jni.h>

#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_string.h"
#include "svn_delta.h"

#include "swigutil_java.h"


/* this baton is used for the editor, directory, and file batons. */
typedef struct {
  jobject editor;     /* the editor handling the callbacks */
  jobject baton;      /* the dir/file baton (or NULL for edit baton) */
  apr_pool_t *pool;     /* pool to use for errors */
} item_baton;

typedef struct {
  jobject handler;    /* the window handler (a callable) */
  apr_pool_t *pool;     /* a pool for constructing errors */
} handler_baton;


static jobject make_pointer(const char *typename, void *ptr)
{
  /* ### cache the swig_type_info at some point? */
  return SWIG_NewPointerObj(ptr, SWIG_TypeQuery(typename), 0);
}

/* for use by the "O&" format specifier */
static jobject make_ob_pool(void *ptr)
{
  return make_pointer("apr_pool_t *", ptr);
}

/* for use by the "O&" format specifier */
static jobject make_ob_window(void *ptr)
{
  return make_pointer("svn_txdelta_window_t *", ptr);
}

static jobject convert_hash(apr_hash_t *hash,
                            jobject  (*converter_func)(void *value,
                                                       void *ctx),
                            void *ctx)
{
  apr_hash_index_t *hi;
  jclass cls = JCALL1(FindClass, jenv, "java/util/HashMap");
  jobject dict = JCALL3(NewObject, jenv, cls,
                        JCALL3(GetMethodID, jenv, cls, "<init>", "(I)V"),
                        (jint) apr_hash_count(hash));
  jmethoID put = JCALL3(GetMethodID, jenv, cls, "put",
                        "(Ljava/lang/Object;Ljava/lang/Object;)"
                        "Ljava/lang/Object;"),

  if (dict == NULL)
    return NULL;

  for (hi = apr_hash_first(NULL, hash); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      jobject value;

      apr_hash_this(hi, &key, NULL, &val);
      value = (*converter_func)(val, ctx);
      JCALL3(CallObjectMethod, jenv, put,
              JCALL2(NewString, jenv, key, strlen(key)), value);
      JCALL1(DeleteLocalRef, jenv, value);
    }

  return dict;
}

static jobject convert_to_swigtype(void *value, void *ctx)
{
  /* ctx is a 'swig_type_info *' */
  return SWIG_NewPointerObj(value, ctx, 0);
}

static jobject convert_svn_string_t(void *value, void *ctx)
{
  JNIEnv *jenv = (JNIEnv *) ctx;
  const svn_string_t *s = value;

  /* ### borrowing from value in the pool. or should we copy? note
     ### that copying is "safest" */

  return JCALL2(NewString, jenv, s->data, s->len);
}


jobject svn_swig_java_prophash_to_dict(JNIEnv *jenv, apr_hash_t *hash)
{
  return convert_hash(hash, convert_svn_string_t, jenv);
}

jobject svn_swig_java_convert_hash(JNIEnv *jenv, apr_hash_t *hash, swig_type_info *type)
{
  return convert_hash(hash, convert_to_swigtype, type);
}

jobject svn_swig_java_c_strings_to_list(JNIEnv *jenv, char **strings)
{
  jclass cls = JCALL1(FindClass, jenv, "java/util/ArrayList");
  jobject list = JCALL2(NewObject, jenv, cls,
                        JCALL3(GetMethodID, jenv, cls, "<init>", "()V"));
  jmethodID add = JCALL3(GetMethodID, jenv, cls, "add", "(Ljava/lang/Object;)Z");
  char *s;
  jobject obj;
  while ((s = *strings++) != NULL)
    {
      obj = JCALL2(NewString, jenv, s, strlen(s));

      if (obj == NULL)
          goto error;

      JCALL3(CallBooleanMethod, jenv, list, add, obj);

      JCALL1(DeleteLocalRef, jenv, obj);
    }

  return list;

 error:
  JCALL1(DeleteLocalRef, jenv, list);
  return NULL;
}

jobject svn_swig_java_array_to_list(JNIEnv *jenv, const apr_array_header_t *strings)
{
  jclass cls = JCALL1(FindClass, jenv, "java/util/ArrayList");
  jobject list = JCALL3(NewObject, jenv, cls,
                        JCALL3(GetMethodID, jenv, cls, "<init>", "(I)V"),
                        strings->nelts);
  int i;
  jobject obj;

  for (i = 0; i < strings->nelts; ++i)
    {
      const char *s;

      s = APR_ARRAY_IDX(strings, i, const char *);
      obj = JCALL2(NewString, jenv, s, strlen(s));
      if (obj == NULL)
        goto error;
      PyList_SET_ITEM(list, i, obj);
      JCALL1(DeleteLocaleRef, jenv, obj);
    }

  return list;

 error:
  JCALL1(DeleteLocaleRef, jenv, list);
  return NULL;
}

const apr_array_header_t *svn_swig_java_strings_to_array(JNIEnv *jenv,
                                                         jobject source,
                                                         apr_pool_t *pool)
{
  int targlen;
  apr_array_header_t *temp;

  jclass cls = JCALL1(FindClass, jenv, "lang/util/List");
  jmethodID size = JCALL3(GetMethodID, jenv, cls, "size", "()I");
  jmethodID get = JCALL3(GetMethodID, jenv, cls, "get",
                         "(I)Ljava/lang/Object;");
  if (!JCALL2(IsInstanceOf, jenv, source, cls))
    {
      /* FIXME: Resport by error (perhaps by throw an Exception)
         PyErr_SetString(PyExc_TypeError, "not a sequence"); */
      return NULL;
    }
  targlen = JCALL2(CallIntegerMethod, jenv, source, size);
  temp = apr_array_make(pool, targlen, sizeof(const char *));
  while (targlen--)
    {
      jobject o = JCALL3(CallObjectMethod, jenv, source, get, targlen);
      if (o == NULL)
          return NULL;
      if (!PyString_Check(o))
        {
          JCALL1(DeleteLocalRef, jenv, o);
          /* FIXME: Resport by error (perhaps by throw an Exception)
             PyErr_SetString(PyExc_TypeError, "not a sequence"); */
          return NULL;
        }
      APR_ARRAY_IDX(temp, targlen, const char *) = PyString_AS_STRING(o);
      JCALL1(DeleteLocalRef, jenv, o);
    }
  return temp;
}


static svn_error_t * convert_java_error(JNIEnv *jenv, apr_pool_t *pool)
{
  /* ### need to fetch the Python error and map it to an svn_error_t */

  return svn_error_create(APR_EGENERAL, NULL, pool,
                          "the Python callback raised an exception");
}

static item_baton * make_baton(JNIEnv *jenv, apr_pool_t *pool,
                               jobject editor, jobject baton)
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

static svn_error_t * close_baton(JNIEnv *jenv, void *baton, const char *method)
{
  item_baton *ib = baton;
  jobject result;

  /* If there is no baton object, then it is an edit_baton, and we should
     not bother to pass an object. Note that we still shove a NULL onto
     the stack, but the format specified just won't reference it.  */
  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)method,
                                    ib->baton ? (char *)"O" : NULL,
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
                                               svn_revnum_t target_revision)
{
  item_baton *ib = edit_baton;
  jobject result;

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
  jobject result;

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
  jobject result;

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
  jobject result;

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
  jobject result;

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
  jobject result;

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

static svn_error_t * thunk_close_directory(void *dir_baton)
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
  jobject result;

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
  jobject result;

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
  jobject result;

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

static svn_error_t * thunk_apply_textdelta(
    void *file_baton, 
    svn_txdelta_window_handler_t *handler,
    void **h_baton)
{
  item_baton *ib = file_baton;
  jobject result;

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"apply_textdelta",
                                    (char *)"O", ib->baton)) == NULL)
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
  jobject result;

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

static svn_error_t * thunk_close_file(void *file_baton)
{
  return close_baton(file_baton, "close_file");
}

static svn_error_t * thunk_close_edit(void *edit_baton)
{
  return close_baton(edit_baton, "close_edit");
}

static svn_error_t * thunk_abort_edit(void *edit_baton)
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

void svn_swig_java_make_editor(JNIEnv *jenv,
                               const svn_delta_editor_t **editor,
                               void **edit_baton,
                               jobject java_editor,
                               apr_pool_t *pool)
{
  *editor = &thunk_editor;
  *edit_baton = make_baton(pool, java_editor, NULL);
}
