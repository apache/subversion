/*
 * svn_fs.i :  SWIG interface file for svn_fs.h
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

%module _fs
%include typemaps.i

%import apr.i
%import svn_types.i
%import svn_delta.i

/* ----------------------------------------------------------------------- */

/* for the FS, 'int *' will always be an OUTPUT parameter */
%typemap(in) int * = int *OUTPUT;
%typemap(ignore) int * = int *OUTPUT;
%typemap(argout) int * = int *OUTPUT;

/* -----------------------------------------------------------------------
   tweak the argument handling for svn_fs_parse_id
*/

%typemap(ignore) apr_size_t len { }
%typemap(check) apr_size_t len {
    $target = strlen(arg0);
}

/* -----------------------------------------------------------------------
   all uses of "const char **" are returning strings
*/

// ### stupid SWIG drops the 'const' in the arg declaration
%typemap(ignore) const char ** (const char * temp) {
    $target = (char **)&temp;
}

// ### check the result of PyString_FromString() ?
%typemap(python,argout) const char ** {
    $target = t_output_helper($target, PyString_FromString(*$source));
}

/* ----------------------------------------------------------------------- */

%include svn_fs.h
%{
#include "svn_fs.h"
%}
