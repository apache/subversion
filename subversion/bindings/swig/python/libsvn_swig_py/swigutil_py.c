/*
 * swigutil_py.c: utility functions for the SWIG Python bindings
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

/* Tell swigutil_py.h that we're inside the implementation */
#define SVN_SWIG_SWIGUTIL_PY_C

#include <Python.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_portable.h>
#include <apr_thread_proc.h>

#include "svn_client.h"
#include "svn_string.h"
#include "svn_opt.h"
#include "svn_delta.h"
#include "svn_auth.h"
#include "svn_pools.h"

#include "svn_private_config.h" /* for SVN_APR_INT64_T_PYCFMT */

#include "swig_python_external_runtime.swg"
#include "swigutil_py.h"



/*** Manage the Global Interpreter Lock ***/

/* If both Python and APR have threads available, we can optimize ourselves
 * by releasing the global interpreter lock when we drop into our SVN calls.
 *
 * In svn_types.i, svn_swig_py_release_py_lock is called before every
 * function, then svn_swig_py_acquire_py_lock is called after every
 * function.  So, if these functions become no-ops, then Python will
 * start to block...
 *
 * The Subversion libraries can be assumed to be thread-safe *only* when
 * APR_HAS_THREAD is 1.  The APR pool allocations aren't thread-safe unless
 * APR_HAS_THREAD is 1.
 */

#if defined(WITH_THREAD) && APR_HAS_THREADS
#define ACQUIRE_PYTHON_LOCK
#endif

#ifdef ACQUIRE_PYTHON_LOCK
static apr_threadkey_t *_saved_thread_key = NULL;
static apr_pool_t *_saved_thread_pool = NULL;
#endif

void svn_swig_py_release_py_lock(void)
{
#ifdef ACQUIRE_PYTHON_LOCK
  PyThreadState *thread_state;

  if (_saved_thread_key == NULL) {
    /* Obviously, creating a top-level pool for this is pretty stupid. */
    apr_pool_create(&_saved_thread_pool, NULL);
    apr_threadkey_private_create(&_saved_thread_key, NULL, _saved_thread_pool);
  }

  thread_state = PyEval_SaveThread();
  apr_threadkey_private_set(thread_state, _saved_thread_key);
#endif
}

void svn_swig_py_acquire_py_lock(void)
{
#ifdef ACQUIRE_PYTHON_LOCK
  void *val;
  PyThreadState *thread_state;
  apr_threadkey_private_get(&val, _saved_thread_key);
  thread_state = val;
  PyEval_RestoreThread(thread_state);
#endif
}



/*** Automatic Pool Management Functions ***/

/* The application pool */
static apr_pool_t *_global_pool = NULL;
static PyObject *_global_svn_swig_py_pool = NULL;
static char assertValid[] = "assert_valid";
static char parentPool[] = "_parent_pool";
static char wrap[] = "_wrap";
static char unwrap[] = "_unwrap";
static char setParentPool[] = "set_parent_pool";
static char emptyTuple[] = "()";
static char objectTuple[] = "(O)";


apr_status_t svn_swig_py_initialize(void)
{
  apr_status_t status;

  if ((status = apr_initialize()) != APR_SUCCESS)
    return status;
  if (atexit(apr_terminate) != 0)
    return APR_EGENERAL;
  return APR_SUCCESS;
}

/* Set the application pool */
void svn_swig_py_set_application_pool(PyObject *py_pool, apr_pool_t *pool) 
{
  _global_pool = pool;
  _global_svn_swig_py_pool = py_pool;
}

/* Clear the application pool */
void svn_swig_py_clear_application_pool()
{
  _global_pool = NULL;
  _global_svn_swig_py_pool = NULL;
}

/* Get the application pool */
void svn_swig_get_application_pool(PyObject **py_pool, apr_pool_t **pool)
{
  *pool = _global_pool;
  *py_pool = _global_svn_swig_py_pool;
}

/* Set the parent pool of a proxy object */
static int proxy_set_pool(PyObject **proxy, PyObject *pool)
{
  PyObject *result;

  if (*proxy != NULL) {
    if (pool == NULL) {
      if (PyObject_HasAttrString(*proxy, setParentPool)) {
        result = PyObject_CallMethod(*proxy, setParentPool, emptyTuple);
        if (result == NULL) {
          return 1;
        }
        Py_DECREF(result);
      }
    } else {
      result = PyObject_CallMethod(pool, wrap, objectTuple, *proxy);
      Py_DECREF(*proxy);
      *proxy = result;
    }
  }

  return 0;
}

/* Wrapper for SWIG_TypeQuery */
#define svn_swig_TypeQuery(x) SWIG_TypeQuery(x)

/** Wrapper for SWIG_NewPointerObj */
PyObject *svn_swig_NewPointerObj(void *obj, swig_type_info *type, 
                                 PyObject *pool)
{
  PyObject *proxy = SWIG_NewPointerObj(obj, type, 0);

  if (proxy == NULL) {
    return NULL;
  }
  
  if (proxy_set_pool(&proxy, pool)) {
    Py_DECREF(proxy);
    return NULL;
  }

  return proxy;
}

/** svn_swig_NewPointerObj, except a string is used to describe the type */
static PyObject *svn_swig_NewPointerObjString(void *ptr, const char *type, 
                                              PyObject *py_pool)
{
  swig_type_info *typeinfo = svn_swig_TypeQuery(type);
  if (typeinfo == NULL) {
    PyErr_SetString(PyExc_TypeError, "Cannot find required typeobject");
    return NULL;
  }
  /* ### cache the swig_type_info at some point? */
  return svn_swig_NewPointerObj(ptr, typeinfo, py_pool);
}

/** Wrapper for SWIG_ConvertPtr */
int svn_swig_ConvertPtr(PyObject *input, void **obj, swig_type_info *type)
{
  if (PyObject_HasAttrString(input, assertValid)) {
    PyObject *result = PyObject_CallMethod(input, assertValid, emptyTuple);
    if (result == NULL) {
      return 1;
    }
    Py_DECREF(result);
  }
  if (PyObject_HasAttrString(input, unwrap)) {
    input = PyObject_CallMethod(input, unwrap, emptyTuple);
    if (input == NULL) {
      return 1;
    }
    Py_DECREF(input);
  }
  return SWIG_ConvertPtr(input, obj, type, SWIG_POINTER_EXCEPTION | 0);
}

/** svn_swig_ConvertPtr, except a string is used to describe the type */
static int svn_swig_ConvertPtrString(PyObject *input,
    void **obj, const char *type)
{
  return svn_swig_ConvertPtr(input, obj, svn_swig_TypeQuery(type));
}

