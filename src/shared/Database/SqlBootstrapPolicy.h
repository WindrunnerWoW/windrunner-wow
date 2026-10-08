#pragma once

#include <regex>
#include <set>
#include <string>
#include <vector>

namespace DBUpdater
{
    // Installation dumps may DROP and seed several tables. Adopt tables that
    // already exist, and initialize only the missing ones on a partial install.
    class SqlBootstrapPolicy
    {
    public:
        explicit SqlBootstrapPolicy(const std::vector<std::string>& queries)
        {
            static const std::regex createTable(
                R"(^\s*CREATE\s+TABLE\s+(?:IF\s+NOT\s+EXISTS\s+)?`?([A-Za-z0-9_]+)`?)", std::regex::icase);
            for (const auto& query : queries)
            {
                std::smatch match;
                if (std::regex_search(query, match, createTable))
                    tables.insert(match[1].str());
                std::string table = SeedTable(query);
                if (!table.empty())
                    seededTables.insert(table);
            }
        }

        bool ShouldSkip(const std::string& query) const
        {
            // A schema-only installation still needs seed rows, but its
            // existing table definition must never be dropped or recreated.
            if (emptySeedTables.count(SeedTable(query)))
                return false;
            if (!tables.empty() && existingTables.size() == tables.size())
                return true;

            static const std::regex targetTable(
                R"(^\s*(?:DROP\s+TABLE\s+(?:IF\s+EXISTS\s+)?|CREATE\s+TABLE\s+(?:IF\s+NOT\s+EXISTS\s+)?|INSERT\s+(?:IGNORE\s+)?INTO\s+|REPLACE\s+INTO\s+|DELETE\s+FROM\s+|UPDATE\s+|ALTER\s+TABLE\s+|TRUNCATE\s+(?:TABLE\s+)?)(?:`?)([A-Za-z0-9_]+))", std::regex::icase);
            std::smatch match;
            return std::regex_search(query, match, targetTable) && existingTables.count(match[1].str());
        }

        static bool CreatedIndex(const std::string& query, std::string& table, std::string& index)
        {
            static const std::regex createIndex(
                R"(^\s*CREATE\s+(?:UNIQUE\s+)?INDEX\s+`?([A-Za-z0-9_]+)`?\s+ON\s+`?([A-Za-z0-9_]+)`?)", std::regex::icase);
            std::smatch match;
            if (!std::regex_search(query, match, createIndex))
                return false;
            index = match[1].str();
            table = match[2].str();
            return true;
        }

        static std::string SeedTable(const std::string& query)
        {
            static const std::regex seed(
                R"(^\s*(?:INSERT\s+(?:IGNORE\s+)?INTO|REPLACE\s+INTO)\s+`?([A-Za-z0-9_]+))", std::regex::icase);
            std::smatch match;
            return std::regex_search(query, match, seed) ? match[1].str() : std::string{};
        }

        std::set<std::string> tables;
        std::set<std::string> existingTables;
        std::set<std::string> seededTables;
        std::set<std::string> emptySeedTables;
    };
}
