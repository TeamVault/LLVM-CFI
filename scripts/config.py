#!/usr/bin/env python

import subprocess as sp
import re
import getpass
import socket
import os
import sys

ldRe = re.compile("GNU ld \(GNU Binutils for Ubuntu\) ([.\d]+)")

# When this is False, we remove any optimization flag from the compiler command
ENABLE_COMPILER_OPT = False

# Enabled linker flags
linker_flags = {
  "ENABLE_CHECKS"    : True, # interleave the vtables and add the range checks
  "ENABLE_LINKER_O2" : True, # runs O2 level optimizations during linking
  "ENABLE_LTO"       : True, # runs link time optimization passes
  "LTO_EMIT_LLVM"    : False, # emit bitcode rather than machine code
  "LTO_SAVE_TEMPS"   : True, # save bitcode before & after linker passes
}

compiler_flag_opt_map = {
  "ENABLE_CHECKS"    : "-femit-vtbl-checks",
}

# corresponding plugin options of the linker flags
linker_flag_opt_map = {
  "ENABLE_CHECKS"    : "-plugin-opt=emit-vtbl-checks",
  "ENABLE_LINKER_O2" : "-plugin-opt=run-O2-passes",
  "ENABLE_LTO"       : "-plugin-opt=run-LTO-passes",
  "LTO_EMIT_LLVM"    : "-plugin-opt=emit-llvm",
  "LTO_SAVE_TEMPS"   : "-plugin-opt=save-temps",
}

def ld_version():
  out = sp.check_output(['ld','-v'])
  m = ldRe.match(out)
  if m is not None:
    return m.group(1)
  else:
    raise Exception("Cannot parse ld version")

def get_username():
  return getpass.getuser()

def get_hostname():
  return socket.gethostname()

def is_on_rami_goto():
  return get_username() == "rkici" and get_hostname() == "goto"

def is_on_rami_chrome():
  return get_hostname() == "safedispatch"

def is_on_rami_chromebuild():
  return get_hostname() == "chromebuild" and get_username() == "sd"

def is_on_rami_local():
  return get_username() == "gokhan" and get_hostname() == "gokhan-ativ9"

# ----------------------------------------------------------------------

