//
//  region.cpp
//  Kraken Engine
//
//  Copyright 2026 Kearwood Gilbert. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are
//  permitted provided that the following conditions are met:
//
//  1. Redistributions of source code must retain the above copyright notice, this list of
//  conditions and the following disclaimer.
//
//  2. Redistributions in binary form must reproduce the above copyright notice, this list
//  of conditions and the following disclaimer in the documentation and/or other materials
//  provided with the distribution.
//
//  THIS SOFTWARE IS PROVIDED BY KEARWOOD GILBERT ''AS IS'' AND ANY EXPRESS OR IMPLIED
//  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
//  FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL KEARWOOD GILBERT OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
//  ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
//  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//  The views and conclusions contained in the software and documentation are those of the
//  authors and should not be interpreted as representing official policies, either expressed
//  or implied, of Kearwood Gilbert.
//

#include "../include/mimir.h"
#include "mimir_impl.h"

#include <cassert>
#include <cstring>
#include <bit>

namespace mimir {

Region::Region()
  : m_data(nullptr)
  , m_committedSize(0)
  , m_maxSize(0)
{

}

Region::~Region()
{
  if (m_data) {
#if defined(_WIN32) || defined(_WIN64)
    if (!VirtualFree((void*)m_data, 0, MEM_RELEASE)) {
      ReportWindowsLastError("VirtualAlloc");
    }
#elif defined(__unix__) || defined(__APPLE__) || defined(ANDROID)
    munmap(m_data, KRAKEN_MEM_ROUND_UP_PAGE(m_maxSize));
#else
    static_assert(false, "Not Implemented");
#endif
  }
}

bool Region::init(size_t maxSize)
{
  assert(m_data == nullptr);
  assert(m_committedSize == 0);

  m_maxSize = KRAKEN_MEM_ROUND_UP_PAGE(maxSize);

#if defined(_WIN32) || defined(_WIN64)
  m_data = (std::byte*)VirtualAlloc(NULL, KRAKEN_MEM_ROUND_UP_PAGE(m_maxSize), MEM_RESERVE, PAGE_NOACCESS);
  if (m_data == nullptr) {
    ReportWindowsLastError("VirtualAlloc");
    return false;
  }
#elif defined(__unix__) || defined(__APPLE__) || defined(ANDROID)
  m_data = (std::byte*)mmap(NULL, KRAKEN_MEM_ROUND_UP_PAGE(m_maxSize), PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (m_data == MAP_FAILED) {
    return false;
  }
#else
  static_assert(false, "Not Implemented");
#endif
  return true;
}
 
bool Region::resize(size_t size)
{
  size_t newSize = KRAKEN_MEM_ROUND_UP_PAGE(size);

  if (newSize == m_committedSize) {
    return true;
  }

  if (newSize > m_maxSize) {
    return false;
  }

  if (newSize > m_committedSize) {
    // ---- Growing ----
#if defined(_WIN32) || defined(_WIN64)
    if (VirtualAlloc(m_data + m_committedSize, newSize - m_committedSize, MEM_COMMIT, PAGE_READWRITE) == nullptr) {
      ReportWindowsLastError("VirtualAlloc");
      return false;
    }
#elif defined(__unix__) || defined(__APPLE__) || defined(ANDROID)
    if (mprotect(m_data + m_committedSize, newSize - m_committedSize, PROT_READ | PROT_WRITE) != 0) {
      return false;
    }
#else
    static_assert(false, "Not Implemented");
#endif
    m_committedSize = newSize;
    return true;
  } // if (newSize > m_committedSize)

  // ---- Shrinking ----
  std::byte* rangeStart = m_data + newSize;
  size_t rangeLen = m_committedSize - newSize;
#if defined(_WIN32) || defined(_WIN64)
  if (!VirtualAlloc(rangeStart, rangeLen, MEM_RESET, PAGE_NOACCESS)) {
    ReportWindowsLastError("VirtualAlloc");
  } else {
    m_committedSize = newSize;
  }
#elif defined(__unix__) || defined(__APPLE__) || defined(ANDROID)
  if (madvise(rangeStart, rangeLen, MADV_DONTNEED) != 0) {
    // TODO - Log Error or debug build assert
  }
  if (mprotect(rangeStart, rangeLen, PROT_NONE) != 0) {
    // TODO - Log Error or debug build assert
  }
#else
  static_assert(false, "Not Implemented");
#endif
  m_committedSize = newSize;

  return true;
}

size_t Region::getSize() const
{
  return m_committedSize;
}

size_t Region::getMaxSize() const
{
  return m_maxSize;
}

std::byte* Region::getAddress() const
{
  return m_data;
}

bool Region::maybeGrow(size_t minSize)
{
  if (minSize < m_committedSize) {
    // Already big enough
    return true;
  }

  if (minSize > m_maxSize) {
    // We can't grow this big
    return false;
  }

  // We need to grow.
  // Increase the size exponentially, capped to m_maxSize
  size_t newSize = std::bit_ceil(minSize) << 1;
  newSize = std::min(m_maxSize, newSize);
  
  return resize(newSize);
}

void Region::maybeShrink(size_t targetSize)
{
  size_t thresholdSize = targetSize << 1; // The threshold for shrinking is greater than the target to implement hysteresis
  if (getSize() > thresholdSize) {
    resize(targetSize);
  }
}

} // namespace mimir
