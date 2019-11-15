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

  def _retrieve_swig_value(self, name, value):
    # If we got back a different object than we have cached, we need to copy
    # all our metadata into it, so that it looks identical to the one
    # originally set.
    members = self.__dict__.get('_members')
    if members is not None and name in members:
      _copy_metadata_deep(value, members[name])

    # Verify that the new object is good
    _assert_valid_deep(value)

    return value

  # Attribute access must be intercepted to ensure that objects coming from
  # read attribute access match those that are set with write attribute access.
  # Specifically the metadata, such as the associated apr_pool object, should
  # match the originally assigned object.
  #
  # For classic classes it is enough to use __getattr__ to intercept swig
  # derived attributes. However, with new style classes SWIG makes use of
  # descriptors which mean that __getattr__ is never called. Therefore,
  # __getattribute__ must be used for the interception.

  if _newclass:
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

      value = _get_instance_attr(self, name)
      fn = object.__getattribute__(self, '_retrieve_swig_value')
      return fn(name, value)
  else:
    def __getattr__(self, name):
      """Get an attribute from this object"""
      self.assert_valid()

      value = _swig_getattr(self, self.__class__, name)

      return self._retrieve_swig_value(name, value)

  def __setattr__(self, name, value):
    """Set an attribute on this object"""
    self.assert_valid()

    # Save a copy of the object, so that the garbage
    # collector won't kill the object while it's in
    # SWIG-land
    self.__dict__.setdefault("_members",{})[name] = value

    return _set_instance_attr(self, name, value)