/** Wrapper for SWIG_MustGetPtr */
void *svn_swig_MustGetPtr(void *input, swig_type_info *type, int argnum,
                          PyObject **py_pool)
{
  if (PyObject_HasAttrString(input, assertValid)) {
    PyObject *result = PyObject_CallMethod(input, assertValid, emptyTuple);
    if (result == NULL) {
      return NULL;
    }
    Py_DECREF(result);
  }
  if (py_pool != NULL) {
    if (PyObject_HasAttrString(input, parentPool)) {
      *py_pool = PyObject_GetAttrString(input, parentPool);
      Py_DECREF(*py_pool);
    } else {
      *py_pool = _global_svn_swig_py_pool;
    }
  }
  if (PyObject_HasAttrString(input, unwrap)) {
    input = PyObject_CallMethod(input, unwrap, emptyTuple);
    if (input == NULL) {
      return NULL;
    }
    Py_DECREF((PyObject *) input);
  }
  return SWIG_MustGetPtr(input, type, argnum, SWIG_POINTER_EXCEPTION | 0);
}
  

/*** Custom SubversionException stuffs. ***/

/* Global SubversionException class object. */
static PyObject *SubversionException = NULL;


PyObject *svn_swig_py_exception_type(void)
{
  Py_INCREF(SubversionException);
  return SubversionException;
}

PyObject *svn_swig_py_register_exception(void)
{
  /* If we haven't created our exception class, do so. */
  if (SubversionException == NULL)
    {
      SubversionException = PyErr_NewException
        ((char *)"libsvn._core.SubversionException", NULL, NULL);
    }

  /* Regardless, return the exception class. */
  return svn_swig_py_exception_type();
}

void svn_swig_py_svn_exception(svn_error_t *err)
{ 
  PyObject *exc_ob, *apr_err_ob;

  if (err == NULL)
    return;

  /* Make an integer for the error code. */
  apr_err_ob = PyInt_FromLong(err->apr_err);
  if (apr_err_ob == NULL)
    return;

  /* Instantiate a SubversionException object. */
  exc_ob = PyObject_CallFunction(SubversionException, (char *)"sO", 
                                 err->message, apr_err_ob);
  if (exc_ob == NULL)
    {
      Py_DECREF(apr_err_ob);
      return;
    }

  /* Set the "apr_err" attribute of the exception to our error code. */
  if (PyObject_SetAttrString(exc_ob, (char *)"apr_err", apr_err_ob) == -1)
    {
      Py_DECREF(apr_err_ob);
      Py_DECREF(exc_ob);
      return;
    }

  /* Finished with the apr_err object. */
  Py_DECREF(apr_err_ob);

  /* Set the error state to our exception object. */
  PyErr_SetObject(SubversionException, exc_ob);

  /* Finished with the exc_ob object. */
  Py_DECREF(exc_ob);
}



/*** Helper/Conversion Routines ***/

/* Functions for making Python wrappers around Subversion structs */
static PyObject *make_ob_pool(void *pool)
{
  /* Return a brand new default pool to Python. This pool isn't
   * normally used for anything. It's just here for compatibility
   * with Subversion 1.2. */
  apr_pool_t *new_pool = svn_pool_create(_global_pool);
  PyObject *new_py_pool = svn_swig_NewPointerObj(new_pool,
    svn_swig_TypeQuery("apr_pool_t *"), _global_svn_swig_py_pool);
  (void) pool; /* Silence compiler warnings about unused parameter. */
  return new_py_pool;
}
static PyObject *make_ob_fs_root(svn_fs_root_t *ptr, PyObject *py_pool)
{
  return svn_swig_NewPointerObjString(ptr, "svn_fs_root_t *", py_pool);
} 
/***/

/* Conversion from Python single objects (not hashes/lists/etc.) to
   Subversion types. */
static const char *make_string_from_ob(PyObject *ob, apr_pool_t *pool)
{
  if (ob == Py_None)
    return NULL;
  if (! PyString_Check(ob)) {
    PyErr_SetString(PyExc_TypeError, "not a string");
    return NULL;
  }
  return apr_pstrdup(pool, PyString_AS_STRING(ob));
}
static svn_string_t *make_svn_string_from_ob(PyObject *ob, apr_pool_t *pool)
{
  if (ob == Py_None)
    return NULL;
  if (! PyString_Check(ob)) {
    PyErr_SetString(PyExc_TypeError, "not a string");
    return NULL;
  }
  return svn_string_create(PyString_AS_STRING(ob), pool);
}


/***/

static PyObject *convert_hash(apr_hash_t *hash,
                              PyObject * (*converter_func)(void *value,
                                                           void *ctx,
                                                           PyObject *py_pool),
                              void *ctx, PyObject *py_pool)
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
        value = (*converter_func)(val, ctx, py_pool);
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

static PyObject *convert_to_swigtype(void *value, void *ctx, PyObject *py_pool)
{
  /* ctx is a 'swig_type_info *' */
  return svn_swig_NewPointerObj(value, ctx, py_pool);
}

static PyObject *convert_svn_string_t(void *value, void *ctx,
                                      PyObject *py_pool)
{
  /* ctx is unused */

  const svn_string_t *s = value;

  /* ### gotta cast this thing cuz Python doesn't use "const" */
  return PyString_FromStringAndSize((void *)s->data, s->len);
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
  return convert_hash(hash, convert_svn_string_t, NULL, NULL);
}


PyObject *svn_swig_py_locationhash_to_dict(apr_hash_t *hash)
{
    /* Need special code for this because of the darned svn_revnum_t
       keys. */
    apr_hash_index_t *hi;
    PyObject *dict = PyDict_New();

    if (dict == NULL)
        return NULL;

    for (hi = apr_hash_first(NULL, hash); hi; hi = apr_hash_next(hi)) 
      {
        const void *k;
        void *v;
        PyObject *key, *value;

        apr_hash_this(hi, &k, NULL, &v);
        key = PyLong_FromLong(*(svn_revnum_t *)k);
        if (key == NULL)
          {
            Py_DECREF(dict);
            return NULL;
          }
        value = PyString_FromString((char *)v);
        if (value == NULL) {
            Py_DECREF(key);
            Py_DECREF(dict);
            return NULL;
        }
        if (PyDict_SetItem(dict, key, value) == -1) 
          {
            Py_DECREF(value);
            Py_DECREF(dict);
            return NULL;
          }
        Py_DECREF(value);
        Py_DECREF(key);
      }
    return dict;
}

PyObject *svn_swig_py_convert_hash(apr_hash_t *hash, swig_type_info *type, 
                                   PyObject *py_pool)
{
  return convert_hash(hash, convert_to_swigtype, type, py_pool);
}  

#define DECLARE_SWIG_CONSTRUCTOR(type, dup) \
static PyObject *make_ob_##type(void *value) \
{ \
  apr_pool_t *new_pool = svn_pool_create(_global_pool); \
  PyObject *new_py_pool = svn_swig_NewPointerObj(new_pool, \
    svn_swig_TypeQuery("apr_pool_t *"), _global_svn_swig_py_pool); \
  svn_##type##_t *new_value = dup(value, new_pool); \
  return svn_swig_NewPointerObjString(new_value, "svn_" #type "_t *", \
      new_py_pool); \
}
 
