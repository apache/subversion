require "my-assertions"
require "util"

require "svn/core"

class SvnCoreTest < Test::Unit::TestCase
  include SvnTestUtil
  
  def setup
    @repos_path = File.join("test", "repos")
    @config_path = File.join("test", "config")
    @config_file = File.join(@config_path, Svn::Core::CONFIG_CATEGORY_CONFIG)
    @servers_file = File.join(@config_path, Svn::Core::CONFIG_CATEGORY_SERVERS)
    setup_repository(@repos_path)
    setup_config
  end

  def teardown
    GC.enable
    teardown_repository(@repos_path)
    teardown_config
  end
  
  def test_binary_mime_type?
    assert(Svn::Core.binary_mime_type?("image/png"))
    assert(!Svn::Core.binary_mime_type?("text/plain"))
  end

  def test_not_new_auth_provider_object
    assert_raise(NoMethodError) do
      Svn::Core::AuthProviderObject.new
    end
  end

  def test_version_to_x
    major = 1
    minor = 2
    patch = 3
    tag = "-dev"
    ver = Svn::Core::Version.new(major, minor, patch, tag)
    
    assert_equal("#{major}.#{minor}.#{patch}#{tag}", ver.to_s)
    assert_equal([major, minor, patch, tag], ver.to_a)
  end
  
  def test_version_valid?
    assert_true(Svn::Core::Version.new(1, 2, 3, "-devel").valid?)
    assert_true(Svn::Core::Version.new(nil, nil, nil, "").valid?)
    assert_true(Svn::Core::Version.new.valid?)
  end
  
  def test_version_equal
    major = 1
    minor = 2
    patch = 3
    tag = ""
    ver1 = Svn::Core::Version.new(major, minor, patch, tag)
    ver2 = Svn::Core::Version.new(major, minor, patch, tag)
    ver3 = Svn::Core::Version.new
    assert_equal(ver1, ver2)
    assert_not_equal(ver1, ver3)
  end

  def test_version_compatible?
    major = 1
    minor = 2
    patch = 3

    my_tag = "-devel"
    lib_tag = "-devel"
    ver1 = Svn::Core::Version.new(major, minor, patch, my_tag)
    ver2 = Svn::Core::Version.new(major, minor, patch, lib_tag)
    ver3 = Svn::Core::Version.new(major, minor, patch, lib_tag + "x")
    assert_true(ver1.compatible?(ver2))
    assert_false(ver1.compatible?(ver3))

    my_tag = "-devel"
    lib_tag = ""
    ver1 = Svn::Core::Version.new(major, minor, patch, my_tag)
    ver2 = Svn::Core::Version.new(major, minor, patch, lib_tag)
    ver3 = Svn::Core::Version.new(major, minor, patch - 1, lib_tag)
    assert_false(ver1.compatible?(ver2))
    assert_true(ver1.compatible?(ver3))

    tag = ""
    ver1 = Svn::Core::Version.new(major, minor, patch, tag)
    ver2 = Svn::Core::Version.new(major, minor, patch, tag)
    ver3 = Svn::Core::Version.new(major, minor, patch - 1, tag)
    ver4 = Svn::Core::Version.new(major, minor + 1, patch, tag)
    ver5 = Svn::Core::Version.new(major, minor - 1, patch, tag)
    assert_true(ver1.compatible?(ver2))
    assert_true(ver1.compatible?(ver3))
    assert_true(ver1.compatible?(ver4))
    assert_false(ver1.compatible?(ver5))
  end

  def test_auth_parameter
    key = "key"
    value = "value"
    auth = Svn::Core::AuthBaton.new
    assert_nil(auth[key])
    auth[key] = value
    assert_equal(value, auth[key])

    assert_raise(TypeError) do
      auth[key] = 1
    end
  end
  
  def test_pool_GC
    GC.disable

    made_number_of_pool = 100
    pools = []
    
    gc
    before_number_of_pools = number_of_pools
    made_number_of_pool.times do
      pools << used_pool
    end
    gc
    current_number_of_pools = number_of_pools
    created_number_of_pools = current_number_of_pools - before_number_of_pools
    assert_operator(made_number_of_pool, :<=, current_number_of_pools)

    gc
    pools.clear
    before_number_of_pools = number_of_pools
    gc
    current_number_of_pools = number_of_pools
    recycled_number_of_pools = before_number_of_pools - current_number_of_pools
    assert_operator(made_number_of_pool * 0.8, :<=, recycled_number_of_pools)
  end

  def test_config
    assert_equal([
                   Svn::Core::CONFIG_CATEGORY_CONFIG,
                   Svn::Core::CONFIG_CATEGORY_SERVERS,
                 ].sort,
                 Svn::Core::Config.config(@config_path).keys.sort)

    config = Svn::Core::Config.read(@config_file)
    section = Svn::Core::CONFIG_SECTION_HELPERS
    option = Svn::Core::CONFIG_OPTION_DIFF_CMD
    value = "diff"
    
    assert_nil(config.get(section, option))
    config.set(section, option, value)
    assert_equal(value, config.get(section, option))
  end
  
  def test_config_bool
    config = Svn::Core::Config.read(@config_file)
    section = Svn::Core::CONFIG_SECTION_MISCELLANY
    option = Svn::Core::CONFIG_OPTION_ENABLE_AUTO_PROPS
    
    assert(config.get_bool(section, option, true))
    config.set_bool(section, option, false)
    assert(!config.get_bool(section, option, true))
  end

  def test_config_each
    config = Svn::Core::Config.read(@config_file)
    section = Svn::Core::CONFIG_SECTION_HELPERS
    options = {
      Svn::Core::CONFIG_OPTION_DIFF_CMD => "diff",
      Svn::Core::CONFIG_OPTION_DIFF3_CMD => "diff3",
    }

    infos = {}
    config.each_option(section) do |name, value|
      infos[name] = value
      true
    end
    assert_equal({}, infos)

    section_names = []
    config.each_section do |name|
      section_names << name
      true
    end
    assert_equal([], section_names)

    options.each do |option, value|
      config.set(section, option, value)
    end

    config.each_option(section) do |name, value|
      infos[name] = value
      true
    end
    assert_equal(options, infos)

    config.each_section do |name|
      section_names << name
      true
    end
    assert_equal([section], section_names)
  end

  def test_config_find_group
    config = Svn::Core::Config.read(@config_file)
    section = Svn::Core::CONFIG_SECTION_HELPERS
    option = Svn::Core::CONFIG_OPTION_DIFF_CMD
    value = "diff"

    assert_nil(config.find_group(value, section))
    config.set(section, option, value)
    assert_equal(option, config.find_group(value, section))
  end

  def test_config_get_server_setting
    group = "group1"
    host_prop_name = "http-proxy-host"
    host_prop_value = "*.example.com"
    default_host_value = "example.net"
    port_prop_name = "http-proxy-port"
    port_prop_value = 8080
    default_port_value = 1818
    
    File.open(@servers_file, "w") do |f|
      f.puts("[#{group}]")
    end

    config = Svn::Core::Config.read(@servers_file)
    assert_equal(default_host_value,
                 config.get_server_setting(group,
                                           host_prop_name,
                                           default_host_value))
    assert_equal(default_port_value,
                 config.get_server_setting_int(group,
                                               port_prop_name,
                                               default_port_value))
    
    File.open(@servers_file, "w") do |f|
      f.puts("[#{group}]")
      f.puts("#{host_prop_name} = #{host_prop_value}")
      f.puts("#{port_prop_name} = #{port_prop_value}")
    end

    config = Svn::Core::Config.read(@servers_file)
    assert_equal(host_prop_value,
                 config.get_server_setting(group,
                                           host_prop_name,
                                           default_host_value))
    assert_equal(port_prop_value,
                 config.get_server_setting_int(group,
                                               port_prop_name,
                                               default_port_value))
  end

  def test_config_auth_data
    cred_kind = Svn::Core::AUTH_CRED_SIMPLE
    realm_string = "sample"
    assert_nil(Svn::Core::Config.read_auth_data(cred_kind,
                                                realm_string,
                                                @config_path))
    Svn::Core::Config.write_auth_data({},
                                      cred_kind,
                                      realm_string,
                                      @config_path)
    assert_equal({Svn::Core::CONFIG_REALMSTRING_KEY => realm_string},
                 Svn::Core::Config.read_auth_data(cred_kind,
                                                  realm_string,
                                                  @config_path))
  end
  
  private
  def used_pool
    pool = Svn::Core::Pool.new
    now = Time.now.gmtime
    apr_time = now.to_i * Svn::Util::MILLION + now.usec
    Svn::Core.time_to_human_cstring(apr_time, pool)
    pool
  end

  def number_of_pools
    ObjectSpace.each_object(Svn::Core::Pool) {}
  end

  def gc
    before_disabled = GC.enable
    GC.start
    GC.disable if before_disabled
  end
end