def read_config():
  assert "HOME" in os.environ
  clang_config = None

  if is_on_rami_local(): # rami's laptop
    clang_config = {
      "LLVM_SCRIPTS_DIR"   : os.environ["HOME"] + "/libs/llvm3.7/llvm/scripts",
      "LLVM_BUILD_DIR"     : os.environ["HOME"] + "/libs/llvm3.7/llvm-build",
      "BINUTILS_BUILD_DIR" : os.environ["HOME"] + "/libs/llvm3.7/binutils-build",
      "SD_DIR"             : os.environ["HOME"] + "/libs/safedispatch-scripts",
      "MY_GCC_VER"         : "4.8.2"
    }

  elif is_on_rami_chrome(): # VM at goto
    clang_config = {
      "LLVM_SCRIPTS_DIR"   : os.environ["HOME"] + "/rami/chrome/cr33/src/third_party/llvm-3.7/scripts",
      "LLVM_BUILD_DIR"     : os.environ["HOME"] + "/rami/chrome/cr33/src/third_party/llvm-build-3.7",
      "BINUTILS_BUILD_DIR" : os.environ["HOME"] + "/rami/libs/binutils-build",
      "SD_DIR"             : os.environ["HOME"] + "/rami/safedispatch-scripts",
      "MY_GCC_VER"         : "4.8.1"
    }

  elif is_on_rami_chromebuild(): # zoidberg
    clang_config = {
      "LLVM_SCRIPTS_DIR"   : os.environ["HOME"] + "/rami/llvm3.7/llvm/scripts",
      "LLVM_BUILD_DIR"     : os.environ["HOME"] + "/rami/llvm3.7/llvm-build",
      "BINUTILS_BUILD_DIR" : os.environ["HOME"] + "/rami/llvm3.7/binutils-build",
      "SD_DIR"             : os.environ["HOME"] + "/rami/safedispatch-scripts",
      "MY_GCC_VER"         : "4.7.3"
    }

  else: # don't know this machine
    return None

  clang_config.update({
    "ENABLE_COMPILER_OPT" : ENABLE_COMPILER_OPT,
    "GOLD_PLUGIN"         : clang_config["LLVM_BUILD_DIR"] + "/Release+Asserts/lib/LLVMgold.so",
    })

  clang_config.update({
    "SD_LIB_FOLDERS"  : ["-L" + clang_config["SD_DIR"] + "/libdyncast"],
    "SD_LIBS"         : ["-ldyncast"],
    "LD_PLUGIN"       : [opt for (key,opt) in linker_flag_opt_map.items()
                         if linker_flags[key]],
    "CC"              : clang_config["LLVM_BUILD_DIR"] + "/Release+Asserts/bin/clang",
    "CXX"             : clang_config["LLVM_BUILD_DIR"] + "/Release+Asserts/bin/clang++",
    "AR"              : clang_config["LLVM_SCRIPTS_DIR"] + "/ar",
    "NM"              : clang_config["LLVM_SCRIPTS_DIR"] + "/nm",
    "RANLIB"          : clang_config["LLVM_SCRIPTS_DIR"] + "/ranlib",
    "LD"              : clang_config["BINUTILS_BUILD_DIR"] + "/gold/ld-new",
    "CXX_FLAGS"       : ["-flto"],
    "LD_FLAGS"        : ["-z", "relro", "--hash-style=gnu", "--build-id", "--eh-frame-hdr",
                         "-m", "elf_x86_64", "-dynamic-linker", "/lib64/ld-linux-x86-64.so.2"],
    "LD_OBJS"         : ["/usr/lib/gcc/x86_64-linux-gnu/" + clang_config["MY_GCC_VER"] + "/../../../x86_64-linux-gnu/crt1.o",
                         "/usr/lib/gcc/x86_64-linux-gnu/" + clang_config["MY_GCC_VER"] + "/../../../x86_64-linux-gnu/crti.o",
                         "/usr/lib/gcc/x86_64-linux-gnu/" + clang_config["MY_GCC_VER"] + "/crtbegin.o"],
    "LD_LIB_FOLDERS"  : ["-L/usr/lib/gcc/x86_64-linux-gnu/" + clang_config["MY_GCC_VER"],
                         "-L/usr/lib/gcc/x86_64-linux-gnu/" + clang_config["MY_GCC_VER"] + "/../../../x86_64-linux-gnu",
                         "-L/lib/x86_64-linux-gnu",
                         "-L/lib/../lib64",
                         "-L/usr/lib/x86_64-linux-gnu",
                         "-L/usr/lib/gcc/x86_64-linux-gnu/" + clang_config["MY_GCC_VER"] + "/../../..",
                         "-L" + clang_config["LLVM_BUILD_DIR"] + "/Release+Asserts/bin/../lib",
                         "-L/lib",
                         "-L/usr/lib"],
    "LD_LIBS"         : ["-lstdc++", "-lm", "-lgcc_s", "-lgcc", "-lc", "-lgcc_s", "-lgcc",
                         "/usr/lib/gcc/x86_64-linux-gnu/" + clang_config["MY_GCC_VER"] + "/crtend.o",
                         "/usr/lib/gcc/x86_64-linux-gnu/" + clang_config["MY_GCC_VER"] + "/../../../x86_64-linux-gnu/crtn.o"],
    })

  for (k,v) in linker_flags.items():
    clang_config[k] = v

  if linker_flags["ENABLE_CHECKS"]:
    clang_config["CXX_FLAGS"].append(compiler_flag_opt_map["ENABLE_CHECKS"])

  return clang_config

if __name__ == '__main__':
  if len(sys.argv) == 2:
    d = read_config()
    key = sys.argv[1].upper()

    if key in d:
      var = d[key]
      if type(var) == list:
        print " ".join(var)
      else:
        print var

    sys.exit(0)
  else:
    print "usage: %s <key>" % sys.argv[0]
    sys.exit(1)

