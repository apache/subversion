package org.tigris.subversion.lib;

/**
 * Class for handling text deltas
 *
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
 * Remark: this class corresponds to the subversion c api type
 * `svn_txdelta_window_t'
 */
public class TextdeltaWindow {
  public final long sviewOffset;
  public final long sviewLen;
  public final long tviewLen;
  public final TextdeltaOp ops[];
  public final Object newData;

  public TextdeltaWindow(long sviewOffset, long sviewLen, long tviewLen,
    TextdeltaOp ops[], Object newData) {
    this.sviewOffset = sviewOffset;
    this.sviewLen = sviewLen;
    this.tviewLen = tviewLen;
    this.ops = ops;
    this.newData = newData;
  }

}
