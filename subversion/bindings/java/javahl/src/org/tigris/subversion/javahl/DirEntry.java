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
/*
 * Created on 7 avr. 2003
 *
 */
package org.tigris.subversion.javahl;

import java.util.Date;

/**
 * A general subversion directory entry.
 * @author Cédric Chabanois 
 *         <a href="mailto:cchabanois@ifrance.com">cchabanois@ifrance.com</a>
 *
 */
public class DirEntry {

	private long lastChanged; // nanoseconds since 01/01/1970
	private long lastChangedRevision;
	private boolean hasProps;
	private String lastAuthor;
	private int nodeKind;
	private long size;
	private String path;

	DirEntry(String path, int nodeKind, long size, boolean hasProps, long lastChangedRevision, long lastChanged, String lastAuthor)
	{
		this.path = path;
		this.nodeKind = nodeKind;
		this.size = size;
		this.hasProps = hasProps;
		this.lastChangedRevision = lastChangedRevision;
		this.lastChanged = lastChanged;
		this.lastAuthor = lastAuthor;
	}

	public String getPath()
	{
		return path;
	}

	/**
	 * Returns the last time the file was changed revision number.
	 */
	public Date getLastChanged()
	{
		return new Date(lastChanged/1000);
	}

	/**
	 * @return revision
	 */
	public Revision.Number getLastChangedRevision()
	{
		return new Revision.Number(lastChangedRevision);
	}

	public boolean getHasProps()
	{
		return hasProps;
	}

	public String getLastAuthor()
	{
		return lastAuthor;
	}

	public int getNodeKind()
	{
		return nodeKind;
	}

	/**
	 * @return length of file text, or 0 for directories
	 */
	public long getSize()
	{
		return size;
	}

}
