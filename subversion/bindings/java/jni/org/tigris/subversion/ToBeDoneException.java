package org.tigris.subversion;

/**
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
 */

/**
 * Exception that might be thrown whenever you want to let
 * the user of the class/method know that there is some
 * unfinished work that has to be done
 */
public class ToBeDoneException extends SubversionException 
{
    public ToBeDoneException(String message)
	{
	    super(message);
	}
    
    public ToBeDoneException() 
	{
	    this(null);
	}
}
