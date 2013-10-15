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

package org.apache.subversion.javahl;

import org.apache.subversion.javahl.callback.*;
import org.apache.subversion.javahl.util.*;

import java.io.OutputStream;

public class SVNUtil
{
    //
    // Global configuration
    //

    /**
     * Enable storing authentication credentials in Subversion's
     * standard credentials store in the configuration directory and
     * system-specific secure locations.
     * <p>
     * The standard credentials store is enabled by default.
     * <p>
     * This setting will be inherited by all ISVNClient and ISVNRemote
     * objects. Changing the setting will not affect existing such
     * objects.
     * @throws ClientException
     */
    public static void enableNativeCredentialsStore()
        throws ClientException
      {
          new ConfigLib().enableNativeCredentialsStore();
      }

    /**
     * Disable storing authentication credentials in Subversion's
     * standard credentials store in the configuration directory and
     * system-specific secure locations. In this mode, the
     * authentication (see {@link ISVNClient#setPrompt} and {@link
     * remote.RemoteFactory#setPrompt}) will be called every time the
     * underlying library needs access to the credentials.
     * <p>
     * This mode is intented to support client implementations that
     * use their own credentials store.
     * <p>
     * The standard credentials store is enabled by default.
     * <p>
     * This setting will be inherited by all ISVNClient and ISVNRemote
     * objects. Changing the setting will not affect existing such
     * objects.
     * @throws ClientException
     */
    public static void disableNativeCredentialsStore()
        throws ClientException
      {
          new ConfigLib().disableNativeCredentialsStore();
      }

    /**
     * Find out if the standard credentials store is enabled.
     */
    public static boolean isNativeCredentialsStoreEnabled()
        throws ClientException
      {
          return new ConfigLib().isNativeCredentialsStoreEnabled();
      }

    /**
     * Set an event handler that will be called every time the
     * configuration is loaded.
     * <p>
     * This setting will be inherited by all ISVNClient and ISVNRemote
     * objects. Changing the setting will not affect existing such
     * objects.
     * @throws ClientException
     */
    public static void setConfigEventHandler(ConfigEvent configHandler)
        throws ClientException
      {
          new ConfigLib().setConfigEventHandler(configHandler);
      }

    /**
     * Return a reference to the installed configuration event
     * handler. The returned value may be <code>null</code>.
     */
    public static ConfigEvent getConfigEventHandler()
        throws ClientException
      {
          return new ConfigLib().getConfigEventHandler();
      }

    //
    // Diff and Merge
    //

    /**
     * Options to control the behaviour of the file diff routines.
     */
    public static class DiffOptions
    {
        /**
         * To what extent whitespace should be ignored when comparing lines.
         */
        public enum IgnoreSpace
        {
            /** Do not ignore whitespace */
            none,

            /**
             * Ignore changes in sequences of whitespace characters,
             * treating each sequence of whitespace characters as a
             * single space.
             */
            change,

            /** Ignore all whitespace characters. */
            all
        }

        /**
         * @param ignoreSpace Whether and how to ignore space differences
         *        in the files. The default is {@link IgnoreSpace#none}.
         * @param ignoreEolStyle Whether to treat all end-of-line
         *        markers the same when comparing lines.  The default
         *        is <code>false</code>.
         * @param showCFunction Whether the "@@" lines of the unified
         *        diff output should include a prefix of the nearest
         *        preceding line that starts with a character that
         *        might be the initial character of a C language
         *        identifier. The default is <code>false</code>.
         */
        public DiffOptions(IgnoreSpace ignoreSpace,
                           boolean ignoreEolStyle,
                           boolean showCFunction)
        {
            this.ignoreSpace = ignoreSpace;
            this.ignoreEolStyle = ignoreEolStyle;
            this.showCFunction = showCFunction;
        }

        public final IgnoreSpace ignoreSpace;
        public final boolean ignoreEolStyle;
        public final boolean showCFunction;
    }

