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
#include "vast/table_slice.hpp"

#include <caf/stream_stage.hpp>
#include <caf/typed_event_based_actor.hpp>
#include <memory>

namespace vast::system {

using transform_step = std::function<caf::expected<table_slice>(table_slice&&)>;

// TODO: Move to `vast/transform.hpp`.
struct transform_step_t {
  // typedefs
  using apply_fn = std::function<caf::expected<table_slice>(table_slice&&)>;

#if VAST_ENABLE_ARROW
  using arrow_apply_fn = std::function<std::shared_ptr<arrow::RecordBatch>(std::shared_ptr<arrow::RecordBatch>)>;
#endif

  // TODO: Add a specialized 

  // data members

  /// Handler mapping table_slice -> table_slice
  apply_fn generic_handler;

#if VAST_ENABLE_ARROW
  /// Optional: Optimized 
  std::optional<arrow_apply_fn> arrow_handler;
#endif
};

// This would be an alternative design based on virtual inheritance, but it
// has the disadvantage that transforms would always need to be passed around
// by pointer.

// struct transform_step_t {
//   virtual caf::expected<table_slice> operator()(table_slice&&) = 0;
// };

// #if VAST_ENABLE_ARROW

// struct arrow_transform_step : public transform_step_t {
//   virtual std::shared_ptr<arrow::RecordBatch> operator()(std::shared_ptr<arrow::RecordBatch>) = 0;
// };

// #endif

// Built-in transform steps.

/// Removes a field from a table slice
transform_step make_delete_step(const std::string& fieldname);

/// Replaces a field in the input by "xxx".
transform_step
make_replace_step(const std::string& fieldname, const std::string& value);

/// Replace a field in the input by its hash value.
//  TODO: Add an option to make the hash function configurable.
transform_step
make_anonymize_step(const std::string& fieldname, const std::string& salt = "");

// TODO: Move to `vast/transform.hpp`.
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

// FIXME: Rename this. and move to `vast/transform.hpp`.
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

// transformer_stream_stage_ptr
// make_transform_stage(typename stream_sink_actor<table_slice>::pointer,
//                      std::vector<transform>&&);

/// An actor containing a transform_stream_stage.
/// @param self The actor handle.
transformer_actor::behavior_type
transformer(transformer_actor::stateful_pointer<transformer_state> self,
            std::vector<transform>&&);

// Same as above, but to be inserted as a stream stage *before* the spawning actor.
using pre_transformer_actor
  = typed_actor_fwd<caf::reacts_to<int>>::
    extend_with<stream_sink_actor<table_slice>>::unwrap;

pre_transformer_actor::behavior_type
pre_transformer(pre_transformer_actor::stateful_pointer<transformer_state> self,
            std::vector<transform>&&,
            const stream_sink_actor<table_slice>& out);

} // namespace vast::system