/*
 * import.c:  import a local file or tree using an svn_delta_edit_fns_t.
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

/* ==================================================================== */


#include <assert.h>
#include <apr.h>
#include <apr_errno.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_hash.h>
#include "svn_types.h"
#include "svn_wc.h"
#include "svn_io.h"
#include "svn_delta.h"
#include "svn_path.h"



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
