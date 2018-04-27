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

import org.apache.subversion.javahl.types.*;
import org.apache.subversion.javahl.callback.*;

/**
 * The update Reporter.
 * <p>
 * An interface which allows a working copy to describe a subset (or
 * possibly all) of its working-copy to a remote session, for the
 * purposes of an update, switch, status, or diff operation.
 * <p>
 * Paths for report calls are relative to the target (not the anchor)
 * of the operation.  Report calls must be made in depth-first order:
 * parents before children, all children of a parent before any
 * siblings of the parent.  The first report call must be a setPath
 * with an empty <code>path</code> argument and a valid revision.  (If
 * the target of the operation is locally deleted or missing, use the
 * anchor's revision.)  If the target of the operation is deleted or
 * switched relative to the anchor, follow up the initial setPath call
 * with a linkPath or deletePath call with an empty <code>path</code>
 * argument to indicate that.  In no other case may there be two
 * report descriptions for the same path.  If the target of the
 * operation is a locally added file or directory (which previously
 * did not exist), it may be reported as having revision 0 or as
 * having the parent directory's revision.
 *
 * @since 1.9
 */
public interface ISVNReporter
{
    /**
     * Release the native peer (should not depend on finalize),
     * and abort the report if it has not been completed yet.
     */
    void dispose();

    /**
     * Describe a working copy <code>path</code> as being at a
     * particular <code>revision</code> and having the given
     * <code>depth</code>.
     * <p>
     * <code>revision</code> may be
     * {@link org.apache.subversion.javahl.types.Revision#SVN_INVALID_REVNUM}
     * if (for example) <code>path</code> represents a locally-added
     * path with no revision number, or <code>depth</code> is
     * {@link org.apache.subversion.javahl.types.Depth#exclude}.
     * <p>
     * <code>path</code> may not be underneath a path on which
     * setPath() was previously called with
     * {@link org.apache.subversion.javahl.types.Depth#exclude}
     * in this report.
     * <p>
     * If <code>startEmpty</code> is set and <code>path</code> is a
     * directory, the implementor should assume the directory has no
     * entries or properties.
     * <p>
     * This will <em>override</em> any previous setPath() calls made
     * on parent paths. <code>path</code> is relative to the URL of
     * the remote session that created this reporter instance.
     * <p>
     * If <code>lockToken</code> is not <code>null</code>, it is the
     * lock token for <code>path</code> in the working copy.
     * @throws ClientException
     */
    void setPath(String path,
                 long revision,
                 Depth depth,
                 boolean startEmpty,
                 String lockToken)
            throws ClientException;

    /**
     * Describe a working copy <code>path</code> as missing.
     * <p>
     * <code>path</code> may not be underneath a path on which
     * setPath() was previously called with
     * {@link org.apache.subversion.javahl.types.Depth#exclude}
     * in this report.
     * @throws ClientException
     */
    void deletePath(String path) throws ClientException;

    /**
     * Like {@link #setPath setPath()},
     * but differs in that <code>path</code> in the working copy
     * (relative to the root of the report driver) isn't a reflection
     * of <code>path</code> in the repository (relative to the URL
     * specified when opening the remote session), but is instead a
     * reflection of a different repository <code>url</code> at
     * <code>revision</code>, and has depth <code>depth</code>.
     * <p>
     * <code>path</code> may not be underneath a path on which
     * setPath() was previously called with
     * {@link org.apache.subversion.javahl.types.Depth#exclude}
     * in this report.
     * <p>
     * If <code>startEmpty</code> is set and <code>path</code> is a
     * directory, the implementor should assume the directory has no
     * entries or properties.
     * <p>
     * If <code>lockToken</code> is not <code>null</code>, it is the
     * lock token for <code>path</code> in the working copy.
     * @throws ClientException
     */
    void linkPath(String url,
                  String path,
                  long revision,
                  Depth depth,
                  boolean startEmpty,
                  String lockToken)
            throws ClientException;

    /**
     * The report driver calls this when the state report is finished.
     * Any directories or files not explicitly "set" are assumed to be
     * at the baseline revision originally passed into do_update(). No
     * other reporting functions, including {@link abortReport()},
     * should be called after calling this function.
     *
     * @return The target revision of the resulting editor drive. In
     *         the case of {@link ISVNRemote#status}, this is the
     *         revision against which the status call was performed;
     *         in the case of {@link ISVNRemote#update}, it is the
     *         revision updated to.
     *
     * @throws ClientException
     */
    long finishReport() throws ClientException;

    /**
     * If an error occurs during a report, this routine should cause
     * the filesystem transaction to be aborted and cleaned up.  No
     * other reporting functions should be called after calling this
     * function.
     * @throws ClientException
     */
    void abortReport() throws ClientException;
}
