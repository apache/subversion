#!/usr/bin/env python2
#
# gen-pipe.py -- generate pipe client/server stubs
#
# USAGE:
#    gen-pipe.py IDL-FILE CLIENT-HEADER CLIENT-IMPL SERVER-HEADER SERVER-IMPL
#

import os, sys
import ConfigParser


def indent(l,n=2):
  return map(lambda x: " "*n + x, l)

class Callable:
  def __init__(self, callable):
    self.__call__ = callable

class Type:
  types = {}

  def for_name(parser, name):
    return Type.types.setdefault(name,Type(parser,name))
  for_name = Callable(for_name)

  def __init__(self, parser, name):
    self.array = 0
    if name.endswith("[]"):
      name = name[0:len(name)-2]
      self.array = 1
    if name not in parser.options('types'):
       print 'Missing type definition: ' + name
       sys.exit(1)
    self.name = name
    self.type = parser.get('types', name)

  def __str__(self):
    if not self.array:
      return self.type
    return self.type + "*"*self.array

  def __func_suffix(self):
    if self.array:
      return self.name + "_array"
    return self.name

  def marshall(self):
    return "svn_pipe_marshall_" + self.__func_suffix()
        
  def unmarshall(self):
    return "svn_pipe_unmarshall_" + self.__func_suffix()



class Param:
  def __init__(self, name, type):
    self.name = name
    self.type = type

  def decl(self, out):
    t = str(self.type)
    s = ""
    if not t.endswith("*"):
      s += " "
    if out:
      s += "*"
    return "".join([
        t,
        s,
        self.name
    ]);

  def in_decl(self):
    return self.decl(0)

  def out_decl(self):
    return self.decl(1)

  def marshall(self, var):
    return self.type.marshall() + "(" + self.name + ", &" + var + ")";

  def unmarshall(self, var, ref):
    s = ref and "&" or ""
    return self.type.unmarshall() + "(" + var + ", " + s + self.name + ")";

