/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2004 CollabNet.  All rights reserved.
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

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * Class to specify a revision in a svn command.
 */
public class Revision
{
    /**
     * kind of revision specified
     */
    protected int revKind;

    /**
     * Create a new revision
     * @deprecated
     * @param kind  kind of revision
     */
    public Revision(int kind)
    {
        revKind = kind;
    }

    /**
     * Internally create a new revision
     * @param kind      kind of revision
     * @param marker    marker to differtiate from the public deprecated version
     */
    protected Revision(int kind, boolean marker)
    {
        revKind = kind;
    }

    /**
     * Returns the kind of the Revsion
     * @return kind
     */
    public int getKind()
    {
        return revKind;
    }

    /**
     * return the textual representation of the revision
     * @return english text
     */
    public String toString()
    {
        switch(revKind) {
            case Kind.base : return "BASE";
            case Kind.committed : return "COMMITTED";
            case Kind.head : return "HEAD";
            case Kind.previous : return "PREV";
            case Kind.working : return "WORKING";
        }
        return super.toString();
    }

    /**
     * compare to revision objects
     * @param target
     * @return if both object have equal content
     */
    public boolean equals(Object target) {
        if (this == target)
            return true;
        if (!(target instanceof Revision))
            return false;

        return ((Revision)target).revKind == revKind;        
    }

    /**
     * last commited revision
     */
    public static final Revision HEAD = new Revision(Kind.head, true);
    /**
     * first existing revision
     */
    public static final Revision START = new Revision(Kind.unspecified, true);
    /**
     * last committed revision, needs working copy
     */
    public static final Revision COMMITTED = new Revision(Kind.committed, true);
    /**
     * previous committed revision, needs working copy
     */
    public static final Revision PREVIOUS = new Revision(Kind.previous, true);
    /**
     * base revision of working copy
     */
    public static final Revision BASE = new Revision(Kind.base, true);
    /**
     * working version in working copy
     */
    public static final Revision WORKING = new Revision(Kind.working, true);
    /**
     * Marker revision number for no real revision
     */
    public static final int SVN_INVALID_REVNUM = -1;

    /**
     * class to specify a Revision by number
     */
    public static class Number extends Revision
    {
        /**
         * the revision number
         */
        protected long revNumber;

        /**
         * create a revision by number object
         * @param number the number
         */
        public Number(long number)
        {
            super(Kind.number, true);
            revNumber = number;
        }

        /**
         * Returns the revision number
         * @return number
         */
        public long getNumber()
        {
            return revNumber;
        }

        /**
         * return the textual representation of the revision
         * @return english text
         */
        public String toString() {
            return Long.toString(revNumber);
        }

        /**
         * compare to revision objects
         * @param target
         * @return if both object have equal content
         */
        public boolean equals(Object target) {
            if (!super.equals(target))
                return false;

            return ((Revision.Number)target).revNumber == revNumber;        
        }
    }

    /**
     * class to specify a revision by a date
     */
    public static class DateSpec extends Revision
    {
        /**
         * the date
         */
        protected Date revDate;

        /**
         * create a revision by date object
         * @param date
         */
        public DateSpec(Date date)
        {
            super(Kind.date, true);
            revDate = date;
        }
        /**
         * Returns the date of the revision
         * @return the date
         */
        public Date getDate()
        {
            return revDate;
        }

        /**
         * return the textual representation of the revision
         * @return english text
         */
        public String toString() {
            
            SimpleDateFormat dateFormat = new SimpleDateFormat("EEE, d MMM yyyy HH:mm:ss Z", Locale.US); 
            return '{'+dateFormat.format(revDate)+'}';
        }

        /**
         * compare to revision objects
         * @param target
         * @return if both object have equal content
         */
        public boolean equals(Object target) {
            if (!super.equals(target))
                return false;

            return ((Revision.DateSpec)target).revDate.equals(revDate);        
        }
        
    }

    /**
     * Various ways of specifying revisions.
     *
     * Note:
     * In contexts where local mods are relevant, the `working' kind
     * refers to the uncommitted "working" revision, which may be modified
     * with respect to its base revision.  In other contexts, `working'
     * should behave the same as `committed' or `current'.
     *
     * the values are defined in RevisionKind because of building reasons
     */
    public static final class Kind implements RevisionKind
    {
    }
}
