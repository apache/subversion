/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 * @endcopyright
 */

package org.apache.subversion.javahl.types;

import org.apache.subversion.javahl.NativeResources;

/**
 * Encapsulates information about the compile-time and run-time
 * properties of the Subversion libraries.
 * @since 1.8
 */
public class VersionExtended
{
    /**
     * Load the required native library.
     */
    static
    {
        NativeResources.loadNativeLibrary();
    }

    /**
     * Release the native peer. This method must be called to release
     * the native resources held by this object.
     * <p>
     * Once this method is called, all object references obtained from
     * the iterators returned by {@link #getLinkedLibs()} and
     * {@link #getLoadedLibs()} become invalid and should no longer be used.
     */
    public native void dispose();

    /**
     * release the native peer (should use dispose instead)
     */
    public native void finalize();

    /**
     * @return The date when the libsvn_subr library was compiled, in
     * the format defined by the C standard macro #__DATE__.
     */
    public native String getBuildDate();

    /**
     * @return The time when the libsvn_subr library was compiled, in
     * the format defined by the C standard macro #__TIME__.
     */
    public native String getBuildTime();

    /**
     * @return The canonical host triplet (arch-vendor-osname) of the
     * system where libsvn_subr was compiled.
     * <p>
     * <b>Note:</b> On Unix-like systems (includng Mac OS X), this string
     * is the same as the output of the config.guess script for the
     * underlying Subversion libraries.
     */
    public native String getBuildHost();

    /**
     * @return The localized copyright notice.
     */
    public native String getCopyright();

    /**
     * @return The canonical host triplet (arch-vendor-osname) of the
     * system where the current process is running.
     * <p>
     * <b>Note:</b> This string may not be the same as the output of
     * config.guess on the same system.
     */
    public native String getRuntimeHost();

    /**
     * @return The "commercial" release name of the running operating
     * system, if available.  Not to be confused with, e.g., the
     * output of "uname -v" or "uname -r".  The returned value may
     * be #null.
     */
    public native String getRuntimeOSName();

    /**
     * Dependent library information.
     * Describes the name and versions of known dependencies
     * used by libsvn_subr.
     */
    public class LinkedLib
    {
        /** @return Library name. */
        public final native String getName();

        /** @return Compile-time version string. */
        public final native String getCompiledVersion();

        /**
         * @return Run-time version string (may be #null, which
         * indicates that the library is embedded or statically
         * linked).
         */
        public final native String getRuntimeVersion();

        LinkedLib(VersionExtended wrapper, int index)
        {
            this.wrapper = wrapper;
            this.index = index;
        }

        private final VersionExtended wrapper;
        private final int index;
    };

    /**
     * @return Iterator for an immutable internal list of #LinkedLib
     * describing dependent libraries.  The list may be empty.
     */
    public java.util.Iterator<LinkedLib> getLinkedLibs()
    {
        return new LinkedLibIterator(this);
    }

    /**
     * Loaded shared library information.
     * Describes the name and, where available, version of the shared
     * libraries loaded by the running program.
     */
    public class LoadedLib
    {
        /** @return Library name. */
        public final native String getName();

        /** @return Library version (may be #null). */
        public final native String getVersion();

        LoadedLib(VersionExtended wrapper, int index)
        {
            this.wrapper = wrapper;
            this.index = index;
        }

        private final VersionExtended wrapper;
        private final int index;
    };

    /**
     * @return Iterator for an immutable internal list of #LoadedLib
     * describing loaded shared libraries.  The the list may be empty.
     * <p>
     * <b>Note:</b> On Mac OS X, the loaded frameworks, private frameworks
     * and system libraries will not be listed.
     */
    public java.util.Iterator<LoadedLib> getLoadedLibs()
    {
        return new LoadedLibIterator(this);
    }

    /**
     * Iterator for #LinkedLib.
     */
    private class LinkedLibIterator implements java.util.Iterator<LinkedLib>
    {
        public LinkedLibIterator(VersionExtended wrapper)
        {
            this.wrapper = wrapper;
            this.index = -1;
        }

        /**
         * Implementation of java.util.Iterator#hasNext().
         * @return #true if next() can be called safely.
         */
        public native boolean hasNext();

        /**
         * Implementation of java.util.Iterator#next().
         * @return The next element of the sequence.
         */
        public LinkedLib next()
        {
            if (!hasNext())
                throw new java.util.NoSuchElementException();
            return new LinkedLib(this.wrapper, ++this.index);
        }

        /**
         * Implementation of java.util.Iterator#remove().
         * <p>
         * <b>Note:</b> Not implemented, all sequences are immutable.
         */
        public void remove()
        {
            throw new java.lang.UnsupportedOperationException();
        }

        private final VersionExtended wrapper;
        private int index;
    };

    /**
     * Iterator for #LoadedLib.
     */
    private class LoadedLibIterator implements java.util.Iterator<LoadedLib>
    {
        public LoadedLibIterator(VersionExtended wrapper)
        {
            this.wrapper = wrapper;
            this.index = -1;
        }

        /**
         * Implementation of java.util.Iterator#hasNext().
         * @return #true if next() can be called safely.
         */
        public native boolean hasNext();

        /**
         * Implementation of java.util.Iterator#next().
         * @return The next element of the sequence.
         */
        public LoadedLib next()
        {
            if (!hasNext())
                throw new java.util.NoSuchElementException();
            return new LoadedLib(this.wrapper, ++this.index);
        }

        /**
         * Implementation of java.util.Iterator#remove().
         * <p>
         * <b>Note:</b> Not implemented, all sequences are immutable.
         */
        public void remove()
        {
            throw new java.lang.UnsupportedOperationException();
        }

        private final VersionExtended wrapper;
        private int index;
    };

    /**
     * Slot for the adress of the native peer.
     * The JNI code is the only user of this member.
     */
    private long cppAddr = 0;
}
