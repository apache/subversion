package org.tigris.subversion.util;

/*
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

import org.tigris.subversion.swig.core;
import org.tigris.subversion.swig.SWIGTYPE_p_apr_pool_t;
import org.tigris.subversion.swig.SWIGTYPE_p_apr_allocator_t;

/**
 * Handles operations involving the native libraries composing
 * Subversion's implementation.
 *
 * @since Subversion 0.30
 */
public class NativeResources
{
    private static NativeResources instance;
    static
    {
        String className = System.getProperty(NativeResources.class.getName());
        if (className != null && className.length() > 0)
        {
            try
            {
                Class c = Class.forName(className);
                instance = (NativeResources) c.newInstance();
            }
            catch (Exception useDefault)
            {
                // Likely an UnsatisfiedLinkError.
                System.err.println("NativeResources '" + className + '\'' +
                                   " not available, falling back to '" +
                                   NativeResources.class.getName() + '\'');
            }
        }

        if (instance == null)
        {
            instance = new NativeResources();
        }
    }

    /**
     * Whether the native libraries have been loaded and initialized.
     */
    protected boolean isInitialized = false;

    /**
     * The APR pool used for native code memory management.
     */
    private SWIGTYPE_p_apr_pool_t pool;

    /**
     * Returns the single instance of this class for this
     * <code>ClassLoader</code> tree.
     */
    public static NativeResources getInstance()
    {
        return instance;
    }

    /**
     * Default constructor, scoped to prevent direct instantiation.
     * @see #getInstance()
     */
    protected NativeResources()
    {
    }

    /**
     * Loads and initializes the native libraries used by this
     * instance.
     */
    public synchronized void initialize()
    {
        if (this.isInitialized)
        {
            return;
        }

        // Load the SWIG-based JNI bindings.
        System.loadLibrary("swigjava");

        // Initialize the Apache Portable Runtime used by Subversion's
        // C implementation.
        core.apr_initialize();

        System.out.println("Allocating parent pool");
        this.pool = core.svn_pool_create((SWIGTYPE_p_apr_pool_t) null,
                                         (SWIGTYPE_p_apr_allocator_t) null);

        this.isInitialized = true;
    }

    public Object getMemoryManager()
    {
        return pool;
    }

    /**
     * Unloads and un-initializes the native libraries used by this
     * instance.
     */
    public synchronized void uninitialize()
    {
        System.out.println("Destroying parent pool");
        core.apr_pool_destroy(this.pool);

        core.apr_terminate();

        this.isInitialized = false;
    }

    /**
     * @see #uninitialize
     */
    protected void finalize()
        throws Throwable
    {
        try
        {
            super.finalize();
        }
        finally
        {
            uninitialize();
        }
    }
}