class Func:
  functions = {}

  def for_name(parser, name, type):
    if name in Func.functions:
      f = Func.functions[name]
      return Func.functions[name]
    return Func(parser, name, type)
  for_name = Callable(for_name)

  def funcs():
    return Func.functions.values()
  funcs = Callable(funcs)

  def methods():
    return [x for x in Func.funcs() if not x.type]
  methods = Callable(methods)

  def callbacks():
    return [x for x in Func.funcs() if x.type]
  callbacks = Callable(callbacks)


  def __init__(self, parser, name, type):
    if not parser.has_section(name):
      print 'Missing method definition: ' + name
      sys.exit(1)
    Func.functions[name]=self
    self.name = name
    self.type = type
    self.ins = []
    self.outs = []
    self.cbs = []
    self.__params(parser, 'in', self.ins)
    self.__params(parser, 'out', self.outs)
    if parser.has_option(name, 'callbacks'):
      self.cbs = map(lambda x:
                       Func.for_name(parser,
                                     x,
                                     1),
                       parser.get(name, 'callbacks').split())
                   
  def __params(self, parser, option, params):
    if not parser.has_option(self.name, option):
      return
    for param in parser.get(self.name, option).split():
      idx = param.find(")")
      if idx == -1:
        type = param
        name = param
      else:
        type = param[1:idx]
        name = param[idx+1:]
      p = Param(name,Type.for_name(parser, type))
      params.append(p)


  def add_cb_param(self, param):
    return self.add_param(param, "CB_ERR")

  def add_param(self, param, err = "METHOD_ERR"):
    return [
        err + "("+param.marshall("xmlrpc_value")+");",
        "XMLRPC_SetValueID(xmlrpc_value,",
        '                  "' + param.name + '",',
        "                  "  + str(len(param.name)) + ');',
        "XMLRPC_AddValueToVector(parameters, xmlrpc_value);"
    ]

  def get_cb_param(self, param):
    return self.get_param(param, 1, "CB_ERR")

  def get_param(self, param, ref=0, err="METHOD_ERR"):
    return [
        'xmlrpc_value = XMLRPC_VectorGetValueWithID(parameters, "' + param.name + '");',
        err + "("+param.unmarshall("xmlrpc_value", ref)+");",
        "XMLRPC_CleanupValue(xmlrpc_value);"
    ]
    
  def register(self, callback):
    return [
        'XMLRPC_ServerRegisterMethod(server,',
        '                            "' + callback.name + '",',
        '                            ' + callback.name + '_callback);'
    ]

  def call(self):
    s = ",\n" + " "*(len(self.name)+10)
    p = ["userdata"] + \
        map(lambda x: x.name, self.ins) + \
        map(lambda x: "&"+x.name, self.outs)
    return "  CB_ERR(" + self.name + "(" + s.join(p) + "));"
       
  def callback_impl(self):
    lines = [
        "static XMLRPC_VALUE",
        self.name+"_callback(XMLRPC_SERVER server,",
        "                    XMLRPC_REQUEST request,",
        "                    void *userdata)",
        "{",
        "  XMLRPC_VALUE parameters;",
        "  XMLRPC_VALUE xmlrpc_value;",
        ""]
    lines += indent(map(lambda x: x.in_decl() + ";", self.ins + self.outs))
    lines += [""]
    lines += indent(reduce(list.__add__,
                           map(self.get_cb_param, self.ins),
                           []))
    lines += ["  XMLRPC_CleanupValue(parameters);",
              ""]
    lines.append(self.call())
    lines += ["",
              '  parameters = XMLRPC_CreateVector("parameters",',
              "                                   xmlrpc_vector_struct);"]
    lines += indent(reduce(list.__add__,
                           map(self.add_cb_param, self.outs),
                           []))
    lines += [
        "  return parameters;",
        "}",
        ""]
    return "\n".join(lines)

  def client_impl(self):
    lines = [
        self.header(),
        "{",
        "  XMLRPC_SERVER server;",
        "  XMLRPC_REQUEST request;",
        "  XMLRPC_REQUEST response;",
        "  XMLRPC_VALUE parameters;",
        "  XMLRPC_VALUE xmlrpc_value;",
        "  STRUCT_XMLRPC_REQUEST_OUTPUT_OPTIONS output;",
        "  char *buffer;",
        "  int len;",
        "  apr_size_t size;",
        "",
        "  request = XMLRPC_RequestNew();",
        '  XMLRPC_RequestSetMethodName(request, "' + self.name + '");',
        "  XMLRPC_RequestSetRequestType(request, xmlrpc_request_call);",
        "",
        "  memset(&output, 0, sizeof(output));",
        "  output.version = xmlrpc_version_1_0;",
        "  XMLRPC_RequestSetOutputOptions(request, &output);",
        "",
        '  parameters = XMLRPC_CreateVector("parameters",',
        "                                   xmlrpc_vector_struct);",
        "  XMLRPC_RequestSetData(request, parameters);",
        ""]

    lines += indent(reduce(list.__add__,
                           map(self.add_param, self.ins),
                           []))

    if self.cbs:
      lines.append("  server = XMLRPC_ServerCreate();")
      lines += indent(reduce(list.__add__,
                             map(self.register, self.cbs),
                             [])); 
    lines += [
        "",
        "  for (;;)",
        "    {",
        "      buffer = XMLRPC_REQUEST_ToXML(request, &len);",
        "      if (!buffer) /* !!! */",
        "        abort();",
        "      METHOD_ERR(svn_pipe_send(pipe, buffer, len, pool));",
        "      XMLRPC_FreeRequest(request, 1);",
        ""
        "      METHOD_ERR(svn_pipe_receive(pipe, &buffer, &size, pool));",
        "      response = XMLRPC_REQUEST_FromXML(buffer, size, NULL);",
        "      if (!response) /* !!! */",
        "        abort();",
        "      if (XMLRPC_RequestGetRequestType(response) !=",
        "          xmlrpc_request_call)",
        "        break;"]
  
    if self.cbs:
      lines += [
        "      xmlrpc_value = XMLRPC_ServerCallMethod(server, response, userdata);",
        "      XMLRPC_FreeRequest(response, 1);",
        "      request = XMLRPC_RequestNew();",
        "      XMLRPC_RequestSetData(request, xmlrpc_value);",
        "      XMLRPC_RequestSetRequestType(request, xmlrpc_request_response);",
        "      XMLRPC_RequestSetOutputOptions(request, &output);"
        ]
    else:
      lines += [
        "      /* !!! */",
        "      abort();"]

    lines += [
        "    }",
        ""]
    lines += indent(reduce(list.__add__,
                           map(self.get_param, self.outs),
                           []))
    if self.cbs:
      lines.append('  XMLRPC_ServerFree(server);')

    lines += [
        "  XMLRPC_RequestFree(response, 1);",
        "  return SVN_NO_ERROR;",
        "}",
        ""]
    return "\n".join(lines)

  def static_proto(self):
    return "static " + self.proto()

  def proto(self):
    return self.header() + ";\n"

  def paramlist(self):
    return map(lambda x: x.in_decl(), self.ins) + \
           map(lambda x: x.out_decl(), self.outs)

  def header(self, method=0):
    s = " "*(len(self.name)+1)
    p = []
    p.append("svn_pipe_t *pipe")
    p.append("void *userdata")
    p.extend(map(lambda x: x.in_decl(), self.ins))
    p.extend(map(lambda x: x.out_decl(), self.outs))
    p.append("apr_pool_t *pool")
    return "svn_error_t *\n" + \
            self.name + "(" + \
            (",\n"+s).join(p) + ")"


