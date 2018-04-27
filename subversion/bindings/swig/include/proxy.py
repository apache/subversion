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

    # If we got back a different object than we have, we need to copy all our
    # metadata into it, so that it looks identical
    members = self.__dict__.get("_members")
    if members is not None:
      _copy_metadata_deep(value, members.get(name))

    # Verify that the new object is good
    _assert_valid_deep(value)

    return value

  def __setattr__(self, name, value):
    """Set an attribute on this object"""
    self.assert_valid()

    # Save a copy of the object, so that the garbage
    # collector won't kill the object while it's in
    # SWIG-land
    self.__dict__.setdefault("_members",{})[name] = value

    return _swig_setattr(self, self.__class__, name, value)
