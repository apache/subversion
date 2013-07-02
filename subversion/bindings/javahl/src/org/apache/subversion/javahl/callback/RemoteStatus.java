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

package org.apache.subversion.javahl.callback;

import org.apache.subversion.javahl.ISVNRemote;

/**
 * Called for each affected element in a remote status driave.
 * <p>
 * <b>Note:</b> All paths sent to the callback methods are relative to
 * the {@link ISVNRemtoe} session's URL.
 * @see ISVNRemote#status
 * @since 1.9
 */
public interface RemoteStatus
{
    /**
     * A directory was added.
     * @param relativePath The session-relative path of the new directory.
     */
    void addedDirectory(String relativePath);

    /**
     * A file was added.
     * @param relativePath The session-relative path of the new file.
     */
    void addedFile(String relativePath);

    /**
     * A symbolic link was added.
     * @param relativePath The session-relative path of the new symbolic link.
     */
    void addedSymlink(String relativePath);

    /**
     * A directory was modified.
     * @param relativePath The session-relative path of the directory.
     * @param revision The revision in which it was last modified.
     * @param childrenModified The directory contents changed.
     * @param propsModified The directory's properties changed.
     */
    void modifiedDirectory(String relativePath, long revision,
                           boolean childrenModified, boolean propsModified);

    /**
     * A file was modified.
     * @param relativePath The session-relative path of the directory.
     * @param revision The revision in which it was last modified.
     * @param textModified The file contents changed.
     * @param propsModified The file's properties changed.
     */
    void modifiedFile(String relativePath, long revision,
                      boolean textModified, boolean propsModified);

    /**
     * A symbolic link was modified.
     * @param relativePath The session-relative path of the symlink.
     * @param revision The revision in which it was last modified.
     * @param textModified The link target changed.
     * @param propsModified The symlink's properties changed.
     */
    void modifiedSymlink(String relativePath, long revision,
                         boolean targetModified, boolean propsModified);

    /**
     * An entry was deleted.
     * @param relativePath The session-relative path of the entry.
     * @param revision The revision in which it was deleted.
     */
    void deleted(String relativePath, long revision);
}
