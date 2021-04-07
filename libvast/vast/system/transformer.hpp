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

// FIXME: would it make sense to use virtual inheritance for this?
using transform_step = std::function<caf::expected<table_slice>(table_slice&&)>;

// Built-in transform steps.

/// Removes a field from a table slice
transform_step make_delete_step(const std::string& fieldname);

/// Replaces a field in the input by "xxx".
transform_step make_anonymize_step(const std::string& fieldname);

/// Replace a field in the input by its hash value.
//  TODO: Add an option to make the hash function configurable.
transform_step make_pseudonymize_step(const std::string& fieldname,
                                      const std::string& salt = "");

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

// FIXME: Rename this.
struct transformation_engine {
  // member functions

  /// Constructor.
  transformation_engine() = default;
  explicit transformation_engine(std::vector<transform>&&);

  /// Apply relevant transformations to the table slice.
  caf::expected<table_slice> apply(table_slice&&) const;

private:
  /// The set of transforms
  std::vector<transform> transforms_;

  /// event type -> applicable transforms
  std::unordered_map<std::string, std::vector<size_t>> layout_mapping_;
};

struct transformer_state {
  /// The transforms that can be applied.
  transformation_engine transforms;

  /// The stream stage
  transformer_stream_stage_ptr stage;

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