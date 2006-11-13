package org.tigris.subversion.javahl;

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
 */

/**
 * Encapsulates version information about the underlying native
 * libraries.  Basically a wrapper for <a
 * href="http://svn.collab.net/repos/svn/trunk/subversion/include/svn_version.h"><code>svn_version.h</code></a>.
 */
public class Version
{
    /**
     * @return The full version string for the loaded JavaHL library,
     * as defined by <code>MAJOR.MINOR.PATCH INFO</code>.
     * @since 1.4.0
     */
    public String toString()
    {
        StringBuffer version = new StringBuffer();
        version.append(getMajor())
            .append('.').append(getMinor())
            .append('.').append(getPatch())
            .append(getNumberTag())
            .append(getTag());
        return version.toString();
    }

    /**
     * @return The major version number for the loaded JavaHL library.
     * @since 1.4.0
     */
    public native int getMajor();

    /**
     * @return The minor version number for the loaded JavaHL library.
     * @since 1.4.0
     */
    public native int getMinor();

    /**
     * @return The patch-level version number for the loaded JavaHL
     * library.
     * @since 1.4.0
     */
    public native int getPatch();

    /**
     * @return Some text further describing the library version
     * (e.g. <code>" (r1234)"</code>, <code>" (Alpha 1)"</code>,
     * <code>" (dev build)"</code>, etc.).
     * @since 1.4.0
     */
    private native String getTag();

    /**
     * @return Some text further describing the library version
     * (e.g. "r1234", "Alpha 1", "dev build", etc.).
     * @since 1.4.0
     */
    private native String getNumberTag();
}
