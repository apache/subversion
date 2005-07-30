/*
 * svn_global.swg :  Global SWIG definitions for Subversion
 *
 * ====================================================================
 * Copyright (c) 2005 CollabNet.  All rights reserved.
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

#ifdef SWIGPYTHON
%feature("autodoc",1);

/* Pointers, references, and arrays */
%typemap (arginit) SWIGTYPE * (PyObject *_global_svn_swig_py_pool, \
      apr_pool_t *_global_pool)
{
  svn_swig_get_application_pool(&_global_pool, &_global_svn_swig_py_pool);
}

%typemap (in) SWIGTYPE *, SWIGTYPE []
  "$1 = ($1_ltype)svn_swig_MustGetPtr($input, $descriptor, $argnum,
     _global_svn_swig_py_is_local_pool ? NULL : &_global_svn_swig_py_pool);
   if (PyErr_Occurred()) {
     return NULL;
   }
  "

/* Output arguments */
%typemap (out) SWIGTYPE *, SWIGTYPE &
    "$result = svn_swig_NewPointerObj((void*)($1), $descriptor, 
                                      _global_svn_swig_py_pool);";
#endif
