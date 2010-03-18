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

import java.util.EventObject;

/**
 * The event passed to the {@link Notify2#onNotify(NotifyInformation)}
 * API to notify {@link SVNClientInterface} of relevant events.
 *
 * @since 1.2
 */
public class NotifyInformation extends EventObject
{
    // Update the serialVersionUID when there is a incompatible change
    // made to this class.  See any of the following, depending upon
    // the Java release.
    // http://java.sun.com/j2se/1.3/docs/guide/serialization/spec/version.doc7.html
    // http://java.sun.com/j2se/1.4/pdf/serial-spec.pdf
    // http://java.sun.com/j2se/1.5.0/docs/guide/serialization/spec/version.html#6678
    // http://java.sun.com/javase/6/docs/platform/serialization/spec/version.html#6678
    private static final long serialVersionUID = 1L;

    /**
     * The {@link NotifyAction} which triggered this event.
     */
    private Action action;

    /**
     * The {@link NodeKind} of the item.
     */
    private NodeKind kind;

    /**
     * The MIME type of the item.
     */
    private String mimeType;

    /**
     * Any lock for the item.
     */
    private Lock lock;

    /**
     * Any error message for the item.
     */
    private String errMsg;

    /**
     * The {@link NotifyStatus} of the content of the item.
     */
    private Status contentState;

    /**
     * The {@link NotifyStatus} of the properties of the item.
     */
    private Status propState;

    /**
     * The {@link LockStatus} of the lock of the item.
     */
    private int lockState;

    /**
     * The revision of the item.
     */
    private long revision;

    /**
     * The name of the changelist.
     * @since 1.5
     */
    private String changelistName;

    /**
     * The range of the merge just beginning to occur.
     * @since 1.5
     */
    private RevisionRange mergeRange;

    /**
     * A common absolute path prefix that can be subtracted from .path.
     * @since 1.6
     */
    private String pathPrefix;

    /**
     * This constructor is to be used by the native code.
     *
     * @param path The path of the item, which is the source of the event.
     * @param action The {@link NotifyAction} which triggered this event.
     * @param kind The {@link NodeKind} of the item.
     * @param mimeType The MIME type of the item.
     * @param lock Any lock for the item.
     * @param errMsg Any error message for the item.
     * @param contentState The {@link NotifyStatus} of the content of
     * the item.
     * @param propState The {@link NotifyStatus} of the properties of
     * the item.
     * @param lockState The {@link LockStatus} of the lock of the item.
     * @param revision The revision of the item.
     * @param changelistName The name of the changelist.
     * @param mergeRange The range of the merge just beginning to occur.
     * @param pathPrefix A common path prefix.
     */
    public NotifyInformation(String path, Action action, NodeKind kind,
                             String mimeType, Lock lock, String errMsg,
                             Status contentState, Status propState,
                             int lockState, long revision,
                             String changelistName, RevisionRange mergeRange,
                             String pathPrefix)
    {
        super(path == null ? "" : path);
        this.action = action;
        this.kind = kind;
        this.mimeType = mimeType;
        this.lock = lock;
        this.errMsg = errMsg;
        this.contentState = contentState;
        this.propState = propState;
        this.lockState = lockState;
        this.revision = revision;
        this.changelistName = changelistName;
        this.mergeRange = mergeRange;
        this.pathPrefix = pathPrefix;
    }

    /**
     * @return The path of the item, which is the source of the event.
     */
    public String getPath()
    {
        return (String) super.source;
    }

    /**
     * @return The {@link NotifyAction} which triggered this event.
     */
    public Action getAction()
    {
        return action;
    }

    /**
     * @return The {@link NodeKind} of the item.
     */
    public NodeKind getKind()
    {
        return kind;
    }

    /**
     * @return The MIME type of the item.
     */
    public String getMimeType()
    {
        return mimeType;
    }

    /**
     * @return Any lock for the item.
     */
    public Lock getLock()
    {
        return lock;
    }

    /**
     * @return Any error message for the item.
     */
    public String getErrMsg()
    {
        return errMsg;
    }

    /**
     * @return The {@link NotifyStatus} of the content of the item.
     */
    public Status getContentState()
    {
        return contentState;
    }

    /**
     * @return The {@link NotifyStatus} of the properties of the item.
     */
    public Status getPropState()
    {
        return propState;
    }

    /**
     * @return The {@link LockStatus} of the lock of the item.
     */
    public int getLockState()
    {
        return lockState;
    }

    /**
     * @return The revision of the item.
     */
    public long getRevision()
    {
        return revision;
    }

    /**
     * @return The name of the changelist.
     * @since 1.5
     */
    public String getChangelistName()
    {
        return changelistName;
    }

    /**
     * @return The range of the merge just beginning to occur.
     * @since 1.5
     */
    public RevisionRange getMergeRange()
    {
        return mergeRange;
    }