    /** Style for displaying conflicts in merge output. */
    public enum ConflictDisplayStyle
    {
        /** Display modified and latest, with conflict markers. */
        modified_latest,

        /**
         * Like <code>modified_latest</code>, but with an extra effort
         * to identify common sequences between modified and latest.
         */
        resolved_modified_latest,

        /** Display modified, original, and latest, with conflict markers. */
        modified_original_latest,

        /** Just display modified, with no markers. */
        modified,

        /** Just display latest, with no markers. */
        latest,

        /**
         * Like <code>modified_original_latest</code>, but
         * <em>only<em> showing conflicts.
         */
        only_conflicts
    }

    /**
     * Given two versions of a file, base (<code>originalFile</code>)
     * and current (<code>modifiedFile</code>), show differences between
     * them in unified diff format.
     *
     * @param originalFile The base file version (unmodified)
     * @param modifiedFile The incoming file version (locally modified)
     * @param diffOptions Options controlling how files are compared.
     *        May be <code>null</code>.
     * @param originalHeader The header to display for the base file
     *        in the unidiff index block. If it is <code>null</code>,
     *        the <code>originalFile</code> path and its modification
     *        time will be used instead.
     * @param modifiedHeader The header to display for the current
     *        file in the unidiff index block. If it is <code>null</code>,
     *        the <code>currentFile</code> path and its modification
     *        time will be used instead.
     * @param headerEncoding The character encoding of the unidiff headers.
     * @param relativeToDir If this parameter is not <null>, it must
     *        be the path of a (possibly non-immediate) parent of both
     *        <code>originalFile</code> and <code>modifiedFile</code>.
     *        This path will be stripped from the beginning of those
     *        file names if they are used in the unidiff index header.
     * @param resultStream The stream that receives the merged output.
     * @return <code>true</code> if there were differences between the files.
     * @throws ClientException
     */
    public static boolean fileDiff(String originalFile,
                                   String modifiedFile,
                                   SVNUtil.DiffOptions diffOptions,

                                   String originalHeader,
                                   String modifiedHeader,
                                   String headerEncoding,
                                   String relativeToDir,

                                   OutputStream resultStream)
        throws ClientException
    {
        return new DiffLib().fileDiff(originalFile, modifiedFile, diffOptions,
                                      originalHeader, modifiedHeader,
                                      headerEncoding,
                                      relativeToDir, resultStream);
    }


    /**
     * Given three versions of a file, base (<code>originalFile</code>),
     * incoming (<code>modifiedFile</code>) and current
     * (<code>latestFile</code>, produce a merged result, possibly
     * displaying conflict markers.
     *
     * @param originalFile The base file version (common ancestor)
     * @param modifiedFile The incoming file version (modified elsewhere)
     * @param latestFile The current file version (locally modified)
     * @param diffOptions Options controlling how files are compared.
     *        May be <code>null</code>.
     * @param conflictOriginal Optional custom conflict marker for
     *        the <code>originalFile</code> contents.
     * @param conflictModified Optional custom conflict marker for
     *        the <code>modifiedFile</code> contents.
     * @param conflictLatest Optional custom conflict marker for
     *        the <code>latestFile</code> contents.
     * @param conflictSeparator Optional custom conflict separator.
     * @param conflictStyle Determines how conflicts are displayed.
     * @param resultStream The stream that receives the merged output.
     * @return <code>true</code> if there were any conflicts.
     * @throws ClientException
     */
    public static boolean fileMerge(String originalFile,
                                    String modifiedFile,
                                    String latestFile,
                                    DiffOptions diffOptions,

                                    String conflictOriginal,
                                    String conflictModified,
                                    String conflictLatest,
                                    String conflictSeparator,
                                    ConflictDisplayStyle conflictStyle,

                                    OutputStream resultStream)
        throws ClientException
    {
        return new DiffLib().fileMerge(originalFile, modifiedFile, latestFile,
                                       diffOptions,
                                       conflictOriginal, conflictModified,
                                       conflictLatest, conflictSeparator,
                                       conflictStyle, resultStream);
    }
}
