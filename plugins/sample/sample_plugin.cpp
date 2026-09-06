#include "sample_plugin.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

void release_bytes(void*, std::uint8_t* data, std::size_t) {
    std::free(data);
}

auto copy_bytes(liquid_frontend_bytes_v1 input,
                liquid_frontend_owned_bytes_v1* output,
                liquid_frontend_error_v1* error) -> int {
    if (!output || (input.size != 0 && !input.data)) {
        if (error) {
            error->code = LIQUID_FRONTEND_STATUS_INVALID_ARGUMENT;
            error->message = "sample plugin received invalid bytes";
        }
        return LIQUID_FRONTEND_STATUS_INVALID_ARGUMENT;
    }
    auto* data = static_cast<std::uint8_t*>(std::malloc(input.size));
    if (!data && input.size != 0) {
        if (error) {
            error->code = LIQUID_FRONTEND_STATUS_INTERNAL;
            error->message = "sample plugin could not allocate output";
        }
        return LIQUID_FRONTEND_STATUS_INTERNAL;
    }
    if (input.size != 0) {
        std::memcpy(data, input.data, input.size);
    }
    *output = {data, input.size, release_bytes, nullptr};
    return LIQUID_FRONTEND_STATUS_OK;
}

auto echo_query(void*,
                liquid_frontend_bytes_v1 arguments,
                const liquid_frontend_callbacks_v1* callbacks,
                liquid_frontend_owned_bytes_v1* output,
                liquid_frontend_error_v1* error) -> int {
    if (callbacks && callbacks->cancelled && callbacks->cancelled(callbacks->context)) {
        if (error) {
            error->code = LIQUID_FRONTEND_STATUS_CANCELLED;
            error->message = "sample query cancelled";
        }
        return LIQUID_FRONTEND_STATUS_CANCELLED;
    }
    if (callbacks && callbacks->progress) {
        callbacks->progress(callbacks->context, "sample.echo", 1, 1);
    }
    return copy_bytes(arguments, output, error);
}

auto reset_command(void*,
                   liquid_frontend_bytes_v1,
                   const liquid_frontend_callbacks_v1*,
                   liquid_frontend_owned_bytes_v1* output,
                   liquid_frontend_error_v1* error) -> int {
    static constexpr std::uint8_t result[] = {0xa1, 0x62, 'o', 'k', 0xf5};
    return copy_bytes({result, sizeof(result)}, output, error);
}

auto status_panel(void*,
                  liquid_frontend_bytes_v1,
                  const liquid_frontend_callbacks_v1*,
                  liquid_frontend_owned_bytes_v1* output,
                  liquid_frontend_error_v1* error) -> int {
    static constexpr std::uint8_t result[] = {
        0xa3, 0x64, 'k', 'i', 'n', 'd', 0x64, 't',  'e', 'x', 't', 0x64, 't', 'e',  'x',
        't',  0x74, 'S', 'a', 'm', 'p', 'l',  'e',  ' ', 'p', 'l', 'u',  'g', 'i',  'n',
        ' ',  'r',  'e', 'a', 'd', 'y', '.',  0x65, 't', 'i', 't', 'l',  'e', 0x6d, 'S',
        'a',  'm',  'p', 'l', 'e', ' ', 'p',  'l',  'u', 'g', 'i', 'n'};
    return copy_bytes({result, sizeof(result)}, output, error);
}

constexpr std::uint8_t argument_schema[] = {
    0xa1, 0x64, 't', 'y', 'p', 'e', 0x66, 'o', 'b', 'j', 'e', 'c', 't'};

const liquify_plugin_query_v1 queries[] = {{"sample.echo", echo_query}};
const liquify_plugin_command_v1 commands[] = {
    {"sample.reset", "Primary+Shift+R", {argument_schema, sizeof(argument_schema)}, reset_command}};
const liquify_plugin_panel_v1 panels[] = {{"sample.status", status_panel}};
const liquify_plugin_v1 descriptor = {LIQUIFY_PLUGIN_ABI_VERSION,
                                      sizeof(liquify_plugin_v1),
                                      "sample",
                                      queries,
                                      1,
                                      commands,
                                      1,
                                      panels,
                                      1,
                                      nullptr,
                                      nullptr};

} // namespace

extern "C" const liquify_plugin_v1* liquify_sample_plugin_v1(void) {
    return &descriptor;
}

extern "C" LIQUIFY_PLUGIN_EXPORT const liquify_plugin_v1* liquify_plugin_get_v1(void) {
    return liquify_sample_plugin_v1();
}