DECLARE_SWIG_CONSTRUCTOR(txdelta_window, svn_txdelta_window_dup)
DECLARE_SWIG_CONSTRUCTOR(log_changed_path, svn_log_changed_path_dup)
DECLARE_SWIG_CONSTRUCTOR(wc_status, svn_wc_dup_status)
DECLARE_SWIG_CONSTRUCTOR(lock, svn_lock_dup)
DECLARE_SWIG_CONSTRUCTOR(auth_ssl_server_cert_info,
    svn_auth_ssl_server_cert_info_dup)
DECLARE_SWIG_CONSTRUCTOR(info, svn_info_dup)

static PyObject *convert_log_changed_path(void *value, void *ctx,
                                          PyObject *py_pool)
{
  return make_ob_log_changed_path(value);
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

apr_hash_t *svn_swig_py_stringhash_from_dict(PyObject *dict,
                                             apr_pool_t *pool)
{
  apr_hash_t *hash;
  PyObject *keys;
  int i, num_keys;
  
  if (dict == Py_None)
    return NULL;

  if (!PyDict_Check(dict)) {
    PyErr_SetString(PyExc_TypeError, "not a dictionary");
    return NULL;
  }

  hash = apr_hash_make(pool);  
  keys = PyDict_Keys(dict);
  num_keys = PyList_Size(keys);
  for (i = 0; i < num_keys; i++)
    {
      PyObject *key = PyList_GetItem(keys, i);
      PyObject *value = PyDict_GetItem(dict, key);
      const char *propname = make_string_from_ob(key, pool);
      const char *propval = make_string_from_ob(value, pool);
      if (! (propname && propval))
        {
          PyErr_SetString(PyExc_TypeError, 
                          "dictionary keys/values aren't strings");
          Py_DECREF(keys);
          return NULL;
        }
      apr_hash_set(hash, propname, APR_HASH_KEY_STRING, propval);
    }
  Py_DECREF(keys);
  return hash;
}


apr_hash_t *svn_swig_py_prophash_from_dict(PyObject *dict,
                                           apr_pool_t *pool)
{
  apr_hash_t *hash;
  PyObject *keys;
  int i, num_keys;

  if (dict == Py_None)
    return NULL;

  if (!PyDict_Check(dict)) {
    PyErr_SetString(PyExc_TypeError, "not a dictionary");
    return NULL;
  }

  hash = apr_hash_make(pool);  
  keys = PyDict_Keys(dict);
  num_keys = PyList_Size(keys);
  for (i = 0; i < num_keys; i++)
    {
      PyObject *key = PyList_GetItem(keys, i);
      PyObject *value = PyDict_GetItem(dict, key);
      const char *propname = make_string_from_ob(key, pool);
      svn_string_t *propval = make_svn_string_from_ob(value, pool);
      if (! (propname && propval))
        {
          PyErr_SetString(PyExc_TypeError, 
                          "dictionary keys/values aren't strings");
          Py_DECREF(keys);
          return NULL;
        }
      apr_hash_set(hash, propname, APR_HASH_KEY_STRING, propval);
    }
  Py_DECREF(keys);
  return hash;
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


const apr_array_header_t *svn_swig_py_revnums_to_array(PyObject *source,
                                                       apr_pool_t *pool)
{
    int targlen;
    apr_array_header_t *temp;

    if (!PySequence_Check(source)) {
        PyErr_SetString(PyExc_TypeError, "not a sequence");
        return NULL;
    }
    targlen = PySequence_Length(source);
    temp = apr_array_make(pool, targlen, sizeof(svn_revnum_t));
    /* APR_ARRAY_IDX doesn't actually increment the array item count
       (like, say, apr_array_push would). */
    temp->nelts = targlen;
    while (targlen--) {
        PyObject *o = PySequence_GetItem(source, targlen);
        if (o == NULL)
            return NULL;
        if (PyLong_Check(o)) {
            APR_ARRAY_IDX(temp, targlen, svn_revnum_t) = 
              (svn_revnum_t)PyLong_AsLong(o);
        }
        else if (PyInt_Check(o)) {
            APR_ARRAY_IDX(temp, targlen, svn_revnum_t) = 
              (svn_revnum_t)PyInt_AsLong(o);
        }
        else {
            Py_DECREF(o);
            PyErr_SetString(PyExc_TypeError, "not an integer type");
            return NULL;
        }
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

/* Formerly used by pre-1.0 APIs. Now unused
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
*/

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



/*** Errors ***/

/* Return a Subversion error about a failed callback. */
static svn_error_t *callback_exception_error(void)
{
  return svn_error_create(SVN_ERR_SWIG_PY_EXCEPTION_SET, NULL,
                          "Python callback raised an exception");
}

/* Raise a TypeError exception with MESSAGE, and return a Subversion
   error about an invalid return from a callback. */
static svn_error_t *callback_bad_return_error(const char *message)
{
  PyErr_SetString(PyExc_TypeError, message);
  return svn_error_create(APR_EGENERAL, NULL,
                          "Python callback returned an invalid object");
}

/* Return a generic error about not being able to map types. */
static svn_error_t *type_conversion_error(const char *datatype)
{
  return svn_error_createf(APR_EGENERAL, NULL,
                           "Error converting object of type '%s'", datatype);
}



/*** Editor Wrapping ***/

/* this baton is used for the editor, directory, and file batons. */
typedef struct {
  PyObject *editor;     /* the editor handling the callbacks */
  PyObject *baton;      /* the dir/file baton (or NULL for edit baton) */
} item_baton;

static item_baton *make_baton(apr_pool_t *pool, 
                              PyObject *editor, 
                              PyObject *baton)
{
  item_baton *newb = apr_palloc(pool, sizeof(*newb));

  /* Note: We steal the caller's reference to 'baton'. Also, to avoid
     memory leaks, we borrow the caller's reference to 'editor'. In this
     case, borrowing the reference to 'editor' is safe because the contents
     of an item_baton struct are only used by functino calls which operate on
     the editor itself. */
  newb->editor = editor;
  newb->baton = baton;

  return newb;
}

static svn_error_t *close_baton(void *baton, 
                                const char *method)
{
  item_baton *ib = baton;
  PyObject *result;
  svn_error_t *err;

  svn_swig_py_acquire_py_lock();

  /* If there is no baton object, then it is an edit_baton, and we should
     not bother to pass an object. Note that we still shove a NULL onto
     the stack, but the format specified just won't reference it.  */
  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)method,
                                    ib->baton ? (char *)"(O)" : NULL,
                                    ib->baton)) == NULL)
    {
      err = callback_exception_error();
      goto finished;
    }

  /* there is no return value, so just toss this object (probably Py_None) */
  Py_DECREF(result);

  /* We're now done with the baton. Since there isn't really a free, all
     we need to do is note that its objects are no longer referenced by
     the baton.  */
  Py_XDECREF(ib->baton);

#ifdef SVN_DEBUG
  ib->editor = ib->baton = NULL;
#endif

  err = SVN_NO_ERROR;

 finished:
  svn_swig_py_release_py_lock();
  return err;
}

