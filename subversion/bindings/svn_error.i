/*
 * svn_error.i :  SWIG interface file for svn_error.h
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

%module error

%import apr.i
%import svn_types.i

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


%include svn_error.h

// ### nothing to do right now
