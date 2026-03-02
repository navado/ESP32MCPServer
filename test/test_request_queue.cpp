#include <unity.h>
#include "RequestQueue.h"
#include <atomic>
#include <thread>
#include <chrono>

void setUp(void) {
    // Set up any test prerequisites
}

void tearDown(void) {
    // Clean up after tests
}

void test_request_queue_push_pop() {
    RequestQueue<int> queue;
    TEST_ASSERT_TRUE(queue.empty());
    
    queue.push(42);
    TEST_ASSERT_FALSE(queue.empty());
    TEST_ASSERT_EQUAL(1, queue.size());
    
    int value;
    TEST_ASSERT_TRUE(queue.pop(value));
    TEST_ASSERT_EQUAL(42, value);
    TEST_ASSERT_TRUE(queue.empty());
}

void test_request_queue_multiple_items() {
    RequestQueue<int> queue;
    
    for (int i = 0; i < 5; i++) {
        queue.push(i);
    }
    
    TEST_ASSERT_EQUAL(5, queue.size());
    
    for (int i = 0; i < 5; i++) {
        int value;
        TEST_ASSERT_TRUE(queue.pop(value));
        TEST_ASSERT_EQUAL(i, value);
    }
    
    TEST_ASSERT_TRUE(queue.empty());
}

void test_request_queue_thread_safety() {
    RequestQueue<int> queue;
    std::atomic<bool> done(false);
    
    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < 100; i++) {
            queue.push(i);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        done = true;
    });
    
    // Consumer thread
    std::thread consumer([&]() {
        int count = 0;
        while (!done || !queue.empty()) {
            int value;
            if (queue.pop(value)) {
                TEST_ASSERT_LESS_THAN(100, value);
                count++;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        TEST_ASSERT_EQUAL(100, count);
    });
    
    producer.join();
    consumer.join();
}

int runUnityTests() {
    UNITY_BEGIN();
    
    RUN_TEST(test_request_queue_push_pop);
    RUN_TEST(test_request_queue_multiple_items);
    RUN_TEST(test_request_queue_thread_safety);
    
    return UNITY_END();
}

#ifdef ARDUINO
void setup() {
    delay(2000);
    runUnityTests();
}

void loop() {
}
#else
int main() {
    return runUnityTests();
}
#endif