static svn_error_t *set_target_revision(void *edit_baton,
                                        svn_revnum_t target_revision,
                                        apr_pool_t *pool)
{
  item_baton *ib = edit_baton;
  PyObject *result;
  svn_error_t *err;

  svn_swig_py_acquire_py_lock();

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"set_target_revision",
                                    (char *)"l", target_revision)) == NULL)
    {
      err = callback_exception_error();
      goto finished;
    }

  /* there is no return value, so just toss this object (probably Py_None) */
  Py_DECREF(result);
  err = SVN_NO_ERROR;
  
 finished:
  svn_swig_py_release_py_lock();
  return err;
}

static svn_error_t *open_root(void *edit_baton,
                              svn_revnum_t base_revision,
                              apr_pool_t *dir_pool,
                              void **root_baton)
{
  item_baton *ib = edit_baton;
  PyObject *result;
  svn_error_t *err;

  svn_swig_py_acquire_py_lock();

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"open_root",
                                    (char *)"lO&", base_revision,
                                    make_ob_pool, dir_pool)) == NULL)
    {
      err = callback_exception_error();
      goto finished;
    }

  /* make_baton takes our 'result' reference */
  *root_baton = make_baton(dir_pool, ib->editor, result);
  err = SVN_NO_ERROR;

 finished:
  svn_swig_py_release_py_lock();
  return err;
}

static svn_error_t *delete_entry(const char *path,
                                 svn_revnum_t revision,
                                 void *parent_baton,
                                 apr_pool_t *pool)
{
  item_baton *ib = parent_baton;
  PyObject *result;
  svn_error_t *err;

  svn_swig_py_acquire_py_lock();

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"delete_entry",
                                    (char *)"slOO&", path, revision, ib->baton,
                                    make_ob_pool, pool)) == NULL)
    {
      err = callback_exception_error();
      goto finished;
    }

  /* there is no return value, so just toss this object (probably Py_None) */
  Py_DECREF(result);
  err = SVN_NO_ERROR;

 finished:
  svn_swig_py_release_py_lock();
  return err;
}

static svn_error_t *add_directory(const char *path,
                                  void *parent_baton,
                                  const char *copyfrom_path,
                                  svn_revnum_t copyfrom_revision,
                                  apr_pool_t *dir_pool,
                                  void **child_baton)
{
  item_baton *ib = parent_baton;
  PyObject *result;
  svn_error_t *err;

  svn_swig_py_acquire_py_lock();

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"add_directory",
                                    (char *)"sOslO&", path, ib->baton,
                                    copyfrom_path, copyfrom_revision,
                                    make_ob_pool, dir_pool)) == NULL)
    {
      err = callback_exception_error();
      goto finished;
    }

  /* make_baton takes our 'result' reference */
  *child_baton = make_baton(dir_pool, ib->editor, result);
  err = SVN_NO_ERROR;

 finished:
  svn_swig_py_release_py_lock();
  return err;
}

static svn_error_t *open_directory(const char *path,
                                   void *parent_baton,
                                   svn_revnum_t base_revision,
                                   apr_pool_t *dir_pool,
                                   void **child_baton)
{
  item_baton *ib = parent_baton;
  PyObject *result;
  svn_error_t *err;

  svn_swig_py_acquire_py_lock();

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"open_directory",
                                    (char *)"sOlO&", path, ib->baton,
                                    base_revision,
                                    make_ob_pool, dir_pool)) == NULL)
    {
      err = callback_exception_error();
      goto finished;
    }

  /* make_baton takes our 'result' reference */
  *child_baton = make_baton(dir_pool, ib->editor, result);
  err = SVN_NO_ERROR;
  
 finished:
  svn_swig_py_release_py_lock();
  return err;
}

static svn_error_t *change_dir_prop(void *dir_baton,
                                    const char *name,
                                    const svn_string_t *value,
                                    apr_pool_t *pool)
{
  item_baton *ib = dir_baton;
  PyObject *result;
  svn_error_t *err;

  svn_swig_py_acquire_py_lock();

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"change_dir_prop",
                                    (char *)"Oss#O&", ib->baton, name,
                                    value ? value->data : NULL, 
                                    value ? value->len : 0,
                                    make_ob_pool, pool)) == NULL)
    {
      err = callback_exception_error();
      goto finished;
    }

  /* there is no return value, so just toss this object (probably Py_None) */
  Py_DECREF(result);
  err = SVN_NO_ERROR;

 finished:
  svn_swig_py_release_py_lock();
  return err;
}

static svn_error_t *close_directory(void *dir_baton,
                                    apr_pool_t *pool)
{
  return close_baton(dir_baton, "close_directory");
}

static svn_error_t *add_file(const char *path,
                             void *parent_baton,
                             const char *copyfrom_path,
                             svn_revnum_t copyfrom_revision,
                             apr_pool_t *file_pool,
                             void **file_baton)
{
  item_baton *ib = parent_baton;
  PyObject *result;
  svn_error_t *err;

  svn_swig_py_acquire_py_lock();

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"add_file",
                                    (char *)"sOslO&", path, ib->baton,
                                    copyfrom_path, copyfrom_revision,
                                    make_ob_pool, file_pool)) == NULL)
    {
      err = callback_exception_error();
      goto finished;
    }

  /* make_baton takes our 'result' reference */
  *file_baton = make_baton(file_pool, ib->editor, result);

  err = SVN_NO_ERROR;

 finished:
  svn_swig_py_release_py_lock();
  return err;
}

static svn_error_t *open_file(const char *path,
                              void *parent_baton,
                              svn_revnum_t base_revision,
                              apr_pool_t *file_pool,
                              void **file_baton)
{
  item_baton *ib = parent_baton;
  PyObject *result;
  svn_error_t *err;
  
  svn_swig_py_acquire_py_lock();

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"open_file",
                                    (char *)"sOlO&", path, ib->baton,
                                    base_revision,
                                    make_ob_pool, file_pool)) == NULL)
    {
      err = callback_exception_error();
      goto finished;
    }

  /* make_baton takes our 'result' reference */
  *file_baton = make_baton(file_pool, ib->editor, result);
  err = SVN_NO_ERROR;

 finished:
  svn_swig_py_release_py_lock();
  return err;
}

