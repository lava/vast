//    _   _____   __________
//   | | / / _ | / __/_  __/     Visibility
//   | |/ / __ |_\ \  / /          Across
//   |___/_/ |_/___/ /_/       Space and Time
//
// SPDX-FileCopyrightText: (c) 2021 The VAST Contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "vast/data.hpp"
#include "vast/error.hpp"
#include "vast/logger.hpp"
#include "vast/plugin.hpp"
#include "vast/table_slice.hpp"
#include "vast/table_slice_builder_factory.hpp"

#include <caf/actor_cast.hpp>
#include <caf/actor_system_config.hpp>
#include <caf/attach_stream_sink.hpp>
#include <caf/settings.hpp>
#include <caf/typed_event_based_actor.hpp>

#include <iostream>

namespace vast::plugins {

/// An example plugin.
class community_id_plugin final : public virtual transform_plugin {
public:
  // plugin API
  caf::error initialize(data) override {
    return {};
  }

  [[nodiscard]] const char* name() const override {
    return "community_id";
  };

  // transform plugin API
  [[nodiscard]] transform_step
  make_transform_step(const caf::settings&) const override {
    return [](table_slice&& slice) -> caf::expected<table_slice> {
      auto layout = slice.layout();
      // TODO: Get fieldname from config.
      layout.fields.emplace_back("community_id", vast::string_type{});
      auto builder_ptr
        = factory<table_slice_builder>::make(slice.encoding(), layout);
      for (size_t i = 0; i < slice.rows(); ++i) {
        for (size_t j = 0; j < slice.columns(); ++j) {
          if (!builder_ptr->add(slice.at(i, j)))
            return caf::make_error(ec::unspecified,
                                   "community_id: unknown error "
                                   "in table slice builder");
        }
        if (!builder_ptr->add("community!"))
          return caf::make_error(ec::unspecified,
                                 "commnity_id: unknown error "
                                 "in table slice builder while adding string");
      }
      return builder_ptr->finish();
    };
  }
};

} // namespace vast::plugins

// Register the example_plugin with version 0.1.0-0.
VAST_REGISTER_PLUGIN(vast::plugins::community_id_plugin, 0, 1, 0, 0)
