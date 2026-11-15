// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "liquid/error.hpp"
#include "liquify/ai_config.hpp"
#include "liquify/ai_provider.hpp"
#include "liquify/context_builder.hpp"

namespace liquify {

enum class AssistantAction : std::uint8_t {
    ask,
    explain_diagnostic,
    propose_edit,
};

struct AssistantSnapshot {
    bool enabled{};
    bool key_configured{};
    bool running{};
    bool refusal{};
    std::string status{"off"};
    std::string response;
    std::string context;
    std::vector<std::string> omissions;
    std::optional<SourceEdit> proposal;
    std::optional<SourceEdit> accepted;
};

using AiProviderFactory = std::function<std::unique_ptr<AiProvider>()>;

class Assistant {
public:
    Assistant(AiConfig config, bool key_configured, AiProviderFactory provider_factory);
    ~Assistant();
    Assistant(const Assistant&) = delete;
    auto operator=(const Assistant&) -> Assistant& = delete;

    void set_enabled(bool enabled);
    void
    submit(AssistantAction action, std::string prompt, ContextBlock context, SourceRange focus);
    void cancel();
    void accept();
    void reject();
    [[nodiscard]] auto take_accepted() -> std::optional<SourceEdit>;

    [[nodiscard]] auto snapshot() const -> AssistantSnapshot;

private:
    struct State;
    std::shared_ptr<State> state_;
    AiConfig config_;
    AiProviderFactory provider_factory_;
    std::unique_ptr<AiProvider> provider_;
};

[[nodiscard]] auto record_assisted_edit(const std::filesystem::path& repository_path,
                                        const std::filesystem::path& edited_path,
                                        const SourceEdit& edit) -> liquid::result<void>;

} // namespace liquify
