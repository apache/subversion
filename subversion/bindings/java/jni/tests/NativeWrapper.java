/**
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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
 *
 */
import java.util.Vector;
import java.util.Date;

/**
 * This classed is used for the unit tests. All of the C helper
 * functions for the Java Subversion binding should be reached
 * with this class. 
 *
 * Sometimes this is not possible, because
 * the class needs non-trivial native parameters. In this case
 * either simple type parameters are used or the methode
 * here designs a special case with no parameter
 */
public class NativeWrapper
{
    static
	{
	    System.loadLibrary("svn_jni_nativewrapper");
	}

    /**
     * Calls the function "vector__create" (vector.h)
     *
     * @return new, empty Vector instance
     */
    public static native Vector vectorCreate();

    /**
     * Calls the function "vector__add" (vector.h)
     *
     * @param vector instance of a vector that should be used for
     *               the operation
     * @param object
     */
    public static native void vectorAdd(Vector vector, Object object);

    /**
     * Create a new date from a long value.
     *
     * @param date milliseconds since Januar 1, 1970 00:00
     */
    public static native Date dateCreate(long date);
}

/* 
 * local variables:
 * eval: (load-file "../../../../../../../svn-dev.el")
 * end: 
 */

