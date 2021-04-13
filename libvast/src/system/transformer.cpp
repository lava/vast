//    _   _____   __________
//   | | / / _ | / __/_  __/     Visibility
//   | |/ / __ |_\ \  / /          Across
//   |___/_/ |_/___/ /_/       Space and Time
//
// SPDX-FileCopyrightText: (c) 2019 The VAST Contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "vast/system/transformer.hpp"

#include "vast/detail/overload.hpp"
#include "vast/error.hpp"
#include "vast/logger.hpp"
#include "vast/plugin.hpp"
#include "vast/table_slice.hpp"
#include "vast/table_slice_builder.hpp"
#include "vast/table_slice_builder_factory.hpp"

#include <caf/attach_continuous_stream_stage.hpp>
#include <caf/settings.hpp>

namespace vast::system {

transform_step delete_step(const std::string& fieldname) {
  return [fieldname](vast::table_slice&& slice) -> caf::expected<table_slice> {
    // TODO: Make a specialized version of this for `arrow`-encoding.
    const auto& layout = slice.layout();
    // FIXME: Use `find()` to handle nested fields etc.
    // auto field = slice.layout().find(self->state.fieldname);
    // FIXME: handle multiple fields with the same name
    auto field = std::find_if(layout.fields.begin(), layout.fields.end(),
                              [&](const record_field& r) {
                                return r.name == fieldname;
                              });
    if (field == layout.fields.end())
      return std::move(slice);
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
          return caf::make_error(ec::unspecified, "delete step: unknown error "
                                                  "in table slice builder");
      }
    }
    return builder_ptr->finish();
  };
}

struct anonymize_step {
  anonymize_step(const std::string& fieldname, const std::string& value)
    : field_(fieldname), value_(value) {
  }

  vast::data anonymize(const vast::data_view&) {
    return value_;
  }

  // TODO: `anonymize` and `pseudonymize` use almost the same code.
  // This could probably be combined into a generic `modify_step`.
  caf::expected<table_slice> operator()(table_slice&& slice) {
    const auto& fields = slice.layout().fields;
    // FIXME: Use `find()` to handle nested fields etc.
    // auto field = slice.layout().find(self->state.fieldname);
    // FIXME: handle multiple fields with the same name
    auto field
      = std::find_if(fields.begin(), fields.end(),
                     [&](const record_field& r) { return r.name == field_; });
    if (field == fields.end())
      return std::move(slice);
    size_t column_index = std::distance(fields.begin(), field);
    vast::data anonymized_value;
    auto type = fields.at(column_index).type;
    auto builder_ptr
      = factory<table_slice_builder>::make(slice.encoding(), slice.layout());
    for (size_t i = 0; i < slice.rows(); ++i) {
      for (size_t j = 0; j < slice.columns(); ++j) {
        const auto& item = slice.at(i, j);
        auto success = j == column_index ? builder_ptr->add(anonymize(item))
                                         : builder_ptr->add(item);
        if (!success)
          return caf::make_error(ec::unspecified,
                                 "anonymize step: unknown error "
                                 "in table slice builder");
      }
    }
    return builder_ptr->finish();
  }

private:
  std::string field_;
  std::string value_;
};

struct pseudonymize_step {
  pseudonymize_step(const std::string& fieldname, const std::string& salt)
    : field_(fieldname), salt_(salt) {
  }

  vast::data pseudonymize(const vast::data_view& data) {
    auto hasher = vast::uhash<xxhash64>{};
    auto hash = hasher(data);
    if (!salt_.empty())
      hash = hasher(salt_);
    return fmt::format("{:x}", hash);
  }

