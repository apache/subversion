/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003 CollabNet.  All rights reserved.
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
 * Subversion notification interface.
 *
 * Implement this interface and implement the onNotify method
 * to provide a custom notification handler to the Modify
 * class.
 * If you need to pass extra information to the notification
 * handler then just add these to you implementing class
 */
public interface Notify
{
    /**
     * Handler for Subversion notifications.
     *
     * Override this function to allow Subversion to
     * send notifications
     * @param path on which action happen
     * @param action subversion action, see svn_wc_notify_action_t
     * @param kind node kind of path after action occurred
     * @param mimeType mime type of path after action occurred
     * @param contentState state of content after action occurred
     * @param propState state of properties after action occurred
     * @param revision revision number  after action occurred
     */
    public void onNotify(String path, int action, int kind, String mimeType, int contentState, int propState, long revision);

    /** The type of action occuring. */
    public static final class Action
    {
        /** Adding a path to revision control. */
        public static final int add = 0;
        
        /** Copying a versioned path. */
        public static final int copy = 1;
        
        /** Deleting a versioned path. */
        public static final int delete =2;
        
        /** Restoring a missing path from the pristine text-base. */
        public static final int restore = 3;
        
        /** Reverting a modified path. */
        public static final int revert = 4;
        
        /** A revert operation has failed. */
        public static final int failed_revert = 5;
        
        /** Resolving a conflict. */
        public static final int resolved = 6;

        /** Skipping a path. */
        public static final int skip = 7;

        /* The update actions are also used for checkouts, switches, and merges. */

        /** Got a delete in an update. */
        public static final int update_delete = 8;

        /** Got an add in an update. */
        public static final int update_add = 9;

        /** Got any other action in an update. */
        public static final int update_update = 10;

        /** The last notification in an update */
        public static final int update_completed = 11;

        /** About to update an external module, use for checkouts and switches too,
         * end with @c svn_wc_update_completed.
         */
        public static final int update_external = 12;

        /** The last notification in a status (including status on externals). */
        public static final int status_completed = 13;
        
        /** Running status on an external module. */
        public static final int status_external = 14;        


        /** Committing a modification. */
        public static final int commit_modified = 15;
        
        /** Committing an addition. */
        public static final int commit_added = 16;
        
        /** Committing a deletion. */
        public static final int commit_deleted = 17;
        
        /** Committing a replacement. */
        public static final int commit_replaced = 18;
        
        /** Transmitting post-fix text-delta data for a file. */
        public static final int commit_postfix_txdelta = 19;
        
 

		private static final String[] actionNames =
		{
			"add",
			"copy",
			"delete",
			"restore",
			"revert",
			"failed revert",
			"resolved",
			"skip",
			"update delete",
			"update add",
			"update modified",
			"update completed",
			"update external",
            "status completed",
            "status external",
			"sending modified",
			"sending added   ",
			"sending deleted ",
			"sending replaced",
			"transfer"

		};
		public static final String getActionName(int action)
		{
			return actionNames[action];
		}

    }
    /** The type of notification that is occuring. */
    public static final class Status
    {
        public static final int inapplicable = 0;

        /** Notifier doesn't know or isn't saying. */
        public static final int unknown = 1;

        /** The state did not change. */
        public static final int unchanged = 2;

        /** The item wasn't present. */
        public static final int missing = 3;

        /** An unversioned item obstructed work. */
        public static final int obstructed = 4;

        /** Pristine state was modified. */
        public static final int changed = 5;

        /** Modified state had mods merged in. */
        public static final int merged = 6;

        /** Modified state got conflicting mods. */
        public static final int conflicted = 7;



		private static final String[] statusNames =
		{
			"inapplicable",
			"unknown",
			"unchanged",
            "missing",
            "obstructed",
			"changed",
			"merged",
			"conflicted",
		};
		public static final String getStatusName(int status)
		{
			return statusNames[status];
		}
    }

}
