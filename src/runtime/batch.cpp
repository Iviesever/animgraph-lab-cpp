#include "animgraph/runtime/batch.hpp"

#include <algorithm>
#include <atomic>
#include <ranges>
#include <set>
#include <thread>

namespace animgraph {
namespace {

std::vector<const EvaluationJob*> ordered_jobs(std::span<const EvaluationJob> jobs) {
  std::vector<const EvaluationJob*> ordered;
  ordered.reserve(jobs.size());
  for (const auto& job : jobs) ordered.push_back(&job);
  std::ranges::stable_sort(ordered, {}, [](const EvaluationJob* job) { return job->character.value; });
  return ordered;
}

CharacterEvaluation run_job(const EvaluationJob& job) {
  CharacterEvaluation result;
  result.character = job.character;
  if (!job.context || !job.graph || !job.instance) {
    result.error = Error{ErrorCode::invalid_argument, "batch job contains a null dependency"};
    return result;
  }
  auto evaluated = evaluate(*job.context, *job.graph, *job.instance);
  if (!evaluated) result.error = evaluated.error();
  else result.result = std::move(*evaluated);
  return result;
}

}  // namespace

std::vector<CharacterEvaluation> evaluate_serial(std::span<const EvaluationJob> jobs) {
  const auto ordered = ordered_jobs(jobs);
  std::vector<CharacterEvaluation> results;
  results.reserve(ordered.size());
  for (const auto* job : ordered) results.push_back(run_job(*job));
  return results;
}

std::vector<CharacterEvaluation> evaluate_parallel(std::span<const EvaluationJob> jobs,
                                                   std::size_t workers,
                                                   std::stop_token external_stop) {
  const auto ordered = ordered_jobs(jobs);
  std::vector<CharacterEvaluation> results(ordered.size());
  for (std::size_t index = 0; index < ordered.size(); ++index) {
    results[index].character = ordered[index]->character;
    results[index].error = Error{ErrorCode::cancelled, "batch evaluation was cancelled"};
  }
  if (ordered.empty()) return results;
  if (workers == 0) {
    for (auto& result : results)
      result.error = Error{ErrorCode::invalid_argument, "worker count must be positive"};
    return results;
  }
  std::set<GraphInstance*> instances;
  for (const auto* job : ordered) {
    if (!job->instance || !instances.insert(job->instance).second) {
      for (auto& result : results)
        result.error = Error{ErrorCode::invalid_argument, "batch instances must be non-null and unique"};
      return results;
    }
  }
  if (external_stop.stop_requested()) return results;

  std::atomic<std::size_t> cursor{0};
  const std::size_t count = std::min(workers, ordered.size());
  std::vector<std::jthread> threads;
  threads.reserve(count);
  for (std::size_t worker = 0; worker < count; ++worker) {
    threads.emplace_back([&](std::stop_token local_stop) {
      while (!local_stop.stop_requested() && !external_stop.stop_requested()) {
        const auto index = cursor.fetch_add(1, std::memory_order_relaxed);
        if (index >= ordered.size()) break;
        results[index] = run_job(*ordered[index]);
      }
    });
  }
  for (auto& thread : threads) thread.join();
  return results;
}

}  // namespace animgraph
