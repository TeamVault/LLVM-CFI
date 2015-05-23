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

# fix the program name to point to the bitcode archiver
args[0] = conf["AR"]

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

sys.stdout.write("AR: %s\n" % " ".join(args))

run(args)

sys.exit(0)
