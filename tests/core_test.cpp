// SPDX-License-Identifier: Apache-2.0
#include "pcbir/core/arena.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/core/handle.hpp"
#include "pcbir/core/snapshot.hpp"
#include "pcbir/core/snapshot_version.hpp"
#include "pcbir/core/workspace.hpp"

#include <cstdint>
#include <type_traits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

struct Widget {
  int32_t value = 0;
};

struct Gadget {
  int32_t value = 0;
};

} // namespace

using pcbir::core::Arena;
using pcbir::core::EntityId;
using pcbir::core::Handle;
using pcbir::core::Snapshot;
using pcbir::core::SnapshotVersion;
using pcbir::core::Workspace;

TEST_CASE("Handle is trivially copyable and null by default", "[core][handle]") {
  static_assert(std::is_trivially_copyable_v<Handle<Widget>>);
  static_assert(std::is_trivially_copyable_v<EntityId>);
  static_assert(std::is_trivially_copyable_v<SnapshotVersion>);

  const Handle<Widget> h;
  REQUIRE(h.is_null());
  REQUIRE(h.generation() == 0);
}

TEST_CASE("Arena insert/get round-trips a value", "[core][arena]") {
  Arena<Widget> arena;
  const Handle<Widget> h = arena.insert(Widget{42}, EntityId{1});

  REQUIRE_FALSE(h.is_null());
  REQUIRE(arena.get(h).value == 42);
  REQUIRE(arena.size() == 1);
  REQUIRE(arena.id_of(h) == EntityId{1});
}

TEST_CASE("Arena erase invalidates the handle and frees the id", "[core][arena]") {
  Arena<Widget> arena;
  const Handle<Widget> h = arena.insert(Widget{7}, EntityId{1});

  arena.erase(h);

  REQUIRE(arena.try_get(h) == nullptr);
  REQUIRE(arena.size() == 0);
  REQUIRE(arena.find(EntityId{1}).is_null());
}

TEST_CASE("Arena reuses erased slots with a bumped generation", "[core][arena]") {
  Arena<Widget> arena;
  const Handle<Widget> first = arena.insert(Widget{1}, EntityId{1});
  arena.erase(first);
  const Handle<Widget> second = arena.insert(Widget{2}, EntityId{2});

  REQUIRE(second.index() == first.index());
  REQUIRE(second.generation() != first.generation());
  REQUIRE(arena.try_get(first) == nullptr); // Stale handle, even at a reused index.
  REQUIRE(arena.get(second).value == 2);
}

TEST_CASE("Arena.find locates a handle by its stable entity id", "[core][arena]") {
  Arena<Widget> arena;
  const Handle<Widget> h = arena.insert(Widget{9}, EntityId{5});

  REQUIRE(arena.find(EntityId{5}) == h);
}

TEST_CASE("Workspace insert/commit exposes the entity through a read-only snapshot",
          "[core][workspace]") {
  Workspace<Widget> ws;
  const Handle<Widget> h = ws.insert(Widget{100});

  const Snapshot<Widget> snap = ws.commit();

  REQUIRE(snap.table<Widget>().get(h).value == 100);
  REQUIRE(snap.version() == SnapshotVersion{1});
}

TEST_CASE("Committing again does not mutate a previously taken snapshot",
          "[core][workspace][determinism]") {
  Workspace<Widget> ws;
  const Handle<Widget> h1 = ws.insert(Widget{1});
  const Snapshot<Widget> base = ws.commit();

  ws.insert(Widget{2});
  const Snapshot<Widget> next = ws.commit();

  REQUIRE(base.table<Widget>().size() == 1);
  REQUIRE(next.table<Widget>().size() == 2);
  REQUIRE(base.table<Widget>().get(h1).value == 1);
  REQUIRE(next.version().value() == base.version().value() + 1);
}

TEST_CASE("Untouched component tables are structurally shared across a commit",
          "[core][workspace][determinism]") {
  Workspace<Widget, Gadget> ws;
  ws.insert(Widget{1});
  ws.insert(Gadget{2});
  const Snapshot<Widget, Gadget> base = ws.commit();

  ws.insert(Gadget{3}); // Only Gadget's table is touched.
  const Snapshot<Widget, Gadget> next = ws.commit();

  // Widget's arena was never written to after `base`, so it is the *same*
  // shared object, not a copy -- structural sharing (docs/architecture.md).
  REQUIRE(&base.table<Widget>() == &next.table<Widget>());
  REQUIRE(&base.table<Gadget>() != &next.table<Gadget>());
}

TEST_CASE("Snapshot iteration order is canonical (insertion order)", "[core][determinism]") {
  Workspace<Widget> ws;
  ws.insert(Widget{10});
  ws.insert(Widget{20});
  ws.insert(Widget{30});
  const Snapshot<Widget> snap = ws.commit();

  std::vector<int32_t> visited;
  snap.table<Widget>().for_each(
      [&visited](EntityId, const Widget& w) { visited.push_back(w.value); });

  REQUIRE(visited == std::vector<int32_t>{10, 20, 30});
}

TEST_CASE("Replaying the same edits into two workspaces is deterministic", "[core][determinism]") {
  auto build = []() {
    Workspace<Widget> ws;
    ws.insert(Widget{1});
    ws.insert(Widget{2});
    ws.insert(Widget{3});
    return ws.commit();
  };

  const Snapshot<Widget> a = build();
  const Snapshot<Widget> b = build();

  std::vector<int32_t> values_a;
  std::vector<int32_t> values_b;
  a.table<Widget>().for_each(
      [&values_a](EntityId, const Widget& w) { values_a.push_back(w.value); });
  b.table<Widget>().for_each(
      [&values_b](EntityId, const Widget& w) { values_b.push_back(w.value); });

  REQUIRE(values_a == values_b);
  REQUIRE(a.version() == b.version());
}
