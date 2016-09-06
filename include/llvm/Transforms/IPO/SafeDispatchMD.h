#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCH_MD_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCH_MD_H

#include "llvm/IR/Metadata.h"

/**
 * name of the replacement function for __dynamic_cast
 */
#define SD_DYNCAST_FUNC_NAME "__ivtbl_dynamic_cast"

/**
 * metadata names used for the SafeDispatch project.
 * This meta data names are added to the new metadata
 * nodes which are added to the source code during analysis
 */
#define SD_MD_CAST_FROM  "sd.cast.from"   // used to identify casts
#define SD_MD_TYPEID     "sd.typeid"      // used to annotate the type id 
#define SD_MD_VCALL      "sd.vcall"       // used to annotate the virtual call 
#define SD_MD_VBASE      "sd.vbase"       // class name, original vbase offset
#define SD_MD_MEMPTR     "sd.memptr"      // class name, annotate the member pointer 1
#define SD_MD_MEMPTR2    "sd.memptr2"     // class name, annotate the member pointer 2 
#define SD_MD_MEMPTR_OPT "sd.memptr3"     // class name, annotate the member pointer 3
#define SD_MD_CHECK      "sd.check"       // class name, annotate the check 

/**
 * named md used to store the vtable info
 */
#define SD_MD_CLASSINFO  "sd.class_info." 

#endif

