/*
 * svn_types.i :  SWIG interface file for svn_types.h
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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

%module _types

%import apr.i

// -----------------------------------------------------------------------

%typemap(python,except) svn_error_t * {
    $function
    if ($source != NULL) {
        PyExc_SetString(PyExc_RuntimeError,
                        $source->message ? $source->message : "unknown error");
        return NULL;
    }
}
%typemap(python,out) svn_error_t * {
    /* we checked for non-NULL with the 'except' typemap, so "result" will
       always be NULL at this point. */
    Py_INCREF(Py_None);
    $target = Py_None;
}

// -----------------------------------------------------------------------

/* 'svn_renum_t *' will always be an OUTPUT parameter */
%typemap(in) svn_renum_t * = long *OUTPUT;
%typemap(ignore) svn_revnum_t * = long *OUTPUT;
%typemap(argout) svn_revnum_t * = long *OUTPUT;

// -----------------------------------------------------------------------

%include svn_types.h
