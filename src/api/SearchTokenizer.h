// Copyright (C) Microsoft Corporation. All rights reserved.
#pragma once

#include <winrt/Windows.Data.Text.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <string>
#include <vector>
#include <cctype>

namespace wsearch
{

/* SearchTokenizer - Uses Windows word segmentation to tokenize search text
 * 
 * This class uses the Windows.Data.Text API to properly segment text into words,
 * respecting language-specific rules and boundaries.
 * 
 * Example:
 *   SearchTokenizer tokenizer(L"hello world");
 *   auto tokens = tokenizer.GetTokens();
 *   // tokens = { L"hello", L"world" }
 */
class SearchTokenizer
{
public:
    SearchTokenizer(std::wstring_view text)
        : m_text(text)
    {
        TokenizeText();
    }

    // Get the list of tokens
    const std::vector<std::wstring>& GetTokens() const
    {
        return m_tokens;
    }

    // Check if input is a single token
    bool IsSingleToken() const
    {
        return m_tokens.size() == 1;
    }

    // Check if input has multiple tokens
    bool HasMultipleTokens() const
    {
        return m_tokens.size() > 1;
    }

    // Get the number of tokens
    size_t GetTokenCount() const
    {
        return m_tokens.size();
    }

    // Get the original text
    std::wstring_view GetOriginalText() const
    {
        return m_text;
    }

    // Check if text is empty
    bool IsEmpty() const
    {
        return m_tokens.empty();
    }

    // Check if text is quoted (user wants exact phrase)
    bool IsQuoted() const
    {
        return m_text.length() >= 2 && 
               m_text.front() == L'"' && 
               m_text.back() == L'"';
    }

private:
    void TokenizeText()
    {
        if (m_text.empty())
        {
            return;
        }

        // Check if the text is quoted - if so, treat as single token
        if (IsQuoted())
        {
            // Remove quotes and store as single token
            m_tokens.push_back(std::wstring(m_text.substr(1, m_text.length() - 2)));
            return;
        }

        try
        {
            // Use Windows word segmenter for proper tokenization
            auto wordSegmenter = winrt::Windows::Data::Text::WordsSegmenter(L"en-US");
            auto segments = wordSegmenter.GetTokens(winrt::hstring(m_text));

            for (const auto& segment : segments)
            {
                std::wstring token(segment.Text());
                // Filter out empty tokens and pure whitespace/punctuation
                if (!token.empty() && iswalnum(token[0]))
                {
                    m_tokens.push_back(token);
                }
            }

            // Fallback: if no tokens found, split on whitespace
            if (m_tokens.empty())
            {
                SplitOnWhitespace();
            }
        }
        catch (...)
        {
            // Fallback to simple whitespace splitting if Windows API fails
            SplitOnWhitespace();
        }
    }

    void SplitOnWhitespace()
    {
        std::wstring token;
        for (wchar_t ch : m_text)
        {
            if (iswspace(ch))
            {
                if (!token.empty())
                {
                    m_tokens.push_back(token);
                    token.clear();
                }
            }
            else
            {
                token += ch;
            }
        }
        
        if (!token.empty())
        {
            m_tokens.push_back(token);
        }
    }

    std::wstring m_text;
    std::vector<std::wstring> m_tokens;
};

} // namespace wsearch
