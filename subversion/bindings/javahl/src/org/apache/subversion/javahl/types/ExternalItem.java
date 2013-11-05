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

import org.apache.subversion.javahl.SubversionException;

/**
 * Describes one external item declaration
 * @since 1.9
 */
public class ExternalItem implements java.io.Serializable
{
    // Update the serialVersionUID when there is a incompatible change
    // made to this class.
    private static final long serialVersionUID = 1L;

    /**
     * Create a new external item declaration.
     * @param targetDir See {@link #getTargetDir}
     * @param url See {@link #getUrl}
     * @param revision See {@link #getRevision};
     *     <code>null</code> will be interpreted as {@link Revision#HEAD}
     * @param pegRevision See {@link #getPegRevision};
     *     <code>null</code> will be interpreted as {@link Revision#HEAD}
     */
    public ExternalItem(String targetDir, String url,
                        Revision revision, Revision pegRevision)
        throws SubversionException
    {
        this(false, targetDir, url, revision, pegRevision);
        validateRevision(revision, "revision");
        validateRevision(pegRevision, "pegRevision");
    }

    /* This constructor is called directly by the native implementation */
    private ExternalItem(boolean dummy_parameter_to_discriminate_constructors,
                         String targetDir, String url,
                         Revision revision, Revision pegRevision)
    {
        this.targetDir = targetDir;
        this.url = url;
        this.revision = (revision != null ? revision : Revision.HEAD);
        this.pegRevision = (pegRevision != null ? pegRevision : Revision.HEAD);
    }

    /**
     * The name of the subdirectory into which this external should be
     * checked out.  This is relative to the parent directory that
     * holds this external item.
     */
    public String getTargetDir()
    {
        return targetDir;
    }

    /**
     * Where to check out from. This is possibly a relative external
     * URL, as allowed in externals definitions, but without the peg
     * revision.
     */
    public String getUrl()
    {
        return url;
    }

    /**
     * What revision to check out. The only valid kinds for this are a
     * numered revision {@link Revision.Number}, a date
     * {@link Revision.DateSpec}, or {@link Revision#HEAD}.
     */
    public Revision getRevision()
    {
        return revision;
    }

    /**
     * The peg revision to use when checking out. The only valid kinds
     * for this are a numered revision {@link Revision.Number}, a date
     * {@link Revision.DateSpec}, or {@link Revision#HEAD}.
     */
    public Revision getPegRevision()
    {
        return pegRevision;
    }

    /* Exception class for failed revision kind validation. */
    private static class BadRevisionKindException extends SubversionException
    {
        public BadRevisionKindException(String param)
        {
            super("the '" + param + "' constructor argument" +
                  " must be a date, a number, or Revision.HEAD");
        }
    }

    /* Validates the revision and pegRevision parameters of the ctor. */
    private static void validateRevision(Revision revision, String param)
        throws SubversionException
    {
        if (revision != null
            && revision.getKind() != Revision.Kind.number
            && revision.getKind() != Revision.Kind.date
            && revision.getKind() != Revision.Kind.head)
            throw new BadRevisionKindException(param);
    }

    private String targetDir;
    private String url;
    private Revision revision;
    private Revision pegRevision;
}
