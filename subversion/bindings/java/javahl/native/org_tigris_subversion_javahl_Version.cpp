/**
 * @copyright
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
 * @endcopyright
 *
 * @file org_tigris_subversion_javahl_Version.cpp
 * @brief Implementation of the native methods in the java class Version.
 */
#include "../include/org_tigris_subversion_javahl_Version.h"
#include "JNIStackElement.h"
#include "svn_version.h"

/*
 * Class:     org_tigris_subversion_javahl_Version
 * Method:    getMajor
 * Signature: (I)V
 */
JNIEXPORT jint JNICALL Java_org_tigris_subversion_javahl_Version_getMajor
  (JNIEnv* env, jobject jthis)
{
    JNIEntry(Version, getMajor);
    return SVN_VER_MAJOR;
}

/*
 * Class:     org_tigris_subversion_javahl_Version
 * Method:    getMinor
 * Signature: (I)V
 */
JNIEXPORT jint JNICALL Java_org_tigris_subversion_javahl_Version_getMinor
  (JNIEnv* env, jobject jthis)
{
    JNIEntry(Version, getMinor);
    return SVN_VER_MINOR;
}

/*
 * Class:     org_tigris_subversion_javahl_Version
 * Method:    getPatch
 * Signature: (I)V
 */
JNIEXPORT jint JNICALL Java_org_tigris_subversion_javahl_Version_getPatch
  (JNIEnv* env, jobject jthis)
{
    JNIEntry(Version, getPatch);
    return SVN_VER_PATCH;
}
