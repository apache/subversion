/*
 * apr.i :  SWIG interface file for selected APR types
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

/* This is the interface for the APR headers. This is not built as a module
   because we aren't going to wrap the APR functions. Thus, we only define
   the various types in here, as necessary. */
/* ### actually, we may need to wrap some things, such as apr_initialize() */

%include "typemaps.i"

/* ----------------------------------------------------------------------- */

/* 'apr_off_t *' will always be an OUTPUT parameter */
%typemap(in) apr_off_t * = long *OUTPUT;
%typemap(ignore) apr_off_t * = long *OUTPUT;
%typemap(argout) apr_off_t * = long *OUTPUT;

/* ----------------------------------------------------------------------- */

%include apr.h

/* ### be nice to have all the error values and macros. there are some
   ### problems including this file, tho. SWIG isn't smart enough with some
   ##3 of the preprocessing and thinks there is a macro redefinition */
//%include apr_errno.h
typedef int apr_status_t;

/* ### seems that SWIG isn't picking up the definition of size_t */
typedef long size_t;
