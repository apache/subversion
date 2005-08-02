/*
 * svn_old_swig.h :  Functions defined for compatibility with old versions
 *                   of SWIG
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

#ifndef SVN_OLD_SWIG_H
#define SVN_OLD_SWIG_H

#include <Python.h>

#if SVN_SWIG_VERSION < 103024

/* If this file is being included outside of a wrapper file, then need to
   create stubs for some of the SWIG types. */

/* if SWIGEXPORT is defined, then we're in a wrapper. otherwise, we need
   the prototypes and type definitions. */
#ifndef SWIGEXPORT
#define SVN_NEED_SWIG_TYPES
#endif

#ifdef SVN_NEED_SWIG_TYPES

#if SVN_SWIG_VERSION >= 103020
#include "python/precommon.swg"
#ifndef SWIG_ConvertPtr
#define SWIG_ConvertPtr SWIG_Python_ConvertPtr
#endif
#ifndef SWIG_NewPointerObj
#define SWIG_NewPointerObj SWIG_Python_NewPointerObj
#endif
#endif /* SVN_SWIG_VERSION >= 103020 */

typedef void *(*swig_converter_func)(void *);
typedef struct swig_type_info *(*swig_dycast_func)(void **);
typedef struct swig_type_info {
  const char             *name;
  swig_converter_func     converter;
  const char             *str;
  void                   *clientdata;
  swig_dycast_func        dcast;
  struct swig_type_info  *next;
  struct swig_type_info  *prev;
} swig_type_info;
#endif /* SVN_NEED_SWIG_TYPES */

int SWIG_ConvertPtr(PyObject *, void **, swig_type_info *, int flags);
PyObject *SWIG_NewPointerObj(void *, swig_type_info *, int own);
swig_type_info *SWIG_TypeQuery(const char *name);

/*
  Return the pretty name associated with this type,
  that is an unmangled type name in a form presentable to the user.
*/
static const char *
SWIG_TypePrettyName(const swig_type_info *type) {
  /* The "str" field contains the equivalent pretty names of the
     type, separated by vertical-bar characters.  We choose
     to print the last name, as it is often (?) the most
     specific. */
  if (type->str != NULL) {
    const char *last_name = type->str;
    const char *s;
    for (s = type->str; *s; s++)
      if (*s == '|') last_name = s+1;
    return last_name;
  }
  else
    return type->name;
}

/* Throw a Python exception to complain about a TypeError */
static void
SWIG_Python_TypeError(const char *type, PyObject *obj)
{
  if (type) {
#if defined(SWIG_COBJECT_TYPES)
    if (obj && PySwigObject_Check(obj)) {
      const char *otype = (const char *) PySwigObject_GetDesc(obj);
      if (otype) {
        PyErr_Format(PyExc_TypeError,
          "a '%s' is expected, 'PySwigObject(%s)' is received", type, otype);
        return;
      }
    } else
#endif
    {
      const char *otype = (obj ? obj->ob_type->tp_name : 0);
      if (otype) {
        PyObject *str = PyObject_Str(obj);
        const char *cstr = str ? PyString_AsString(str) : 0;
        if (cstr) {
          PyErr_Format(PyExc_TypeError,
            "a '%s' is expected, '%s(%s)' is received", type, otype, cstr);
        } else {
          PyErr_Format(PyExc_TypeError, "a '%s' is expected, '%s' is received",
                       type, otype);
        }
        Py_XDECREF(str);
        return;
      }
    }
    PyErr_Format(PyExc_TypeError, "a '%s' is expected", type);
  } else {
    PyErr_Format(PyExc_TypeError, "unexpected type is received");
  }
}

/* Throw a Python exception to complain about a null reference */
static void
SWIG_Python_NullRef(const char *type)
{
  if (type) {
    PyErr_Format(PyExc_TypeError,
      "null reference of type '%s' was received",type);
  } else {
    PyErr_Format(PyExc_TypeError, "null reference was received");
  }
}

/* Add additional information to an existing error message */
static int
SWIG_Python_AddErrMesg(const char* mesg, int infront)
{
  if (PyErr_Occurred()) {
    PyObject *type = 0;
    PyObject *value = 0;
    PyObject *traceback = 0;
    PyErr_Fetch(&type, &value, &traceback);
    if (value) {
      PyObject *old_str = PyObject_Str(value);
      Py_XINCREF(type);
      PyErr_Clear();
      if (infront) {
        PyErr_Format(type, "%s %s", mesg, PyString_AsString(old_str));
      } else {
        PyErr_Format(type, "%s %s", PyString_AsString(old_str), mesg);
      }
      Py_DECREF(old_str);
    }
    return 1;
  } else {
    return 0;
  }
}

/* If an error has occurred, add the specified argument number to it.
 * Otherwise, return 0. */
static int
SWIG_Python_ArgFail(int argnum)
{
  if (PyErr_Occurred()) {
    /* add information about failing argument */
    char mesg[256];
    PyOS_snprintf(mesg, sizeof(mesg), "argument number %d:", argnum);
    return SWIG_Python_AddErrMesg(mesg, 1);
  } else {
    return 0;
  }
}

#define SWIG_POINTER_EXCEPTION 0x1

/* Extract the underlying C struct from a SWIG/Python object.
 * If a conversion error returns, report an error with the specified
 * argument number */
static void*
SWIG_Python_MustGetPtr_Wrapper(PyObject *obj, swig_type_info *ty, int argnum,
  int flags)
{
  void *result;
  if (SWIG_ConvertPtr(obj, &result, ty, flags) == -1) {
    PyErr_Clear();
    if (flags & SWIG_POINTER_EXCEPTION) {
      SWIG_Python_TypeError(SWIG_TypePrettyName(ty), obj);
      SWIG_Python_ArgFail(argnum);
    }
  }
  return result;
}
#ifdef SWIG_MustGetPtr
#undef SWIG_MustGetPtr
#endif
#define SWIG_MustGetPtr SWIG_Python_MustGetPtr_Wrapper
#define SWIG_arg_fail(arg) SWIG_Python_ArgFail(arg)

#endif /* SVN_SWIG_VERSION < 103024 */
#endif /* SVN_OLD_SWIG_H */
