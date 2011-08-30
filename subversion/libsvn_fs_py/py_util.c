/* py_util.c : some help for the embedded python interpreter
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

#include "svn_error.h"
#include "svn_pools.h"

#include "py_util.h"

#include "svn_private_config.h"

#define ROOT_MODULE_NAME "svn"
#define FS_MODULE_NAME "svn.fs"

static svn_error_t *
create_py_stack(PyObject *p_exception,
                PyObject *p_traceback)
{
  svn_error_t *err;
  PyObject *p_reason;
  char *reason;

  p_reason = PyObject_Str(p_exception);
  reason = PyString_AsString(p_reason);

  err = svn_error_createf(SVN_ERR_BAD_PYTHON, NULL,
                          _("Exception while executing Python; cause: \"%s\""),
                          reason);

  /* If the exception object has a 'code' attribute, and it's an integer,
     assume it's an apr_err code */
  if (PyObject_HasAttrString(p_exception, "code"))
    {
      PyObject *p_code = PyObject_GetAttrString(p_exception, "code");

      if (PyInt_Check(p_code))
        {
        err->apr_err = (int) PyInt_AS_LONG(p_code);
        }

      Py_DECREF(p_code);
    }

#ifdef SVN_ERR__TRACING
  if (p_traceback)
    {
      PyObject *p_module_name;
      PyObject *p_traceback_mod;
      PyObject *p_stack;
      PyObject *p_frame;
      PyObject *p_filename;
      PyObject *p_lineno;
      Py_ssize_t i;

      /* We don't use load_module() here to avoid an infinite recursion. */
      /* ### This could use more python error checking. */
      p_module_name = PyString_FromString("traceback");
      p_traceback_mod = PyImport_Import(p_module_name);
      Py_DECREF(p_module_name);

      /* ### Cast away const for the Python API. */
      p_stack = PyObject_CallMethod(p_traceback_mod, (char *) "extract_tb",
                                    (char *) "(O)", p_traceback);
      Py_DECREF(p_traceback_mod);

      i = PySequence_Length(p_stack);

      /* Build the "root error" for the chain. */
      p_frame = PySequence_GetItem(p_stack, i-1);
      p_filename = PySequence_GetItem(p_frame, 0);
      p_lineno = PySequence_GetItem(p_frame, 1);
      Py_DECREF(p_frame);

      err->file = apr_pstrdup(err->pool, PyString_AsString(p_filename));
      err->line = PyInt_AsLong(p_lineno);

      Py_DECREF(p_filename);
      Py_DECREF(p_lineno);

      for (i = i-2; i >=0; i--)
        {
          p_frame = PySequence_GetItem(p_stack, i);
          p_filename = PySequence_GetItem(p_frame, 0);
          p_lineno = PySequence_GetItem(p_frame, 1);
          Py_DECREF(p_frame);

          err = svn_error_quick_wrap(err, SVN_ERR__TRACED);
          err->file = apr_pstrdup(err->pool, PyString_AsString(p_filename));
          err->line = PyInt_AsLong(p_lineno);
        
          Py_DECREF(p_filename);
          Py_DECREF(p_lineno);
        }

      Py_DECREF(p_stack);
    }
#endif

  Py_DECREF(p_reason);

  return svn_error_trace(err);
}

typedef void (*py_exc_func_t)(void *baton, va_list argp);

/* Call FUNC with BATON, and upon returning check to see if the Python
   interpreter has a pending exception.  If it does, convert that exception
   to an svn_error_t and return it (or SVN_NO_ERROR if no error), resetting
   the interpreter state and releasing the exception.
   
   Note: This function assumes whatever locking we need for the interpreter
   has already happened and will be released after it is done. */
static svn_error_t *
catch_py_exception(py_exc_func_t func,
                   void *baton,
                   va_list argp)
{
  PyObject *p_type;
  PyObject *p_exception;
  PyObject *p_traceback;
  svn_error_t *err;

  /* Call the handler. */
  func(baton, argp);

  /* Early out if we didn't have any errors. */
  if (!PyErr_Occurred())
    return SVN_NO_ERROR;

  PyErr_Fetch(&p_type, &p_exception, &p_traceback);

  if (p_exception)
    err = create_py_stack(p_exception, p_traceback);
  else
    err = svn_error_create(SVN_ERR_BAD_PYTHON, NULL,
                           _("Python error."));

  PyErr_Clear();

  Py_DECREF(p_type);
  Py_XDECREF(p_exception);
  Py_XDECREF(p_traceback);

  return svn_error_trace(err);
}

