#include "SafeNameGenerator.hpp"
#include <fstream>
#include <algorithm>
#include <cctype>
#include "Database.hpp"

bool SafeNameGenerator::loadWordList(const std::string& filename, std::vector<std::string>& list) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            std::string trimmed = line;
            trimmed.erase(std::remove_if(trimmed.begin(), trimmed.end(),
                [](unsigned char c) { return std::isspace(c); }), trimmed.end());

            if (!trimmed.empty()) {
                list.push_back(trimmed);
            }
        }
    }

    return true;
}

bool SafeNameGenerator::initialize(const std::string& wordsFile1, const std::string& wordsFile2) {
    bool success = true;

    if (!loadWordList(wordsFile1, word_list_1)) {
        success = false;
    }

    if (!loadWordList(wordsFile2, word_list_2)) {
        success = false;
    }

    initialized = success;
    return success;
}

bool SafeNameGenerator::isValidWord(const std::string& word, int listNumber) {
    const std::vector<std::string>& list = (listNumber == 1) ? word_list_1 : word_list_2;

    return std::find(list.begin(), list.end(), word) != list.end();
}

std::optional<std::string> SafeNameGenerator::generateSafeDisplayName(
    const std::string& word1,
    const std::string& word2,
    const std::string& username)
{
    if (!initialized) {
        return std::nullopt;
    }

    if (!isValidWord(word1, 1) || !isValidWord(word2, 2)) {
        return std::nullopt;
    }

    std::string baseName = word1 + word2;
    std::string candidate = baseName;

    auto& db = Database::getInstance().gameDB();
    int count = 0;

    db << "SELECT COUNT(*) FROM users WHERE safeDisplayName LIKE ? || '%';"
       << baseName
       >> [&](int c) { count = c; };

    if (count > 0) {
        candidate = baseName + std::to_string(count);
    }

    return candidate;
}