// Melody Matrix — Unit Test Entry Point
// Placeholder: actual test files will be added per module

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Kernel singleton returns same instance", "[core]") {
    // Basic sanity: singleton should return the same reference
    auto& a = melody_matrix::core::Kernel::instance();
    auto& b = melody_matrix::core::Kernel::instance();
    REQUIRE(&a == &b);
}

TEST_CASE("Clock default time is zero", "[core]") {
    melody_matrix::core::Clock clock;
    REQUIRE(clock.nowMs() == 0);
}

TEST_CASE("Clock sync and offset", "[core]") {
    melody_matrix::core::Clock clock;
    clock.syncFromAudio(1000);
    REQUIRE(clock.nowMs() == 1000);

    clock.setUserOffset(50);
    REQUIRE(clock.nowMs() == 1050);

    clock.setUserOffset(-50);
    REQUIRE(clock.nowMs() == 950);
}

TEST_CASE("Result<T> success case", "[util]") {
    auto r = melody_matrix::util::Result<int>(42);
    REQUIRE(r.ok());
    REQUIRE(r.value() == 42);
}

TEST_CASE("Result<T> error case", "[util]") {
    auto r = melody_matrix::util::Result<int>(melody_matrix::util::Error{1, "test error"});
    REQUIRE(!r.ok());
    REQUIRE(r.error().code == 1);
    REQUIRE(r.error().message == "test error");
}

TEST_CASE("Result<void> success", "[util]") {
    auto r = melody_matrix::util::success();
    REQUIRE(r.ok());
}

TEST_CASE("Result<void> failure", "[util]") {
    auto r = melody_matrix::util::failure<void>(42, "bad");
    REQUIRE(!r.ok());
    REQUIRE(r.error().code == 42);
}

TEST_CASE("FileSystem safeResolve rejects path traversal", "[platform]") {
    auto r = melody_matrix::platform::FileSystem::safeResolve("/base", "../../../etc/passwd");
    REQUIRE(!r.ok());
}

TEST_CASE("FileSystem safeResolve rejects absolute paths", "[platform]") {
    auto r = melody_matrix::platform::FileSystem::safeResolve("/base", "/etc/passwd");
    REQUIRE(!r.ok());
}

TEST_CASE("FileSystem safeResolve accepts normal paths", "[platform]") {
    auto r = melody_matrix::platform::FileSystem::safeResolve("/base", "subdir/file.txt");
    REQUIRE(r.ok());
}

TEST_CASE("EventManager subscribe and emit", "[util]") {
    melody_matrix::util::EventManager em;
    int received = 0;

    struct TestEvent { int value; };
    em.subscribe<TestEvent>([&](const TestEvent& e) {
        received = e.value;
    });

    em.emit(TestEvent{42});
    REQUIRE(received == 42);
}

TEST_CASE("EventManager multiple subscribers in order", "[util]") {
    melody_matrix::util::EventManager em;
    std::vector<int> order;

    struct OrderEvent { int seq; };
    em.subscribe<OrderEvent>([&](const OrderEvent& e) { order.push_back(e.seq * 10); });
    em.subscribe<OrderEvent>([&](const OrderEvent& e) { order.push_back(e.seq * 20); });

    em.emit(OrderEvent{1});
    REQUIRE(order.size() == 2);
    REQUIRE(order[0] == 10);
    REQUIRE(order[1] == 20);
}
