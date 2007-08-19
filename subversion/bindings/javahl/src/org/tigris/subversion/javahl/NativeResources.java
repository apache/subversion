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

package org.tigris.subversion.javahl;

/**
 * Handles activities related to management of native resouces
 * (e.g. loading of native libraries).
 */
class NativeResources
{
    /**
     * Version information about the underlying native libraries.
     */
    static Version version;

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
     * @throws LinkageError If the version of the loaded native
     * library is not compatible with this version of JavaHL's Java
     * APIs.
     * @since 1.3.0
     */
    public static synchronized void loadNativeLibrary()
    {
        UnsatisfiedLinkError loadException = null;

        // If the user specified the fully qualified path to the
        // native library, try loading that first.
        try
        {
            String specifiedLibraryName =
                System.getProperty("subversion.native.library");
            if (specifiedLibraryName != null)
            {
                System.load(specifiedLibraryName);
                init();
                return;
            }
        }
        catch (UnsatisfiedLinkError ex)
        {
            // Since the user explicitly specified this path, this is
            // the best error to return if no other method succeeds.
            loadException = ex;
        }

        // Try to load the library by its name.  Failing that, try to
        // load it by its old name.
        String[] libraryNames = {"svnjavahl-1", "libsvnjavahl-1", "svnjavahl"};
        for (int i = 0; i < libraryNames.length; i++)
        {
            try
            {
                System.loadLibrary(libraryNames[i]);
                init();
                return;
            }
            catch (UnsatisfiedLinkError ex)
            {
                if (loadException == null)
                {
                    loadException = ex;
                }
            }
        }

        // Re-throw the most relevant exception.
        if (loadException == null)
        {
            // This could only happen as the result of a programming error.
            loadException = new UnsatisfiedLinkError("Unable to load JavaHL " +
                                                     "native library");
        }
        throw loadException;
    }

    /**
     * Initializer for native resources to be invoked <em>after</em>
     * the native library has been loaded.  Sets library version
     * information, and initializes the re-entrance hack for native
     * code.
     * @throws LinkageError If the version of the loaded native
     * library is not compatible with this version of JavaHL's Java
     * APIs.
     */
    private static final void init()
    {
        initNativeLibrary();
        version = new Version();
        if (!version.isAtLeast(1, 5, 0))
        {
            throw new LinkageError("Native library version must be at least " +
                                   "1.5.0, but is only " + version);
        }
    }

    /**
     * Initialize the native library layer.
     * @since 1.5.0
     */
    private static native void initNativeLibrary();
}
