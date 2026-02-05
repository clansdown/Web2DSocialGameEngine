#pragma once
#include <string>
#include <vector>
#include <optional>

class SafeNameGenerator {
public:
    static SafeNameGenerator& getInstance() {
        static SafeNameGenerator instance;
        return instance;
    }

    bool initialize(const std::string& wordsFile1, const std::string& wordsFile2);

    std::optional<std::string> generateSafeDisplayName(
        const std::string& word1,
        const std::string& word2,
        const std::string& username);

    bool isValidWord(const std::string& word, int listNumber);

private:
    SafeNameGenerator() = default;

    bool loadWordList(const std::string& filename, std::vector<std::string>& list);

    std::vector<std::string> word_list_1;
    std::vector<std::string> word_list_2;
    bool initialized = false;
};