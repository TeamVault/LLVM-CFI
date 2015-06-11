#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCH_MD_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCH_MD_H

#include "llvm/IR/Metadata.h"

/**
 * name of the replacement function for __dynamic_cast
 */
#define SD_DYNCAST_FUNC_NAME "__ivtbl_dynamic_cast"

/**
 * metadata names used for the SafeDispatch project
 */
#define SD_MD_VFUN_CALL  "sd.vfun.call"   // class name
#define SD_MD_CAST_FROM  "sd.cast.from"   // class name
#define SD_MD_TYPEID     "sd.typeid"      // class name
#define SD_MD_VCALL      "sd.vcall"       // -
#define SD_MD_VBASE      "sd.vbase"       // class name, original vbase offset
#define SD_MD_MEMPTR     "sd.memptr"      // class name
#define SD_MD_MEMPTR2    "sd.memptr2"     // class name 1, class name 2
#define SD_MD_MEMPTR_OPT "sd.memptr3"     // class name
#define SD_MD_CHECK      "sd.check"       // class name

/**
 * named md used to store the vtable info
 */
#define SD_MD_CLASSINFO  "sd.class_info."

#endif