static svn_error_t *
load_module(PyObject **p_module_out,
            const char *module_name)
{
  PyObject *p_module_name = NULL;
  PyObject *p_module = NULL;

  p_module_name = PyString_FromString(module_name);
  if (PyErr_Occurred())
    goto load_error;

  p_module = PyImport_Import(p_module_name);
  Py_DECREF(p_module_name);
  if (PyErr_Occurred())
    goto load_error;

  *p_module_out = p_module;

  return SVN_NO_ERROR;

load_error:
  {
    PyObject *p_type, *p_exception, *p_traceback;
    svn_error_t *err;

    /* Release the values above. */
    Py_XDECREF(p_module);
    *p_module_out = NULL;

    PyErr_Fetch(&p_type, &p_exception, &p_traceback);

    if (p_exception && p_traceback)
      err = create_py_stack(p_exception, p_traceback);
    else
      err = svn_error_create(SVN_ERR_BAD_PYTHON, NULL,
                             _("Cannot load Python module"));

    PyErr_Clear();

    Py_DECREF(p_type);
    Py_XDECREF(p_exception);
    Py_XDECREF(p_traceback);

    return err;
  }
}

static void
raise_and_clear_err(svn_error_t *error_chain)
{
  PyObject *args_list = NULL;
  PyObject *args = NULL;
  PyObject *apr_err_ob = NULL;
  PyObject *message_ob = NULL;
  PyObject *file_ob = NULL;
  PyObject *line_ob = NULL;
  PyObject *svn_module = NULL;
  PyObject *exc_class = NULL;
  PyObject *exc_ob = NULL;
  svn_error_t *err;

  if (error_chain == NULL)
    return;

  args_list = PyList_New(0);
  if (PyErr_Occurred())
    goto finished;

  for (err = error_chain; err; err = err->child)
    {
      int i;

      args = PyTuple_New(4);
      if (PyErr_Occurred())
        goto finished;

      /* Convert the fields of the svn_error_t to Python objects. */
      apr_err_ob = PyInt_FromLong(err->apr_err);
      if (PyErr_Occurred())
        goto finished;

      if (err->message)
        {
          message_ob = PyString_FromString(err->message);
          if (PyErr_Occurred())
            goto finished;
        }
      else
        {
          Py_INCREF(Py_None);
          message_ob = Py_None;
        }

      if (err->file)
        {
          file_ob = PyString_FromString(err->file);
          if (PyErr_Occurred())
            goto finished;
        }
      else
        {
          Py_INCREF(Py_None);
          file_ob = Py_None;
        }

      line_ob = PyInt_FromLong(err->line);
      if (PyErr_Occurred())
        goto finished;

      /* Store the objects in the tuple. */
      i = 0;
#define append(item)                                            \
      if (PyTuple_SetItem(args, i++, item) == 0)                \
        /* tuple stole our reference, so don't DECREF */        \
        item = NULL;                                            \
      else                                                      \
        goto finished;
      append(apr_err_ob);
      append(message_ob);
      append(file_ob);
      append(line_ob);
#undef append

      /* Append the tuple to the args list. */
      PyList_Append(args_list, args);
      if (PyErr_Occurred())
        goto finished;

      /* The list takes its own reference, so release ours. */
      Py_DECREF(args);
    }
  args = NULL;
  svn_error_clear(error_chain);

  /* Create the exception object chain. */
  svn_module = PyImport_ImportModule((char *)"svn");
  if (PyErr_Occurred())
    goto finished;

  exc_class = PyObject_GetAttrString(svn_module, (char *)"SubversionException");
  if (PyErr_Occurred())
    goto finished;

  exc_ob = PyObject_CallMethod(exc_class, (char *)"_new_from_err_list",
                               (char *)"(O)", args_list);
  if (PyErr_Occurred())
    goto finished;

  /* Raise the exception. */
  PyErr_SetObject(exc_class, exc_ob);

 finished:
  /* Release any references. */
  Py_XDECREF(args_list);
  Py_XDECREF(args);
  Py_XDECREF(apr_err_ob);
  Py_XDECREF(message_ob);
  Py_XDECREF(file_ob);
  Py_XDECREF(line_ob);
  Py_XDECREF(svn_module);
  Py_XDECREF(exc_class);
  Py_XDECREF(exc_ob);
}

