#ifndef LLVM_TRANSFORMS_IPO_SAFEDISPATCH_LOG_STREAM_H
#define LLVM_TRANSFORMS_IPO_SAFEDISPATCH_LOG_STREAM_H

#define SD_DEBUG

namespace sdLog {

  typedef llvm::raw_string_ostream stream_t;

  static inline llvm::raw_ostream &stream() {
#ifdef SD_DEBUG
    llvm::errs() << "SD] ";
    return llvm::errs();
#else
    return llvm::nulls();
#endif
  }

  static inline sdLog::stream_t &delayed_stream() {
    std::string str;
    llvm::raw_string_ostream stream(str);
    return stream;
  }

  static inline void print_delayed_stream(sdLog::stream_t &stream) {
#ifdef SD_DEBUG
    llvm::errs() << "SD] " << stream.str();
#endif
  }

}

#endif

