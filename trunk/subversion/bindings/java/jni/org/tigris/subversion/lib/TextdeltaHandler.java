package org.tigris.subversion.lib;

/**
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
 *  Remark: this class corresponds to the subversion c api type
 * 'svn_txdelta_window_handler_t'
 */

import org.tigris.subversion.SubversionException;

public interface TextdeltaHandler {
  public void handleTextdeltaWindow(TextdeltaWindow window, Object baton)
    throws SubversionException;
}
