#!/usr/bin/env python

import sys
import os
import subprocess
import config
from copy import copy

# get the command name and the arguments
args = sys.argv

# read the configuration map
conf = config.read_config()

# fix the program name to point to the clang++ compiler
args[0] = conf["CC"]

# check if this command is used for compiling the source code
hasArgC = '-c' in args

# remove the optimization flags
isArgRemoved = True
while isArgRemoved:
  isArgRemoved = False
  for (i,a) in enumerate(args):
    if "-O" in a:
      args.pop(i)
      isArgRemoved = True
      break

for a in args:
  assert "-O" not in a

def run(args, **env):
  new_env = copy(os.environ)
  for key in env:
    new_env[key] = str(env[key])
  try:
    res = subprocess.check_output(args, env=new_env, stderr=subprocess.STDOUT)
    sys.stderr.write(res)
  except subprocess.CalledProcessError, e:
    print "FAILED: ", " ".join(args)
    sys.stderr.write(e.output)
    sys.exit(-1)

# COMPILING : do nothing, just execute the given command
if hasArgC:
  if "-flto" not in args:
    args.insert(1,"-flto")

  sys.stderr.write("CC: %s\n" % " ".join(args))
  run(args)

# LINKING : construct the linking command and execute it
else:
  # check if this command is used for linking
  try:
    argOInd = args.index('-o') + 1
  except ValueError:
    sys.stderr.write("couldn't find the -o parameter for the linking command!\n%s\n" %
                     " ".join(args))
    sys.exit(-1)

  assert argOInd >= 0

  # extract the object files
  object_files = list(set([obj for obj in args if obj.endswith(".o")]))

  static_lib_folders = list(set([obj for obj in args if obj.startswith("-L")]))

  static_libs = list(set([obj for obj in args if obj.startswith("-l")]))

  # construct the linking command:
  #   $(LD) $(LD_FLAGS) -o $@ $(LD_OBJS) $(LD_LIB_FOLDERS) $(LD_PLUGIN) $^ $(LD_LIBS)
  new_args = [conf["LD"]] + conf["LD_FLAGS"] + \
             ["-o", args[argOInd]] + conf["LD_OBJS"] + \
             conf["LD_LIB_FOLDERS"] + static_lib_folders + \
             conf["LD_PLUGIN"] + \
             object_files + conf["LD_LIBS"] + static_libs

  # create an array to use in the subprocess
  sys.stderr.write("LD: %s\n" % " ".join(new_args))

  # run the command
  run(new_args)

sys.exit(0)

