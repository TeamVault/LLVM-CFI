# RUN: llvm-mc %s -triple=mips-unknown-linux -show-encoding -mcpu=mips32r6 -mattr=micromips | FileCheck %s

  .set noat
  balc 14572256            # CHECK: balc 14572256       # encoding: [0xb4,0x37,0x96,0xb8]
  bc 14572256              # CHECK: bc 14572256         # encoding: [0x94,0x37,0x96,0xb8]
  bitswap $4, $2           # CHECK: bitswap $4, $2      # encoding: [0x00,0x44,0x0b,0x3c]
  cache 1, 8($5)           # CHECK: cache 1, 8($5)      # encoding: [0x20,0x25,0x60,0x08]
  pref 1, 8($5)            # CHECK: pref 1, 8($5)       # encoding: [0x60,0x25,0x20,0x08]
