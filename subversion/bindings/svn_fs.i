/*
 * svn_fs.i :  SWIG interface file for svn_fs.h
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

%module fs

%include svn_delta.i
%include svn_error.i
%include svn_types.i

%{
#include "svn_fs.h"
%}

// ### nothing to do right now