static svn_error_t *window_handler(svn_txdelta_window_t *window,
                                   void *baton)
{
  PyObject *handler = baton;
  PyObject *result;
  svn_error_t *err;

  svn_swig_py_acquire_py_lock();

  if (window == NULL)
    {
      /* the last call; it closes the handler */

      /* invoke the handler with None for the window */
      /* ### python doesn't have 'const' on the format */
      result = PyObject_CallFunction(handler, (char *)"O", Py_None);

      /* we no longer need to refer to the handler object */
      Py_DECREF(handler);
    }
  else
    {
      /* invoke the handler with the window */
      /* ### python doesn't have 'const' on the format */
      result = PyObject_CallFunction(handler, (char *)"O&", 
        make_ob_txdelta_window, window);
    }

  if (result == NULL)
    {
      err = callback_exception_error();
      goto finished;
    }

  /* there is no return value, so just toss this object (probably Py_None) */
  Py_DECREF(result);
  err = SVN_NO_ERROR;

 finished:
  svn_swig_py_release_py_lock();
  return err;
}

static svn_error_t *apply_textdelta(void *file_baton, 
                                    const char *base_checksum,
                                    apr_pool_t *pool,
                                    svn_txdelta_window_handler_t *handler,
                                    void **h_baton)
{
  item_baton *ib = file_baton;
  PyObject *result;
  svn_error_t *err;

  svn_swig_py_acquire_py_lock();

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"apply_textdelta",
                                    (char *)"(Os)", ib->baton,
                                    base_checksum)) == NULL)
    {
      err = callback_exception_error();
      goto finished;
    }

  /* Interpret None to mean svn_delta_noop_window_handler. This is much
     easier/faster than making code always have to write a NOOP handler
     in Python.  */
  if (result == Py_None)
    {
      Py_DECREF(result);

      *handler = svn_delta_noop_window_handler;
      *h_baton = NULL;
    }
  else
    {
      /* return the thunk for invoking the handler. the baton takes our
         'result' reference, which is the handler. */
      *handler = window_handler;
      *h_baton = result;
    }

  err = SVN_NO_ERROR;

 finished:
  svn_swig_py_release_py_lock();
  return err;
}

static svn_error_t *change_file_prop(void *file_baton,
                                     const char *name,
                                     const svn_string_t *value,
                                     apr_pool_t *pool)
{
  item_baton *ib = file_baton;
  PyObject *result;
  svn_error_t *err;

  svn_swig_py_acquire_py_lock();

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"change_file_prop",
                                    (char *)"Oss#O&", ib->baton, name,
                                    value ? value->data : NULL, 
                                    value ? value->len : 0,
                                    make_ob_pool, pool)) == NULL)
    {
      err = callback_exception_error();
      goto finished;
    }

  /* there is no return value, so just toss this object (probably Py_None) */
  Py_DECREF(result);
  err = SVN_NO_ERROR;
  
 finished:
  svn_swig_py_release_py_lock();
  return err;
}

static svn_error_t *close_file(void *file_baton,
                               const char *text_checksum,
                               apr_pool_t *pool)
{
  item_baton *ib = file_baton;
  PyObject *result;
  svn_error_t *err;

  svn_swig_py_acquire_py_lock();

  /* ### python doesn't have 'const' on the method name and format */
  if ((result = PyObject_CallMethod(ib->editor, (char *)"close_file",
                                    (char *)"(Os)", ib->baton,
                                    text_checksum)) == NULL)
    {
      err = callback_exception_error();
      goto finished;
    }

  /* there is no return value, so just toss this object (probably Py_None) */
  Py_DECREF(result);

  /* We're now done with the baton. Since there isn't really a free, all
     we need to do is note that its objects are no longer referenced by
     the baton.  */
  Py_XDECREF(ib->baton);

#ifdef SVN_DEBUG
  ib->editor = ib->baton = NULL;
#endif

  err = SVN_NO_ERROR;

 finished:
  svn_swig_py_release_py_lock();
  return err;
}

static svn_error_t *close_edit(void *edit_baton,
                               apr_pool_t *pool)
{
  return close_baton(edit_baton, "close_edit");
}

static svn_error_t *abort_edit(void *edit_baton,
                               apr_pool_t *pool)
{
  return close_baton(edit_baton, "abort_edit");
}

void svn_swig_py_make_editor(const svn_delta_editor_t **editor,
                             void **edit_baton,
                             PyObject *py_editor,
                             apr_pool_t *pool)
{
  svn_delta_editor_t *thunk_editor = svn_delta_default_editor(pool);
  
  thunk_editor->set_target_revision = set_target_revision;
  thunk_editor->open_root = open_root;
  thunk_editor->delete_entry = delete_entry;
  thunk_editor->add_directory = add_directory;
  thunk_editor->open_directory = open_directory;
  thunk_editor->change_dir_prop = change_dir_prop;
  thunk_editor->close_directory = close_directory;
  thunk_editor->add_file = add_file;
  thunk_editor->open_file = open_file;
  thunk_editor->apply_textdelta = apply_textdelta;
  thunk_editor->change_file_prop = change_file_prop;
  thunk_editor->close_file = close_file;
  thunk_editor->close_edit = close_edit;
  thunk_editor->abort_edit = abort_edit;

  *editor = thunk_editor;
  *edit_baton = make_baton(pool, py_editor, NULL);
}



/*** Other Wrappers for SVN Functions ***/


apr_file_t *svn_swig_py_make_file(PyObject *py_file,
                                  apr_pool_t *pool)
{
  apr_file_t *apr_file = NULL;
  apr_status_t apr_err;

  if (py_file == NULL || py_file == Py_None)
    return NULL;

  if (PyString_Check(py_file))
    {
      /* input is a path -- just open an apr_file_t */
      char* fname = PyString_AS_STRING(py_file);
      apr_err = apr_file_open(&apr_file, fname,
                              APR_CREATE | APR_READ | APR_WRITE,
                              APR_OS_DEFAULT, pool);
      if (apr_err)
        {
          char buf[256];
          apr_strerror(apr_err, buf, sizeof(buf));
          PyErr_Format(PyExc_IOError, "apr_file_open failed: %s: '%s'",
                       buf, fname);
          return NULL;
        }
    }
  else if (PyFile_Check(py_file))
    {
      FILE *file;
      apr_os_file_t osfile;

      /* input is a file object -- convert to apr_file_t */
      file = PyFile_AsFile(py_file);
#ifdef WIN32
      osfile = (apr_os_file_t)_get_osfhandle(_fileno(file));
#else
      osfile = (apr_os_file_t)fileno(file);
#endif
      apr_err = apr_os_file_put(&apr_file, &osfile, O_CREAT | O_WRONLY, pool);
      if (apr_err)
        {
          char buf[256];
          apr_strerror(apr_err, buf, sizeof(buf));
          PyErr_Format(PyExc_IOError, "apr_os_file_put failed: %s", buf);
          return NULL;
        }
    }
  return apr_file;
}


