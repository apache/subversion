/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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

import java.util.ArrayList;
import java.util.Date;
import java.util.List;

/**
 * Implementation of {@link BlameCallback} interface.
 *
 * @since 1.5
 */
public class BlameCallbackImpl implements BlameCallback, BlameCallback2
{

    /** list of blame records (lines) */
    private List lines = new ArrayList();

    /*
     * (non-Javadoc)
     * @see org.tigris.subversion.javahl.BlameCallback#singleLine(java.util.Date,
     * long, java.lang.String, java.lang.String)
     */
    public void singleLine(Date changed, long revision, String author,
                           String line)
    {
        addBlameLine(new BlameLine(revision, author, changed, line));
    }

    public void singleLine(Date date, long revision, String author,
                           Date merged_date, long merged_revision,
                           String merged_author, String merged_path,
                           String line)
    {
        addBlameLine(new BlameLine(getRevision(revision, merged_revision),
                                   getAuthor(author, merged_author),
                                   getDate(date, merged_date),
                                   line));
    }

    private Date getDate(Date date, Date merged_date) {
        return (merged_date == null ? date : merged_date);
    }

    private String getAuthor(String author, String merged_author) {
        return (merged_author == null ? author : merged_author);
    }

    private long getRevision(long revision, long merged_revision) {
        return (merged_revision == -1 ? revision : merged_revision);
    }

    /**
     * Retrieve the number of line of blame information
     * @return number of lines of blame information
     */
    public int numberOfLines()
    {
        return this.lines.size();
    }

    /**
     * Retrieve blame information for specified line number
     * @param i the line number to retrieve blame information about
     * @return  Returns object with blame information for line
     */
    public BlameLine getBlameLine(int i)
    {
        if (i >= this.lines.size())
        {
            return null;
        }
        return (BlameLine) this.lines.get(i);
    }

    /**
     * Append the given blame info to the list
     * @param blameLine
     */
    protected void addBlameLine(BlameLine blameLine)
    {
        this.lines.add(blameLine);
    }

    /**
     * Class represeting one line of the lines, i.e. a blame record
     *
     */
    public static class BlameLine
    {

        private long revision;

        private String author;

        private Date changed;

        private String line;

        /**
         * Constructor
         *
         * @param revision
         * @param author
         * @param changed
         * @param line
         */
        public BlameLine(long revision, String author,
                         Date changed, String line)
        {
            super();
            this.revision = revision;
            this.author = author;
            this.changed = changed;
            this.line = line;
        }

        /**
         * @return Returns the author.
         */
        public String getAuthor()
        {
            return author;
        }

        /**
         * @return Returns the date changed.
         */
        public Date getChanged()
        {
            return changed;
        }

        /**
         * @return Returns the source line content.
         */
        public String getLine()
        {
            return line;
        }


        /**
         * @return Returns the revision.
         */
        public long getRevision()
        {
            return revision;
        }

        /*
         * (non-Javadoc)
         * @see java.lang.Object#toString()
         */
        public String toString()
        {
            StringBuffer sb = new StringBuffer();
            if (revision > 0)
            {
                pad(sb, Long.toString(revision), 6);
                sb.append(' ');
            }
            else
            {
                sb.append("     - ");
            }

            if (author != null)
            {
                pad(sb, author, 10);
                sb.append(" ");
            }
            else
            {
                sb.append("         - ");
            }

            sb.append(line);

            return sb.toString();
        }

        /**
         * Left pad the input string to a given length, to simulate printf()-
         * style output. This method appends the output to the class sb member.
         * @param sb StringBuffer to append to
         * @param val the input string
         * @param len the minimum length to pad to
         */
        private void pad(StringBuffer sb, String val, int len)
        {
            int padding = len - val.length();

            for (int i = 0; i < padding; i++)
            {
                sb.append(' ');
            }

            sb.append(val);
        }

    }
}
