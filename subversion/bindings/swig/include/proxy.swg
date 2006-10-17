/*
 * proxy.swg :  SWIG include file for defining automatic proxy classes
 *
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

#ifdef SWIGPYTHON

/* Note: See the big comment at the top of proxy_apr.swg for details on
 *       how this _is_valid stuff actually works.  It's part of the magic
 *       that lets us gracefully handle objects that are allocated from
 *       a pool that's been cleared or destroyed.
 */

/* Default code for all wrapped proxy classes in Python */
%define %proxy_pythoncode(TYPE)
%pythoncode {
  def set_parent_pool(self, parent_pool=None):
    """Create a new proxy object for TYPE"""
    import libsvn.core, weakref
    self.__dict__["_parent_pool"] = \
      parent_pool or libsvn.core.application_pool;
    if self.__dict__["_parent_pool"]:
      self.__dict__["_is_valid"] = weakref.ref(
        self.__dict__["_parent_pool"]._is_valid)

  def assert_valid(self):
    """Assert that this object is using valid pool memory"""
    if "_is_valid" in self.__dict__:
      assert self.__dict__["_is_valid"](), "Variable has already been deleted"

  def __getattr__(self, name):
    """Get an attribute from this object"""
    self.assert_valid()

    value = _swig_getattr(self, self.__class__, name)

    # Now, if this item has changed, SWIG must have generated a new object.
    # Copy the pool metadata from the old object over to the new one.
    members = self.__dict__.get("_members")
    if members is not None:
      old_value = members.get(name)
      if (old_value is not None and value is not None and
          value is not old_value):
        try:
          value.__dict__.update(old_value.__dict__)
        except AttributeError:
          pass

    # Verify that the new object is good
    if hasattr(value, "assert_valid"):
      value.assert_valid()

    return value

  def __setattr__(self, name, value):
    """Set an attribute on this object"""
    self.assert_valid()

    # Save a copy of the object, so that the garbage
    # collector won't kill the object while it's in
    # SWIG-land
    self.__dict__.setdefault("_members",{})[name] = value

    return _swig_setattr(self, self.__class__, name, value)
}
%enddef

/* Define a proxy for wrapping an existing struct */
%define %proxy(TYPE)
%extend TYPE {
%proxy_pythoncode(TYPE);
}
%enddef

/* Define a proxy class for wrapping an opaque struct */
%define %opaque_proxy(TYPE)
struct TYPE {
%extend {
%proxy_pythoncode(TYPE);
}
}
%enddef

/* Treat typemapped function pointers as objects, which have a bound
 * __call__ method. This mapping depends on the function pointer being
 * typemapped as a CALLABLE_CALLBACK. */
%define %funcptr_proxy(TYPE, INVOKE)
%nodefault TYPE;
struct TYPE {
%extend {
%proxy_pythoncode(TYPE);
%pythoncode %{
  def __call__(self, *args):
    return INVOKE(self, *args)
%}
}
};

%enddef

/* Add member functions to objects so that their function pointers can
 * be invoked.
 *
 * Unlike the CALLABLE_CALLBACKS, these member functions don't have or
 * need typemaps, because the underlying C/SWIG object for the callback
 * is hidden.
 */
%define %funcptr_member_proxy(TYPE, MEMBER, INVOKE)
%extend TYPE {
%pythoncode %{
  def MEMBER(self, *args):
    return INVOKE(self, *args)
%}
}
%enddef


#endif
