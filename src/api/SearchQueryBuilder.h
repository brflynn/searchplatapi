// Copyright (C) Microsoft Corporation. All rights reserved.
#pragma once

#include "WSearchLogging.h"
#include "SearchTokenizer.h"
#include <string>
#include <vector>
#include <sstream>

namespace wsearch
{

/* SearchQueryBuilder - Builder pattern for constructing Windows Search SQL queries
 * 
 * This class provides a fluent interface for building complex search queries with:
 * - Proper ranking (ABSOLUTE for exact, MINMAX for prefix/content)
 * - Scope filtering
 * - Additional property selection
 * - Multi-word tokenization support
 * 
 * Example:
 *   SearchQueryBuilder builder;
 *   auto query = builder
 *       .WithScopes({L"C:\\Users\\Documents"})
 *       .WithProperties({L"System.ItemNameDisplay", L"System.Size"})
 *       .WithSearchText(L"annual report")
 *       .WithTopN(30)
 *       .Build();
 */
class SearchQueryBuilder
{
public:
    SearchQueryBuilder()
        : m_topN(0)
        , m_locale(1033) // en-US
    {
    }

    // Set the scopes to search within
    SearchQueryBuilder& WithScopes(const std::vector<std::wstring>& includedScopes)
    {
        m_includedScopes = includedScopes;
        return *this;
    }

    // Set excluded scopes
    SearchQueryBuilder& WithExcludedScopes(const std::vector<std::wstring>& excludedScopes)
    {
        m_excludedScopes = excludedScopes;
        return *this;
    }

    // Set additional properties to retrieve
    SearchQueryBuilder& WithProperties(const std::vector<std::wstring>& properties)
    {
        m_additionalProperties = properties;
        return *this;
    }

    // Set the search text
    SearchQueryBuilder& WithSearchText(std::wstring_view searchText)
    {
        m_searchText = searchText;
        return *this;
    }

    // Set TOP N limit
    SearchQueryBuilder& WithTopN(size_t topN)
    {
        m_topN = topN;
        return *this;
    }

    // Set locale for CONTAINS queries (default: 1033 = en-US)
    SearchQueryBuilder& WithLocale(DWORD locale)
    {
        m_locale = locale;
        return *this;
    }

    // Build the complete query string
    std::wstring Build()
    {
        TelemetryProvider::LogInfo(L"Building search query");
        
        std::wostringstream query;
        
        // SELECT clause
        query << BuildSelectClause();
        
        // FROM clause
        query << L" FROM SystemIndex";
        
        // WHERE clause
        query << BuildWhereClause();
        
        // ORDER BY clause
        query << BuildOrderByClause();
        
        std::wstring result = query.str();
        TelemetryProvider::LogInfo(L"Built query: %ls", result.c_str());
        
        return result;
    }

    // Build a priming query (without search text, for optimization)
    std::wstring BuildPrimingQuery()
    {
        TelemetryProvider::LogInfo(L"Building priming query");
        
        std::wostringstream query;
        
        // SELECT clause
        query << BuildSelectClause();
        
        // FROM clause
        query << L" FROM SystemIndex";
        
        // WHERE clause (only scopes, no search text)
        query << BuildScopeOnlyWhereClause();
        
        std::wstring result = query.str();
        TelemetryProvider::LogInfo(L"Built priming query: %ls", result.c_str());
        
        return result;
    }

private:
    std::wstring BuildSelectClause()
    {
        std::wostringstream select;
        
        if (m_topN > 0)
        {
            select << L"SELECT TOP " << m_topN << L" ";
        }
        else
        {
            select << L"SELECT ";
        }
        
        // Always include core properties
        select << L"System.ItemUrl, System.Search.Rank";
        
        // Add System.Document.LineCount for click tracking
        select << L", System.Document.LineCount";
        
        // Add System.DateAccessed for recently accessed files
        select << L", System.DateAccessed";
        
        // Add common useful properties
        select << L", System.ItemNameDisplay";
        select << L", System.Kind";
        select << L", System.FileExtension";
        select << L", System.Size";
        select << L", System.DateCreated";
        select << L", System.DateModified";
        select << L", System.Search.GatherTime";
        
        // Add user-specified additional properties
        for (const auto& prop : m_additionalProperties)
        {
            // Check if not already added
            std::wstring selectStr = select.str();
            if (selectStr.find(prop) == std::wstring::npos)
            {
                select << L", " << prop;
            }
        }
        
        return select.str();
    }

    std::wstring BuildScopeOnlyWhereClause()
    {
        if (m_includedScopes.empty())
        {
            return L"";
        }
        
        std::wostringstream where;
        where << L" WHERE ";
        
        // Build included scopes
        where << L"(";
        for (size_t i = 0; i < m_includedScopes.size(); ++i)
        {
            if (i > 0) where << L" OR ";
            
            std::wstring scope = m_includedScopes[i];
            std::replace(scope.begin(), scope.end(), L'\\', L'/');
            
            if (scope.find(L"file:") != 0)
            {
                scope = L"file:" + scope;
            }
            
            where << L"SCOPE='" << scope << L"'";
        }
        where << L")";
        
        // Build excluded scopes
        for (size_t i = 0; i < m_excludedScopes.size(); ++i)
        {
            std::wstring scope = m_excludedScopes[i];
            std::replace(scope.begin(), scope.end(), L'\\', L'/');
            
            if (scope.find(L"file:") != 0)
            {
                scope = L"file:" + scope;
            }
            
            where << L" AND SCOPE<>'" << scope << L"'";
        }
        
        return where.str();
    }

