#include "ecs.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <print>
#include <vector>

struct v3 {
  float x, y, z;
};

using test_data = std::array<int, 20>;

[[noreturn]] int main() {
  using namespace std::chrono;
  using namespace mm::ecs;

  std::srand(1234);

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
    if (i % 2 == 0) {
      continue;
    }
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
  {
    std::println("Testing ecs_view iteration with file output");
    auto start_view = steady_clock::now();
    size_t count = 0;
    volatile float sink = 0.0f;
    for (auto [e, v] : view<test_data, v3>(ecs)) {
      count++;
      auto &[data, val] = v;
      sink += val.x;
      for (auto k : data) {
        sink += k;
      }
    }
    auto end_view = steady_clock::now();
    std::println("Iterated over {} ECS entities in {:.3f} s (sink = {})", count,
                 duration<double>(end_view - start_view).count(), (float)sink);
  }

  // ---------------- SMART REF FUNCTIONAL TEST ----------------
  {
    std::println("Testing smart_ref correctness and performance");
    auto start_ref_tests = steady_clock::now();

    entity test_e = ecs.add_entity();
    v3 init_v{1.0f, 2.0f, 3.0f};
    ecs.add_component<v3, safety_policy::unchecked>(test_e, init_v);

    // baseline reference count
    auto &pool = std::get<mm::ecs::_private::component_pool<v3>>(ecs._data);
    size_t idx = pool.forward[test_e];
    auto before_refs = pool.refcounts[idx];

    // Construct
    {
      smart_ref<v3> r1(&pool, test_e);
      assert(r1.valid());
      assert(pool.refcounts[idx] == before_refs + 1);

      // Copy
      {
        smart_ref<v3> r2 = r1;
        assert(pool.refcounts[idx] == before_refs + 2);
        assert(&r1.get() == &r2.get());
      }

      // After copy destroyed
      assert(pool.refcounts[idx] == before_refs + 1);

      // Move
      {
        smart_ref<v3> r3 = std::move(r1);
        assert(r3.valid());
        assert(pool.refcounts[idx] == before_refs + 1);
      }

      // After move destroyed
      assert(pool.refcounts[idx] == before_refs);
    }

    // After all destroyed
    assert(pool.refcounts[idx] == before_refs);

    // Mutate via ref
    {
      smart_ref<v3> r4(&pool, test_e);
      r4->x += 10.0f;
      r4->y += 20.0f;
      r4->z += 30.0f;

      auto &val = ecs.get_component<v3>(test_e);
      assert(std::fabs(val.x - 11.0f) < 1e-5);
      assert(std::fabs(val.y - 22.0f) < 1e-5);
      assert(std::fabs(val.z - 33.0f) < 1e-5);
    }

    auto end_ref_tests = steady_clock::now();
    std::println("Smart_ref correctness tests completed in {:.6f} s",
                 duration<double>(end_ref_tests - start_ref_tests).count());
  }

  // ---------------- SMART REF PERFORMANCE TEST ----------------
  {
    std::println("Testing smart_ref performance");
    auto start_perf = steady_clock::now();

    std::vector<smart_ref<v3>> refs;
    refs.reserve(ENTITY_COUNT);

    for (const auto &[e, i] : entities) {
      if (ecs.has_component<v3>(e)) {
        refs.emplace_back(ecs.get_component<v3, reference_style::stable>(e));
      }
    }

    volatile float sink = 0.0f;

    auto start_access = steady_clock::now();
    for (auto &r : refs) {
      if (r.valid()) {
        sink += r->x + r->y + r->z;
      }
    }
    auto end_access = steady_clock::now();

    refs.clear();
    auto end_perf = steady_clock::now();

    std::println(
        "smart_ref access loop: {:.6f} s, total test: {:.6f} s (sink={:.3})",
        duration<double>(end_access - start_access).count(),
        duration<double>(end_perf - start_perf).count(), (float)sink);
  }

  // ---------------- TOTAL ----------------
  auto end_total = steady_clock::now();
  std::println("Total runtime: {:.3f} s",
               duration<double>(end_total - start_total).count());
}