static svn_error_t *
read_handler_pyio(void *baton, char *buffer, apr_size_t *len)
{
  PyObject *result;
  PyObject *py_io = baton;
  apr_size_t bytes;
  svn_error_t *err = SVN_NO_ERROR;

  svn_swig_py_acquire_py_lock();
  if ((result = PyObject_CallMethod(py_io, (char *)"read",
                                    (char *)"i", *len)) == NULL)
    {
      err = callback_exception_error();
    }
  else if (PyString_Check(result))
    {
      bytes = PyString_GET_SIZE(result);
      if (bytes > *len)
        {
          err = callback_bad_return_error("Too many bytes");
        }
      else
        {
          /* Writeback, in case this was a short read, indicating EOF */
          *len = bytes;
          memcpy(buffer, PyString_AS_STRING(result), *len);
        }
    }
  else
    {
      err = callback_bad_return_error("Not a string");
    }
  Py_XDECREF(result);
  svn_swig_py_release_py_lock();

  return err;
}

static svn_error_t *
write_handler_pyio(void *baton, const char *data, apr_size_t *len)
{
  PyObject *result;
  PyObject *py_io = baton;
  svn_error_t *err = SVN_NO_ERROR;

  if (data != NULL)
    {
      svn_swig_py_acquire_py_lock();
      if ((result = PyObject_CallMethod(py_io, (char *)"write",
                                        (char *)"s#", data, *len)) == NULL)
        {
          err = callback_exception_error();
        }
      Py_XDECREF(result);
      svn_swig_py_release_py_lock();
    }

  return err;
}

svn_stream_t *
svn_swig_py_make_stream(PyObject *py_io, apr_pool_t *pool)
{
  svn_stream_t *stream;

  /* Borrow the caller's reference to py_io - this is safe only because the
   * caller must have a reference in order to pass the object into the 
   * bindings, and we will be finished with the py_io object before we return
   * to python. I.e. DO NOT STORE AWAY THE RESULTING svn_stream_t * for use
   * over multiple calls into the bindings. */
  stream = svn_stream_create(py_io, pool);
  svn_stream_set_read(stream, read_handler_pyio);
  svn_stream_set_write(stream, write_handler_pyio);

  return stream;
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
  svn_error_t *err = SVN_NO_ERROR;

  if (function == NULL || function == Py_None)
    return;

  svn_swig_py_acquire_py_lock();
  if ((result = PyObject_CallFunction(function, 
                                      (char *)"(siisiii)", 
                                      path, action, kind,
                                      mime_type,
                                      content_state, prop_state, 
                                      revision)) == NULL)
    {
      err = callback_exception_error();
    }
  else
    {
      /* The callback shouldn't be returning anything. */
      if (result != Py_None)
        err = callback_bad_return_error("Not None");
      Py_DECREF(result);
    }

  /* Our error has no place to go. :-( */
  if (err)
    svn_error_clear(err);

  svn_swig_py_release_py_lock();
}


void svn_swig_py_status_func(void *baton,
                             const char *path,
                             svn_wc_status_t *status)
{
  PyObject *function = baton;
  PyObject *result;
  svn_error_t *err = SVN_NO_ERROR;

  if (function == NULL || function == Py_None)
    return;

  svn_swig_py_acquire_py_lock();
  if ((result = PyObject_CallFunction(function, (char *)"sO&", path,
                                      make_ob_wc_status, status)) == NULL)
    {
      err = callback_exception_error();
    }
  else
    {
      /* The callback shouldn't be returning anything. */
      if (result != Py_None)
        err = callback_bad_return_error("Not None");
      Py_DECREF(result);
    }

  /* Our error has no place to go. :-( */
  if (err)
    svn_error_clear(err);
    
  svn_swig_py_release_py_lock();
}


svn_error_t *svn_swig_py_cancel_func(void *cancel_baton)
{
  PyObject *function = cancel_baton;
  PyObject *result;
  svn_error_t *err = SVN_NO_ERROR;

  if (function == NULL || function == Py_None)
    return SVN_NO_ERROR;

  svn_swig_py_acquire_py_lock();
  if ((result = PyObject_CallFunction(function, NULL)) == NULL)
    {
      err = callback_exception_error();
    }
  else
    {
      if (PyInt_Check(result))
        {
          if (PyInt_AsLong(result))
            err = svn_error_create(SVN_ERR_CANCELLED, 0, NULL);
        }
      else if (PyLong_Check(result))
        {
          if (PyLong_AsLong(result))
            err = svn_error_create(SVN_ERR_CANCELLED, 0, NULL);
        }
      else if (result != Py_None)
        {
          err = callback_bad_return_error("Not an integer or None");
        }
      Py_DECREF(result);
    }
  svn_swig_py_release_py_lock();
  return err;
}

svn_error_t *svn_swig_py_fs_get_locks_func(void *baton, 
                                           svn_lock_t *lock,
                                           apr_pool_t *pool)
{
  PyObject *function = baton;
  PyObject *result;
  svn_error_t *err = SVN_NO_ERROR;

  if (function == NULL || function == Py_None)
    return SVN_NO_ERROR;

  svn_swig_py_acquire_py_lock();
  
  if ((result = PyObject_CallFunction(function, (char *)"O&O&",
                                      make_ob_lock, lock,
                                      make_ob_pool, pool)) == NULL)
    {
      err = callback_exception_error();
    }
  else
    {
      /* The callback shouldn't be returning anything. */
      if (result != Py_None)
        err = callback_bad_return_error("Not None");
      Py_DECREF(result);
    }

  svn_swig_py_release_py_lock();
  return err;
}

svn_error_t *svn_swig_py_get_commit_log_func(const char **log_msg,
                                             const char **tmp_file,
                                             apr_array_header_t *commit_items,
                                             void *baton,
                                             apr_pool_t *pool)
{
  PyObject *function = baton;
  PyObject *result;
  PyObject *cmt_items;
  svn_error_t *err;

  *log_msg = NULL;
  *tmp_file = NULL;

  /* ### todo: for now, just ignore the whole tmp_file thing.  */

  if ((function == NULL) || (function == Py_None))
    return SVN_NO_ERROR;

  svn_swig_py_acquire_py_lock();

  if (commit_items)
    {
      cmt_items = commit_item_array_to_list(commit_items);
    }
  else
    {
      cmt_items = Py_None;
      Py_INCREF(Py_None);
    }

  if ((result = PyObject_CallFunction(function, 
                                      (char *)"OO&",
                                      cmt_items,
                                      make_ob_pool, pool)) == NULL)
    {
      Py_DECREF(cmt_items);
      err = callback_exception_error();
      goto finished;
    }

  Py_DECREF(cmt_items);

  if (result == Py_None)
    {
      Py_DECREF(result);
      *log_msg = NULL;
      err = SVN_NO_ERROR;
    }
  else if (PyString_Check(result)) 
    {
      *log_msg = apr_pstrdup(pool, PyString_AS_STRING(result));
      Py_DECREF(result);
      err = SVN_NO_ERROR;
    }
  else
    {
      Py_DECREF(result);
      err = callback_bad_return_error("Not a string");
    }

 finished:
  svn_swig_py_release_py_lock();
  return err;
}


