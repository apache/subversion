package org.tigris.subversion.javahl;

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

public class PropertyData
{
    private String name;
    private String value;
    private byte[] data;
    private String path;
    private SVNClient client;
    public static final String MIME_TYPE = "svn:mime-type";
    public static final String IGNORE = "svn:ignore";
    public static final String EOL_STYLE = "svn:eol-style";
    public static final String KEYWORDS = "svn:keywords";
    public static final String EXECUTABLE = "svn:executable";
    public static final String EXECUTABLE_VALUE = "*";
    public static final String EXTERNALS = "svn:externals";
    public static final String REV_AUTHOR = "svn:author";
    public static final String REV_LOG = "svn:log";
    public static final String REV_DATE = "svn:date";
    public static final String REV_ORIGINAL_DATE = "svn:original-date";


    PropertyData(SVNClient cl, String p, String n, String v, byte[] d)
    {
        path = p;
        name = n;
        value = v;
        client = cl;
        data = d;
    }

    public String getName()
    {
        return name;
    }

    public String getValue()
    {
        return value;
    }

    public String getPath()
    {
        return path;
    }
    public byte[] getData()
    {
        return data;
    }
    public void setValue(String newValue, boolean recurse) throws ClientException
    {
        client.propertySet(path, name, newValue, recurse);
        value = newValue;
        data = null;
    }
    public void setValue(byte[] newValue, boolean recurse) throws ClientException
    {
        client.propertySet(path, name, newValue, recurse);
        data = newValue;
        value = null;
    }

    public void remove(boolean recurse) throws ClientException
    {
        client.propertyRemove(path, name, recurse);
    }
}
