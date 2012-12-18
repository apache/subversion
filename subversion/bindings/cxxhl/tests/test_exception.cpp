#include <algorithm>
#include <iomanip>
#include <ios>
#include <iostream>

#include "svncxxhl.hpp"

#include <apr.h>
#include "svn_error.h"

namespace {
void trace(const svn::error::message& msg)
{
  std::cout << "    ";
  if (msg.first)
    std::cout << "test_exception: E"
              << std::setw(6) << std::setfill('0') << std::right
              << msg.first << ':' << ' ';
  std::cout << msg.second << std::endl;
}
} // anonymous namespace

int main()
{
  apr_initialize();

  try
    {
      svn_error_t* err;
      err = svn_error_create(SVN_ERR_TEST_FAILED, NULL, "original message");
      err = svn_error_create(SVN_ERR_BASE, err, "wrapper message");
      err = svn_error_create(SVN_ERR_CANCELLED, err, NULL);
      err = svn_error_create(SVN_ERR_CANCELLED, err, NULL);
      err = svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, err, NULL);
      err = svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, err, NULL);
      err = svn_error_create(SVN_ERR_CANCELLED, err, NULL);
      err = svn_error_trace(err);
      svn::error::throw_svn_error(err);
    }
  catch (const svn::error& err)
    {
      typedef svn::error::message_list message_list;
      std::cout << "Traced Messages:" << std::endl;
      message_list ml = err.traced_messages();
      std::for_each(ml.begin(), ml.end(), trace);
      std::cout << "Just Messages:" << std::endl;
      ml = err.messages();
      std::for_each(ml.begin(), ml.end(), trace);
    }

  return 0;
}
