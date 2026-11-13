// test_auth_policy.cpp — token parsing, authentication, per-method
// authorization, rate limiting and lockout (issue #12).  Pure logic.

#include <unity.h>
#include "AuthPolicy.h"

using namespace mcp::auth;

void setUp(void) {}
void tearDown(void) {}

static Credentials creds() {
    Credentials c;
    c.fullToken = "full-0123456789abcdef";
    c.readToken = "read-0123456789abcdef";
    return c;
}

// --- extraction ---------------------------------------------------------------

void test_extract_bearer(void) {
    TEST_ASSERT_EQUAL_STRING("abc", extractBearer("Bearer abc").c_str());
    TEST_ASSERT_EQUAL_STRING("abc", extractBearer("  bearer   abc  ").c_str());
    TEST_ASSERT_EQUAL_STRING("abc", extractBearer("BEARER abc").c_str());
    TEST_ASSERT_EQUAL_STRING("", extractBearer("Basic YWRtaW46YWRtaW4=").c_str());
    TEST_ASSERT_EQUAL_STRING("", extractBearer("Bearer").c_str());
    TEST_ASSERT_EQUAL_STRING("", extractBearer("Bearerabc").c_str());
    TEST_ASSERT_EQUAL_STRING("", extractBearer("").c_str());
}

void test_extract_query_token(void) {
    TEST_ASSERT_EQUAL_STRING("abc", extractQueryToken("/?token=abc").c_str());
    TEST_ASSERT_EQUAL_STRING("abc", extractQueryToken("/mcp?x=1&token=abc&y=2").c_str());
    TEST_ASSERT_EQUAL_STRING("a b+c", extractQueryToken("/?token=a%20b+c").c_str());
    TEST_ASSERT_EQUAL_STRING("", extractQueryToken("/").c_str());
    TEST_ASSERT_EQUAL_STRING("", extractQueryToken("/?mytoken=abc").c_str());
    TEST_ASSERT_EQUAL_STRING("", extractQueryToken("/?token").c_str());
    TEST_ASSERT_EQUAL_STRING("%zz", extractQueryToken("/?token=%zz").c_str());
}

void test_constant_time_equals(void) {
    TEST_ASSERT_TRUE(constantTimeEquals("secret", "secret"));
    TEST_ASSERT_FALSE(constantTimeEquals("secret", "secreT"));
    TEST_ASSERT_FALSE(constantTimeEquals("secret", "secret2"));
    TEST_ASSERT_FALSE(constantTimeEquals("", "x"));
    TEST_ASSERT_TRUE(constantTimeEquals("", ""));
}

// --- authenticate -------------------------------------------------------------

void test_missing_or_wrong_token_is_rejected(void) {
    Credentials c = creds();
    TEST_ASSERT_TRUE(authenticate(c, "", "/") == Access::None);
    TEST_ASSERT_TRUE(authenticate(c, "Bearer nope", "/") == Access::None);
    TEST_ASSERT_TRUE(authenticate(c, "", "/?token=nope") == Access::None);
    // A prefix of the real token must not pass.
    TEST_ASSERT_TRUE(authenticate(c, "Bearer full-0123", "/") == Access::None);
}

void test_full_and_read_tokens(void) {
    Credentials c = creds();
    TEST_ASSERT_TRUE(authenticate(c, "Bearer full-0123456789abcdef", "/") == Access::Full);
    TEST_ASSERT_TRUE(authenticate(c, "", "/?token=read-0123456789abcdef") == Access::ReadOnly);
}

void test_header_wins_over_query(void) {
    Credentials c = creds();
    TEST_ASSERT_TRUE(authenticate(c, "Bearer read-0123456789abcdef",
                                  "/?token=full-0123456789abcdef") == Access::ReadOnly);
}

void test_no_read_token_configured(void) {
    Credentials c = creds();
    c.readToken.clear();
    // An empty presented token must never match the empty read token.
    TEST_ASSERT_TRUE(authenticate(c, "", "/?token=") == Access::None);
}

void test_no_token_configured_refuses_unless_lab_build(void) {
    Credentials c;
    TEST_ASSERT_TRUE(authenticate(c, "Bearer anything", "/") == Access::None);
    c.allowUnauthenticated = true;
    TEST_ASSERT_TRUE(authenticate(c, "", "/") == Access::Full);
}

void test_lab_flag_does_not_bypass_a_configured_token(void) {
    Credentials c = creds();
    c.allowUnauthenticated = true;
    TEST_ASSERT_TRUE(authenticate(c, "", "/") == Access::None);
}

// --- method authorization -------------------------------------------------------

void test_full_access_allows_everything(void) {
    TEST_ASSERT_TRUE(methodAllowed(Access::Full, "logs/clear"));
    TEST_ASSERT_TRUE(methodAllowed(Access::Full, "ble/connect"));
    TEST_ASSERT_TRUE(methodAllowed(Access::Full, "some/future/method"));
}

void test_read_only_whitelist(void) {
    TEST_ASSERT_TRUE(methodAllowed(Access::ReadOnly, "initialize"));
    TEST_ASSERT_TRUE(methodAllowed(Access::ReadOnly, "resources/read"));
    TEST_ASSERT_TRUE(methodAllowed(Access::ReadOnly, "sensors/read"));
    TEST_ASSERT_TRUE(methodAllowed(Access::ReadOnly, "logs/query"));
    TEST_ASSERT_TRUE(methodAllowed(Access::ReadOnly, "ble/scan"));
    TEST_ASSERT_TRUE(methodAllowed(Access::ReadOnly, "bus/history/read"));
}