  caf::expected<table_slice> operator()(table_slice&& slice) {
    const auto& fields = slice.layout().fields;
    // FIXME: Use `find()` to handle nested fields etc.
    // auto field = slice.layout().find(self->state.fieldname);
    // FIXME: handle multiple fields with the same name
    auto field
      = std::find_if(fields.begin(), fields.end(),
                     [&](const record_field& r) { return r.name == field_; });
    if (field == fields.end())
      return std::move(slice);
    size_t column_index = std::distance(fields.begin(), field);
    auto layout = slice.layout();
    layout.fields.at(column_index).type = string_type{};
    auto builder_ptr
      = factory<table_slice_builder>::make(slice.encoding(), layout);
    for (size_t i = 0; i < slice.rows(); ++i) {
      for (size_t j = 0; j < slice.columns(); ++j) {
        const auto& item = slice.at(i, j);
        auto success = j == column_index ? builder_ptr->add(pseudonymize(item))
                                         : builder_ptr->add(item);
        if (!success)
          return caf::make_error(ec::unspecified,
                                 "pseudonymize step: unknown error "
                                 "in table slice builder");
      }
    }
    return builder_ptr->finish();
  }

private:
  std::string field_;
  std::string salt_;
};

transform_step make_delete_step(const std::string& fieldname) {
  return delete_step(fieldname);
}

transform_step
make_replace_step(const std::string& fieldname, const std::string& value) {
  return anonymize_step{fieldname, value};
}

/// Replace a field in the input by its hash value.
transform_step
make_anonymize_step(const std::string& fieldname, const std::string& salt) {
  return pseudonymize_step{fieldname, salt};
}

transformation_engine::transformation_engine(std::vector<transform>&& transforms)
  : transforms_(transforms) {
  for (size_t i = 0; i < transforms.size(); ++i)
    for (const auto& type : transforms[i].event_types)
      layout_mapping_[type].push_back(i);
}

/// Apply relevant transformations to the table slice.
caf::expected<table_slice> transformation_engine::apply(table_slice&& x) const {
  auto offset = x.offset();
  const auto& matching = layout_mapping_.find(x.layout().name());
  if (matching == layout_mapping_.end())
    return std::move(x);
  VAST_INFO("applying {} transforms for received table slice w/ layout {}",
            matching->second.size(), x.layout().name());
  for (auto idx : matching->second) {
    const auto& t = transforms_.at(idx);
    // FIXME: Make 'transform::apply()' function
    VAST_INFO("applying {} steps of transform {}", t.steps.size(),
              t.transform_name);
    for (const auto& step : t.steps) {
      auto transformed = step(std::move(x));
      if (!transformed) {
        return transformed;
      }
      x = std::move(*transformed);
    }
  }
  x.offset(offset);
  return std::move(x);
}

transformer_stream_stage_ptr
make_transform_stage(stream_sink_actor<table_slice>::pointer self,
                     std::vector<transform>&& transforms) {
  transformation_engine transformer{std::move(transforms)};
  // transformation_engine transformer(std::move(transforms));
  return caf::attach_continuous_stream_stage(
    self,
    [](caf::unit_t&) {
      // nop
    },
    [transformer = std::move(transformer)](
      caf::unit_t&, caf::downstream<table_slice>& out, table_slice x) {
      auto transformed = transformer.apply(std::move(x));
      if (!transformed) {
        VAST_ERROR("discarding data: error in transformation step. {}",
                   transformed.error());
        return;
      }
      VAST_WARN("pushing table slice");
      out.push(std::move(*transformed));
    },
    [](caf::unit_t&, const caf::error&) {
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

pre_transformer_actor::behavior_type
pre_transformer(pre_transformer_actor::stateful_pointer<transformer_state> self,
                std::vector<transform>&& transforms,
                const stream_sink_actor<table_slice>& out) {
  VAST_WARN("creating pre-transformer");
  self->state.stage = make_transform_stage(
    caf::actor_cast<stream_sink_actor<table_slice>::pointer>(self),
    std::move(transforms));
  self->state.stage->add_outbound_path(out);
  return {[](int) { /* dummy */ },
          [self](caf::stream<table_slice> in)
            -> caf::inbound_stream_slot<table_slice> {
            VAST_WARN("pre-transformer got a new stream source from {} msg {}", self->current_sender(), typeid(self->current_mailbox_element()->content()).name());
            return self->state.stage->add_inbound_path(in);
          }};
}

} // namespace vast::system