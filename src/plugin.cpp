#include "liquify/plugin.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "liquid/graph.hpp"
#include "liquid/runtime.hpp"

namespace liquify {
namespace {

auto plugin_error(liquid::errc code, std::string message, std::string context = {})
    -> liquid::error {
    return {code, std::move(message), std::move(context)};
}

auto utf8_valid(std::span<const std::byte> value) -> bool {
    auto index = std::size_t{};
    while (index < value.size()) {
        const auto first = std::to_integer<std::uint8_t>(value[index]);
        if (first <= 0x7fU) {
            ++index;
            continue;
        }
        auto count = std::size_t{};
        auto minimum = std::uint32_t{};
        auto codepoint = std::uint32_t{};
        if ((first & 0xe0U) == 0xc0U) {
            count = 2;
            minimum = 0x80U;
            codepoint = first & 0x1fU;
        } else if ((first & 0xf0U) == 0xe0U) {
            count = 3;
            minimum = 0x800U;
            codepoint = first & 0x0fU;
        } else if ((first & 0xf8U) == 0xf0U) {
            count = 4;
            minimum = 0x10000U;
            codepoint = first & 0x07U;
        } else {
            return false;
        }
        if (value.size() - index < count) {
            return false;
        }
        for (std::size_t offset = 1; offset < count; ++offset) {
            const auto next = std::to_integer<std::uint8_t>(value[index + offset]);
            if ((next & 0xc0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if (codepoint < minimum || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
            return false;
        }
        index += count;
    }
    return true;
}

auto half_to_float(std::uint16_t encoded) -> float {
    const auto sign = static_cast<std::uint32_t>(encoded & 0x8000U) << 16U;
    auto exponent = static_cast<std::uint32_t>((encoded >> 10U) & 0x1fU);
    auto fraction = static_cast<std::uint32_t>(encoded & 0x03ffU);
    auto bits = std::uint32_t{};
    if (exponent == 0) {
        if (fraction == 0) {
            bits = sign;
        } else {
            auto shift = 0U;
            while ((fraction & 0x0400U) == 0) {
                fraction <<= 1U;
                ++shift;
            }
            fraction &= 0x03ffU;
            exponent = 127U - 15U - shift + 1U;
            bits = sign | (exponent << 23U) | (fraction << 13U);
        }
    } else if (exponent == 0x1fU) {
        bits = sign | 0x7f800000U | (fraction << 13U);
    } else {
        exponent += 127U - 15U;
        bits = sign | (exponent << 23U) | (fraction << 13U);
    }
    return std::bit_cast<float>(bits);
}

auto representable_as_half(std::uint32_t bits) -> bool {
    for (auto encoded = std::uint32_t{}; encoded <= 0xffffU; ++encoded) {
        if (std::bit_cast<std::uint32_t>(half_to_float(static_cast<std::uint16_t>(encoded))) ==
            bits) {
            return true;
        }
    }
    return false;
}

class CborValidator {
public:
    explicit CborValidator(std::span<const std::byte> input) : input_(input) {}

    [[nodiscard]] auto validate() -> bool {
        return item(0) && cursor_ == input_.size();
    }

private:
    [[nodiscard]] auto take(std::size_t count) -> std::optional<std::uint64_t> {
        if (count > input_.size() - cursor_) {
            return std::nullopt;
        }
        auto value = std::uint64_t{};
        for (std::size_t index = 0; index < count; ++index) {
            value = (value << 8U) | std::to_integer<std::uint8_t>(input_[cursor_++]);
        }
        return value;
    }

    [[nodiscard]] auto argument(std::uint8_t additional) -> std::optional<std::uint64_t> {
        if (additional < 24U) {
            return additional;
        }
        const auto width = additional == 24U   ? 1U
                           : additional == 25U ? 2U
                           : additional == 26U ? 4U
                           : additional == 27U ? 8U
                                               : 0U;
        if (width == 0) {
            return std::nullopt;
        }
        auto value = take(width);
        if (!value) {
            return std::nullopt;
        }
        const auto minimum = width == 1U   ? 24ULL
                             : width == 2U ? 256ULL
                             : width == 4U ? 65'536ULL
                                           : 4'294'967'296ULL;
        if (*value < minimum) {
            return std::nullopt;
        }
        return value;
    }

    [[nodiscard]] auto raw_less(std::span<const std::byte> left,
                                std::span<const std::byte> right) const -> bool {
        if (left.size() != right.size()) {
            return left.size() < right.size();
        }
        return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end());
    }

    [[nodiscard]] auto float_value(std::uint8_t additional) -> bool {
        if (additional == 25U) {
            auto encoded = take(2);
            if (!encoded) {
                return false;
            }
            const auto half = static_cast<std::uint16_t>(*encoded);
            const auto exponent = half & 0x7c00U;
            const auto fraction = half & 0x03ffU;
            return exponent != 0x7c00U || fraction == 0 || half == 0x7e00U;
        }
        if (additional == 26U) {
            auto encoded = take(4);
            if (!encoded) {
                return false;
            }
            const auto bits = static_cast<std::uint32_t>(*encoded);
            const auto value = std::bit_cast<float>(bits);
            return !std::isnan(value) && !representable_as_half(bits);
        }
        if (additional == 27U) {
            auto encoded = take(8);
            if (!encoded) {
                return false;
            }
            const auto value = std::bit_cast<double>(*encoded);
            if (std::isnan(value)) {
                return false;
            }
            const auto narrowed = static_cast<float>(value);
            return static_cast<double>(narrowed) != value;
        }
        return false;
    }

    [[nodiscard]] auto sequence(std::uint64_t count, std::size_t depth) -> bool {
        if (count > input_.size() - cursor_) {
            return false;
        }
        for (std::uint64_t index = 0; index < count; ++index) {
            if (!item(depth + 1)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto map(std::uint64_t count, std::size_t depth) -> bool {
        auto previous = std::span<const std::byte>{};
        for (std::uint64_t index = 0; index < count; ++index) {
            const auto key_start = cursor_;
            if (!item(depth + 1)) {
                return false;
            }
            const auto key = input_.subspan(key_start, cursor_ - key_start);
            if (index != 0 && !raw_less(previous, key)) {
                return false;
            }
            previous = key;
            if (!item(depth + 1)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto item(std::size_t depth) -> bool {
        if (depth > 128 || cursor_ == input_.size()) {
            return false;
        }
        const auto initial = std::to_integer<std::uint8_t>(input_[cursor_++]);
        const auto major = initial >> 5U;
        const auto additional = initial & 0x1fU;
        if (major == 7U) {
            if (additional < 24U) {
                return true;
            }
            if (additional == 24U) {
                auto simple = take(1);
                return simple && *simple >= 32U;
            }
            return float_value(additional);
        }
        auto value = argument(additional);
        if (!value) {
            return false;
        }
        if (major <= 1U) {
            return true;
        }
        if (major == 2U || major == 3U) {
            if (*value > input_.size() - cursor_) {
                return false;
            }
            const auto bytes = input_.subspan(cursor_, static_cast<std::size_t>(*value));
            cursor_ += static_cast<std::size_t>(*value);
            return major == 2U || utf8_valid(bytes);
        }
        if (major == 4U) {
            return sequence(*value, depth);
        }
        if (major == 5U) {
            return map(*value, depth);
        }
        if (major == 6U) {
            return item(depth + 1);
        }
        return false;
    }

    std::span<const std::byte> input_;
    std::size_t cursor_{};
};

auto validate_cbor(std::span<const std::byte> value, liquid::errc code, std::string context)
    -> liquid::result<void> {
    if (value.empty() || !CborValidator(value).validate()) {
        return std::unexpected(
            plugin_error(code, "plugin value is not canonical CBOR", std::move(context)));
    }
    return {};
}

class OwnedBytes {
public:
    explicit OwnedBytes(liquid_frontend_owned_bytes_v1 value) : value_(value) {}

    ~OwnedBytes() {
        if (value_.data && value_.release) {
            value_.release(value_.context, value_.data, value_.size);
        }
    }

    OwnedBytes(const OwnedBytes&) = delete;
    auto operator=(const OwnedBytes&) -> OwnedBytes& = delete;

    [[nodiscard]] auto copy() const -> std::vector<std::byte> {
        auto output = std::vector<std::byte>(value_.size);
        if (!output.empty()) {
            std::memcpy(output.data(), value_.data, output.size());
        }
        return output;
    }

private:
    liquid_frontend_owned_bytes_v1 value_;
};

struct Operation {
    std::string plugin;
    std::string name;
    liquify_plugin_operation_v1 invoke{};
};

struct CommandOperation : Operation {
    std::string default_binding;
    std::vector<std::byte> argument_schema;
};

struct PluginModule {
    std::string name;
    const liquify_plugin_v1* descriptor{};
    void* library{};
    bool owns_library{};
    std::vector<Operation> queries;
    std::vector<CommandOperation> commands;
    std::vector<Operation> panels;

    ~PluginModule() {
        if (descriptor && descriptor->destroy) {
            descriptor->destroy(descriptor->plugin_context);
        }
        if (!owns_library || !library) {
            return;
        }
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(library));
#else
        dlclose(library);
#endif
    }
};

auto status_code(std::uint32_t status) -> liquid::errc {
    switch (status) {
    case LIQUID_FRONTEND_STATUS_INVALID_ARGUMENT:
        return liquid::errc::invalid_argument;
    case LIQUID_FRONTEND_STATUS_UNSUPPORTED:
        return liquid::errc::unsupported;
    case LIQUID_FRONTEND_STATUS_CANCELLED:
        return liquid::errc::unavailable;
    default:
        return liquid::errc::internal;
    }
}

auto invoke_operation(const Operation& operation,
                      void* context,
                      std::span<const std::byte> arguments)
    -> liquid::result<std::vector<std::byte>> {
    auto input = validate_cbor(
        arguments, liquid::errc::invalid_argument, operation.plugin + ":" + operation.name);
    if (!input) {
        return std::unexpected(input.error());
    }
    auto output = liquid_frontend_owned_bytes_v1{};
    auto error = liquid_frontend_error_v1{};
    const auto callbacks = liquid_frontend_callbacks_v1{};
    auto status = int{};
    try {
        status = operation.invoke(
            context,
            {reinterpret_cast<const std::uint8_t*>(arguments.data()), arguments.size()},
            &callbacks,
            &output,
            &error);
    } catch (const std::exception& exception) {
        return std::unexpected(
            plugin_error(liquid::errc::internal,
                         "plugin operation threw an exception",
                         operation.plugin + ":" + operation.name + ": " + exception.what()));
    } catch (...) {
        return std::unexpected(plugin_error(liquid::errc::internal,
                                            "plugin operation threw an exception",
                                            operation.plugin + ":" + operation.name));
    }
    if (status != LIQUID_FRONTEND_STATUS_OK) {
        auto owned = OwnedBytes(output);
        return std::unexpected(
            plugin_error(status_code(static_cast<std::uint32_t>(status)),
                         error.message ? error.message : "plugin operation failed",
                         operation.plugin + ":" + operation.name));
    }
    if (!output.data || !output.release) {
        return std::unexpected(plugin_error(liquid::errc::corrupt,
                                            "plugin returned invalid owned bytes",
                                            operation.plugin + ":" + operation.name));
    }
    auto owned = OwnedBytes(output);
    auto copied = owned.copy();
    auto valid =
        validate_cbor(copied, liquid::errc::corrupt, operation.plugin + ":" + operation.name);
    if (!valid) {
        return std::unexpected(valid.error());
    }
    return copied;
}

auto nonempty(const char* value) -> bool {
    return value && value[0] != '\0';
}

auto library_error(const std::filesystem::path& path, std::string action) -> liquid::error {
#if defined(_WIN32)
    return plugin_error(liquid::errc::unavailable, std::move(action), path.string());
#else
    const auto* detail = dlerror();
    auto message = detail ? std::string(detail) : std::move(action);
    return plugin_error(liquid::errc::unavailable, std::move(message), path.string());
#endif
}

class PanelDecoder {
public:
    explicit PanelDecoder(std::span<const std::byte> input) : input_(input) {}

    [[nodiscard]] auto decode() -> liquid::result<PluginTextPanel> {
        auto count = header(5U);
        if (!count) {
            return std::unexpected(
                plugin_error(liquid::errc::corrupt, "plugin panel must be a CBOR map"));
        }
        auto kind = std::string{};
        auto output = PluginTextPanel{};
        for (std::uint64_t index = 0; index < *count; ++index) {
            auto key = text();
            auto value = text();
            if (!key || !value) {
                return std::unexpected(
                    plugin_error(liquid::errc::corrupt, "plugin text panel fields must be text"));
            }
            if (*key == "kind") {
                kind = std::move(*value);
            } else if (*key == "text") {
                output.text = std::move(*value);
            } else if (*key == "title") {
                output.title = std::move(*value);
            } else {
                return std::unexpected(plugin_error(
                    liquid::errc::corrupt, "plugin text panel has an unknown field", *key));
            }
        }
        if (cursor_ != input_.size() || kind != "text" || output.title.empty()) {
            return std::unexpected(
                plugin_error(liquid::errc::corrupt, "plugin text panel is incomplete"));
        }
        return output;
    }

private:
    [[nodiscard]] auto take(std::size_t count) -> std::optional<std::uint64_t> {
        if (count > input_.size() - cursor_) {
            return std::nullopt;
        }
        auto value = std::uint64_t{};
        for (std::size_t index = 0; index < count; ++index) {
            value = (value << 8U) | std::to_integer<std::uint8_t>(input_[cursor_++]);
        }
        return value;
    }

    [[nodiscard]] auto header(std::uint8_t expected_major) -> std::optional<std::uint64_t> {
        if (cursor_ == input_.size()) {
            return std::nullopt;
        }
        const auto initial = std::to_integer<std::uint8_t>(input_[cursor_++]);
        if ((initial >> 5U) != expected_major) {
            return std::nullopt;
        }
        const auto additional = initial & 0x1fU;
        if (additional < 24U) {
            return additional;
        }
        const auto width = additional == 24U   ? 1U
                           : additional == 25U ? 2U
                           : additional == 26U ? 4U
                           : additional == 27U ? 8U
                                               : 0U;
        return width == 0 ? std::nullopt : take(width);
    }

    [[nodiscard]] auto text() -> std::optional<std::string> {
        auto size = header(3U);
        if (!size || *size > input_.size() - cursor_) {
            return std::nullopt;
        }
        const auto* data = reinterpret_cast<const char*>(input_.data() + cursor_);
        auto output = std::string(data, static_cast<std::size_t>(*size));
        cursor_ += static_cast<std::size_t>(*size);
        return output;
    }

    std::span<const std::byte> input_;
    std::size_t cursor_{};
};

} // namespace

struct PluginHost::Impl {
    template <class Member>
    [[nodiscard]] auto contains(std::string_view name, Member member) const -> bool {
        for (const auto& module : modules) {
            const auto named = [name](const auto& operation) { return operation.name == name; };
            if (std::ranges::any_of(module.get()->*member, named)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto find_query(std::string_view name) const
        -> std::pair<const PluginModule*, const Operation*> {
        for (const auto& module : modules) {
            const auto found = std::ranges::find(module->queries, name, &Operation::name);
            if (found != module->queries.end()) {
                return {module.get(), &*found};
            }
        }
        return {};
    }

    [[nodiscard]] auto find_command(std::string_view name) const
        -> std::pair<const PluginModule*, const CommandOperation*> {
        for (const auto& module : modules) {
            const auto found = std::ranges::find(module->commands, name, &Operation::name);
            if (found != module->commands.end()) {
                return {module.get(), &*found};
            }
        }
        return {};
    }

    [[nodiscard]] auto add(const liquify_plugin_v1* descriptor, void* library, bool owns_library)
        -> liquid::result<void> {
        constexpr auto descriptor_size = sizeof(liquify_plugin_v1);
        if (!descriptor || descriptor->abi_version != LIQUIFY_PLUGIN_ABI_VERSION ||
            descriptor->struct_size < descriptor_size || !nonempty(descriptor->name) ||
            (descriptor->query_count != 0 && !descriptor->queries) ||
            (descriptor->command_count != 0 && !descriptor->commands) ||
            (descriptor->panel_count != 0 && !descriptor->panels)) {
            return std::unexpected(plugin_error(liquid::errc::invalid_argument,
                                                "plugin descriptor does not implement ABI v1",
                                                "plugin"));
        }
        auto module = std::make_unique<PluginModule>();
        module->name = descriptor->name;
        const auto query_exists = [&module, this](std::string_view name) {
            const auto named = [name](const auto& operation) { return operation.name == name; };
            return contains(name, &PluginModule::queries) ||
                   std::ranges::any_of(module->queries, named);
        };
        const auto command_exists = [&module, this](std::string_view name) {
            const auto named = [name](const auto& operation) { return operation.name == name; };
            return contains(name, &PluginModule::commands) ||
                   std::ranges::any_of(module->commands, named);
        };
        const auto panel_exists = [&module, this](std::string_view name) {
            const auto named = [name](const auto& operation) { return operation.name == name; };
            return contains(name, &PluginModule::panels) ||
                   std::ranges::any_of(module->panels, named);
        };
        for (std::size_t index = 0; index < descriptor->query_count; ++index) {
            const auto& query = descriptor->queries[index];
            if (!nonempty(query.name) || !query.invoke || query_exists(query.name)) {
                return std::unexpected(plugin_error(
                    liquid::errc::invalid_argument, "plugin has an invalid query", module->name));
            }
            module->queries.push_back({module->name, query.name, query.invoke});
        }
        for (std::size_t index = 0; index < descriptor->command_count; ++index) {
            const auto& command = descriptor->commands[index];
            if (!nonempty(command.name) || !command.default_binding || !command.invoke ||
                command_exists(command.name) ||
                (!command.argument_schema_cbor.data && command.argument_schema_cbor.size != 0)) {
                return std::unexpected(plugin_error(
                    liquid::errc::invalid_argument, "plugin has an invalid command", module->name));
            }
            const auto schema =
                std::span(reinterpret_cast<const std::byte*>(command.argument_schema_cbor.data),
                          command.argument_schema_cbor.size);
            auto valid = validate_cbor(
                schema, liquid::errc::invalid_argument, module->name + ":" + command.name);
            if (!valid) {
                return std::unexpected(valid.error());
            }
            module->commands.push_back({{module->name, command.name, command.invoke},
                                        command.default_binding,
                                        {schema.begin(), schema.end()}});
        }
        for (std::size_t index = 0; index < descriptor->panel_count; ++index) {
            const auto& panel = descriptor->panels[index];
            if (!nonempty(panel.name) || !panel.view_model || panel_exists(panel.name)) {
                return std::unexpected(plugin_error(
                    liquid::errc::invalid_argument, "plugin has an invalid panel", module->name));
            }
            module->panels.push_back({module->name, panel.name, panel.view_model});
        }
        module->descriptor = descriptor;
        module->library = library;
        module->owns_library = owns_library;
        modules.push_back(std::move(module));
        return {};
    }

    std::vector<std::unique_ptr<PluginModule>> modules;
};

PluginHost::PluginHost() : impl_(std::make_unique<Impl>()) {}
PluginHost::~PluginHost() = default;
PluginHost::PluginHost(PluginHost&&) noexcept = default;
auto PluginHost::operator=(PluginHost&&) noexcept -> PluginHost& = default;

auto PluginHost::add(const liquify_plugin_v1* descriptor) -> liquid::result<void> {
    return impl_->add(descriptor, nullptr, false);
}

auto PluginHost::load(std::filesystem::path shared_object) -> liquid::result<void> {
#if defined(_WIN32)
    auto library = LoadLibraryW(shared_object.wstring().c_str());
    if (!library) {
        return std::unexpected(library_error(shared_object, "could not load plugin"));
    }
    auto* symbol = reinterpret_cast<liquify_plugin_entrypoint_v1>(
        GetProcAddress(library, LIQUIFY_PLUGIN_ENTRYPOINT_V1));
#else
    dlerror();
    auto* library = dlopen(shared_object.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        return std::unexpected(library_error(shared_object, "could not load plugin"));
    }
    auto* symbol = reinterpret_cast<liquify_plugin_entrypoint_v1>(
        dlsym(library, LIQUIFY_PLUGIN_ENTRYPOINT_V1));
#endif
    if (!symbol) {
        auto error = library_error(shared_object, "plugin entry point is missing");
#if defined(_WIN32)
        FreeLibrary(library);
#else
        dlclose(library);
#endif
        return std::unexpected(std::move(error));
    }
    const liquify_plugin_v1* descriptor = nullptr;
    try {
        descriptor = symbol();
    } catch (...) {
#if defined(_WIN32)
        FreeLibrary(library);
#else
        dlclose(library);
#endif
        return std::unexpected(plugin_error(liquid::errc::internal,
                                            "plugin entry point threw an exception",
                                            shared_object.string()));
    }
    auto added = impl_->add(descriptor, library, true);
    if (!added) {
#if defined(_WIN32)
        FreeLibrary(library);
#else
        dlclose(library);
#endif
        return std::unexpected(added.error());
    }
    return {};
}

auto PluginHost::query_names() const -> std::vector<std::string> {
    auto output = std::vector<std::string>{};
    for (const auto& module : impl_->modules) {
        for (const auto& query : module->queries) {
            output.push_back(query.name);
        }
    }
    return output;
}

auto PluginHost::commands() const -> std::vector<PluginCommand> {
    auto output = std::vector<PluginCommand>{};
    for (const auto& module : impl_->modules) {
        for (const auto& command : module->commands) {
            output.push_back(
                {command.plugin, command.name, command.default_binding, command.argument_schema});
        }
    }
    return output;
}

auto PluginHost::invoke_command(std::string_view name, std::span<const std::byte> arguments_cbor)
    -> liquid::result<std::vector<std::byte>> {
    const auto [module, command] = impl_->find_command(name);
    if (!module || !command) {
        return std::unexpected(plugin_error(
            liquid::errc::not_found, "plugin command is not registered", std::string(name)));
    }
    return invoke_operation(*command, module->descriptor->plugin_context, arguments_cbor);
}

auto PluginHost::panels(std::span<const std::byte> context_cbor)
    -> std::vector<PluginPanelSnapshot> {
    auto output = std::vector<PluginPanelSnapshot>{};
    for (const auto& module : impl_->modules) {
        for (const auto& panel : module->panels) {
            auto snapshot = PluginPanelSnapshot{panel.plugin, panel.name, {}, {}};
            auto value = invoke_operation(panel, module->descriptor->plugin_context, context_cbor);
            if (!value) {
                snapshot.error = value.error().message;
                if (!value.error().context.empty()) {
                    snapshot.error += ": " + value.error().context;
                }
            } else {
                snapshot.view_model_cbor = std::move(*value);
            }
            output.push_back(std::move(snapshot));
        }
    }
    return output;
}

auto PluginHost::register_queries(liquid::query_registry& registry) const -> liquid::result<void> {
    for (const auto& module : impl_->modules) {
        for (const auto& query : module->queries) {
            const auto* descriptor = module->descriptor;
            auto added =
                registry.add({.name = query.name,
                              .encode_arguments =
                                  [](std::span<const std::byte> arguments) {
                                      return canonical_plugin_cbor(arguments);
                                  },
                              .compute = [operation = query, descriptor](auto&, auto arguments)
                                  -> liquid::runtime::task<std::vector<std::byte>> {
                                  auto value = invoke_operation(
                                      operation, descriptor->plugin_context, arguments);
                                  if (!value) {
                                      co_return std::unexpected(value.error());
                                  }
                                  co_return std::move(*value);
                              }});
            if (!added) {
                return std::unexpected(added.error());
            }
        }
    }
    return {};
}

auto PluginHost::invoke_query(std::string_view name, std::span<const std::byte> arguments_cbor)
    -> liquid::result<std::vector<std::byte>> {
    const auto [module, query] = impl_->find_query(name);
    if (!module || !query) {
        return std::unexpected(plugin_error(
            liquid::errc::not_found, "plugin query is not registered", std::string(name)));
    }
    return invoke_operation(*query, module->descriptor->plugin_context, arguments_cbor);
}

auto canonical_plugin_cbor(std::span<const std::byte> value)
    -> liquid::result<std::vector<std::byte>> {
    auto valid = validate_cbor(value, liquid::errc::invalid_argument, "plugin CBOR");
    if (!valid) {
        return std::unexpected(valid.error());
    }
    return std::vector<std::byte>(value.begin(), value.end());
}

auto decode_plugin_text_panel(std::span<const std::byte> value) -> liquid::result<PluginTextPanel> {
    auto valid = validate_cbor(value, liquid::errc::corrupt, "plugin panel");
    if (!valid) {
        return std::unexpected(valid.error());
    }
    return PanelDecoder(value).decode();
}

auto plugin_null_cbor() -> std::span<const std::byte> {
    static constexpr auto value = std::array{std::byte{0xf6}};
    return value;
}

} // namespace liquify
