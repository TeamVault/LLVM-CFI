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
sd_config = {
  "SD_ENABLE_INTERLEAVING" : True,  # interleave the vtables and add the range checks
  "SD_ENABLE_CHECKS"       : True,  # interleave the vtables and add the range checks
  "SD_ENABLE_LINKER_O2"    : True,  # runs O2 level optimizations during linking
  "SD_ENABLE_LTO"          : True,  # runs link time optimization passes
  "SD_LTO_EMIT_LLVM"       : False, # emit bitcode rather than machine code
  "SD_LTO_SAVE_TEMPS"      : True,  # save bitcode before & after linker passes
  "LLVM_CFI"               : False, # compile with llvm's cfi technique
}

isTrue = lambda s : s.lower() in ['true', '1', 't', 'y', 'yes', 'ok']

for k in sd_config:
  env_value = os.environ.get(k)
  if env_value is not None:
    sd_config[k] = isTrue(env_value)

assert not sd_config["SD_ENABLE_CHECKS"] or sd_config["SD_ENABLE_INTERLEAVING"]

compiler_flag_opt_map = {
  "SD_ENABLE_CHECKS"    : "-femit-vtbl-checks",
}

# corresponding plugin options of the linker flags
linker_flag_opt_map = {
  "SD_ENABLE_INTERLEAVING" : "-plugin-opt=emit-vtbl-checks",
  "SD_ENABLE_LINKER_O2"    : "-plugin-opt=run-O2-passes",
  "SD_ENABLE_LTO"          : "-plugin-opt=run-LTO-passes",
  "SD_LTO_EMIT_LLVM"       : "-plugin-opt=emit-llvm",
  "SD_LTO_SAVE_TEMPS"      : "-plugin-opt=save-temps",
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
      "LLVM_SCRIPTS_DIR"   : os.environ["HOME"] + "/src/src/third_party/llvm-3.7/scripts",
      "LLVM_BUILD_DIR"     : os.environ["HOME"] + "/src/src/third_party/llvm-build-3.7",
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
    "CC"              : clang_config["LLVM_BUILD_DIR"] + "/Release+Asserts/bin/clang",
    "CXX"             : clang_config["LLVM_BUILD_DIR"] + "/Release+Asserts/bin/clang++",
    "CXX_FLAGS"       : ["-flto"],
    "LD"              : clang_config["BINUTILS_BUILD_DIR"] + "/gold/ld-new",
    "LD_FLAGS"        : [],
    "SD_LIB_FOLDERS"  : ["-L" + clang_config["SD_DIR"] + "/libdyncast"],
    "SD_LIBS"         : ["-ldyncast"],
    "LD_PLUGIN"       : [opt for (key,opt) in linker_flag_opt_map.items()
                         if sd_config[key]],
    "AR"              : clang_config["LLVM_SCRIPTS_DIR"] + "/ar",
    "NM"              : clang_config["LLVM_SCRIPTS_DIR"] + "/nm",
    "RANLIB"          : clang_config["LLVM_SCRIPTS_DIR"] + "/ranlib",
    })

  for (k,v) in sd_config.items():
    clang_config[k] = v

  if sd_config["SD_ENABLE_CHECKS"]:
    clang_config["CXX_FLAGS"].append(compiler_flag_opt_map["SD_ENABLE_CHECKS"])

  if sd_config["LLVM_CFI"]:
    clang_config["CXX_FLAGS"].append('-fsanitize=cfi-vcall')
    clang_config["SD_LIB_FOLDERS"] = []
    clang_config["SD_LIBS"] = []

  return clang_config

if __name__ == '__main__':
  if len(sys.argv) == 2:
    d = read_config()
    key = sys.argv[1].upper()

    if key == "ENABLE_SD":
      print d["SD_ENABLE_INTERLEAVING"] or d["SD_ENABLE_CHECKS"]
      sys.exit(0)

    assert key in d
    var = d[key]
    if type(var) == list:
      print " ".join(var)
    else:
      print var

    sys.exit(0)
  else:
    print "usage: %s <key>" % sys.argv[0]
    sys.exit(1)

