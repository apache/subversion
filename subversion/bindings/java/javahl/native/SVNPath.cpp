/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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
 * @endcopyright
 */

#include <jni.h>
#include "SVNPath.h"
#include "Pool.h"
#include "svn_path.h"

jboolean SVNPath::isValid(const char *path)
{
    if (path == NULL)
    {
        return JNI_FALSE;
    }

    Pool requestPool;
    svn_error_t *err = svn_path_check_valid(path, requestPool.pool());
    if (err == SVN_NO_ERROR)
    {
        return JNI_TRUE;
    }
    else
    {
        svn_error_clear(err);
        return JNI_FALSE;
    }
}
