//    _   _____   __________
//   | | / / _ | / __/_  __/     Visibility
//   | |/ / __ |_\ \  / /          Across
//   |___/_/ |_/___/ /_/       Space and Time
//
// SPDX-FileCopyrightText: (c) 2016 The VAST Contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "vast/fwd.hpp"

#include "vast/system/actors.hpp"
#include "vast/system/sink.hpp"

#include <caf/stream_stage.hpp>
#include <caf/typed_event_based_actor.hpp>

namespace vast::system {

using transform_step = std::function<caf::expected<table_slice>(table_slice&&)>;

// OR

// struct transform_step {
// 	virtual ~transform_step() = default;
// 	virtual table_slice transform(table_slice&&);
// };

// struct erase_step : public transform_step {
//   // FIXME: This probably needs a qualified fieldname?
//   std::string fieldname;
// };

// struct add_step : public transform_step {
//   // FIXME
// };

// struct modify_step : public transform_step {
//   // FIXME
// };

// Built-in transform steps.

/// Removes a field from a table slice
transform_step make_delete_step(const std::string& fieldname);

/// Replaces a field in the input by "xxx".
transform_step make_anonymize_step(const std::string& fieldname);

/// Replace a field in the input by its hash value.
//  TODO: Add an option to make the hash function configurable.
transform_step make_pseudonymize_step(const std::string& fieldname,
                                      const std::string& salt = "");

// transform_step add_community_id_step(const std::string& out, const
// std::string& )

// TODO: Do these make sense? Or should we just make a builtin hash_step?
// transform_step add_step(const std::string& fieldname, );
// transform_step modify_step(const std::string& fieldname);

struct transform {
  /// Sequence of transformation steps
  std::vector<transform_step> steps;

  /// triggers for this transform
  std::vector<std::string> event_types;

  /// Name assigned to this transformation.
  std::string transform_name;

  caf::expected<table_slice> apply(table_slice&&);
};

using transformer_stream_stage_ptr
  = caf::stream_stage_ptr<table_slice,
                          caf::broadcast_downstream_manager<table_slice>>;

struct transformer_state {
  /// The transform that can might be applied
  std::vector<transform> transforms;

  transformer_stream_stage_ptr stage;

  // TODO: add LRU cache for layout -> subset of transforms

  /// Name of the TRANSFORMER actor.
  static constexpr const char* name = "transformer";
};

transformer_stream_stage_ptr
make_transform_stage(typename stream_sink_actor<table_slice>::pointer,
                     std::vector<transform>&&);

/// An actor containing a transform_stream_stage.
/// @param self The actor handle.
transformer_actor::behavior_type
transformer(transformer_actor::stateful_pointer<transformer_state> self,
            std::vector<transform>&&);

} // namespace vast::system