def footer():
    return "\n".join([
      '',
      '/* ----------------------------------------------------------------',
      ' * local variables:',
      ' * eval: (load-file "../../tools/dev/svn-dev.el")',
      ' * end:',
      ' */',
      ''])

def client_header():
  return "\n".join([
    "/* DO NOT EDIT -- AUTOMATICALLY GENERATED */",
    "#ifndef __SVN_PIPE_PROTO_H__",
    "#define __SVN_PIPE_PROTO_H__",
    "",
    "#ifdef __cplusplus",
    'extern "C" {',
    "#endif /* __cplusplus */",
    "",
    "\n".join(map(lambda x: x.proto(), Func.methods())),
    "",
    "#ifdef __cplusplus",
    "}",
    "#endif /* __cplusplus */",
    "",
    "#endif /* __SVN_SVNPIPE_PIPE_CLIENT_H__ */",
    footer()])

def client_impl():
  return "\n".join([
    '#include "xmlrpc.h"',
    '',
    '#include "svn_pipe.h"',
    '#include "svn_error.h"',
    '#include "svn_types.h"', 
    '#include "svn_string.h"', 
    '#include "svn_pipe_proto.h"',
    "",
    "\n".join(map(lambda x: x.callback_impl(), Func.callbacks())),
    "",
    "\n".join(map(lambda x: x.client_impl(), Func.methods())),
    footer()])

def main(idl, client_h, client_c, server_h, server_c):
  parser = ConfigParser.ConfigParser()
  parser.read(idl)

  for name in parser.get('methods', 'names').split():
    Func.for_name(parser, name, 0) 

  for name in parser.get('callbacks', 'names').split():
    callback = Func.for_name(parser, name, 1) 
    for f in Func.methods():
      f.cbs.append(callback)

  fd = open(client_h,"w") 
  fd.write(client_header())
  fd.close()

  fd = open(client_c,"w") 
  fd.write(client_impl())
  fd.close()

#  server_h = open("pipe_server.h", "w")
#  write_server_h(server_h)
#  server_h.close()

#  server_c = open("pipe_server.c", "w")
#  write_server_c(server_c)
#  server_c.close()

if __name__ == '__main__':
  argc = len(sys.argv)

  if argc != 6 :
    print "usage:  gen-pipe.py IDL-FILE CLIENT-HEADER CLIENT-IMPL SERVER-HEADER SERVER-IMPL\n"
    sys.exit(1)

  main(*sys.argv[1:])

### End of file.
# local variables:
# eval: (load-file "../../tools/dev/svn-dev.el")
# end:
