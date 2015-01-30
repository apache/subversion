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

/**
 * The description of a merge conflict, encountered during
 * merge/update/switch operations.
 */
public class ConflictDescriptor
{
    private String path;

    private Kind conflictKind;

    private NodeKind nodeKind;

    private String propertyName;

    private boolean isBinary;
    private String mimeType;

    private Action action;
    private Reason reason;

    // File paths, present only when the conflict involves the merging
    // of two files descended from a common ancestor, here are the
    // paths of up to four fulltext files that can be used to
    // interactively resolve the conflict.  NOTE: The content of these
    // files will be in repository-normal form (LF line endings and
    // contracted keywords).
    private String basePath;
    private String theirPath;
    private String myPath;
    private String mergedPath;

    /**
     * @see Operation
     */
    private Operation operation;

    /**
     * @see ConflictVersion
     */
    private ConflictVersion srcLeftVersion;

    /**
     * @see ConflictVersion
     */
    private ConflictVersion srcRightVersion;

    // Information about property conflicts. New in 1.9
    private String propRejectAbspath;
    private byte[] propValueBase;
    private byte[] propValueWorking;
    private byte[] propValueIncomingOld;
    private byte[] propValueIncomingNew;


    // Private constructor, only called from the JNI code.
    private ConflictDescriptor(String path, Kind conflictKind, NodeKind nodeKind,
                       String propertyName, boolean isBinary, String mimeType,
                       Action action, Reason reason, Operation operation,
                       String basePath, String theirPath,
                       String myPath, String mergedPath,
                       ConflictVersion srcLeft, ConflictVersion srcRight,
                       String propRejectAbspath, byte[] propValueBase,
                       byte[] propValueWorking,
                       byte[] propValueIncomingOld,
                       byte[] propValueIncomingNew)

    {
        this.path = path;
        this.conflictKind = conflictKind;
        this.nodeKind = nodeKind;
        this.propertyName = propertyName;
        this.isBinary = isBinary;
        this.mimeType = mimeType;
        this.action = action;
        this.reason = reason;
        this.basePath = basePath;
        this.theirPath = theirPath;
        this.myPath = myPath;
        this.mergedPath = mergedPath;
        this.operation = operation;
        this.srcLeftVersion = srcLeft;
        this.srcRightVersion = srcRight;
        this.propRejectAbspath = propRejectAbspath;
        this.propValueBase = propValueBase;
        this.propValueWorking = propValueWorking;
        this.propValueIncomingOld = propValueIncomingOld;
        this.propValueIncomingNew = propValueIncomingNew;
    }

    /**
     * This constructor should only be called from JNI code.
     * @deprecated
     */
    @Deprecated
    public ConflictDescriptor(String path, Kind conflictKind, NodeKind nodeKind,
                       String propertyName, boolean isBinary, String mimeType,
                       Action action, Reason reason, Operation operation,
                       String basePath, String theirPath,
                       String myPath, String mergedPath,
                       ConflictVersion srcLeft, ConflictVersion srcRight)
    {
        this(path, conflictKind, nodeKind, propertyName, isBinary, mimeType,
             action, reason, operation, basePath, theirPath, myPath, mergedPath,
             srcLeft, srcRight, null, null, null, null, null);
    }

    public String getPath()
    {
        return path;
    }

    public Kind getKind()
    {
        return conflictKind;
    }

    public NodeKind getNodeKind()
    {
        return nodeKind;
    }

    public String getPropertyName()
    {
        return propertyName;
    }

    public boolean isBinary()
    {
        return isBinary;
    }

    public String getMIMEType()
    {
        return mimeType;
    }

    public Action getAction()
    {
        return action;
    }

    public Reason getReason()
    {
        return reason;
    }

    public String getBasePath()
    {
        return basePath;
    }

    public String getTheirPath()
    {
        return theirPath;
    }

    public String getMyPath()
    {
        return myPath;
    }

    public String getMergedPath()
    {
        return mergedPath;
    }

    public Operation getOperation()
    {
        return operation;
    }

    public ConflictVersion getSrcLeftVersion()
    {
        return srcLeftVersion;
    }

    public ConflictVersion getSrcRightVersion()
    {
        return srcRightVersion;
    }

    public String getPropRejectAbspath()
    {
        return propRejectAbspath;
    }

    public byte[] getPropValueBase()
    {
        return propValueBase;
    }

    public byte[] getPropValueWorking()
    {
        return propValueWorking;
    }

    public byte[] getPropValueIncomingOld()
    {
        return propValueIncomingOld;
    }

    public byte[] getPropValueIncomingNew()
    {
        return propValueIncomingNew;
    }


    /**
     * Rich man's enum for <code>svn_wc_conflict_kind_t</code>.
     */
    public enum Kind
    {
        /** Attempting to change text or props.  */
        text,

        /** Attempting to add object.  */
        property,

        /** Tree conflict.  */
        tree;
    }

    /**
     * Rich man's enum for <code>svn_wc_conflict_action_t</code>.
     */
    public enum Action
    {
        /**
         * Attempting to change text or props.
         */
        edit,

        /**
         * Attempting to add object.
         */
        add,

        /**
         * Attempting to delete object.
         */
        delete,

        /**
         * Attempting to replace object.
         */
        replace;
    }

    /**
     * Rich man's enum for <code>svn_wc_conflict_reason_t</code>.
     */
    public enum Reason
    {
        /**
         * Local edits are already present.
         */
        edited,

        /**
         * Another object is in the way.
         */
        obstructed,

        /**
         * Object is already schedule-delete.
         */
        deleted,

        /**
         * Object is unknown or missing.
         */
        missing,

        /**
         * Object is unversioned.
         */
        unversioned,

        /**
         * Object is already added or schedule-add.
         * @since 1.6
         */
        added,

        /**
         * Object is already replaced.
         * @since 1.7
         */
        replaced,

        /**
         * Object is moved away.
         * @since 1.8
         */
        moved_away,

        /**
         * Object is moved here.
         * @since 1.8
         */
        moved_here;
    }

    public enum Operation
    {
        /* none */
        none,

        /* update */
        update,

        /* switch */
        /* Note: this is different that svn_wc.h, because 'switch' is a
        * reserved word in java  :(  */
        switched,

        /* merge */
        merge;
    }
}
