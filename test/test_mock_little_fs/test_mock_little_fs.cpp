#include <unity.h>
#include "mock/MockLittleFS.h"
#include <string>

MockLittleFS MockFS;

void setUp() {
    MockFS.begin(true);
}

void tearDown() {
    MockFS.reset();
}

void test_file_write_read() {
    const char* testPath = "/test.txt";
    const char* testData = "Hello, World!";
    size_t dataLen = strlen(testData);
    
    // Write test
    MockFile file = MockFS.open(testPath, "w");
    TEST_ASSERT_TRUE(file.isOpen());
    TEST_ASSERT_EQUAL(dataLen, file.write((uint8_t*)testData, dataLen));
    file.close();
    
    // Read test
    file = MockFS.open(testPath, "r");
    TEST_ASSERT_TRUE(file.isOpen());
    uint8_t buffer[64] = {0};
    TEST_ASSERT_EQUAL(dataLen, file.read(buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING(testData, (char*)buffer);
    file.close();
}

void test_file_append() {
    const char* testPath = "/append.txt";
    const char* data1 = "First line\n";
    const char* data2 = "Second line";
    
    // Write initial data
    MockFile file = MockFS.open(testPath, "w");
    file.write((uint8_t*)data1, strlen(data1));
    file.close();
    
    // Append data
    file = MockFS.open(testPath, "a");
    file.write((uint8_t*)data2, strlen(data2));
    file.close();
    
    // Verify combined data
    file = MockFS.open(testPath, "r");
    char buffer[64] = {0};
    size_t totalLen = strlen(data1) + strlen(data2);
    TEST_ASSERT_EQUAL(totalLen, file.read((uint8_t*)buffer, sizeof(buffer)));
    TEST_ASSERT_TRUE(strstr(buffer, data1) != nullptr);
    TEST_ASSERT_TRUE(strstr(buffer, data2) != nullptr);
    file.close();
}

void test_file_seek() {
    const char* testPath = "/seek.txt";
    const char* testData = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    
    // Write test data
    MockFile file = MockFS.open(testPath, "w");
    file.write((uint8_t*)testData, strlen(testData));
    file.close();
    
    // Test seeking
    file = MockFS.open(testPath, "r");
    TEST_ASSERT_TRUE(file.seek(5));
    TEST_ASSERT_EQUAL('F', file.read());
    
    TEST_ASSERT_TRUE(file.seek(10));
    TEST_ASSERT_EQUAL('K', file.read());
    
    TEST_ASSERT_FALSE(file.seek(100)); // Beyond file size
    file.close();
}

void test_file_operations() {
    const char* sourcePath = "/source.txt";
    const char* destPath = "/dest.txt";
    const char* testData = "Test data";
    
    // Create file
    MockFile file = MockFS.open(sourcePath, "w");
    file.write((uint8_t*)testData, strlen(testData));
    file.close();
    
    // Test exists
    TEST_ASSERT_TRUE(MockFS.exists(sourcePath));
    TEST_ASSERT_FALSE(MockFS.exists(destPath));
    
    // Test rename
    TEST_ASSERT_TRUE(MockFS.rename(sourcePath, destPath));
    TEST_ASSERT_FALSE(MockFS.exists(sourcePath));
    TEST_ASSERT_TRUE(MockFS.exists(destPath));
    
    // Test remove
    TEST_ASSERT_TRUE(MockFS.remove(destPath));
    TEST_ASSERT_FALSE(MockFS.exists(destPath));
}

void test_directory_operations() {
    const char* dirPath = "/testdir";
    
    TEST_ASSERT_TRUE(MockFS.mkdir(dirPath));
    TEST_ASSERT_TRUE(MockFS.rmdir(dirPath));
}

void test_file_modes() {
    const char* testPath = "/modes.txt";
    const char* testData = "Test data";
    uint8_t buffer[64];
    
    // Test write-only mode
    MockFile file = MockFS.open(testPath, "w");
    TEST_ASSERT_TRUE(file.isOpen());
    TEST_ASSERT_EQUAL(0, file.read(buffer, sizeof(buffer))); // Should fail
    TEST_ASSERT_GREATER_THAN(0, file.write((uint8_t*)testData, strlen(testData)));
    file.close();
    
    // Test read-only mode
    file = MockFS.open(testPath, "r");
    TEST_ASSERT_TRUE(file.isOpen());
    TEST_ASSERT_EQUAL(0, file.write((uint8_t*)"New data", 8)); // Should fail
    TEST_ASSERT_GREATER_THAN(0, file.read(buffer, sizeof(buffer)));
    file.close();
    
    // Test read-write mode
    file = MockFS.open(testPath, "r+");
    TEST_ASSERT_TRUE(file.isOpen());
    TEST_ASSERT_GREATER_THAN(0, file.read(buffer, sizeof(buffer)));
    TEST_ASSERT_GREATER_THAN(0, file.write((uint8_t*)"New data", 8));
    file.close();
}

int runUnityTests() {
    UNITY_BEGIN();
    
    RUN_TEST(test_file_write_read);
    RUN_TEST(test_file_append);
    RUN_TEST(test_file_seek);
    RUN_TEST(test_file_operations);
    RUN_TEST(test_directory_operations);
    RUN_TEST(test_file_modes);
    
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