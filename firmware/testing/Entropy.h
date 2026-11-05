#pragma once
#include <stdint.h>
struct FakeEntropy {
  void Initialize() {}
  uint32_t random() {
    static uint32_t boot;
    return ++boot;
  }
};
inline FakeEntropy Entropy;
