#include "TaintTracking.h"
#include "StringImpl.h"

#include <iostream>

namespace tainttracking {
namespace webkit {

// static
void StringTaint::InitTaintData(WTF::StringImpl* impl) {
  if (impl) {
    memset(FromString(impl), 0, impl->length() + sizeof(int64_t));
  }
}

// static
TaintData* StringTaint::FromString(WTF::StringImpl* impl) {
  if (!impl) {
    return nullptr;
  }

  size_t len = impl->length();
  if (impl->is8Bit()) {
    return reinterpret_cast<TaintData*>(
        &(reinterpret_cast<LChar*>(impl + 1)[len]));
  } else {
    return reinterpret_cast<TaintData*>(
        &(reinterpret_cast<UChar*>(impl + 1)[len]));
  }
}

// static
size_t StringTaint::AllocationSize(unsigned length) {
  return (length * sizeof(TaintData)) + sizeof(int64_t);
}

// static
void StringTaint::SetTainted(WTF::StringImpl* impl, TaintType type) {
  if (impl) {
    memset(FromString(impl), static_cast<TaintData>(type), impl->length());
  }
}

namespace {

int64_t* TaintInfoFromString(WTF::StringImpl* impl) {
  if (impl) {
    return reinterpret_cast<int64_t*>(
        StringTaint::FromString(impl) + impl->length());
  } else {
    return nullptr;
  }
}

}


// static
int64_t StringTaint::GetTaintInfo(WTF::StringImpl* impl) {
  return *TaintInfoFromString(impl);
}

// static
void StringTaint::SetTaintInfo(WTF::StringImpl* impl, int64_t info) {
  *TaintInfoFromString(impl) = info;
}


} // namespace tainttracking
} // namespace webkit
