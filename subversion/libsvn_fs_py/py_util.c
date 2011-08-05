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

static PyObject *p_exception_type;
static PyObject *p_root_module;

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
    PyObject *p_reason;
    const char *reason;
    svn_error_t *err;

    /* Release the values above. */
    Py_XDECREF(p_module);
    *p_module_out = NULL;

    PyErr_Fetch(&p_type, &p_exception, &p_traceback);

    if (p_exception)
      {
        p_reason = PyObject_Str(p_exception);
        reason = PyString_AsString(p_reason);

        err = svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                                _("Cannot load Python module, cause: %s"),
                                reason);
      }
    else
      err = svn_error_create(SVN_ERR_FS_GENERAL, NULL, NULL);

    Py_XDECREF(p_exception);
    Py_XDECREF(p_type);
    Py_XDECREF(p_traceback);

    Py_XDECREF(p_reason);
    return err;
  }
}

static apr_status_t
finalize_python(void *data)
{
  Py_XDECREF(p_exception_type);
  p_exception_type = NULL;
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
  
  p_exception_type = PyObject_GetAttrString(p_root_module,
                                            "SubversionException");
  if (PyErr_Occurred())
    {
      PyErr_Clear();
      return svn_error_create(SVN_ERR_FS_GENERAL, NULL,
                              _("Cannot load Python module"));
    }


  return SVN_NO_ERROR;
}
