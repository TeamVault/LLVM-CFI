#!/usr/bin/python

import sys
import subprocess
import re
import itertools
import pprint
from mydemangler import demangle

import pdb
pp = pprint.PrettyPrinter(depth=5)

class Bunch:
  def __init__(self, **kwds):
    self.__dict__.update(kwds)

  def __getattribute__(self, name):
    if name == "fullname" and self.__dict__[name] is None:
        fn = demangle(name)
        self.__dict__["fullname"] = fn
        return fn
    else:
      return self.__dict__[name]

  def __str__(self):
    return "Bunch(" + \
        ", ".join(["%s=%s" % (k,self.__dict__[k])
                  for k in self.__dict__]) + \
        ")"
  def __repr__(self):
    return str(self)

class VTableExtractor(object):

  def __init__(self, elf_filename="main", names=None):
    self.elf_filename = elf_filename

    # list of elf things
    self.sections = self.loadSections()
    self.symbols  = self.loadSymbols()
    self.names    = [] if names is None else names
    self.vtables  = self.getVTables()

    # build maps for easier access
    self.sections_name = dict([(s.name, s)
                              for s in self.sections])
    self.sections_id   = dict([(s.secId, s)
                              for s in self.sections])
    self.symbols_name  = dict([(s.name, s)
                              for s in self.symbols])
    self.symbols_addr  = dict([(s.addr, s)
                              for s in self.symbols])
    self.vtblSecMap    = {}
    for v in self.vtables:
      if not v.secId in self.vtblSecMap:
        self.vtblSecMap[v.secId] = []
      self.vtblSecMap[v.secId].append(v)


  def loadSections(self):
    sections = []
    isSecDec = re.compile(r'^\s*\[\s*\d+\]')
    try:
      for line in subprocess\
                  .check_output(['readelf', '-SW', self.elf_filename])\
                  .split('\n'):
        if isSecDec.match(line):
          cols            = line.split()
          if cols[0] == "[":
            cols[0] += cols[1]
            del cols[1]
          secId   = int(cols[0][1:-1])
          secName = cols[1]
          secType = cols[2]
          offset  = int(cols[3],16) + int(cols[4], 16)
          size    = int(cols[5],16)
          sections.append(Bunch(name=secName, secId=secId,
                                secType=secType, offset=offset,
                                size=size))
      return sections
    except Exception as e:
      print "while reading sections: %s" % e
      sys.exit(-1)

  def loadSymbols(self):
    symAddrSet = set()
    syms = []
    vtnRe = re.compile(r'(construction )?vtable for (.*)')

    readelf = subprocess.Popen(('readelf', '-sW', self.elf_filename),
                               stdout=subprocess.PIPE)
    output = subprocess.check_output(('grep', '-iP', ' (_Z|_SD_Z|_SVT_Z)'),
                                     stdin=readelf.stdout)
    readelf.wait()

    try:
      for line in output.split('\n'):
        cols = line.split()
        try:
          if len(cols) < 8 or int(cols[6]) <= 0:
            continue
        except ValueError:
          continue

        addr     = int(cols[1],16)
        size     = VTableExtractor.atoi(cols[2])
        secId    = int(cols[6])

        if (addr,secId) in symAddrSet:
          continue

        name     = cols[7]

        fullname = None
        isVtable = False
        isCons   = False

        if any(map(name.startswith, ["_ZTV", "_ZTC", "_SD_ZTV", "_SD_ZTC"])):
          isVtable = True
          if name.startswith("_SD"):
            name = name[3:]
          fullname = demangle(name)
          m        = vtnRe.match(fullname)
          fullname = m.group(2)
          isCons   = m.group(1) is not None

        syms.append(Bunch(addr=addr, size=size, secId=secId, name=name,
                          fullname=fullname, isCons=isCons, isVtable=isVtable))

        symAddrSet.add((addr,secId))
      return syms
    except Exception as e:
      print "while reading symbols: %s\n%s " % (e,line)
      sys.exit(-1)


  def getVTables(self):
    return [
            v
            for v in self.symbols
            if v.isVtable and \
              (not self.names or v.fullname in self.names) and \
              "__cxx" not in v.name
           ]


  def printVtableHexdump(self):
    if self.elf_filename.endswith(".o"):
      if "relocations" not in self.__dict__:
        self.readRelocationEntries()
      return self.printVtableHexdumpO()
    else:
      return self.printVtableHexdumpL()


  def readRelocationEntries(self):
    self.relocations  = {}
    relocs  = {}
    cmd     = ("readelf -rW %s" % self.elf_filename).split()
    isDecl  = re.compile(r'Relocation section \'([^ ]*)\' at offset')
    isReloc = re.compile(r'^[0-9a-fA-F]+')
    sec     = None
    for line in subprocess.check_output(cmd).split('\n'):
      m = isDecl.search(line)
      # this is a new set of reloc entries
      if m is not None:
        # store the current set
        if sec is not None:
          self.relocations[sec.secId] = relocs
        sec    = self.sections_name[m.group(1)]
        relocs = {}
        continue

      m = isReloc.match(line)
      if m is not None:
        cols           = line.split()
        offset         = int(cols[0],16)
        sname          = cols[4]
        addend         = int("".join(cols[5:]), 16)
        relocs[offset] = Bunch(offset=offset, sname=sname, addend=addend)

    # add the last reloc set
    if sec is not None:
      self.relocations[sec.secId] = relocs


  def printVtableHexdumpO(self):
    if not self.vtables:
      return

    hxdmp_cmd = lambda i : "readelf -x %s %s" % (str(i), self.elf_filename)
    isHexdump = re.compile(r'^\s*0x([0-9a-f]*)\s*([0-9a-f]*)\s*([0-9a-f]*)\s*([0-9a-f]*)\s*([0-9a-f]*)')

    def parse(s):
      m = isHexdump.match(s)
      return (int(m.group(1),16),
              [str(m.group(2)), str(m.group(3))],
              [str(m.group(4)), str(m.group(5))])

    for secId in self.vtblSecMap:
      sec    = self.sections_id[secId]
      relaSec = self.sections_name[".rela" + sec.name]
      #pp.pprint(sec)
      #pp.pprint(relaSec)
      #pp.pprint(self.relocations)
      curRelocs = self.relocations[relaSec.secId]
      hxdmp = [parse(s)
               for s in subprocess
                          .check_output(hxdmp_cmd(secId).split())
                          .split('\n')
               if isHexdump.match(s)]

      for vtbl in self.vtblSecMap[secId]:
        addr     = vtbl.addr
        size     = vtbl.size
        fullname = vtbl.fullname

        print "HEXDUMP for %s%s" % ("(cons) " if vtbl.isCons else "", fullname)
        print "0x%x to 0x%x" % (addr, addr + size)

        for (lineAddr,d1,d2) in hxdmp:
          if addr <= lineAddr < addr + size:
            for li,d in zip(range(min(2,(addr+size-lineAddr)/8)),[d1,d2]):
              la = lineAddr + li*8
              if la in curRelocs:
                r = curRelocs[la]
                exp = r.sname
                # this is an offset from a section
                if exp.startswith("."):
                  offSec = self.sections_name[exp]
                  expSym = filter(lambda s : s.secId == offSec.secId
                                  and s.addr == r.addend, self.symbols)
                  if len(expSym) == 1:
                    exp = expSym[0].name
                  else:
                    exp = "???"
              else:
                exp = self._hexdump_to_int(d)
              print "0x%x: %s %s => %s" % (lineAddr+sec.offset+li*8,\
                                           d[0], d[1], exp)
        print ""


  def printVtableHexdumpL(self):
    if not self.vtables:
      return

    hxdmp_cmd = lambda i : "readelf -x %s %s" % (str(i), self.elf_filename)
    isHexdump = re.compile(r'^\s*0x')

    for secId in self.vtblSecMap:
      hxdmp = [s.strip()
               for s in subprocess
                          .check_output(hxdmp_cmd(secId).split())
                          .split('\n')
               if isHexdump.match(s)]

      for vtbl in self.vtblSecMap[secId]:
        addr     = vtbl.addr
        size     = vtbl.size
        fullname = vtbl.fullname

        print "HEXDUMP for %s%s" % ("(cons) " if vtbl.isCons else "", fullname)
        print "0x%x to 0x%x" % (addr, addr + size)

        lc = 0
        for line in hxdmp:
          cols = line.split()
          lineno = int(cols[0],16)
          if addr <= lineno < addr + size:
            for li in range(min(2,(addr+size-lineno)/8)):
              vtbl_elem = self._hexdump_to_int(cols[li*2+1:li*2+3])
              if vtbl_elem in self.symbols_addr:
                print "%-3d: %x: %s %s => %s" % (lc, lineno+li*8, cols[li*2+1],\
                                           cols[li*2+2],\
                                           self.symbols_addr[vtbl_elem].name)
              else:
                print "%-3d: %x: %s %s => %s" % (lc, lineno+li*8, cols[li*2+1],
                                           cols[li*2+2],
                                           self._hex_to_str(vtbl_elem))
              lc += 1
        print ""

  @staticmethod
  def _concatMap(fun, it):
    return list(itertools.chain.from_iterable(fun(x) for x in it))

  @staticmethod
  def _hexdump_to_int(hx):
    def rev_32(s):
      assert(len(s)%2 == 0)
      return ''.join([s[ii-2:ii] for ii in range(len(s),0,-2)])
    hxr= ''.join(reversed(map(rev_32, hx)))
    hxi = int(hxr, 16)
    if hxr[0] >= 'a':
      return hxi - (1 << (len(hxr)*4))
    else:
      return hxi

  @staticmethod
  def _hex_to_str(h):
    if h >= 0x400000:
      return "0x%x" % h
    else:
      return "%d" % h

  @staticmethod
  def atoi(s):
    if "0x" in s:
      return int(s,16)
    else:
      return int(s)

if __name__ == '__main__':
  argCount = len(sys.argv)

  if argCount < 2:
    print "usage: extract_vtables <filename> [class name]*"
    sys.exit(-1)
  else:
    ve = VTableExtractor(sys.argv[1],names=sys.argv[2:])

  ve.printVtableHexdump()
