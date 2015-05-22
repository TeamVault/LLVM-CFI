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

# check if this command is used for linking
try:
  argOInd = args.index('-o') + 1
except ValueError:
  argOInd = -1

# remove the optimization flags
for (i,a) in enumerate(args):
  if a.startswith("-O"):
    args.pop(i)
    break

for a in args:
  assert not a.startswith("-O")

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

  print "CC: %s" % " ".join(args)
  run(args)

# LINKING : construct the linking command and execute it
else:
  assert argOInd >= 0

  # extract the object files
  object_files = list(set([obj for obj in args if obj.endswith(".o") or obj.endswith(".a")]))

  # construct the linking command:
  #   $(LD) $(LD_FLAGS) -o $@ $(LD_OBJS) $(LD_LIB_FOLDERS) $(LD_PLUGIN) $^ $(LD_LIBS)
  new_args = [conf["LD"]] + conf["LD_FLAGS"] + \
             ["-o", args[argOInd]] + conf["LD_OBJS"] + conf["LD_LIB_FOLDERS"] + conf["LD_PLUGIN"] + \
             object_files + conf["LD_LIBS"]

  # create an array to use in the subprocess
  print "LD: %s" % " ".join(args)

  # run the command
  run(new_args)

sys.exit(0)