void test_read_only_denies_state_changes(void) {
    TEST_ASSERT_FALSE(methodAllowed(Access::ReadOnly, "logs/clear"));
    TEST_ASSERT_FALSE(methodAllowed(Access::ReadOnly, "ble/connect"));
    TEST_ASSERT_FALSE(methodAllowed(Access::ReadOnly, "ble/services/start"));
    TEST_ASSERT_FALSE(methodAllowed(Access::ReadOnly, "ble/disconnect"));
    TEST_ASSERT_FALSE(methodAllowed(Access::ReadOnly, "bus/history/config/set"));
    // Deny by default: new methods need full control until whitelisted.
    TEST_ASSERT_FALSE(methodAllowed(Access::ReadOnly, "some/future/method"));
}

void test_no_access_allows_nothing(void) {
    TEST_ASSERT_FALSE(methodAllowed(Access::None, "initialize"));
}

// --- rate limiter ---------------------------------------------------------------

void test_rate_limiter_burst_then_refill(void) {
    RateLimiter rl(3, 10);  // 3 burst, 10/s -> one token per 100 ms
    TEST_ASSERT_TRUE(rl.allow(1000));
    TEST_ASSERT_TRUE(rl.allow(1000));
    TEST_ASSERT_TRUE(rl.allow(1000));
    TEST_ASSERT_FALSE(rl.allow(1000));
    TEST_ASSERT_EQUAL_UINT32(100, rl.retryAfterMs());
    TEST_ASSERT_FALSE(rl.allow(1050));
    TEST_ASSERT_TRUE(rl.allow(1100));
}

void test_rate_limiter_caps_at_capacity(void) {
    RateLimiter rl(2, 10);
    rl.allow(0);
    // A long idle period refills to the burst size, not beyond.
    TEST_ASSERT_TRUE(rl.allow(100000));
    TEST_ASSERT_TRUE(rl.allow(100000));
    TEST_ASSERT_FALSE(rl.allow(100000));
}

void test_rate_limiter_survives_millis_wrap(void) {
    RateLimiter rl(1, 10);
    TEST_ASSERT_TRUE(rl.allow(0xFFFFFFF0u));
    TEST_ASSERT_FALSE(rl.allow(0xFFFFFFF0u));
    TEST_ASSERT_TRUE(rl.allow(0x00000060u));  // 112 ms later, across the wrap
}

// --- failure lockout -------------------------------------------------------------

void test_lockout_after_threshold(void) {
    FailureLockout lo(3, 1000, 8000);
    lo.recordFailure(0);
    lo.recordFailure(0);
    TEST_ASSERT_FALSE(lo.locked(0));
    lo.recordFailure(0);
    TEST_ASSERT_TRUE(lo.locked(0));
    TEST_ASSERT_EQUAL_UINT32(1000, lo.retryAfterMs(0));
    TEST_ASSERT_FALSE(lo.locked(1000));
}

void test_lockout_doubles_and_caps(void) {
    FailureLockout lo(1, 1000, 3000);
    lo.recordFailure(0);           // 1 s
    TEST_ASSERT_EQUAL_UINT32(1000, lo.retryAfterMs(0));
    lo.recordFailure(1000);        // 2 s
    TEST_ASSERT_EQUAL_UINT32(2000, lo.retryAfterMs(1000));
    lo.recordFailure(3000);        // 4 s -> capped at 3 s
    TEST_ASSERT_EQUAL_UINT32(3000, lo.retryAfterMs(3000));
}

void test_lockout_success_resets(void) {
    FailureLockout lo(2, 1000, 8000);
    lo.recordFailure(0);
    lo.recordSuccess();
    lo.recordFailure(0);
    TEST_ASSERT_FALSE(lo.locked(0));
    TEST_ASSERT_EQUAL_UINT32(1, lo.failures());
}

// --- token generation ---------------------------------------------------------------

void test_generate_token_hex(void) {
    uint32_t seq = 0x11223344u;
    std::string t = generateToken([&seq]() { uint32_t v = seq; seq += 0x01010101u; return v; }, 6);
    TEST_ASSERT_EQUAL(12, (int)t.size());
    TEST_ASSERT_EQUAL_STRING("443322114534", t.c_str());  // LE bytes of 0x11223344, 0x12233445
    for (char c : generateToken([]() { return 0xDEADBEEFu; }, 16)) {
        TEST_ASSERT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_extract_bearer);
    RUN_TEST(test_extract_query_token);
    RUN_TEST(test_constant_time_equals);
    RUN_TEST(test_missing_or_wrong_token_is_rejected);
    RUN_TEST(test_full_and_read_tokens);
    RUN_TEST(test_header_wins_over_query);
    RUN_TEST(test_no_read_token_configured);
    RUN_TEST(test_no_token_configured_refuses_unless_lab_build);
    RUN_TEST(test_lab_flag_does_not_bypass_a_configured_token);
    RUN_TEST(test_full_access_allows_everything);
    RUN_TEST(test_read_only_whitelist);
    RUN_TEST(test_read_only_denies_state_changes);
    RUN_TEST(test_no_access_allows_nothing);
    RUN_TEST(test_rate_limiter_burst_then_refill);
    RUN_TEST(test_rate_limiter_caps_at_capacity);
    RUN_TEST(test_rate_limiter_survives_millis_wrap);
    RUN_TEST(test_lockout_after_threshold);
    RUN_TEST(test_lockout_doubles_and_caps);
    RUN_TEST(test_lockout_success_resets);
    RUN_TEST(test_generate_token_hex);
    return UNITY_END();
}