svn_error_t *svn_swig_py_repos_authz_func(svn_boolean_t *allowed,
                                          svn_fs_root_t *root,
                                          const char *path,
                                          void *baton,
                                          apr_pool_t *pool)
{
  PyObject *function = baton;
  PyObject *result;
  PyObject *py_pool, *py_root;
  svn_error_t *err = SVN_NO_ERROR;

  *allowed = TRUE;

  if (function == NULL || function == Py_None)
    return SVN_NO_ERROR;

  svn_swig_py_acquire_py_lock();
  
  py_pool = make_ob_pool(pool);
  if (py_pool == NULL) {
    err = callback_exception_error();
    goto finished;
  }
  py_root = make_ob_fs_root(root, py_pool);
  if (py_root == NULL) {
    Py_DECREF(py_pool);
    err = callback_exception_error();
    goto finished; 
  }

  if ((result = PyObject_CallFunction(function, 
                                      (char *)"OsO", 
                                      py_root, path, py_pool)) == NULL)
    {
      err = callback_exception_error();
    }
  else
    {
      if (PyInt_Check(result))
        *allowed = PyInt_AsLong(result);
      else if (PyLong_Check(result))
        *allowed = PyLong_AsLong(result);
      else
        err = callback_bad_return_error("Not an integer");
      Py_DECREF(result);
    }
  Py_DECREF(py_root);
  Py_DECREF(py_pool);
finished:
  svn_swig_py_release_py_lock();
  return err;
}


svn_error_t *svn_swig_py_repos_history_func(void *baton,
                                            const char *path,
                                            svn_revnum_t revision,
                                            apr_pool_t *pool)
{
  PyObject *function = baton;
  PyObject *result;
  svn_error_t *err = SVN_NO_ERROR;

  if (function == NULL || function == Py_None)
    return SVN_NO_ERROR;

  svn_swig_py_acquire_py_lock();
  if ((result = PyObject_CallFunction(function, 
                                      (char *)"slO&", 
                                      path, revision, 
                                      make_ob_pool, pool)) == NULL)
    {
      err = callback_exception_error();
    }
  else
    {
      if (result != Py_None)
        err = callback_bad_return_error("Not None");
      Py_DECREF(result);
    }
  svn_swig_py_release_py_lock();
  return err;
}


svn_error_t *svn_swig_py_log_receiver(void *baton,
                                      apr_hash_t *changed_paths,
                                      svn_revnum_t rev,
                                      const char *author,
                                      const char *date,
                                      const char *msg,
                                      apr_pool_t *pool)
{
  PyObject *receiver = baton;
  PyObject *result, *py_pool;
  PyObject *chpaths;
  svn_error_t *err = SVN_NO_ERROR;
 
  if ((receiver == NULL) || (receiver == Py_None))
    return SVN_NO_ERROR;

  svn_swig_py_acquire_py_lock();

  py_pool = make_ob_pool(pool);
  if (py_pool == NULL) {
    err = callback_exception_error();
    goto finished;
  }

  if (changed_paths)
    {
      chpaths = convert_hash(changed_paths, convert_log_changed_path,
                             NULL, NULL);
    }
  else
    {
      chpaths = Py_None;
      Py_INCREF(Py_None);
    }

  if ((result = PyObject_CallFunction(receiver, 
                                      (char *)"OlsssO", 
                                      chpaths, rev, author, date, msg, 
                                      py_pool)) == NULL)
    {
      err = callback_exception_error();
    }
  else
    {
      if (result != Py_None)
        err = callback_bad_return_error("Not None");
      Py_DECREF(result);
    }

  Py_DECREF(chpaths);
  Py_DECREF(py_pool);
finished:
  svn_swig_py_release_py_lock();
  return err;
}

svn_error_t *svn_swig_py_info_receiver_func(void *baton,
                                            const char *path,
                                            const svn_info_t *info,
                                            apr_pool_t *pool)
{
  PyObject *receiver = baton;
  PyObject *result;
  svn_error_t *err = SVN_NO_ERROR;
 
  if ((receiver == NULL) || (receiver == Py_None))
    return SVN_NO_ERROR;

  svn_swig_py_acquire_py_lock();

  if ((result = PyObject_CallFunction(receiver, 
                                      (char *)"sO&O&", 
                                      path, make_ob_info, info,
                                      make_ob_pool, pool)) == NULL)
    {
      err = callback_exception_error();
    }
  else
    {
      if (result != Py_None)
        err = callback_bad_return_error("Not None");
      Py_DECREF(result);
    }

  svn_swig_py_release_py_lock();

  return err;
}

svn_error_t *svn_swig_py_client_blame_receiver_func(void *baton,
                                                    apr_int64_t line_no,
                                                    svn_revnum_t revision,
                                                    const char *author,
                                                    const char *date,
                                                    const char *line,
                                                    apr_pool_t *pool)
{
  PyObject *receiver = baton;
  PyObject *result;
  svn_error_t *err = SVN_NO_ERROR;
 
  if ((receiver == NULL) || (receiver == Py_None))
    return SVN_NO_ERROR;

  svn_swig_py_acquire_py_lock();

  if ((result = PyObject_CallFunction(receiver, 
                                      (char *)
                                      (SVN_APR_INT64_T_PYCFMT "lsssO&"), 
                                      line_no, revision, author, date, line, 
                                      make_ob_pool, pool)) == NULL)
    {
      err = callback_exception_error();
    }
  else
    {
      if (result != Py_None)
        err = callback_bad_return_error("Not None");
      Py_DECREF(result);
    }

  svn_swig_py_release_py_lock();
  return err;
}


svn_error_t *
svn_swig_py_auth_simple_prompt_func(svn_auth_cred_simple_t **cred,
                                    void *baton,
                                    const char *realm,
                                    const char *username,
                                    svn_boolean_t may_save,
                                    apr_pool_t *pool)
{
  PyObject *function = baton;
  PyObject *result;
  svn_auth_cred_simple_t *creds = NULL;
  svn_error_t *err = SVN_NO_ERROR;

  if ((function == NULL) || (function == Py_None))
    return SVN_NO_ERROR;

  svn_swig_py_acquire_py_lock();

  if ((result = PyObject_CallFunction(function, 
                                      (char *)"sslO&", 
                                      realm, username, may_save, 
                                      make_ob_pool, pool)) == NULL)
    {
      err = callback_exception_error();
    }
  else
    {
      if (result != Py_None)
        {
          svn_auth_cred_simple_t *tmp_creds = NULL;
          if (svn_swig_ConvertPtrString(result, (void **)&tmp_creds, 
                "svn_auth_cred_simple_t *"))
            {
              err = type_conversion_error("svn_auth_cred_simple_t *");
            }
          else
            {
              creds = apr_pcalloc(pool, sizeof(*creds));
              creds->username = tmp_creds->username ? \
                apr_pstrdup(pool, tmp_creds->username) : NULL;
              creds->password = tmp_creds->password ? \
                apr_pstrdup(pool, tmp_creds->password) : NULL;
              creds->may_save = tmp_creds->may_save;
            }
        }
      Py_DECREF(result);
    }
  svn_swig_py_release_py_lock();
  *cred = creds;
  return err;
}

