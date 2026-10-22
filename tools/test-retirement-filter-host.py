#!/usr/bin/env python3
"""Verify that the production retirement filter never hides a registered method."""
from pathlib import Path
import shutil
import subprocess
from evidence_paths import new_evidence_directory
ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "platform/common/method_retirement_filter.h"
#include <atomic>
#include <iostream>
#include <thread>
#include <unordered_set>
#include <vector>
#include <cstdlib>
using artdcevm::member_retirement::MethodRetirementFilter;
const void* Method(uint64_t n) { return reinterpret_cast<const void*>(uintptr_t(0x10000000)+n*64); }
void Require(bool value) { if(!value) std::abort(); }
int main() {
 MethodRetirementFilter filter;
 std::unordered_set<const void*> exact;
 for(uint64_t i=1;i<=100000;++i) Require(!filter.MayContain(Method(i)));
 std::cout << "Empty filter PASS\n";
 for(uint64_t i=1;i<=100000;i+=3) {filter.Add(Method(i));exact.insert(Method(i));}
 for(uint64_t i=1;i<=100000;++i) {
  bool actual=filter.MayContain(Method(i)) && exact.count(Method(i));
  Require(actual==bool(exact.count(Method(i))));
 }
 std::cout << "Collisions preserve exact membership PASS\n";
 for(uint64_t i=1;i<=200000;++i) filter.Add(Method(i));
 for(uint64_t i=1;i<=200000;++i) Require(filter.MayContain(Method(i)));
 std::cout << "Saturation never hides a retired method PASS\n";
 MethodRetirementFilter shared;
 constexpr size_t writers=4, count=10000;
 std::vector<std::atomic<bool>> published(writers*count);
 for(auto& p:published)p.store(false,std::memory_order_relaxed);
 std::atomic<bool> start{false};
 std::vector<std::thread> workers;
 for(size_t w=0;w<writers;++w)workers.emplace_back([&,w] {
  while(!start.load(std::memory_order_acquire))std::this_thread::yield();
  for(size_t i=0;i<count;++i) {size_t n=w*count+i;shared.Add(Method(n+1));published[n].store(true,std::memory_order_release);}
 });
 for(size_t w=0;w<writers;++w)workers.emplace_back([&,w] {
  while(!start.load(std::memory_order_acquire))std::this_thread::yield();
  for(size_t i=0;i<count;++i) {size_t n=w*count+i;while(!published[n].load(std::memory_order_acquire))std::this_thread::yield();Require(shared.MayContain(Method(n+1)));}
 });
 start.store(true,std::memory_order_release);
 for(auto& t:workers)t.join();
 std::cout << "Concurrent publication never hides a registered method PASS\n";
}
'''
def main():
    out = new_evidence_directory(ROOT / 'build/evidence/host/retirement-filter')
    source = out / 'filter.cc'; binary = out / 'filter'; source.write_text(SOURCE)
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler: raise RuntimeError('C++ compiler unavailable')
    subprocess.run([compiler, '-std=c++17', '-O2', '-pthread', '-I'+str(ROOT/'artdcevm/src/main/cpp'), str(source), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], check=True, text=True, capture_output=True)
    (out/'results.txt').write_text(result.stdout)
    print(result.stdout, end=''); print('Evidence: '+str(out.relative_to(ROOT)))
if __name__ == '__main__': main()
