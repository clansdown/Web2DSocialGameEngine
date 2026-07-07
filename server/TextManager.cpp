#include "TextManager.hpp"
#include <fstream>
#include <sstream>
#include <regex>

TextManager::TextManager(std::string_view base_path)
    : m_base_path(base_path)
{
}

std::string TextManager::get_text(const std::string& language, const std::string& text_id) const
{
    auto lang_path = m_base_path / language / (text_id + ".txt");
    auto content = read_file(lang_path);
    if (!content.empty()) {
        return content;
    }

    if (language != "en") {
        auto en_path = m_base_path / "en" / (text_id + ".txt");
        content = read_file(en_path);
        if (!content.empty()) {
            return content;
        }
    }

    return "";
}

std::string TextManager::substitute_gender(const std::string& text, const std::optional<std::string>& sex)
{
    static const std::regex pattern(R"(\{([^|}]+)\|([^}]+)\})");

    bool use_male = !sex.has_value() || sex->empty() || *sex != "female";

    std::string result;
    std::sregex_iterator iter(text.begin(), text.end(), pattern);
    std::sregex_iterator end;
    std::size_t last_pos = 0;

    for (; iter != end; ++iter) {
        result += text.substr(last_pos, iter->position() - last_pos);
        if (use_male) {
            result += (*iter)[1].str();
        } else {
            result += (*iter)[2].str();
        }
        last_pos = iter->position() + iter->length();
    }

    result += text.substr(last_pos);
    return result;
}

std::string TextManager::read_file(const std::filesystem::path& path) const
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
