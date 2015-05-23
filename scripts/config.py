#!/usr/bin/env python

import subprocess as sp
import re
import getpass
import socket
import os
import sys

ldRe = re.compile("GNU ld \(GNU Binutils for Ubuntu\) ([.\d]+)")

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
  folders = None

  if is_on_rami_local(): # rami's laptop
    folders = {
      "LLVM_SCRIPTS_DIR"   : os.environ["HOME"] + "/libs/llvm3.7/llvm/scripts",
      "LLVM_BUILD_DIR"     : os.environ["HOME"] + "/libs/llvm3.7/llvm-build",
      "BINUTILS_BUILD_DIR" : os.environ["HOME"] + "/libs/llvm3.7/binutils-build",
      "SD_DIR"             : os.environ["HOME"] + "/libs/safedispatch-scripts",
      "MY_GCC_VER"         : "4.8.2"
    }

  elif is_on_rami_chrome(): # VM at goto
    folders = {
      "LLVM_SCRIPTS_DIR"   : os.environ["HOME"] + "/rami/chrome/cr33/src/third_party/llvm-3.7/scripts",
      "LLVM_BUILD_DIR"     : os.environ["HOME"] + "/rami/chrome/cr33/src/third_party/llvm-build-3.7",
      "BINUTILS_BUILD_DIR" : os.environ["HOME"] + "/rami/libs/binutils-build",
      "SD_DIR"             : os.environ["HOME"] + "/rami/safedispatch-scripts",
      "MY_GCC_VER"         : "4.8.1"
    }

  elif is_on_rami_chromebuild(): # zoidberg
    folders = {
      "LLVM_SCRIPTS_DIR"   : os.environ["HOME"] + "/rami/llvm3.7/llvm/scripts",
      "LLVM_BUILD_DIR"     : os.environ["HOME"] + "/rami/llvm3.7/llvm-build",
      "BINUTILS_BUILD_DIR" : os.environ["HOME"] + "/rami/llvm3.7/binutils-build",
      "SD_DIR"             : os.environ["HOME"] + "/rami/safedispatch-scripts",
      "MY_GCC_VER"         : "4.7.3"
    }

  else: # don't know this machine
    return None

  folders.update({
    "GOLD_PLUGIN"     : folders["LLVM_BUILD_DIR"] + "/Release+Asserts/lib/LLVMgold.so",
    })

  folders.update({
    "CC"              : folders["LLVM_BUILD_DIR"] + "/Release+Asserts/bin/clang",
    "CXX"             : folders["LLVM_BUILD_DIR"] + "/Release+Asserts/bin/clang++",
    "AR"              : folders["LLVM_SCRIPTS_DIR"] + "/ar",
    "NM"              : folders["LLVM_SCRIPTS_DIR"] + "/nm",
    "RANLIB"          : folders["LLVM_SCRIPTS_DIR"] + "/ranlib",
    "LD"              : folders["BINUTILS_BUILD_DIR"] + "/gold/ld-new",
    "LD_FLAGS"        : ["-z", "relro", "--hash-style=gnu", "--build-id", "--eh-frame-hdr",
                         "-m", "elf_x86_64", "-dynamic-linker", "/lib64/ld-linux-x86-64.so.2"],
    "LD_OBJS"         : ["/usr/lib/gcc/x86_64-linux-gnu/" + folders["MY_GCC_VER"] + "/../../../x86_64-linux-gnu/crt1.o",
                         "/usr/lib/gcc/x86_64-linux-gnu/" + folders["MY_GCC_VER"] + "/../../../x86_64-linux-gnu/crti.o",
                         "/usr/lib/gcc/x86_64-linux-gnu/" + folders["MY_GCC_VER"] + "/crtbegin.o"],
    "LD_LIB_FOLDERS"  : ["-L/usr/lib/gcc/x86_64-linux-gnu/" + folders["MY_GCC_VER"],
                         "-L/usr/lib/gcc/x86_64-linux-gnu/" + folders["MY_GCC_VER"] + "/../../../x86_64-linux-gnu",
                         "-L/lib/x86_64-linux-gnu",
                         "-L/lib/../lib64",
                         "-L/usr/lib/x86_64-linux-gnu",
                         "-L/usr/lib/gcc/x86_64-linux-gnu/" + folders["MY_GCC_VER"] + "/../../..",
                         "-L" + folders["LLVM_BUILD_DIR"] + "/Release+Asserts/bin/../lib",
                         "-L/lib",
                         "-L/usr/lib",
                         "-L" + folders["SD_DIR"] + "/libdyncast"],
    "LD_PLUGIN"       : ["-plugin", folders["GOLD_PLUGIN"],
                         "-plugin-opt=mcpu=x86-64"],
    "LD_LIBS"         : ["-lstdc++", "-lm", "-lgcc_s", "-lgcc", "-lc", "-lgcc_s", "-lgcc", "-ldyncast",
                         "/usr/lib/gcc/x86_64-linux-gnu/" + folders["MY_GCC_VER"] + "/crtend.o",
                         "/usr/lib/gcc/x86_64-linux-gnu/" + folders["MY_GCC_VER"] + "/../../../x86_64-linux-gnu/crtn.o"],
    })

  return folders

if __name__ == '__main__':
  if len(sys.argv) == 2:
    d = read_config()
    key = sys.argv[1].upper()

    if key in d:
      print d[key]

    sys.exit(0)
  else:
    print "usage: %s <key>" % sys.argv[0]
    sys.exit(1)

