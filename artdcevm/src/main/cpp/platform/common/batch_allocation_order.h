#pragma once
#include <cstddef>
#include <vector>
#include <utility>

namespace artdcevm::batch_allocation {
// Indices identify rooted classes, never descriptors (loaders may share names).
// A node depends on its final direct superclass and direct interfaces. Keep
// holder/migration ownership independent of this allocation-only ordering.
inline bool Order(const std::vector<std::vector<size_t>>& dependencies,
                  std::vector<size_t>* output) {
  std::vector<std::vector<size_t>> children(dependencies.size());
  std::vector<size_t> incoming(dependencies.size());
  for (size_t i = 0; i < dependencies.size(); ++i) {
    for (size_t parent : dependencies[i]) {
      if (parent >= dependencies.size()) return false;
      children[parent].push_back(i);
      ++incoming[i];
    }
  }
  std::vector<size_t> order;
  for (size_t i = 0; i < incoming.size(); ++i)
    if (!incoming[i]) order.push_back(i);
  for (size_t head = 0; head < order.size(); ++head)
    for (size_t child : children[order[head]])
      if (!--incoming[child]) order.push_back(child);
  if (order.size() != dependencies.size()) return false;
  *output = std::move(order);
  return true;
}
}  // namespace artdcevm::batch_allocation
