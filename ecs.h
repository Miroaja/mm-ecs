#pragma once
#include <algorithm>
#include <any>
#include <array>
#include <cassert>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <print>
#include <type_traits>
#include <utility>
#include <vector>

namespace mm {
namespace ecs {
enum class error {
  component_already_exists,
  component_does_not_exist,
  no_such_entity
};
enum class remove_policy { strict, lax };
enum class safety_policy { checked, unchecked };
enum class reference_style { raw, stable };

using entity = uint32_t;
constexpr entity invalid_entity = std::numeric_limits<entity>::max();

namespace _private {
using component_id = uint32_t;

constexpr size_t invalid_component_index = std::numeric_limits<size_t>::max();
template <typename C> struct component_pool {
  std::vector<C> data = {};
  std::vector<entity> back = {};
  std::vector<size_t> forward = {};

  template <typename... Args>
  inline std::expected<void, error> add_element(entity e, Args &&...args) {
    if (e >= forward.size()) {
      forward.resize(e + 1, invalid_component_index);
    } else if (forward[e] != invalid_component_index) {
      return std::unexpected(error::component_already_exists);
    }

    forward[e] = back.size();
    back.push_back(e);

    if constexpr (sizeof...(Args) == 0) {
      data.emplace_back();
    } else {
      data.emplace_back(std::forward<Args>(args)...);
    }

    return {};
  }

  template <typename... Args>
  inline void add_element_fast(entity e, Args &&...args) {
    assert(e != invalid_entity);
    if (e >= forward.size()) {
      forward.resize(e + 1, invalid_component_index);
    }

    forward[e] = back.size();
    back.push_back(e);

    if constexpr (sizeof...(Args) == 0) {
      data.emplace_back();
    } else {
      data.emplace_back(std::forward<Args>(args)...);
    }
  }

  inline std::expected<void, error> remove_element(entity e) {
    if (forward.size() == 0 || back.size() == 0) {
      return std::unexpected(error::component_does_not_exist);
    }
    if (forward[e] == invalid_component_index) {
      return std::unexpected(error::component_does_not_exist);
    }

    remove_element_fast(e);

    return {};
  }

  inline void remove_element_fast(entity e) {
    size_t idx = forward[e];
    size_t last_idx = back.size() - 1;

    if (idx != last_idx) {
      std::swap(back[idx], back[last_idx]);
      std::swap(data[idx], data[last_idx]);
      forward[back[idx]] = idx;
    }
    back.pop_back();
    data.pop_back();
    forward[e] = invalid_component_index;
  }

  inline std::expected<C &, error> get_element(entity e) {
    if (forward.size() == 0 || back.size() == 0) {
      return std::unexpected(error::component_does_not_exist);
    }
    if (forward[e] == invalid_component_index) {
      return std::unexpected(error::component_does_not_exist);
    }

    return get_element_fast(e);
  }
  inline C &get_element_fast(entity e) { return data[forward[e]]; }

  inline bool has_component(entity e) const {
    if (forward.size() == 0 || back.size() == 0) {
      return false;
    }

    return forward[e] != invalid_component_index;
  }
};
} // namespace _private

template <typename... Cs> struct ecs {
  template <safety_policy P>
  using method_result_void_t =
      std::conditional_t<P == safety_policy::unchecked, void,
                         std::expected<void, error>>;

  template <reference_style S, safety_policy P, typename C>
  using method_result_ref_t = std::conditional_t<
      P == safety_policy::unchecked, void,
      std::expected<std::conditional_t<S == reference_style::raw, C &,
                                       C & /* TODO:  make this smart ref*/>,
                    error>>;

  template <typename C, safety_policy policy = safety_policy::unchecked,
            typename... Ts>
  inline method_result_void_t<policy> add_component(entity e, Ts &&...ts) {
    if constexpr (policy == safety_policy::checked) {
      if (std::find(_entities.cbegin(), _entities.cend(), e) ==
          _entities.cend()) {
        return std::unexpected(error::no_such_entity);
      }
    }

    _private::component_pool<C> &pool =
        std::get<_private::component_pool<C>>(_data);

    if constexpr (policy == safety_policy::checked) {
      return pool.add_element(e, std::forward<Ts>(ts)...);
    } else {
      pool.add_element_fast(e, std::forward<Ts>(ts)...);
    }
  }

  template <typename C, safety_policy policy = safety_policy::unchecked>
  inline method_result_void_t<policy> remove_component(entity e) {
    if constexpr (policy == safety_policy::checked) {
      if (std::find(_entities.cbegin(), _entities.cend(), e) ==
          _entities.cend()) {
        return std::unexpected(error::no_such_entity);
      }
    }
    _private::component_pool<C> &pool =
        std::get<_private::component_pool<C>>(_data);

    if constexpr (policy == safety_policy::checked) {
      return pool.remove_element(e);
    } else {
      pool.remove_element_fast(e);
    }
  }

  template <typename C, reference_style style = reference_style::raw,
            safety_policy policy = safety_policy::unchecked>
  inline method_result_ref_t<style, policy, C> get_component(entity e) {
    if constexpr (policy == safety_policy::checked) {
      if (std::find(_entities.cbegin(), _entities.cend(), e) ==
          _entities.cend()) {
        return std::unexpected(error::no_such_entity);
      }
    }

    _private::component_pool<C> &pool =
        std::get<_private::component_pool<C>>(_data);

    if constexpr (policy == safety_policy::checked) {
      return pool.get_element(e);
    } else {
      return pool.get_element_fast(e);
    }
  }

  template <typename C> bool has_component(entity e) {
    _private::component_pool<C> &pool =
        std::get<_private::component_pool<C>>(_data);
    return pool.has_component(e);
  }

  template <remove_policy rem_policy = remove_policy::lax,
            safety_policy saf_policy = safety_policy::unchecked,
            typename... Ccs>
  inline method_result_void_t<saf_policy> remove_components(entity e) {
    if (std::find(_entities.cbegin(), _entities.cend(), e) ==
        _entities.cend()) {
      return std::unexpected(error::no_such_entity);
    }

    auto checked_remove =
        []<typename C>(const _private::component_pool<C> &pool,
                       entity e) -> method_result_void_t<saf_policy> {
      if (pool.has_component(e)) {
        if constexpr (saf_policy == safety_policy::checked) {
          return pool.remove_element(e);
        } else {
          pool.remove_element_fast(e);
        }
      }
      if constexpr (saf_policy == safety_policy::checked) {
        if constexpr (rem_policy == remove_policy::lax) {
          return {};
        } else {
          return std::unexpected(error::component_does_not_exist);
        }
      }
    };

    if constexpr (saf_policy == safety_policy::checked) {
      std::expected<void, error> res{};

      auto try_remove = [&](auto &pool) {
        if (!res)
          return;
        res = checked_remove(pool, e);
      };

      (try_remove(std::get<_private::component_pool<Ccs>>(_data)), ...);

      return res;
    } else {
      (checked_remove(std::get<_private::component_pool<Ccs>>(_data), e), ...);
    }
  }

  inline entity add_entity() {
    _entities.push_back(_entity_counter++);
    return _entities.back();
  }

  template <safety_policy policy = safety_policy::unchecked>
  inline method_result_void_t<policy> remove_entity(entity e) {
    if (auto result = std::find(_entities.cbegin(), _entities.cend(), e);
        result != _entities.cend()) {
      _entities.erase(result);
    } else {
      if constexpr (policy == safety_policy::checked) {
        return std::unexpected(error::no_such_entity);
      }
    }

    if constexpr (policy == safety_policy::checked) {
      if (auto result = remove_components<remove_policy::lax,
                                          safety_policy::checked, Cs...>(e);
          !result) {
        return result;
      }
      return {};
    } else {
      remove_components<Cs...>(e);
    }
  }

private:
  std::vector<entity> _entities = {};
  entity _entity_counter = 0;
  std::tuple<_private::component_pool<Cs>...> _data = {};

  template <typename... Ccs> friend struct ecs_view;
};

template <typename... Ccs> struct ecs_view {
  ecs_view() = delete;
  template <typename... Cs>
  inline ecs_view(ecs<Cs...> &c)
      : _pools({std::get<_private::component_pool<Ccs>>(c._data)...}) {}

  struct iterator {
    inline iterator(ecs_view<Ccs...> &view, size_t index)
        : _view(view), _index(index) {}

    inline iterator &operator++() {
      auto &pool = _view._smallest_pool();
      ++_index;
      _skip_non_matching(pool);
      return *this;
    }

    inline bool operator!=(const iterator &other) const {
      return _index != other._index;
    }

    inline std::pair<entity, std::tuple<Ccs &...>> operator*() const {
      auto &pool = _view._smallest_pool();
      auto e = pool.back[_index];
      return {e, _view._get_components(e)};
    }

  private:
    inline void _skip_non_matching(auto &smallest) {
      while (_index < smallest.back.size()) {
        if (_has_all(smallest.back[_index])) {
          break;
        }
        ++_index;
      }
    }

    bool _has_all(entity e) {
      return _has_all_impl(e, std::index_sequence_for<Ccs...>{});
    }

    template <std::size_t... I>
    bool _has_all_impl(entity e, std::index_sequence<I...>) {
      return (... && std::get<I>(_view._pools).has_component(e));
    }

    ecs_view<Ccs...> &_view;
    size_t _index = _private::invalid_component_index;
  };

  inline iterator begin() { return iterator(*this, 0); }
  inline iterator end() {
    auto &pool = _smallest_pool();
    return iterator(*this, pool.back.size());
  }

private:
  auto &_smallest_pool() {
    return _smallest_pool_impl(std::index_sequence_for<Ccs...>{});
  }

  template <std::size_t... I>
  auto &_smallest_pool_impl(std::index_sequence<I...>) {
    auto *min_pool = &std::get<0>(_pools);
    size_t min_size = std::get<0>(_pools).back.size();
    ((std::get<I>(_pools).back.size() < min_size
          ? (min_size = std::get<I>(_pools).back.size(),
             min_pool = &std::get<I>(_pools))
          : 0),
     ...);
    return *min_pool;
  }

  std::tuple<Ccs &...> _get_components(entity e) const {
    return _get_components_impl(e, std::index_sequence_for<Ccs...>{});
  }

  template <std::size_t... I>
  std::tuple<Ccs &...> _get_components_impl(entity e,
                                            std::index_sequence<I...>) const {
    return {std::get<I>(_pools).get_element_fast(e)...};
  }

  std::tuple<_private::component_pool<Ccs> &...> _pools;
};

}; // namespace ecs
} // namespace mm