    std::wstring BuildWhereClause()
    {
        std::wostringstream where;
        
        // Start with scope filtering
        std::wstring scopeWhere = BuildScopeOnlyWhereClause();
        if (!scopeWhere.empty())
        {
            where << scopeWhere;
        }
        else
        {
            where << L" WHERE ";
        }
        
        // Add search text filtering if provided
        if (!m_searchText.empty())
        {
            if (!scopeWhere.empty())
            {
                where << L" AND ";
            }
            
            where << BuildSearchCondition();
        }
        
        return where.str();
    }

    std::wstring BuildSearchCondition()
    {
        // Tokenize the search text
        SearchTokenizer tokenizer(m_searchText);
        
        if (tokenizer.IsEmpty())
        {
            return L"";
        }
        
        TelemetryProvider::LogInfo(L"Search text tokenized into %zu token(s)", tokenizer.GetTokenCount());
        
        std::wostringstream condition;
        
        // Use WITH clause to define #MRProps (Most Relevant Properties)
        condition << L"WITH (System.ItemNameDisplay) AS #MRProps ";
        
        if (tokenizer.IsQuoted())
        {
            // User explicitly quoted the search - use exact phrase only
            condition << BuildQuotedSearchCondition(tokenizer);
        }
        else if (tokenizer.IsSingleToken())
        {
            // Single word search - use simplified ranking
            condition << BuildSingleWordCondition(tokenizer.GetTokens()[0]);
        }
        else
        {
            // Multiple words - use complex multi-word matching
            condition << BuildMultiWordCondition(tokenizer.GetTokens());
        }
        
        return condition.str();
    }

    std::wstring BuildQuotedSearchCondition(const SearchTokenizer& tokenizer)
    {
        std::wostringstream condition;
        const auto& tokens = tokenizer.GetTokens();
        std::wstring phrase = tokens[0]; // Already has quotes removed by tokenizer
        
        // Exact phrase match only
        condition << L"(CONTAINS(#MRProps, '\"" << EscapeForContains(phrase) << L"\"', " << m_locale << L") ";
        condition << L"RANK BY COERCION(ABSOLUTE, 999))";
        
        return condition.str();
    }

    std::wstring BuildSingleWordCondition(const std::wstring& word)
    {
        std::wostringstream condition;
        std::wstring escaped = EscapeForContains(word);
        
        condition << L"(";
        
        // 1. Exact match on filename (highest priority) - ABSOLUTE rank
        condition << L"(CONTAINS(#MRProps, '\"" << escaped << L"\"', " << m_locale << L") ";
        condition << L"RANK BY COERCION(ABSOLUTE, 990))";
        
        // 2. Prefix match on filename - MINMAX for natural ranking
        condition << L" OR (CONTAINS(#MRProps, '\"" << escaped << L"*\"', " << m_locale << L") ";
        condition << L"RANK BY COERCION(MINMAX, 900, 980))";
        
        // 3. Content match anywhere - MINMAX for natural ranking
        condition << L" OR (CONTAINS(*, '\"" << escaped << L"\"', " << m_locale << L") ";
        condition << L"RANK BY COERCION(MINMAX, 0, 899))";
        
        condition << L")";
        
        return condition.str();
    }

    std::wstring BuildMultiWordCondition(const std::vector<std::wstring>& tokens)
    {
        std::wostringstream condition;
        
        // Build the full phrase for exact matching
        std::wstring fullPhrase;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            if (i > 0) fullPhrase += L" ";
            fullPhrase += tokens[i];
        }
        std::wstring escapedPhrase = EscapeForContains(fullPhrase);
        
        condition << L"(";
        
        // 1. Exact phrase match on filename (highest priority) - ABSOLUTE rank
        condition << L"(CONTAINS(#MRProps, '\"" << escapedPhrase << L"\"', " << m_locale << L") ";
        condition << L"RANK BY COERCION(ABSOLUTE, 990))";
        
        // 2. Prefix match on full phrase on filename - MINMAX
        condition << L" OR (CONTAINS(#MRProps, '\"" << escapedPhrase << L"*\"', " << m_locale << L") ";
        condition << L"RANK BY COERCION(MINMAX, 900, 980))";
        
        // 3. All words present (AND) with prefix matching - MINMAX
        condition << L" OR (";
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            if (i > 0) condition << L" AND ";
            std::wstring escaped = EscapeForContains(tokens[i]);
            condition << L"CONTAINS(*, '\"" << escaped << L"*\"', " << m_locale << L")";
        }
        condition << L" RANK BY COERCION(MINMAX, 0, 899))";
        
        condition << L")";
        
        return condition.str();
    }

    std::wstring BuildOrderByClause()
    {
        // Order by:
        // 1. Search.Rank (primary - includes our COERCION ranking)
        // 2. Document.LineCount (click count - secondary)
        // 3. DateAccessed (recently accessed - tertiary)
        // 4. Search.GatherTime (recently indexed - quaternary)
        return L" ORDER BY System.Search.Rank DESC, System.Document.LineCount DESC, System.DateAccessed DESC, System.Search.GatherTime DESC";
    }

    std::wstring EscapeForContains(const std::wstring& text)
    {
        std::wstring escaped;
        for (wchar_t ch : text)
        {
            // Escape single quotes for SQL
            if (ch == L'\'')
            {
                escaped += L"''";
            }
            // Escape double quotes for CONTAINS
            else if (ch == L'"')
            {
                escaped += L"\"\"";
            }
            else
            {
                escaped += ch;
            }
        }
        return escaped;
    }

    std::vector<std::wstring> m_includedScopes;
    std::vector<std::wstring> m_excludedScopes;
    std::vector<std::wstring> m_additionalProperties;
    std::wstring m_searchText;
    size_t m_topN;
    DWORD m_locale;
};

} // namespace wsearch
