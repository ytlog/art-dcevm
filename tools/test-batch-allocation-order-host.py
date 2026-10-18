#!/usr/bin/env python3
"""Exercise the production final-hierarchy order independently of ART groups."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "platform/common/batch_allocation_order.h"
#include <stdexcept>
#include <iostream>
using namespace artdcevm;
void Check(std::vector<std::vector<size_t>> graph) {
  std::vector<size_t> order;
  if (!batch_allocation::Order(graph, &order)) throw std::runtime_error("valid DAG rejected");
  std::vector<size_t> positions(order.size());
  for (size_t i = 0; i < order.size(); ++i) positions[order[i]] = i;
  for (size_t i = 0; i < graph.size(); ++i)
    for (size_t parent : graph[i])
      if (positions[parent] >= positions[i]) throw std::runtime_error("dependency linked too late");
}
int main() {
  Check({{1}, {2}, {}});             // s1: C -> B -> A; one old subtree.
  Check({{1}, {}, {0}});             // s2 DAG; admission independently rejects both moves.
  Check({{1}, {2}, {}});             // s3: C -> B -> I across rebuild owners.
  Check({{1}, {2}, {3}, {}});         // s4: C -> B -> B2 -> A.
  Check({{1, 2}, {}, {}});           // s5: C -> B and I.
  Check({{1, 3}, {2}, {}, {}});      // s6: Impl -> Base -> B; Impl -> I.
  Check({{2}, {3}, {}, {}});         // Same descriptors in different loaders stay distinct.
  Check({{2}, {3}, {3}, {}});        // Group projection cycles; class graph is acyclic.
  Check({{1, 1}, {}}); Check({});    // Duplicate edge and empty batch.
  std::vector<size_t> unchanged{17};
  for (auto graph : std::vector<std::vector<std::vector<size_t>>>{{{0}}, {{1}, {0}}, {{2}}})
    if (batch_allocation::Order(graph, &unchanged) || unchanged != std::vector<size_t>{17})
      throw std::runtime_error("invalid graph accepted or partial order published");
  std::cout << "PASS: batch final hierarchy ordering\n";
}
'''

def main():
    evidence = ROOT / 'build/evidence/host/batch-allocation-order'
    evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=evidence) as work:
        source = Path(work) / 'test.cc'
        source.write_text(SOURCE)
        binary = Path(work) / 'test'
        build = subprocess.run([shutil.which('clang++') or 'c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
            '-I' + str(ROOT / 'artdcevm/src/main/cpp'), str(source), '-o', str(binary)], capture_output=True, text=True)
        (evidence / 'compile.log').write_text(build.stdout + build.stderr)
        if build.returncode: raise RuntimeError('Compilation failed; see local evidence')
        test = subprocess.run([str(binary)], capture_output=True, text=True)
        (evidence / 'test.log').write_text(test.stdout + test.stderr)
        if test.returncode: raise RuntimeError('Assertions failed; see local evidence')
        print(test.stdout.strip())

if __name__ == '__main__':
    main()
