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

#include "py_util.h"

#include "svn_private_config.h"

#define ROOT_MODULE_NAME "svn"
#define FS_MODULE_NAME "svn.fs"

static PyObject *p_root_module;

static svn_error_t *
create_py_stack(PyObject *p_exception,
                PyObject *p_traceback)
{
  svn_error_t *err;
  PyObject *p_reason;
  char *reason;

  p_reason = PyObject_Str(p_exception);
  reason = PyString_AsString(p_reason);

#ifdef SVN_ERR__TRACING
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

    p_stack = PyObject_CallMethod(p_traceback_mod, "extract_tb",
                                  "(O)", p_traceback);
    Py_DECREF(p_traceback_mod);

    i = PySequence_Length(p_stack);

    /* Build the "root error" for the chain. */
    p_frame = PySequence_GetItem(p_stack, i-1);
    p_filename = PySequence_GetItem(p_frame, 0);
    p_lineno = PySequence_GetItem(p_frame, 1);
    Py_DECREF(p_frame);

    err = svn_error_createf(SVN_ERR_BAD_PYTHON, NULL,
                          _("Exception while executing Python; cause: '%s'"),
                          reason);

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
#else
  err = svn_error_createf(SVN_ERR_BAD_PYTHON, NULL,
                          _("Exception while executing Python; cause: '%s'"),
                          reason);
#endif

  /* If the exception object has a 'code' attribute, and it's an integer,
     assume it's an apr_err code */
  if (PyObject_HasAttrString(p_exception, "code"))
    {
      PyObject *p_code = PyObject_GetAttrString(p_exception, "code");

      if (PyInt_Check(p_code))
        err->apr_err = PyInt_AS_LONG(p_code);

      Py_DECREF(p_code);
    }

  Py_DECREF(p_reason);

  return err;
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

static apr_status_t
finalize_python(void *data)
{
  Py_XDECREF(p_root_module);
  p_root_module = NULL;

  /* Cleanup the python interpreter. */
  Py_Finalize();

  return APR_SUCCESS;
}

svn_error_t *
svn_fs_py__init_python(apr_pool_t *pool)
{
  if (Py_IsInitialized())
    return SVN_NO_ERROR;

  Py_SetProgramName((char *) "svn");
  Py_InitializeEx(0);
  apr_pool_cleanup_register(pool, NULL, finalize_python,
                            apr_pool_cleanup_null);

  SVN_ERR(load_module(&p_root_module, ROOT_MODULE_NAME));
  
  if (PyErr_Occurred())
    {
      PyErr_Clear();
      return svn_error_create(SVN_ERR_FS_GENERAL, NULL,
                              _("Cannot load Python module"));
    }

  return SVN_NO_ERROR;
}


apr_status_t
svn_fs_py__destroy_py_object(void *data)
{
  PyObject *p_fs = data;
  Py_XDECREF(p_fs);

  return APR_SUCCESS;
}


svn_error_t*
svn_fs_py__call_method(PyObject **p_result,
                       PyObject *p_obj,
                       const char *name,
                       const char *format,
                       ...)
{
  PyObject *p_func = NULL;
  PyObject *p_value = NULL;
  PyObject *p_args = NULL;
  va_list argp;

  SVN_ERR_ASSERT(p_obj != NULL);

  va_start(argp, format);

  p_args = Py_VaBuildValue(format, argp);
  if (PyErr_Occurred())
    goto create_error;

  p_func = PyObject_GetAttrString(p_obj, name);
  if (PyErr_Occurred())
    goto create_error;
    
  /* ### Need a callable check here? */

  p_value = PyObject_CallObject(p_func, p_args);
  Py_DECREF(p_args);
  p_args = NULL;
  if (PyErr_Occurred())
    goto create_error;

  if (p_result)
    *p_result = p_value;
  else
    Py_DECREF(p_value);

  Py_DECREF(p_func);

  va_end(argp);
  return SVN_NO_ERROR;

create_error:
  {
    PyObject *p_type = NULL;
    PyObject *p_exception = NULL;
    PyObject *p_traceback = NULL;
    svn_error_t *err;

    va_end(argp);
    Py_XDECREF(p_args);
    Py_XDECREF(p_func);

    PyErr_Fetch(&p_type, &p_exception, &p_traceback);

    if (p_exception && p_traceback)
      err = create_py_stack(p_exception, p_traceback);
    else
      err = svn_error_create(SVN_ERR_BAD_PYTHON, NULL,
                             _("Error calling method"));

    PyErr_Clear();

    Py_DECREF(p_type);
    Py_XDECREF(p_exception);
    Py_XDECREF(p_traceback);

    return err;
  }
}

PyObject *
svn_fs_py__convert_hash(void *object)
{
  apr_hash_t *hash = object;
  apr_hash_index_t *hi;
  PyObject *p_dict;

  if (hash == NULL)
    Py_RETURN_NONE;

  if ((p_dict = PyDict_New()) == NULL)
    return NULL;

  for (hi = apr_hash_first(NULL, hash); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      PyObject *value;

      apr_hash_this(hi, &key, NULL, &val);
      value = PyString_FromString(val);
      if (value == NULL)
        {
          Py_DECREF(p_dict);
          return NULL;
        }
      /* ### gotta cast this thing cuz Python doesn't use "const" */
      if (PyDict_SetItemString(p_dict, (char *)key, value) == -1)
        {
          Py_DECREF(value);
          Py_DECREF(p_dict);
          return NULL;
        }
      Py_DECREF(value);
    }

  return p_dict;
}

svn_error_t *
svn_fs_py__load_module(fs_fs_data_t *ffd)
{
  return svn_error_trace(load_module(&ffd->p_module, FS_MODULE_NAME));
}
