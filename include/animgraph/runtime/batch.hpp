#pragma once

#include "animgraph/runtime/runtime.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace animgraph {

struct CharacterId { std::uint64_t value{}; auto operator<=>(const CharacterId&) const = default; };
struct EvaluationJob {
  CharacterId character;
  const EvaluationContext* context{};
  const CompiledGraph* graph{};
  GraphInstance* instance{};
};
struct CharacterEvaluation {
  CharacterId character;
  EvaluationResult result;
  std::optional<Error> error;
};

[[nodiscard]] std::vector<CharacterEvaluation> evaluate_serial(
    std::span<const EvaluationJob> jobs);
[[nodiscard]] std::vector<CharacterEvaluation> evaluate_parallel(
    std::span<const EvaluationJob> jobs, std::size_t workers,
    std::stop_token external_stop = {});

class CharacterBatchEvaluator {
 public:
  [[nodiscard]] static std::vector<CharacterEvaluation> serial(
      std::span<const EvaluationJob> jobs) { return evaluate_serial(jobs); }
  [[nodiscard]] static std::vector<CharacterEvaluation> parallel(
      std::span<const EvaluationJob> jobs, std::size_t workers,
      std::stop_token stop = {}) { return evaluate_parallel(jobs, workers, stop); }
};

}  // namespace animgraph
