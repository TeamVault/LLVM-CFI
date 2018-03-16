#!/usr/bin/env python

import subprocess as sp
import re
import getpass
import socket
import os
import sys

ldRe = re.compile("GNU ld \(GNU Binutils for Ubuntu\) ([.\d]+)")

# When this is False, we remove any optimization flag from the compiler command
ENABLE_COMPILER_OPT = True
ENABLE_LLVM_CFI = False
BUILD_TYPE = os.environ.get('BUILD_TYPE', 'RELEASE')

# Enabled linker flags
sd_config = {
  # SafeDispatch options
  "SD_ENABLE_INTERLEAVING" : True,  # interleave the vtables
  "SD_ENABLE_ORDERING"     : False, # order the vtables
  "SD_ENABLE_CHECKS"       : True,  # add the range checks

  # LLVM's cfi sanitizer option
  "SD_LLVM_CFI"            : False, # compile with llvm's cfi technique

  # Common options
  "SD_ENABLE_LINKER_O2"    : False, # runs O2 level optimizations during linking
  "SD_ENABLE_LTO"          : True,  # runs link time optimization passes
  "SD_LTO_SAVE_TEMPS"      : True,  # save bitcode before & after linker passes
  "SD_LTO_EMIT_LLVM"       : False, # emit bitcode rather than machine code
}

if ENABLE_LLVM_CFI:
  sd_config["SD_ENABLE_INTERLEAVING"] = True
  sd_config["SD_ENABLE_CHECKS"]       = False
  sd_config["SD_LLVM_CFI"]            = True

isTrue = lambda s : s.lower() in ['true', '1', 't', 'y', 'yes', 'ok']

for k in sd_config:
  env_value = os.environ.get(k)
  if env_value is not None:
    sd_config[k] = isTrue(env_value)

#assert not sd_config["SD_ENABLE_CHECKS"] or sd_config["SD_ENABLE_INTERLEAVING"]

