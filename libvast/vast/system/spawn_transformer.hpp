//    _   _____   __________
//   | | / / _ | / __/_  __/     Visibility
//   | |/ / __ |_\ \  / /          Across
//   |___/_/ |_/___/ /_/       Space and Time
//
// SPDX-FileCopyrightText: (c) 2021 The VAST Contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "vast/fwd.hpp"

#include "vast/system/actors.hpp"
#include "vast/system/transformer.hpp"

#include <caf/expected.hpp>
#include <caf/typed_actor.hpp>

#include <vector>

namespace vast::system {

enum class transforms_location {
  server_import,
  server_export,
  client_source,
  client_sink,
};

caf::expected<std::vector<transform>>
parse_transforms(transforms_location, const caf::settings& args);

// FIXME: does this need to be in the header?
transform_step
make_step(const std::string& function, const caf::settings& opts);

/// Tries to spawn a new TRANSFORMER.
/// @param self Points to the parent actor.
/// @param args Configures the new actor.
/// @returns a handle to the spawned actor on success, an error otherwise
// FIXME: not sure if we need this function?
// caf::expected<caf::actor>
// spawn_transformer(node_actor::stateful_pointer<node_state> self,
//                   spawn_arguments& args);

} // namespace vast::system