    /**
     * @return The common absolute path prefix.
     * @since 1.6
     */
    public String getPathPrefix()
    {
        return pathPrefix;
    }

    /**
     * The type of action triggering the notification
     */
    public enum Action
    {
        /** Adding a path to revision control. */
        add             ("add"),

        /** Copying a versioned path. */
        copy            ("copy"),

        /** Deleting a versioned path. */
        delete          ("delete"),

        /** Restoring a missing path from the pristine text-base. */
        restore         ("restore"),

        /** Reverting a modified path. */
        revert          ("revert"),

        /** A revert operation has failed. */
        failed_revert   ("failed revert"),

        /** Resolving a conflict. */
        resolved        ("resolved"),

        /** Skipping a path. */
        skip            ("skip"),

        /* The update actions are also used for checkouts, switches, and
           merges. */

        /** Got a delete in an update. */
        update_delete   ("update delete"),

        /** Got an add in an update. */
        update_add      ("update add"),

        /** Got any other action in an update. */
        update_update   ("update modified"),

        /** The last notification in an update */
        update_completed ("update completed"),

        /** About to update an external module, use for checkouts and switches
         *  too, end with @c svn_wc_update_completed.
         */
        update_external ("update external"),

        /** The last notification in a status (including status on externals).
         */
        status_completed ("status completed"),

        /** Running status on an external module. */
        status_external ("status external"),

        /** Committing a modification. */
        commit_modified ("sending modified"),

        /** Committing an addition. */
        commit_added    ("sending added"),

        /** Committing a deletion. */
        commit_deleted  ("sending deleted"),

        /** Committing a replacement. */
        commit_replaced ("sending replaced"),

        /** Transmitting post-fix text-delta data for a file. */
        commit_postfix_txdelta ("transfer"),

        /** Processed a single revision's blame. */
        blame_revision  ("blame revision processed"),

        /**
         * @since 1.2
         * Locking a path
         */
        locked          ("locked"),

        /**
         * @since 1.2
         * Unlocking a path
         */
        unlocked        ("unlocked"),

        /**
         * @since 1.2
         * Failed to lock a path
         */
        failed_lock     ("locking failed"),

        /**
         * @since 1.2
         * Failed to unlock a path
         */
        failed_unlock   ("unlocking failed"),

        /**
         * @since 1.5
         * Tried adding a path that already exists.
         */
        exists          ("path exists"),

        /**
         * @since 1.5
         * Set the changelist for a path.
         */
        changelist_set  ("changelist set"),

        /**
         * @since 1.5
         * Clear the changelist for a path.
         */
        changelist_clear ("changelist cleared"),

        /**
         * @since 1.5
         * A merge operation has begun.
         */
        merge_begin     ("merge begin"),

        /**
         * @since 1.5
         * A merge operation from a foreign repository has begun.
         */
        foreign_merge_begin ("foreign merge begin"),

        /**
         * @since 1.5
         * Got a replaced in an update.
         */
        update_replaced ("replaced"),

        /**
         * @since 1.6
         * Property added.
         */
        property_added  ("property added"),

        /**
         * @since 1.6
         * Property modified.
         */
        property_modified ("property modified"),

        /**
         * @since 1.6
         * Property deleted.
         */
        property_deleted ("property deleted"),

        /**
         * @since 1.6
         * Property delete nonexistent.
         */
        property_deleted_nonexistent ("nonexistent property deleted"),

        /**
         * @since 1.6
         * Revision property set.
         */
        revprop_set     ("revprop set"),

        /**
         * @since 1.6
         * Revision property deleted.
         */
        revprop_deleted ("revprop deleted"),

        /**
         * @since 1.6
         * The last notification in a merge
         */
        merge_completed ("merge completed"),

        /**
         * @since 1.6
         * The path is a tree-conflict victim of the intended action
         */
        tree_conflict   ("tree conflict");

        /**
         * The description of the action.
         */
        private String description;

        Action(String description)
        {
            this.description = description;
        }

        public String toString()
        {
            return description;
        }
    }

    public enum Status
    {
        /** It not applicable*/
        inapplicable    ("inapplicable"),

        /** Notifier doesn't know or isn't saying. */
        unknown         ("unknown"),

        /** The state did not change. */
        unchanged       ("unchanged"),

        /** The item wasn't present. */
        missing         ("missing"),

        /** An unversioned item obstructed work. */
        obstructed      ("obstructed"),

        /** Pristine state was modified. */
        changed         ("changed"),

        /** Modified state had mods merged in. */
        merged          ("merged"),

        /** Modified state got conflicting mods. */
        conflicted      ("conflicted");

        /**
         * The description of the action.
         */
        private String description;

        Status(String description)
        {
            this.description = description;
        }

        public String toString()
        {
            return description;
        }
    }
}
