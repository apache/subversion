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
/* Pointers, references, and arrays */
%typemap (in) SWIGTYPE *, SWIGTYPE []
  "svn_swig_ConvertPtr($input, (void**)&$1, $descriptor);
   if (SWIG_arg_fail($argnum)) SWIG_fail;";

/* Output arguments */
%typemap (out) SWIGTYPE *, SWIGTYPE &
    "$result = svn_swig_NewPointerObj((void*)($1), $descriptor);";
#endif