svn_error_t *
svn_fs_py__init_python(apr_pool_t *pool)
{
  if (Py_IsInitialized())
    return SVN_NO_ERROR;

  Py_SetProgramName((char *) "svn");
  Py_InitializeEx(0);

  /* Note: we don't have a matching call to Py_Finalize() because we don't
     know if we initialize python, or if we are in an environment where
     finalizing Python would interact with interpreters which are didn't
     create.  The interpreter state isn't very large (1-2MB), so we essentially
     just leak it. */

  return SVN_NO_ERROR;
}


apr_status_t
svn_fs_py__destroy_py_object(void *data)
{
  PyObject *p_fs = data;
  Py_XDECREF(p_fs);

  return APR_SUCCESS;
}


struct get_string_attr_baton
{
  const char **result;
  PyObject *p_obj;
  const char *name;
  apr_pool_t *result_pool;
};


static void
get_string_attr(void *baton,
                va_list argp)
{
  struct get_string_attr_baton *gsab = baton;
  PyObject *p_attr;
  PyObject *p_str;

  /* ### This needs some exception handling */
  
  p_attr = PyObject_GetAttrString(gsab->p_obj, gsab->name);
  if (PyErr_Occurred())
    return;

  p_str = PyObject_Str(p_attr);
  Py_DECREF(p_attr);
  if (PyErr_Occurred())
    return;

  *gsab->result = PyString_AsString(p_str);

  if (gsab->result)
    *gsab->result = apr_pstrdup(gsab->result_pool, *gsab->result);

  Py_DECREF(p_str);

  return;
}


svn_error_t *
svn_fs_py__get_string_attr(const char **result,
                           PyObject *p_obj,
                           const char *name,
                           apr_pool_t *result_pool)
{
  struct get_string_attr_baton gsab = {
      result, p_obj, name, result_pool
    };
  return svn_error_trace(catch_py_exception(get_string_attr, &gsab, NULL));
}



struct get_int_attr_baton
{
  int *result;
  PyObject *p_obj;
  const char *name;
};


static void
get_int_attr(void *baton,
             va_list argp)
{
  struct get_int_attr_baton *giab = baton;
  PyObject *p_int;

  /* ### This needs some exception handling */
  
  p_int = PyObject_GetAttrString(giab->p_obj, giab->name);
  if (PyErr_Occurred())
    return;

  *giab->result = (int) PyInt_AsLong(p_int);
  Py_DECREF(p_int);

  return;
}


svn_error_t *
svn_fs_py__get_int_attr(int *result,
                        PyObject *p_obj,
                        const char *name)
{
  struct get_int_attr_baton giab = { result, p_obj, name };
  return svn_error_trace(catch_py_exception(get_int_attr, &giab, NULL));
}


struct set_int_attr_baton
{
  PyObject *p_obj;
  const char *name;
  long int val;
};


static void
set_int_attr(void *baton,
             va_list argp)
{
  struct set_int_attr_baton *siab = baton;
  PyObject *p_int;

  p_int = PyInt_FromLong(siab->val);
  if (PyErr_Occurred())
    return;

  PyObject_SetAttrString(siab->p_obj, siab->name, p_int);
  Py_DECREF(p_int);

  return;
}


svn_error_t *
svn_fs_py__set_int_attr(PyObject *p_obj,
                        const char *name,
                        long int val)
{
  struct set_int_attr_baton siab = { p_obj, name, val };
  return svn_error_trace(catch_py_exception(set_int_attr, &siab, NULL));
}


struct call_method_baton
{
  PyObject **p_result;
  PyObject *p_obj;
  const char *name;
  const char *format;
};


