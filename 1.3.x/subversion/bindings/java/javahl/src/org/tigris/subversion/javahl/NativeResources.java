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
 * Handles activities related to management of native resouces
 * (e.g. loading of native libraries).
 */
class NativeResources
{
    /**
     * Load the required native library whose path is specified by the
     * system property <code>subversion.native.library</code> (which
     * can be passed to the JVM on start-up using an argument like
     * <code>-Dsubversion.native.library=/usr/local/lib/libsvnjavahl-1.so</code>).
     * If the system property is not specified or cannot be loaded,
     * attempt to load the library using its expected name, and the
     * platform-dependent loading mechanism.
     *
     * @throws UnsatisfiedLinkError If the native library cannot be
     * loaded.
     */
    public static synchronized void loadNativeLibrary()
    {
        // If the user specified the fully qualified path to the
        // native library, try loading that first.
        try
        {
            String specifiedLibraryName =
                System.getProperty("subversion.native.library");
            if (specifiedLibraryName != null)
            {
                System.load(specifiedLibraryName);
                SVNClient.initNative();
                return;
            }
        }
        catch (UnsatisfiedLinkError ex)
        {
            // ignore that error to try again
        }

        // Try to load the library by its name.  Failing that, try to
        // load it by its old name.
        try
        {
            System.loadLibrary("svnjavahl-1");
            SVNClient.initNative();
            return;
        }
        catch (UnsatisfiedLinkError ex)
        {
            try
            {
                System.loadLibrary("libsvnjavahl-1");
                SVNClient.initNative();
                return;
            }
            catch (UnsatisfiedLinkError e)
            {
                System.loadLibrary("svnjavahl");
                SVNClient.initNative();
            }
        }
    }
}
