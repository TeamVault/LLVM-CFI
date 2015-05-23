#!/usr/bin/env python

import sys
import os
import subprocess
import config
from copy import copy

bypassLTO = False
#bypassLTO = True

# check if this command is used for compiling the source code
hasArgC = '-c' in sys.argv

# read the configuration map
conf = config.read_config()

argOInd    = None
groupStart = None
groupEnd   = None

flag_blacklist = set(["--start-group", "--end-group"])

def nicer_args(input_args):
  global argOInd
  global flag_blacklist

  new_args = []
  flags = []

  if hasArgC:
    new_args.append(conf["CXX"])
  else:
    new_args.append(conf["LD"])

  for i,arg in enumerate(input_args):
    arg = arg.replace(',', ' ').strip()

    if arg == "":
      continue

    isFlag = False

    if arg.startswith("-Wl "):
      arg = arg[4:]
      isFlag = True

    if arg.startswith("-O"):
      # remove the optimization flags
      continue
    elif arg == "-o":
      argOInd = len(new_args) + 1
    elif arg == "--start-group":
      groupStart = len(new_args) + 1
    elif arg == "--end-group":
      groupEnd = len(new_args) - 1

    if isFlag and arg not in flag_blacklist:
      print "|%s|" % arg
      flags.extend(arg.split())

    new_args.extend(arg.split())

  return new_args, flags

def run(args, **env):
  new_env = copy(os.environ)
  for key in env:
    new_env[key] = str(env[key])
  try:
    res = subprocess.check_output(args, env=new_env, stderr=subprocess.STDOUT)
    sys.stderr.write(res)
  except subprocess.CalledProcessError, e:
    print "FAILED: %s\n" % " ".join(args)
    sys.stderr.write(e.output)
    sys.exit(-1)

if bypassLTO:
  args = sys.argv
  args[0] = conf["CXX"]
  run(args)
  sys.exit(0)

args,flags = nicer_args(sys.argv[1:])

# COMPILING : do nothing, just execute the given command
if hasArgC:
  if "-flto" not in args:
    args.insert(1,"-flto")

  sys.stdout.write("CC: %s\n" % " ".join(args))
  run(args)

# LINKING : construct the linking command and execute it
else:
  assert argOInd is not None

  if groupEnd is not None:
    assert groupEnd == (len(args) - 2)

  # extract the object files
  object_files = list(set([obj
                             for obj in args
                               if obj.endswith(".o") or obj.endswith(".a")]))

  static_lib_folders = list(set([obj for obj in args if obj.startswith("-L")]))

  static_libs = list(set([obj for obj in args if obj.startswith("-l")]))

  # construct the linking command:
  #   $(LD) $(LD_FLAGS) -o $@ $(LD_OBJS) $(LD_LIB_FOLDERS) $(LD_PLUGIN) $^ $(LD_LIBS)
  new_args  = [conf["LD"]]
  new_args += conf["LD_FLAGS"]
  new_args += flags
  new_args += ["-o", args[argOInd]]
  new_args += conf["LD_OBJS"]
  new_args += conf["LD_LIB_FOLDERS"]
  new_args += static_lib_folders
  new_args += conf["LD_PLUGIN"]
  new_args += object_files
  new_args += conf["LD_LIBS"]
  new_args += static_libs

  # create an array to use in the subprocess
  sys.stdout.write("LD: %s\n" % " ".join(new_args))

  # run the command
  run(new_args)

sys.exit(0)

