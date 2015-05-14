#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCH_MD_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCH_MD_H

#include "llvm/IR/Metadata.h"

// name of the replacement function for __dynamic_cast

#define SD_DYNCAST_FUNC_NAME "__ivtbl_dynamic_cast"

// metadata names used for the SafeDispatch project

#define SD_MD_CLASS_NAME "sd.class.name"
#define SD_MD_CAST_FROM  "sd.cast.from"
#define SD_MD_TYPEID     "sd.typeid"
#define SD_MD_VCALL      "sd.vcall"
#define SD_MD_VBASE      "sd.vbase"
#define SD_MD_MEMPTR     "sd.memptr"
#define SD_MD_MEMPTR2    "sd.memptr2"
#define SD_MD_MEMPTR_OPT "sd.memptr3"

#define SD_MD_CLASSINFO  "sd.info."

#endif

