//    _   _____   __________
//   | | / / _ | / __/_  __/     Visibility
//   | |/ / __ |_\ \  / /          Across
//   |___/_/ |_/___/ /_/       Space and Time
//
// SPDX-FileCopyrightText: (c) 2019 The VAST Contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "vast/system/transformer.hpp"

#include "vast/error.hpp"
#include "vast/logger.hpp"
#include "vast/plugin.hpp"
#include "vast/table_slice.hpp"
#include "vast/table_slice_builder.hpp"
#include "vast/table_slice_builder_factory.hpp"

#include <caf/attach_continuous_stream_stage.hpp>
#include <caf/settings.hpp>

namespace vast::system {

transform_step erase_step(const std::string& fieldname) {
  return [fieldname](vast::table_slice&& slice) -> caf::expected<table_slice> {
    // TODO: Make a specialized version of this for `arrow`-encoding.
    const auto& layout = slice.layout();
    // FIXME: Use `find()` to handle nested fields etc.
    // auto field = slice.layout().find(self->state.fieldname);
    // FIXME: handle multiple fields with the same name
    auto field = std::find_if(layout.fields.begin(), layout.fields.end(),
                              [&](const record_field& r) {
                                VAST_INFO(r.name);
                                return r.name == fieldname;
                              });
    if (field == layout.fields.end()) {
      VAST_WARN("did not fiend field {}", fieldname);
      return std::move(slice);
    }
    size_t erased_column = std::distance(layout.fields.begin(), field);
    auto modified_fields = layout.fields;
    modified_fields.erase(modified_fields.begin() + erased_column);
    vast::record_type modified_layout(modified_fields);
    modified_layout.name(layout.name());
    auto builder_ptr
      = factory<table_slice_builder>::make(slice.encoding(), modified_layout);
    builder_ptr->reserve(slice.rows());
    for (size_t i = 0; i < slice.rows(); ++i) {
      for (size_t j = 0; j < slice.columns(); ++j) {
        if (j == erased_column)
          continue;
        if (!builder_ptr->add(slice.at(i, j)))
          return caf::make_error(ec::unspecified, "erase step: unknown error "
                                                  "in table slice builder");
      }
    }
    VAST_WARN("returning new slice");
    return builder_ptr->finish();
  };
}

transformer_stream_stage_ptr
make_transform_stage(stream_sink_actor<table_slice>::pointer self,
                     std::vector<transform>&& transforms) {
  std::unordered_map<std::string, std::vector<size_t>> transforms_mapping;
  for (size_t i = 0; i < transforms.size(); ++i) {
    for (const auto& et : transforms[i].event_types) {
      transforms_mapping[et].push_back(i);
    }
  }
  return caf::attach_continuous_stream_stage(
    self,
    [](caf::unit_t&) {
      VAST_WARN("init stream");
      // nop
    },
    [self, transforms = std::move(transforms),
     transforms_mapping = std::move(transforms_mapping)](
      caf::unit_t&, caf::downstream<table_slice>& out, table_slice x) {
      auto offset = x.offset();
      VAST_WARN("applying {} transforms for received table slice w/ layout {}",
                transforms.size(), x.layout().name());
      const auto& matching = transforms_mapping.find(x.layout().name());
      if (matching == transforms_mapping.end()) {
        out.push(std::move(x));
        return;
      }
      for (auto idx : matching->second) {
        const auto& t = transforms.at(idx);
        // FIXME: Make 'transform::apply()' function
        VAST_WARN("applying {} steps of transform {}", t.steps.size(),
                  t.transform_name);
        for (const auto& step : t.steps) {
          auto transformed = step(std::move(x));
          if (!transformed) {
            VAST_ERROR("discarding data: error in transformation step");
            return;
          }
          x = std::move(*transformed);
        }
      }
      // TODO: Ideally we'd want to apply transform *before* the importer
      // assigns ids.
      x.offset(offset);
      VAST_WARN("pushing slice w/ offset {} size {}", x.offset(), x.rows());
      out.push(std::move(x));
    },
    [self](caf::unit_t&, const caf::error&) {
      // nop
    });
}

transformer_actor::behavior_type
transformer(transformer_actor::stateful_pointer<transformer_state> self,
            std::vector<transform>&& transforms) {
  self->state.stage = make_transform_stage(
    caf::actor_cast<stream_sink_actor<table_slice>::pointer>(self),
    std::move(transforms));
  return {[self](const stream_sink_actor<table_slice>& out) {
            VAST_WARN("transformer adding stream sink {}", out);
            self->state.stage->add_outbound_path(out);
          },
          [self](const stream_sink_actor<table_slice>& out,
                 int) -> caf::outbound_stream_slot<table_slice> {
            return self->state.stage->add_outbound_path(out);
          },
          [self](caf::stream<table_slice> in)
            -> caf::inbound_stream_slot<table_slice> {
            VAST_WARN("{} got a new stream source", self);
            return self->state.stage->add_inbound_path(in);
          }};
}

} // namespace vast::system