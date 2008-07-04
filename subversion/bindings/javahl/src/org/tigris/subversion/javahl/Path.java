/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2006, 2008 CollabNet.  All rights reserved.
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

package org.tigris.subversion.javahl;

/**
 * Subversion path validation and manipulation.
 *
 * @since 1.4.0
 */
public class Path
{
    /**
     * Load the required native library.
     */
    static
    {
        NativeResources.loadNativeLibrary();
    }

    /**
     * A valid path is a UTF-8 string without any control characters.
     *
     * @return Whether Subversion can store the path in a repository.
     */
    public static native boolean isValid(String path);

    /**
     * Whether a URL is valid. Implementation may behave differently
     * than <code>svn_path_is_url()</code>.
     *
     * @param path The Subversion "path" to inspect.
     * @return Whether <code>path</code> is a URL.
     * @throws IllegalArgumentException If <code>path</code> is
     * <code>null</code>.
     */
    public static boolean isURL(String path)
    {
        if (path == null)
        {
            throw new IllegalArgumentException();
        }
        // Require at least "s://".
        return (path.indexOf("://") > 0);
    }
}
