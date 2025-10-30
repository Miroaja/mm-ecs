#include "ecs.h"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <print>
#include <vector>

struct v3 {
  float x, y, z;
};

using test_data = std::array<int, 20>;

[[noreturn]] int main() {
  using namespace std::chrono;
  using namespace mm::ecs;

  std::srand(1234); // reproducible random data

  ecs<v3, test_data> ecs;

  constexpr int ENTITY_COUNT = 1'000'000;
  constexpr int THRESHOLD = 700'000;

  std::println("Adding {} entities", ENTITY_COUNT);
  auto start_total = steady_clock::now();

  // ---------------- ENTITY CREATION ----------------
  auto start_entities = steady_clock::now();
  std::vector<std::pair<entity, int>> entities;
  entities.reserve(ENTITY_COUNT);
  for (int i = 1; i <= ENTITY_COUNT; i++) {
    entities.emplace_back(ecs.add_entity(), i);
  }
  auto end_entities = steady_clock::now();
  std::println("Created entities in {:.3f} s",
               duration<double>(end_entities - start_entities).count());

  // ---------------- BASELINE COMPUTE LOOP ----------------
  std::println("Running baseline compute loop");
  {
    auto start_baseline = steady_clock::now();
    std::vector<v3> v3s;
    std::vector<test_data> dta;
    volatile float sink = 0.0f;
    for (const auto &[_, i] : entities) {
      v3 tmp{std::sinf(i / 1'000'000.0f), std::cosf(i / 1'000'000.0f),
             i / 1'000'000.0f};
      sink += tmp.x + tmp.y + tmp.z;
      v3s.emplace_back(tmp);
      if (i > THRESHOLD) {
        test_data t{};
        for (auto &v : t)
          v = std::rand() % i;
        dta.emplace_back(t);
        sink += static_cast<float>(t[0]);
      }
    }
    auto end_baseline = steady_clock::now();
    std::println("Baseline math/random loop in {:.3} s  (sink={:.3})",
                 duration<double>(end_baseline - start_baseline).count(),
                 static_cast<float>(sink));
  }

  // ---------------- ECS COMPONENT ADDITION ----------------
  std::println("Adding components to ECS");
  auto start_components = steady_clock::now();
  for (const auto &[e, i] : entities) {
    ecs.add_component<v3, safety_policy::unchecked>(
        e, v3{std::sinf(i / 1'000'000.0f), std::cosf(i / 1'000'000.0f),
              i / 1'000'000.0f});
    if (i > THRESHOLD) {
      test_data tmp{};
      for (auto &v : tmp)
        v = std::rand() % i;
      ecs.add_component<test_data, safety_policy::unchecked>(e, std::move(tmp));
    }
    if (i < 100) {
      test_data tmp{};
      for (auto &v : tmp)
        v = std::rand() % i;
      if (auto result = ecs.add_component<test_data, safety_policy::checked>(
              e, std::move(tmp));
          !result)
        std::abort();
    }
  }
  auto end_components = steady_clock::now();
  std::println("ECS component add loop in {:.3} s",
               duration<double>(end_components - start_components).count());

  // ---------------- ECS COMPONENT REMOVAL TEST ----------------
  std::println("Testing safe vs unsafe component removal");
  auto start_removal = steady_clock::now();
  size_t remove_count = 0;
  for (const auto &[e, i] : entities) {
    if (i % 100'000 == 0) {
      // checked removal
      if (auto result =
              ecs.remove_component<test_data, safety_policy::checked>(e);
          !result) {
        std::println("Checked remove failed: {}", (int)result.error());
      } else {
        remove_count++;
      }
    } else {
      // unchecked removal
      if (ecs.has_component<test_data>(e)) {
        ecs.remove_component<test_data, safety_policy::unchecked>(e);
        remove_count++;
      }
    }
  }
  auto end_removal = steady_clock::now();
  std::println("Removed {} components in {:.3f} s", remove_count,
               duration<double>(end_removal - start_removal).count());
  // ---------------- ECS VIEW ITERATION ----------------
  std::println("Testing ecs_view iteration with file output");
  auto start_view = steady_clock::now();
  size_t count = 0;
  {
    std::ofstream output("ecs_view.txt");
    if (!output.is_open())
      std::abort();

    for (auto [e, v] : ecs_view<v3>(ecs)) {
      count++;
      auto [val] = v;
      std::println(output, "{},{}", e, val.x);
    }
  }
  auto end_view = steady_clock::now();
  std::println("Iterated over {} ECS entities and wrote output in {:.3f} s",
               count, duration<double>(end_view - start_view).count());

  // ---------------- TOTAL ----------------
  auto end_total = steady_clock::now();
  std::println("Total runtime: {:.3f} s",
               duration<double>(end_total - start_total).count());
}