static void
call_method(void *baton, va_list argp)
{
  struct call_method_baton *cmb = baton;
  PyObject *p_args = NULL;
  PyObject *p_func = NULL;
  PyObject *p_value = NULL;

  p_args = Py_VaBuildValue(cmb->format, argp);
  if (PyErr_Occurred())
    goto cm_free_objs;

  p_func = PyObject_GetAttrString(cmb->p_obj, cmb->name);
  if (PyErr_Occurred())
    goto cm_free_objs;

  p_value = PyObject_CallObject(p_func, p_args);
  Py_DECREF(p_args);
  p_args = NULL;
  Py_DECREF(p_func);
  p_func = NULL;
  if (PyErr_Occurred())
    goto cm_free_objs;

  if (cmb->p_result)
    *cmb->p_result = p_value;
  else
    Py_DECREF(p_value);

  return;

cm_free_objs:
  /* Error handler, decrefs all python objects we may have. */
  Py_XDECREF(p_args);
  Py_XDECREF(p_func);
  Py_XDECREF(p_value);
}


svn_error_t*
svn_fs_py__call_method(PyObject **p_result,
                       PyObject *p_obj,
                       const char *name,
                       const char *format,
                       ...)
{
  svn_error_t *err;
  va_list argp;
  struct call_method_baton cmb = {
      p_result, p_obj, name, format
    };

  SVN_ERR_ASSERT(p_obj != NULL);

  va_start(argp, format);
  err = catch_py_exception(call_method, &cmb, argp);
  va_end(argp);

  return svn_error_trace(err);
}


static PyObject *
convert_hash(apr_hash_t *hash,
             PyObject *(*converter_func)(void *value))
{
  apr_hash_index_t *hi;
  PyObject *dict;

  if (hash == NULL)
    Py_RETURN_NONE;

  if ((dict = PyDict_New()) == NULL)
    return NULL;

  for (hi = apr_hash_first(NULL, hash); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      PyObject *value;

      apr_hash_this(hi, &key, NULL, &val);
      value = (*converter_func)(val);
      if (value == NULL)
        {
          Py_DECREF(dict);
          return NULL;
        }
      /* ### gotta cast this thing cuz Python doesn't use "const" */
      if (PyDict_SetItemString(dict, (char *)key, value) == -1)
        {
          Py_DECREF(value);
          Py_DECREF(dict);
          return NULL;
        }
      Py_DECREF(value);
    }

  return dict;
}

static PyObject *
convert_svn_string_t(void *value)
{
  const svn_string_t *s = value;

  /* ### gotta cast this thing cuz Python doesn't use "const" */
  return PyString_FromStringAndSize((void *)s->data, s->len);
}

PyObject *
svn_fs_py__convert_cstring_hash(void *object)
{
  /* Cast PyString_FromString to silence a compiler warning. */
  return convert_hash(object, (PyObject *(*)(void *value)) PyString_FromString);
}

PyObject *
svn_fs_py__convert_proplist(void *object)
{
  return convert_hash(object, convert_svn_string_t);
}

svn_error_t *
svn_fs_py__load_module(fs_fs_data_t *ffd)
{
  return svn_error_trace(load_module(&ffd->p_module, FS_MODULE_NAME));
}

/* Conversion from Python single objects (not hashes/lists/etc.) to
   Subversion types. */
static const char *
make_string_from_ob(PyObject *ob, apr_pool_t *pool)
{
  if (ob == Py_None)
    return NULL;
  if (! PyString_Check(ob))
    {
      PyErr_SetString(PyExc_TypeError, "not a string");
      return NULL;
    }
  return apr_pstrdup(pool, PyString_AS_STRING(ob));
}

static svn_string_t *
make_svn_string_from_ob(PyObject *ob, apr_pool_t *pool)
{
  if (ob == Py_None)
    return NULL;
  if (! PyString_Check(ob))
    {
      PyErr_SetString(PyExc_TypeError, "not a string");
      return NULL;
    }
  return svn_string_create(PyString_AS_STRING(ob), pool);
}

/* ### We may need to wrap this in something to catch any Python errors which
 * ### are generated. */
