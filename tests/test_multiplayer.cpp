#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <string>

#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquify/multiplayer.hpp"

namespace {

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::string_view prefix) {
        static std::atomic_uint64_t suffix{};
        path_ = std::filesystem::temp_directory_path() /
                (std::string(prefix) + "-" + std::to_string(++suffix));
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] auto path() const -> const std::filesystem::path& {
        return path_;
    }

private:
    std::filesystem::path path_;
};

auto bytes(std::string_view value) -> std::vector<std::byte> {
    const auto characters = std::span(value.data(), value.size());
    const auto output = std::as_bytes(characters);
    return {output.begin(), output.end()};
}

} // namespace

TEST(Multiplayer, FormatsPresenceLeaseAndAttributionForTheEditor) {
    auto presence = liquid::presence{};
    presence.actor.taap_id = "reviewer";
    presence.path = "src/main.cpp";
    presence.activity = liquid::presence_activity::editing;
    EXPECT_EQ(liquify::multiplayer_presence_text(presence),
              "reviewer is editing in src/main.cpp (session 00000000000000000000000000000000)");

    auto lease = liquid::semantic_lease{};
    lease.by.taap_id = "reviewer";
    lease.claim.path = "src/main.cpp";
    lease.claim.kind = liquid::lease_kind::range;
    EXPECT_EQ(
        liquify::multiplayer_lease_text(lease),
        "reviewer holds a range lease for src/main.cpp (session 00000000000000000000000000000000)");

    auto line = liquid::line_attribution{};
    line.line = 4;
    line.by.taap_id = "helper";
    line.agent = true;
    EXPECT_EQ(liquify::multiplayer_attribution_text(line),
              "line 4: helper (agent) / session 00000000000000000000000000000000 / change "
              "unknown:0000000000000000000000000000000000000000000000000000000000000000");
}

TEST(Multiplayer, RefreshesAttributionPresenceAndLeaseStateFromLiquid) {
    auto directory = TemporaryDirectory("liquify-multiplayer");
    auto repository = liquid::runtime::block_on(liquid::repository::create(directory.path()));
    ASSERT_TRUE(repository) << repository.error().message;
    ASSERT_TRUE(liquid::runtime::block_on(repository->write("main.cpp", bytes("one\ntwo\n"))));
    ASSERT_TRUE(liquid::runtime::block_on(repository->cut("initial")));

    const auto session = liquify::make_multiplayer_session();
    const auto range = liquid::frontend_source_range{.start = {.byte = 0, .line = 1, .column = 1},
                                                     .end = {.byte = 0, .line = 1, .column = 1}};
    auto snapshot = liquify::refresh_multiplayer(directory.path(), "main.cpp", session, range);
    ASSERT_TRUE(snapshot) << snapshot.error().message;
    ASSERT_EQ(snapshot->attribution.size(), 2);
    ASSERT_FALSE(snapshot->presence.empty());

    const auto claim = liquid::lease_claim{.path = "main.cpp", .kind = liquid::lease_kind::file};
    ASSERT_TRUE(liquify::acquire_multiplayer_lease(directory.path(), session, claim));
    auto with_lease = liquify::refresh_multiplayer(directory.path(), "main.cpp", session, range);
    ASSERT_TRUE(with_lease) << with_lease.error().message;
    ASSERT_EQ(with_lease->leases.size(), 1);
    EXPECT_EQ(with_lease->leases.front().session, session);
    ASSERT_TRUE(liquify::release_multiplayer_lease(directory.path(), session, claim));
}
