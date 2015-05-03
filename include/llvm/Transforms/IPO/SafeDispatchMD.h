#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCH_MD_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCH_MD_H

// name of the replacement function for __dynamic_cast

#define SD_DYNCAST_FUNC_NAME "__ivtbl_dynamic_cast"

// metadata names used for the SafeDispatch project

#define SD_MD_CLASS_NAME "sd.class.name"
#define SD_MD_CAST_FROM  "sd.cast.from"
#define SD_MD_TYPEID     "sd.typeid"
#define SD_MD_VCALL      "sd.vcall"
#define SD_MD_VBASE      "sd.vbase"

#endif