apr_hash_t *
svn_fs_py__prophash_from_dict(PyObject *dict, apr_pool_t *pool)
{
  apr_hash_t *hash;
  PyObject *keys;
  long num_keys;
  long i;

  if (dict == Py_None)
    return NULL;

  if (!PyDict_Check(dict))
    {
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


/**********************
 * Wrapping C callback functions
 */
static PyObject *
notify_func_wrapper(PyObject *p_tuple, PyObject *args)
{
  PyObject *c_func;
  PyObject *baton;
  apr_int64_t shard;
  svn_fs_pack_notify_action_t action;
  svn_fs_pack_notify_t notify_func;
  void *notify_baton;
  svn_error_t *err;
  apr_pool_t *pool;

  PyArg_ParseTuple(args, (char *)"li", &shard, &action);
  if (PyErr_Occurred())
    return NULL;

  c_func = PySequence_GetItem(p_tuple, 0);
  notify_func = PyCObject_AsVoidPtr(c_func);
  Py_DECREF(c_func);

  baton = PySequence_GetItem(p_tuple, 1);
  if (PyErr_Occurred())
    return NULL;

  if (baton == Py_None)
    notify_baton = NULL;
  else
    notify_baton = PyCObject_AsVoidPtr(baton);
  Py_DECREF(baton);

  pool = svn_pool_create(NULL);
  err = notify_func(notify_baton, shard, action, pool);
  svn_pool_destroy(pool);
  if (err)
    {
      raise_and_clear_err(err);
      return NULL;
    }

  Py_RETURN_NONE;
}

PyObject *
svn_fs_py__wrap_pack_notify_func(svn_fs_pack_notify_t notify_func,
                                 void *notify_baton)
{
  static PyMethodDef method_def = { "notify", notify_func_wrapper,
                                    METH_VARARGS, NULL };
  PyObject *func;
  PyObject *c_func;
  PyObject *baton;
  PyObject *p_tuple;

  if (!notify_func)
    Py_RETURN_NONE;

  c_func = PyCObject_FromVoidPtr(notify_func, NULL);

  if (notify_baton)
    {
      baton = PyCObject_FromVoidPtr(notify_baton, NULL);
    }
  else
    {
      baton = Py_None;
      Py_INCREF(baton);
    }

  p_tuple = Py_BuildValue("(NN)", c_func, baton);
  func = PyCFunction_New(&method_def, p_tuple);
  Py_DECREF(p_tuple);

  return func;
}

static PyObject *
cancel_func_wrapper(PyObject *p_tuple, PyObject *args)
{
  PyObject *c_func;
  PyObject *baton;
  void *cancel_baton;
  svn_cancel_func_t cancel_func;
  svn_error_t *err;

  c_func = PySequence_GetItem(p_tuple, 0);
  cancel_func = PyCObject_AsVoidPtr(c_func);
  Py_DECREF(c_func);

  baton = PySequence_GetItem(p_tuple, 1);
  if (PyErr_Occurred())
    return NULL;

  if (baton == Py_None)
    cancel_baton = NULL;
  else
    cancel_baton = PyCObject_AsVoidPtr(baton);
  Py_DECREF(baton);

  err = cancel_func(cancel_baton);
  if (err)
    {
      raise_and_clear_err(err);
      return NULL;
    }

  Py_RETURN_NONE;
}

PyObject *
svn_fs_py__wrap_cancel_func(svn_cancel_func_t cancel_func,
                            void *cancel_baton)
{
  static PyMethodDef method_def = { "cancel", cancel_func_wrapper,
                                    METH_NOARGS, NULL };
  PyObject *func;
  PyObject *c_func;
  PyObject *baton;
  PyObject *p_tuple;

  if (!cancel_func)
    Py_RETURN_NONE;

  c_func = PyCObject_FromVoidPtr(cancel_func, NULL);

  if (cancel_baton)
    {
      baton = PyCObject_FromVoidPtr(cancel_baton, NULL);
    }
  else
    {
      baton = Py_None;
      Py_INCREF(baton);
    }

  p_tuple = Py_BuildValue("(NN)", c_func, baton);
  func = PyCFunction_New(&method_def, p_tuple);
  Py_DECREF(p_tuple);

  return func;
}
