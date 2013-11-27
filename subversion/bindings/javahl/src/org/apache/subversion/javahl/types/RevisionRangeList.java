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

import org.apache.subversion.javahl.ClientException;
import org.apache.subversion.javahl.NativeResources;

import java.util.List;

/**
 * Object that describes a revision range list, including operations on it.
 * Returned from new accessors in {@link Mergeinfo}.
 * @since 1.9
 */
public class RevisionRangeList implements java.io.Serializable
{
    // Update the serialVersionUID when there is a incompatible change
    // made to this class.  See any of the following, depending upon
    // the Java release.
    private static final long serialVersionUID = 1L;

    /**
     * Load the required native library.
     */
    static
    {
        NativeResources.loadNativeLibrary();
    }

    private List<RevisionRange> ranges;

    /**
     * Wrap a list of revision ranges.
     */
    public RevisionRangeList(List<RevisionRange> ranges)
    {
        this.ranges = ranges;
    }

    /**
     * @return The wrapped list of revision ranges.
     */
    public List<RevisionRange> getRanges()
    {
        return ranges;
    }

    /**
     * Remove revisions in <code>eraser</code> from the current object
     * and return the resulting difference.
     * @param eraser The list of revisions to remoove.
     * @param considerInheritance Determines how to account for the
     *   {@link RevisionRange#isInherited} property when comparing
     *   revision ranges for equality.
     */
    public native List<RevisionRange> remove(List<RevisionRange> eraser,
                                             boolean considerInheritance)
        throws ClientException;

    /**
     * Remove revisions in <code>eraser</code> from the current object
     * and return the resulting difference.
     * @param eraser The list of revisions to remoove.
     * @param considerInheritance Determines how to account for the
     *   {@link RevisionRange#isInherited} property when comparing
     *   revision ranges for equality.
     */
    public RevisionRangeList remove(RevisionRangeList eraser,
                                    boolean considerInheritance)
        throws ClientException
    {
        return new RevisionRangeList
            (remove(eraser.ranges, considerInheritance));
    }

    // TODO: More svn_rangelist_t operations
}
