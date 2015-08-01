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

package org.apache.subversion.javahl.util;

import org.apache.subversion.javahl.SVNUtil;
import org.apache.subversion.javahl.ClientException;
import org.apache.subversion.javahl.NativeResources;

import java.io.OutputStream;

/**
 * Encapsulates utility functions provided by libsvn_diff.
 * @since 1.9
 */
public class DiffLib
{
    /**
     * Load the required native library.
     */
    static
    {
        NativeResources.loadNativeLibrary();
    }

    /** @see SVNUtil.fileDiff */
    public boolean fileDiff(String originalFile,
                            String modifiedFile,
                            SVNUtil.DiffOptions diffOptions,

                            String originalHeader,
                            String modifiedHeader,
                            String headerEncoding,
                            String relativeToDir,

                            OutputStream resultStream)
        throws ClientException
    {
        return nativeFileDiff(originalFile, modifiedFile,

                              // Interpret the diff options
                              (diffOptions == null
                               ? SVNUtil.DiffOptions.IgnoreSpace.none.ordinal()
                               : diffOptions.ignoreSpace.ordinal()),
                              (diffOptions == null ? false
                               : diffOptions.ignoreEolStyle),
                              (diffOptions == null ? false
                               : diffOptions.showCFunction),
                              (diffOptions == null ? -1
                               : diffOptions.contextSize),

                              originalHeader, modifiedHeader, headerEncoding,
                              relativeToDir, resultStream);
    }

    private native
        boolean nativeFileDiff(String originalFile,
                               String modifiedFile,

                               // Interpreted diff options
                               int ignoreSpace,
                               boolean ignoreEolStyle,
                               boolean showCFunction,
                               int contextSize,

                               String originalHeader,
                               String modifiedHeader,
                               String headerEncoding,
                               String relativeToDir,

                               OutputStream resultStream)
        throws ClientException;

    /** @see SVNUtil.fileMerge */
    public boolean fileMerge(String originalFile,
                             String modifiedFile,
                             String latestFile,
                             SVNUtil.DiffOptions diffOptions,

                             String conflictOriginal,
                             String conflictModified,
                             String conflictLatest,
                             String conflistSeparator,
                             SVNUtil.ConflictDisplayStyle conflictStyle,

                             OutputStream resultStream)
        throws ClientException
    {
        return nativeFileMerge(originalFile, modifiedFile, latestFile,

                               // Interpret the diff options
                               (diffOptions == null
                                ? SVNUtil.DiffOptions.IgnoreSpace.none.ordinal()
                                : diffOptions.ignoreSpace.ordinal()),
                               (diffOptions == null ? false
                                : diffOptions.ignoreEolStyle),
                               (diffOptions == null ? false
                                : diffOptions.showCFunction),

                               conflictOriginal, conflictModified,
                               conflictLatest, conflistSeparator,

                               // Interpret the conflict style
                               conflictStyle.ordinal(),

                               resultStream);
    }

    private native
        boolean nativeFileMerge(String originalFile,
                                String modifiedFile,
                                String latestFile,

                                // Interpreted diff options
                                int ignoreSpace,
                                boolean ignoreEolStyle,
                                boolean showCFunction,

                                String conflictOriginal,
                                String conflictModified,
                                String conflictLatest,
                                String conflistSeparator,

                                // Interpreted conflict display style
                                int conflictStyle,

                                OutputStream resultStream)
        throws ClientException;
}
