#pragma once
#include <string>
#include <filesystem>
#include <optional>

/**
 * Manages loading of game text files with language fallback.
 * 
 * Reads text/{language}/{id}.txt files from a configurable base path.
 * Automatically falls back to English if a translation is not available.
 * Supports gender substitution via {male|female} patterns in text files.
 */
class TextManager {
public:
    /**
     * @param base_path Directory containing language subdirectories (e.g. "./text")
     */
    explicit TextManager(std::string_view base_path);

    /**
     * Get text content for the given language and text ID.
     * Falls back to English if the requested language doesn't have the text.
     * Returns empty string if neither exists.
     */
    std::string get_text(const std::string& language, const std::string& text_id) const;

    /**
     * Replaces {male|female} gender tokens in text based on character sex.
     * Male option always comes first: {his|her} → "his" for male, "her" for female.
     * If sex is nullopt or empty, defaults to male form.
     * If sex is unknown (not "male" or "female"), defaults to male form.
     */
    static std::string substitute_gender(const std::string& text, const std::optional<std::string>& sex);

private:
    std::filesystem::path m_base_path;

    /** Read the full contents of a file as a string. Returns empty string if file doesn't exist. */
    std::string read_file(const std::filesystem::path& path) const;
};
