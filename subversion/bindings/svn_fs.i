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

%module fs
%include typemaps.i

%import svn_types.i
%import svn_error.i
%import svn_delta.i

// -----------------------------------------------------------------------
// tweak the argument handling for svn_fs_file_length()

// ### this only marks it as in/out. the "ignore" does the work.
//%apply apr_off_t *OUTPUT { apr_off_t *length_p };

%typemap(ignore) apr_off_t *length_p (apr_off_t temp) {
    $target = &temp;
}

// ### check the result of PyLong_FromLong() ?
%typemap(python,argout) apr_off_t *length_p {
    $target = t_output_helper($target, PyLong_FromLong(*$source));
}

// -----------------------------------------------------------------------
// all "svn_revnum_t *" arguments are considered OUT arguments

// ### this only marks it as in/out. the "ignore" does the work.
//%apply svn_revnum_t *OUTPUT { svn_revnum_t * };

%typemap(ignore) svn_revnum_t * (svn_revnum_t temp) {
    $target = &temp;
}

// ### check the result of PyLong_FromLong() ?
%typemap(python,argout) svn_revnum_t * {
    $target = t_output_helper($target, PyLong_FromLong(*$source));
}

// -----------------------------------------------------------------------
// tweak the argument handling for svn_fs_is_different

%apply int *OUTPUT { int *is_different, int *is_file, int *is_dir };

// -----------------------------------------------------------------------
// tweak the argument handling for svn_fs_parse_id

%typemap(ignore) apr_size_t len { }
%typemap(python, check) apr_size_t len {
    $target = strlen(arg0);
}

// -----------------------------------------------------------------------
// all uses of "const char **" are returning strings

// ### dang. doesn't work
//const char **OUTPUT;

%typemap(ignore) const char ** (const char * temp) {
    $target = &temp;
}

// ### check the result of PyString_FromString() ?
%typemap(python,argout) const char ** {
    $target = t_output_helper($target, PyString_FromString(*$source));
}

// -----------------------------------------------------------------------

%include svn_fs.h


// ### nothing to do right now
