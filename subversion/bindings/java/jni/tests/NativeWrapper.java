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
import java.util.Hashtable;
import java.util.Date;
import org.tigris.subversion.lib.*;

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
     * @param date milliseconds since Januar 1, 1970 00:00:00
     */
    public static native Date dateCreate(long date);

    /**
     * wrapper for function "date__create_from_apr_time_t" (date.h)
     */
    public static native Date dateCreateFromAprTimeT(long time);

    /**
     * wrapper for function "entry__create" (entry.h)
     */
    public static native Entry entryCreate();

    /**
     * setter for "url" in Entry class
     */
    public static native void entrySetUrl(Entry entry, String url);

    /**
     * setter for "revision" in Entry class
     */
    public static native void entrySetRevision(Entry entry, Revision revision);
    
    /**
     * setter for "nodekind" in Entry class
     */
     public static native void entrySetKind(Entry entry, Nodekind kind);

    /**
     * setter for "schedule" in Entry class
     */
    public static native void entrySetSchedule(Entry entry, Schedule schedule);
    
    /**
     * setter for "conflicted" in Entry class
     */
    public static native void entrySetConflicted(Entry entry, boolean conflicted);
       
    /**
     * setter for "copied" in Entry class
     */
    public static native void entrySetCopied(Entry entry, boolean copied);

    /**
     * setter for "texttime" in Entry class
     */
    public static native void entrySetTexttime(Entry entry, Date texttime);
    
    /**
     * setter for "proptime" in Entry class
     */
    public static native void entrySetProptime(Entry entry, Date proptime);

    /**
     * setter for "attributes" in Entry class
     */
    public static native void entrySetAttributes(Entry entry, Hashtable attributes);
    
    /**
     * wrapper for function "hashtable__create" (hashtable.h)
     */
    public static native Hashtable hashtableCreate();

    /**
     * wrapper for function "hashtable__put" (hashtable.h)
     */
    public static native void hashtablePut(Hashtable hashtable, Object key, Object value);

    /**
     * wrapper for function "misc__throw_exception_by_name" (misc.h)
     */
    public static native void miscThrowExceptionByName(String name, String msg);
    
    /**
     * wrapper for function "status__create" (status.h)
     */
    public static native Status statusCreate();

    /**
     * wrapper for function "status__set_entry" (status.h)
     */
    public static native void statusSetEntry(Status status, Entry entry);

    /**
     * wrapper for function "status__set_text_status" (status.h)
     */
    public static native void statusSetTextStatus(Status status, StatusKind text_status);

    /**
     * wrapper for function "status__set_prop_status" (status.h)
     */
    public static native void statusSetPropStatus(Status status, StatusKind prop_status);

    /**
     * wrapper for function "status__set_copied" (status.h)
     */
    public static native void statusSetCopied(Status status, boolean copied);

    /**
     * wrapper for function "status__set_locked" (status.h)
     */
    public static native void statusSetLocked(Status status, boolean locked);
    
    /**
     * wrapper for function "status__set_repos_text_status" (status.h)
     */
    public static native void statusSetReposTextStatus(Status status, StatusKind repos_text_status);

    /**
     * wrapper for function "status__set_repos_prop_status" (status.h)
     */
    public static native void statusSetReposPropStatus(Status status, StatusKind repos_prop_status);

    /**
     * wrapper for function "nodekind__create" (nodekind.h)
     */
    public static native Nodekind nodekindCreate(int kind);

    /**
     * wrapper for function "revision__create" (revision.h)
     */
    public static native Revision revisionCreate(long rev);

  /** 
   * wrapper for function "statuskind__create" (statuskind.h)
   */
  public static native StatusKind statuskindCreate(int kind);

  
}


