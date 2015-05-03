from ctypes import *
import sys
import re
import subprocess as sp
import tempfile

libstdcxx = CDLL("libstdc++.so.6")
libstdcxx.__cxa_demangle.argtypes = [c_char_p, c_char_p, POINTER(c_size_t), POINTER(c_int)]
libstdcxx.__cxa_demangle.restype = c_char_p
onlyNameRE = re.compile(r'^(construction )?vtable for (.*)')

def demangle(mangled, onlyName=False, consVtablePrefix=False):
  """
  Demangles the given C++ symbol name.
  onlyName:         used to remove the "vtable for..." part
  consVtablePrefix: returned string starts with 0 for normal vtables and 1 for 
                    construction vtables (it also assumes onlyName=True)
  """
  if "@" in mangled:
    mangled = mangled[:mangled.index("@")]
  output_len = c_size_t(0)
  status = c_int(0)
  print mangled
  demangled = libstdcxx.__cxa_demangle(c_char_p(mangled),
                                 None,
                                 byref(output_len),
                                 byref(status))

  if status.value == 0:
    assert demangled != None

    if onlyName:
      m = onlyNameRE.match(demangled)
      assert m is not None

      return m.group(2)
    elif consVtablePrefix:
      m = onlyNameRE.match(demangled)
      assert m is not None

      if m.group(1) is None:
        return "0 " + m.group(2)
      else:
        return "1 " + m.group(2)
    else:
      return demangled
  else:
    if (mangled != '__cxa_pure_virtual'):
      sys.stderr.write("WARN: Couldn't demangle the given symbol %s\n" % mangled)
    return mangled

def demangle_cloud_dot(mangled):
  f = tempfile.NamedTemporaryFile(mode='w')
  f.write(mangled)
  f.flush()

  p1 = sp.Popen(["cat", f.name], stdout=sp.PIPE)
  p2 = sp.Popen(["c++filt"], stdin=p1.stdout, stdout=sp.PIPE)
  p1.stdout.close()  # Allow p1 to receive a SIGPIPE if p2 exits.
  out = p2.communicate()[0]

  out = out.replace("construction vtable for", "cons")
  out = out.replace("vtable for ", "")
  f.close()
  return out

if __name__ == '__main__':
  if len(sys.argv) == 2:
    print demangle(sys.argv[1])
  elif len(sys.argv) == 3:
    print demangle(sys.argv[1], onlyName=bool(int(sys.argv[2])))