svn_error_t *
svn_swig_py_auth_username_prompt_func(svn_auth_cred_username_t **cred,
                                      void *baton,
                                      const char *realm,
                                      svn_boolean_t may_save,
                                      apr_pool_t *pool)
{
  PyObject *function = baton;
  PyObject *result;
  svn_auth_cred_username_t *creds = NULL;
  svn_error_t *err = SVN_NO_ERROR;

  if ((function == NULL) || (function == Py_None))
    return SVN_NO_ERROR;

  svn_swig_py_acquire_py_lock();

  if ((result = PyObject_CallFunction(function, 
                                      (char *)"slO&", 
                                      realm, may_save, 
                                      make_ob_pool, pool)) == NULL)
    {
      err = callback_exception_error();
    }
  else
    {
      if (result != Py_None)
        {
          svn_auth_cred_username_t *tmp_creds = NULL;
          if (svn_swig_ConvertPtrString(result, (void **)&tmp_creds, 
                "svn_auth_cred_username_t *"))
            {
              err = type_conversion_error("svn_auth_cred_username_t *");
            }
          else
            {
              creds = apr_pcalloc(pool, sizeof(*creds));
              creds->username = tmp_creds->username ? \
                apr_pstrdup(pool, tmp_creds->username) : NULL;
              creds->may_save = tmp_creds->may_save;
            }
        }
      Py_DECREF(result);
    }
  svn_swig_py_release_py_lock();
  *cred = creds;
  return err;
}


svn_error_t *
svn_swig_py_auth_ssl_server_trust_prompt_func(
    svn_auth_cred_ssl_server_trust_t **cred,
    void *baton,
    const char *realm,
    apr_uint32_t failures,
    const svn_auth_ssl_server_cert_info_t *cert_info,
    svn_boolean_t may_save,
    apr_pool_t *pool)
{
  PyObject *function = baton;
  PyObject *result;
  svn_auth_cred_ssl_server_trust_t *creds = NULL;
  svn_error_t *err = SVN_NO_ERROR;

  if ((function == NULL) || (function == Py_None))
    return SVN_NO_ERROR;

  svn_swig_py_acquire_py_lock();

  if ((result = PyObject_CallFunction(function, (char *)"slO&lO&", 
                  realm, failures, make_ob_auth_ssl_server_cert_info,
                  cert_info, may_save, make_ob_pool, pool)) == NULL)
    {
      err = callback_exception_error();
    }
  else
    {
      if (result != Py_None)
        {
          svn_auth_cred_ssl_server_trust_t *tmp_creds = NULL;
          if (svn_swig_ConvertPtrString
              (result, (void **)&tmp_creds, 
               "svn_auth_cred_ssl_server_trust_t *"))
            {
              err = type_conversion_error
                ("svn_auth_cred_ssl_server_trust_t *");
            }
          else
            {
              creds = apr_pcalloc(pool, sizeof(*creds));
              *creds = *tmp_creds;
            }
        }
      Py_DECREF(result);
    }

  svn_swig_py_release_py_lock();
  *cred = creds;
  return err;
}

svn_error_t *
svn_swig_py_auth_ssl_client_cert_prompt_func(
    svn_auth_cred_ssl_client_cert_t **cred,
    void *baton,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool)
{
  PyObject *function = baton;
  PyObject *result;
  svn_auth_cred_ssl_client_cert_t *creds = NULL;
  svn_error_t *err = SVN_NO_ERROR;

  if ((function == NULL) || (function == Py_None))
    return SVN_NO_ERROR;

  svn_swig_py_acquire_py_lock();

  if ((result = PyObject_CallFunction(function, 
                                      (char *)"slO&", 
                                      realm, may_save, 
                                      make_ob_pool, pool)) == NULL)
    {
      err = callback_exception_error();
    }
  else
    {
      if (result != Py_None)
        {
          svn_auth_cred_ssl_client_cert_t *tmp_creds = NULL;
          if (svn_swig_ConvertPtrString
              (result, (void **)&tmp_creds, 
               "svn_auth_cred_ssl_client_cert_t *"))
            {
              err = type_conversion_error("svn_auth_cred_ssl_client_cert_t *");
            }
          else
            {
              creds = apr_pcalloc(pool, sizeof(*creds));
              creds->cert_file = tmp_creds->cert_file ? \
                apr_pstrdup(pool, tmp_creds->cert_file) : NULL;
              creds->may_save = tmp_creds->may_save;
            }
        }
      Py_DECREF(result);
    }
  svn_swig_py_release_py_lock();
  *cred = creds;
  return err;
}

svn_error_t *
svn_swig_py_auth_ssl_client_cert_pw_prompt_func(
    svn_auth_cred_ssl_client_cert_pw_t **cred,
    void *baton,
    const char *realm,
    svn_boolean_t may_save,
    apr_pool_t *pool)
{
  PyObject *function = baton;
  PyObject *result;
  svn_auth_cred_ssl_client_cert_pw_t *creds = NULL;
  svn_error_t *err = SVN_NO_ERROR;

  if ((function == NULL) || (function == Py_None))
    return SVN_NO_ERROR;

  svn_swig_py_acquire_py_lock();

  if ((result = PyObject_CallFunction(function, 
                                      (char *)"slO&", 
                                      realm, may_save, 
                                      make_ob_pool, pool)) == NULL)
    {
      err = callback_exception_error();
    }
  else
    {
      if (result != Py_None)
        {
          svn_auth_cred_ssl_client_cert_pw_t *tmp_creds = NULL;
          if (svn_swig_ConvertPtrString
              (result, (void **)&tmp_creds, 
               "svn_auth_cred_ssl_client_cert_pw_t *"))
            {
              err = type_conversion_error
                ("svn_auth_cred_ssl_client_cert_pw_t *");
            }
          else
            {
              creds = apr_pcalloc(pool, sizeof(*creds));
              creds->password = tmp_creds->password ? \
                apr_pstrdup(pool, tmp_creds->password) : NULL;
              creds->may_save = tmp_creds->may_save;
            }
        }
      Py_DECREF(result);
    }
  svn_swig_py_release_py_lock();
  *cred = creds;
  return err;
}
