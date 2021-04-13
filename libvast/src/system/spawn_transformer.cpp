//    _   _____   __________
//   | | / / _ | / __/_  __/     Visibility
//   | |/ / __ |_\ \  / /          Across
//   |___/_/ |_/___/ /_/       Space and Time
//
// SPDX-FileCopyrightText: (c) 2018 The VAST Contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "vast/system/spawn_transformer.hpp"

#include "vast/error.hpp"
#include "vast/logger.hpp"
#include "vast/plugin.hpp"
#include "vast/si_literals.hpp"
#include "vast/system/node.hpp"
#include "vast/system/spawn_arguments.hpp"
#include "vast/system/transformer.hpp"
#include "vast/table_slice.hpp"

#include <caf/actor.hpp>
#include <caf/actor_cast.hpp>
#include <caf/config_value.hpp>
#include <caf/expected.hpp>
#include <caf/local_actor.hpp>
#include <caf/settings.hpp>
#include <caf/typed_event_based_actor.hpp>

using namespace vast::binary_byte_literals;

namespace vast::system {

caf::error parse_transform_steps(std::vector<transform_step>& result,
                                 const caf::config_value::list& steps) {
  VAST_WARN("parsing steps for {}", steps.size());
  for (auto step : steps) {
    auto dict = caf::get_if<caf::config_value::dictionary>(&step);
    if (!dict)
      return caf::make_error(ec::invalid_configuration, "step is not a dict");
    if (dict->size() != 1)
      return caf::make_error(ec::invalid_configuration, "step has more than 1 "
                                                        "entry");
    auto& [function, value] = *dict->begin();
    auto opts = caf::get_if<caf::config_value::dictionary>(&value);
    if (function == "delete") {
      auto fieldname = caf::get_if<std::string>(opts, "field");
      if (!fieldname)
        return caf::make_error(ec::invalid_configuration, "missing 'field' key "
                                                          "in delete step");
      result.push_back(make_delete_step(fieldname.value()));
    } else if (function == "replace") {
      auto fieldname = caf::get_if<std::string>(opts, "field");
      if (!fieldname)
        return caf::make_error(ec::invalid_configuration, "missing 'field' key "
                                                          "in replace step");
      auto value = caf::get_if<std::string>(opts, "value");
      if (!value)
        return caf::make_error(ec::invalid_configuration, "missing 'value' key "
                                                          "in replace step");
      result.push_back(make_replace_step(fieldname.value(), value.value()));
    } else if (function == "anonymize") {
      auto fieldname = caf::get_if<std::string>(opts, "field");
      if (!fieldname)
        return caf::make_error(ec::invalid_configuration, "missing 'field' key "
                                                          "in anonymize step");
      // TODO: Maybe expose 'hash' option so people can choose between
      result.push_back(make_anonymize_step(fieldname.value()));
    } else {
      bool is_plugin = false;
      // FIXME: Register all transform plugins in a transform_step_factory
      // during vast startup.
      for (const auto& plugin : plugins::get()) {
        if (function != plugin->name())
          continue;
        const auto* t = plugin.as<transform_plugin>();
        if (!t)
          continue;
        is_plugin = true;
        // FIXME
        // result.push_back(t->make_transform_step(*opts));
      }
      if (!is_plugin)
        return caf::make_error(ec::invalid_configuration,
                               fmt::format("unknown step '{}'", function));
    }
  }
  return caf::none;
}

caf::expected<std::vector<transform>>
parse_transforms(transforms_location loc, const caf::settings& opts) {
  std::vector<transform> result;
  std::string key;
  bool server = true;
  switch (loc) {
    case transforms_location::server_import:
      key = "vast.transform-triggers.import";
      server = true;
      break;
    case transforms_location::server_export:
      key = "vast.transform-triggers.export";
      server = true;
      break;
    case transforms_location::client_sink:
      key = "vast.transform-triggers.import";
      server = false;
      break;
    case transforms_location::client_source:
      key = "vast.transform-triggers.export";
      server = false;
      break;
  }
  auto transforms_list = caf::get_if<caf::config_value::list>(&opts, key);
  if (!transforms_list) {
    // TODO: Distinguish between the case whre no transforms were specified
    // (= return) and where there is something other than a list (= error).
    VAST_VERBOSE("No transformations found for key {}", key);
    return result;
  }
  // (name, [event_type]), ...
  std::vector<std::pair<std::string, std::vector<std::string>>>
    transform_triggers;
  for (auto list_item : *transforms_list) {
    auto transform = caf::get_if<caf::config_value::dictionary>(&list_item);
    if (!transform)
      return caf::make_error(ec::invalid_configuration, "transform definition "
                                                        "must be dict");
    if (transform->find("location") == transform->end())
      return caf::make_error(ec::invalid_configuration,
                             "missing 'location' key for transform trigger");
    if (transform->find("transform") == transform->end())
      return caf::make_error(ec::invalid_configuration,
                             "missing 'transform' key for transform trigger");
    if (transform->find("events") == transform->end())
      return caf::make_error(ec::invalid_configuration,
                             "missing 'events' key for transform trigger");
    auto location = caf::get_if<std::string>(&(*transform)["location"]);
    if (!location || (*location != "server" && *location != "client"))
      return caf::make_error(ec::invalid_configuration, "transform location "
                                                        "must be either "
                                                        "'server' or 'client'");
    auto name = caf::get_if<std::string>(&(*transform)["transform"]);
    if (!name)
      return caf::make_error(ec::invalid_configuration, "transform name must "
                                                        "be a string");
    auto events
      = caf::get_if<std::vector<std::string>>(&(*transform)["events"]);
    if (!events)
      return caf::make_error(ec::invalid_configuration,
                             "transform event types must be a list of strings");
    auto server_transform = *location == "server";
    if (server != server_transform)
      continue;
    transform_triggers.emplace_back(*name, *events);
  }
  if (transform_triggers.empty()) {
    VAST_WARN("no triggers");
    return result;
  }
  result.reserve(transform_triggers.size());
  auto transform_definitions
    = caf::get_if<caf::config_value::dictionary>(&opts, "vast.transforms");
  if (!transform_definitions) {
    return caf::make_error(ec::invalid_configuration, "invalid");
  }
  std::map<std::string, caf::config_value::list> transforms;
  for (auto [name, value] : *transform_definitions) {
    auto transform_steps = caf::get_if<caf::config_value::list>(&value);
    if (!transform_steps) {
      // VAST_WARN(...)
      continue;
    }
    transforms[name] = *transform_steps;
  }
  for (auto [name, event_types] : transform_triggers) {
    if (!transforms.count(name)) {
      return caf::make_error(ec::invalid_configuration,
                             fmt::format("unknown transform '{}'", name));
    }
    auto& transform = result.emplace_back();
    transform.transform_name = name;
    transform.event_types = event_types;
    if (auto err = parse_transform_steps(transform.steps, transforms.at(name)))
      return err;
  }
  return result;
}

// caf::expected<caf::actor>
// spawn_transfomer(node_actor::stateful_pointer<node_state> self,
//                  spawn_arguments& args) {
//   const auto& transform_name = args.label;
//   const auto& opts = args.inv.options;
//   const auto* transforms = caf::get_if(&opts, "vast.transforms");
//   if (!transforms)
//     return ec::no_error;
//   const auto& transforms_data = transforms->get_data();
//   const auto* dict = caf::get_if<caf::config_value::dictionary>(transforms);
//   if (!dict)
//     return ec::invalid_configuration;
//   auto definitions = caf::get_if(dict, "definitions");
//   if (!definitions)
//     return ec::no_error;
//   auto list = caf::get_if<caf::config_value::dictionary>(definitions);
//   if (!list)
//     return ec::invalid_configuration;
//   std::vector<transform_step> steps;
//   for (const auto& [name, settings] : *list) {
//     if (name != transform_name)
//       continue;
//   }
//   auto handle = self->spawn(transfomer, steps);
// }

} // namespace vast::system
