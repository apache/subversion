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

  def __getattribute__(self, name):
    """Manage access to all attributes of this object."""

    # Start by mimicing __getattr__ behavior: immediately return __dict__ or
    # items directly present in __dict__
    mydict = object.__getattribute__(self, '__dict__')
    if name == "__dict__":
      return mydict

    if name in mydict:
      return mydict[name]

    object.__getattribute__(self, 'assert_valid')()

    try:
      value = object.__getattribute__(self, name)
    except AttributeError:
      value = _swig_getattr(self,
                            object.__getattribute__(self, '__class__'),
                            name)

    # If we got back a different object than we have, we need to copy all our
    # metadata into it, so that it looks identical
    try:
      members = object.__getattribute__(self, '_members')
      if name in members:
          _copy_metadata_deep(value, members[name])
          # Verify that the new object is good
    except AttributeError:
      pass

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
