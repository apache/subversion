/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 * @endcopyright
 *
 * @file Iterator.h
 * @brief Interface of the class Iterator
 */

#ifndef JAVAHL_ITERATOR_H
#define JAVAHL_ITERATOR_H

#include "JNIUtil.h"

/**
 * Encapsulates an immutable java.lang.Iterator implementation.
 */
class Iterator
{
public:
  Iterator(jobject jiterable);
  ~Iterator();
  bool hasNext() const;
  jobject next() const;

protected:
  Iterator(jobject jiterable, bool);

private:
  bool m_persistent;
  jobject m_jiterator;
};


/**
 * Like Iterator, but the implementation will hold a global reference
 * to the internal iterator object to protect it across JNI calls.
 */
class PersistentIterator : protected Iterator
{
public:
  PersistentIterator(jobject jiterable) : Iterator(jiterable, true) {}
  bool hasNext() const { return Iterator::hasNext(); }
  jobject next() const { return Iterator::next(); }
};

#endif  // JAVAHL_ITERATOR_H
