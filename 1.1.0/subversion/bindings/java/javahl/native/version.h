/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2004 CollabNet.  All rights reserved.
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
#define JNI_VER_MAJOR    0
#define JNI_VER_MINOR    9
#define JNI_VER_MICRO    0

#define JNI_VER_NUM APR_STRINGIFY(JNI_VER_MAJOR) "." \
        APR_STRINGIFY(JNI_VER_MINOR) "." APR_STRINGIFY(JNI_VER_MICRO)

/** Version number with tag (contains no whitespace) */
#define JNI_VER_NUMBER     JNI_VER_NUM

/** Complete version string */
#define JNI_VERSION        JNI_VER_NUM
