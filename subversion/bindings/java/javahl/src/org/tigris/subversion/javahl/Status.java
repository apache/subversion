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
 * Subversion status API.
 */
public class Status
{
    Status(String p, boolean id, long r, long lc, String lca, int tt, int pt, boolean iv,
                   boolean il, boolean ic, int rtt, int rpt, String coo, String con, String cow, String ur, String urcf, long recf)
    {
        pa = p;
        idi = id;
        re = r;
        lch = lc;
        lcoa = lca;
        tty = tt;
        pty = pt;
        ive = iv;
        ilo = il;
        ico = ic;
        rtty = rtt;
        rpty = rpt;
        co = coo;
        cn = con;
        cw = cow;
        u = ur;
        ucf = urcf;
        rcf = recf;
    }
    private String pa;
    private boolean idi;
    private long re;
    private long lch;
    private String lcoa;
    private int tty;
    private int pty;
    private boolean ive;
    private boolean ilo;
    private boolean ico;
    private int rtty;
    private int rpty;
    private String cn;
    private String co;
    private String cw;
    private String u;
    private String ucf;
    private long rcf;
    /**
     * @return path of status entry
     */
    public String path()
    {
        return pa;
    }
    /**
     * @return true if entry is a dir
     */
    public boolean isDir()
    {
        return idi;
    }
    /**
     * @return revision if versioned, otherwise SVN_INVALID_REVNUM
     */
    public long revision()
    {
        return re;
    }
    /**
     * Returns the last time the file was changed revision number.
     */
    public long lastChanged()
    {
        return lch;
    }
    /**
     * @return name of author if versioned, NULL otherwise
     */
    public String lastCommitAuthor()
    {
        return lcoa;
    }
    /**
     * @return file status of the "textual" component.
     */
    public String textDescription()
    {
        return Kind.getDescription(tty);
    }
    /**
     * @return file status property enum of the "textual" component.
     */
    public int textType()
    {
        return tty;
    }
    /**
     * @return textual file status of the "property" component.
     */
    public String propDescription()
    {
        return Kind.getDescription(pty);
    }
    /**
     * @return file status property enum of the "property" component.
     */
    public int propType()
    {
        return pty;
    }
    /**
     * @return file status of the "textual" component im the repository.
     */
    public String repositoryTextDescription()
    {
        return Kind.getDescription(rtty);
    }
    /**
     * @return file status property enum of the "textual" component im the repository.
     */
    public int repositoryTextType()
    {
        return rtty;
    }
    /**
     * @return textual file status of the "property" component im the repository.
     */
    public String repositoryPropDescription()
    {
        return Kind.getDescription(rpty);
    }
    /**
     * @return file status property enum of the "property" component im the repository.
     */
    public int repositoryPropType()
    {
        return rpty;
    }
    /**
     * @return true if under version control
     */
    public boolean isVersioned()
    {
        return ive;
    }
    /**
     * @return true if locked
     */
     public boolean isLocked()
    {
        return ilo;
    }
    /**
     * @return true if copied
     */
    public boolean isCopied()
    {
        return ico;
    }
    public String conflictNew()
    {
        return cn;
    }
    public String conflictOld()
    {
        return co;
    }
    public String conflictWorking()
    {
        return cw;
    }
    public String url()
    {
        return u;
    }
    public String urlCopiedFrom()
    {
        return ucf;
    }
    public long revisionCopiedFrom()
    {
        return rcf;
    }

    public static final class Kind
    {
        /** does not exist */
        public static final int none = 0;

        /** exists, but uninteresting. */
        public static final int normal = 1;

		/** text or props have been modified */
		public static final int modified = 2;

        /** is scheduled for additon */
        public static final int added = 3;

        /** scheduled for deletion */
        public static final int deleted = 4;

		/** is not a versioned thing in this wc */
		public static final int unversioned = 5;

		/** under v.c., but is missing */
		public static final int absent = 6;

        /** was deleted and then re-added */
        public static final int replaced = 7;

        /** local mods received repos mods */
        public static final int merged = 8;

        /** local mods received conflicting repos mods */
        public static final int conflicted = 9;

        /** an unversioned resource is in the way of the versioned resource */
        public static final int obstructed = 10;

        public static final String getDescription(int kind)
        {
            switch (kind)
            {
            case none:
              return "non-svn";
            case normal:
              return "normal";
            case added:
              return "added";
            case absent:
              return "absent";
            case deleted:
              return "deleted";
            case replaced:
              return "replaced";
            case modified:
              return "modified";
            case merged:
              return "merged";
            case conflicted:
              return "conflicted";
            case unversioned:
            default:
              return "unversioned";
            }
        }
    }
}