# corresponding plugin options of the linker flags
linker_flag_opt_map = {
  "SD_ENABLE_INTERLEAVING" : "-plugin-opt=sd-ivtbl",
  "SD_ENABLE_ORDERING"     : "-plugin-opt=sd-ovtbl",
  "SD_ENABLE_CHECKS"       : "",
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

def is_on_fry():
  return get_hostname() == "fry"

def is_on_bender():
  return get_hostname() == "bender"

def is_on_rami_goto():
  return get_username() == "rkici" and get_hostname() == "goto"

def is_on_rami_chrome():
  return get_hostname() == "safedispatch"

def is_on_rami_chromebuild():
  return get_hostname() == "chromebuild" and get_username() == "sd"

def is_on_paul_local():
  return get_username() == "paul" and get_hostname() == "paul-MacBookAir"

def is_on_rami_local():
  return get_username() == "gokhan" and get_hostname() == "gokhan-ativ9"

def is_on_zoidberg():
  return get_hostname() == "zoidberg" and get_username() == "rami"

def is_on_zoidberg_dimo():
  return get_hostname() == "zoidberg" and get_username() == "dimo"

def is_on_matt_desktop():
  return get_hostname() == "mattDesktop" and get_username() == "matt"


# ----------------------------------------------------------------------

def read_config():
  assert "HOME" in os.environ
  clang_config = None

  if is_on_rami_local(): # rami's laptop
    clang_config = {
      "LLVM_DIR"           : os.environ["HOME"] + "/libs/llvm3.7/llvm",
      "LLVM_BUILD_DIR"     : os.environ["HOME"] + "/libs/llvm3.7/llvm-build",
      "BINUTILS_BUILD_DIR" : os.environ["HOME"] + "/libs/llvm3.7/binutils-build",
      "SD_DIR"             : os.environ["HOME"] + "/libs/safedispatch-scripts",
      "MY_GCC_VER"         : "4.8.2"
    }

  elif is_on_paul_local(): # Linux paul-MacBookAir 3.19.0-25-generic #26~14.04.1-Ubuntu SMP Fri Jul 24 21:16:20 UTC 2015 x86_64 x86_64 x86_64 GNU/Linux

    clang_config = {
      "LLVM_DIR"           : "/home/paul/Desktop/llvm/llvm",
      "LLVM_BUILD_DIR"     : "/home/paul/Desktop/llvm/llvm-build/",
      "BINUTILS_BUILD_DIR" : "/home/paul/Desktop/llvm/binutils-build",
      "SD_DIR"             : "/home/paul/Desktop/llvm/llvm/scripts",
      "MY_GCC_VER"         : "4.8.4"
    }

  elif is_on_rami_chrome(): # VM at goto
    clang_config = {
      "LLVM_DIR"           : os.environ["HOME"] + "/rami/chrome/cr33/src/third_party/llvm-3.7",
      "LLVM_BUILD_DIR"     : os.environ["HOME"] + "/rami/chrome/cr33/src/third_party/llvm-build-3.7",
      "BINUTILS_BUILD_DIR" : os.environ["HOME"] + "/rami/libs/binutils-build",
      "SD_DIR"             : os.environ["HOME"] + "/rami/safedispatch-scripts",
      "MY_GCC_VER"         : "4.8.1"
    }

  elif is_on_rami_chromebuild(): # sd @ zoidberg
    clang_config = {
      "LLVM_DIR"           : os.environ["HOME"] + "/src/src/third_party/llvm-3.7",
      "LLVM_BUILD_DIR"     : os.environ["HOME"] + "/src/src/third_party/llvm-build-3.7",
      "BINUTILS_BUILD_DIR" : os.environ["HOME"] + "/rami/llvm3.7/binutils-build",
      "SD_DIR"             : os.environ["HOME"] + "/rami/safedispatch-scripts",
      "MY_GCC_VER"         : "4.7.3"
    }

  elif is_on_zoidberg(): # zoidberg
    clang_config = {
      "LLVM_DIR"           : os.environ["HOME"] + "/llvm",
      "LLVM_BUILD_DIR"     : os.environ["HOME"] + "/llvm-build",
      "BINUTILS_BUILD_DIR" : os.environ["HOME"] + "/binutils-build",
      "SD_DIR"             : os.environ["HOME"] + "/safedispatch-scripts",
      "MY_GCC_VER"         : "4.8.4"
    }

  elif is_on_fry(): # fry
    clang_config = {
      "LLVM_DIR"           : os.environ["HOME"] + "/work/sd3.0/llvm-3.7",
      "LLVM_BUILD_DIR"     : os.environ["HOME"] + "/work/sd3.0/llvm-build",
      "BINUTILS_BUILD_DIR" : os.environ["HOME"] + "/work/sd3.0/binutils-build",
      "SD_DIR"             : os.environ["HOME"] + "/work/sd2.0/scripts",
      "MY_GCC_VER"         : "4.7.3"
    }
  elif is_on_bender(): # fry
    clang_config = {
      "LLVM_DIR"           : os.environ["HOME"] + "/work/sd3.0/llvm-3.7",
      "LLVM_BUILD_DIR"     : os.environ["HOME"] + "/work/sd3.0/llvm-build",
      "BINUTILS_BUILD_DIR" : os.environ["HOME"] + "/work/sd3.0/binutils-build",
      "SD_DIR"             : os.environ["HOME"] + "/work/sd3.0/safedispatch-scripts",
      "MY_GCC_VER"         : "4.7.3"
    }
  elif is_on_zoidberg_dimo(): # zoidberg
    clang_config = {
      "LLVM_DIR"           : os.environ["HOME"] + "/work/sd3.0/llvm-3.7",
      "LLVM_BUILD_DIR"     : os.environ["HOME"] + "/work/sd3.0/llvm-build",
      "BINUTILS_BUILD_DIR" : os.environ["HOME"] + "/work/sd3.0/binutils-build",
      "SD_DIR"             : os.environ["HOME"] + "/work/sd3.0/llvm-3.7/scripts",
      "MY_GCC_VER"         : "4.8.4"
    }
  elif is_on_matt_desktop(): # mattDesktop
    clang_config = {
      "LLVM_DIR"            : os.environ["HOME"] + "/thesis/llvm",
      "LLVM_BUILD_DIR"      : os.environ["HOME"] + "/thesis/llvm-build",
      "LLVM_DEBUG_BUILD_DIR": os.environ["HOME"] + "/thesis/llvm-debug",
      "LLVM_ANALYSIS_BUILD_DIR": os.environ["HOME"] + "/thesis/llvm-build-analysis",
      "BINUTILS_BUILD_DIR"  : os.environ["HOME"] + "/thesis/binutils-build",
      "SD_DIR"              : os.environ["HOME"] + "/thesis/llvm/scripts",
      "MY_GCC_VER"          : "5.4.0"
    }

  else: # don't know this machine
    return None

  #Debug build?
  if BUILD_TYPE == "DEBUG":
    LLVM_BUILD_OUTPUT = clang_config["LLVM_DEBUG_BUILD_DIR"] + "/Debug+Asserts"
  elif BUILD_TYPE == "ANALYSIS":
    LLVM_BUILD_OUTPUT = clang_config["LLVM_ANALYSIS_BUILD_DIR"] + "/Release"
  else:
    LLVM_BUILD_OUTPUT = clang_config["LLVM_BUILD_DIR"] + "/Release"

  clang_config.update({
    "LLVM_SCRIPTS_DIR"    : clang_config["LLVM_DIR"] + "/scripts",
    "ENABLE_COMPILER_OPT" : ENABLE_COMPILER_OPT,
    "GOLD_PLUGIN"         : LLVM_BUILD_OUTPUT + "/lib/LLVMgold.so",
    "LLVM_BUILD_BIN"      : LLVM_BUILD_OUTPUT + "/bin",
    })

  clang_config.update({
    "CC"              : clang_config["LLVM_BUILD_BIN"] + "/clang",
    "CXX"             : clang_config["LLVM_BUILD_BIN"] + "/clang++",
    "CXX_FLAGS"       : ["-flto"],
    "LD"              : clang_config["BINUTILS_BUILD_DIR"] + "/gold/ld-new",
    "LD_FLAGS"        : [],
    "SD_LIB_FOLDERS"  : ["-L" + clang_config["LLVM_DIR"] + "/libdyncast"],
    "SD_LIBS"         : ["-ldyncast"],
    "LD_PLUGIN"       : [opt for (key,opt) in linker_flag_opt_map.items()
                         if sd_config[key]],
    "AR"              : clang_config["LLVM_SCRIPTS_DIR"] + "/ar",
    "NM"              : clang_config["LLVM_SCRIPTS_DIR"] + "/nm",
    "RANLIB"          : clang_config["LLVM_SCRIPTS_DIR"] + "/ranlib",
    })

  for (k,v) in sd_config.items():
    clang_config[k] = v

  if sd_config["SD_LLVM_CFI"]:
    clang_config["CXX_FLAGS"].append('-fsanitize=cfi-vcall')
    clang_config["SD_LIB_FOLDERS"] = []
    clang_config["SD_LIBS"] = []

  if sd_config["SD_ENABLE_INTERLEAVING"] or sd_config["SD_ENABLE_ORDERING"]:
    clang_config["CXX_FLAGS"].append('-femit-ivtbl')
  if sd_config["SD_ENABLE_CHECKS"]:
    clang_config["CXX_FLAGS"].append('-femit-vtbl-checks')

  return clang_config

if __name__ == '__main__':
  if len(sys.argv) == 2:
    d = read_config()
    key = sys.argv[1].upper()

    if key == "ENABLE_SD":
      print d["SD_ENABLE_INTERLEAVING"] or \
              d["SD_ENABLE_CHECKS"] or \
              d["SD_ENABLE_ORDERING"]
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

