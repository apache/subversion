/*
 * svn_wc.i :  SWIG interface file for svn_wc.h
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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

%module _wc
%include typemaps.i

%import apr.i
%import svn_types.i
%import svn_string.i
%import svn_delta.i

/* -----------------------------------------------------------------------
   ### these functions require a pool, which we don't have immediately
   ### handy. just eliminate these funcs for now.
*/
%ignore svn_wc_set_auth_file;

/* ### ignore this structure because the accessors will need a pool */
%ignore svn_wc_keywords_t;

/* -----------------------------------------------------------------------
   these types (as 'type **') will always be an OUT param
*/
%apply SWIGTYPE **OUTPARAM {
    svn_wc_entry_t **,
    svn_wc_adm_access_t **
};

/* ----------------------------------------------------------------------- */

%include svn_wc.h
%{
#include "svn_wc.h"
%}
