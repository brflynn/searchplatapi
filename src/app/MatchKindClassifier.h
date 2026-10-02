// MatchKindClassifier.h - pure, COM-free match-provenance classification.
//
// Deliberately has zero dependency on wsearch::SearchResult/IPropertyStore
// (or LocalIndex/SQLite) so it can be exercised directly by src/test without
// needing a live Windows Search indexer or a real filesystem - see
// src/test/MatchKindClassifierTests.cpp.
//
// Not part of the public src/api contract: this header is app-specific.
#pragma once

#include <algorithm>
#include <cwctype>
#include <string>

namespace applocal
{
    enum class ClassifiedMatchKind
    {
        Filename,
        Metadata,
        Content,
    };

    // The four PKEY_* string properties cheaply checked for a query-text
    // substring hit when an indexer-backed result's rank isn't already in
    // the filename tier.
    struct MetadataFields
    {
        std::wstring title;
        std::wstring author;
        std::wstring keywords;
        std::wstring comment;
    };

    // Below this, SearchQueryBuilder's ABSOLUTE/MINMAX COERCION tiers put
    // filename hits (exact ~990, prefix ~900-980); everything below 900 is
    // the content tier (0-899). See SearchQueryBuilder.h.
    constexpr int kFilenameRankThreshold = 900;

    inline bool ContainsCaseInsensitive(const std::wstring& haystack, const std::wstring& needle)
    {
        if (needle.empty() || haystack.empty())
        {
            return false;
        }
        auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
            [](wchar_t a, wchar_t b) { return towlower(a) == towlower(b); });
        return it != haystack.end();
    }

    // Classifies an indexer-backed result. Primarily derived from the
    // already-produced System.Search.Rank tier (no duplicate query-building
    // logic); falls back to a cheap metadata substring check to distinguish
    // Metadata from Content for sub-900-rank hits.
    inline ClassifiedMatchKind ClassifyIndexerMatch(
        int rank, const std::wstring& queryText, const MetadataFields& metadata)
    {
        if (rank >= kFilenameRankThreshold)
        {
            return ClassifiedMatchKind::Filename;
        }

        if (ContainsCaseInsensitive(metadata.title, queryText) ||
            ContainsCaseInsensitive(metadata.author, queryText) ||
            ContainsCaseInsensitive(metadata.keywords, queryText) ||
            ContainsCaseInsensitive(metadata.comment, queryText))
        {
            return ClassifiedMatchKind::Metadata;
        }

        return ClassifiedMatchKind::Content;
    }

    // Merge priority when the same path is produced by both the indexer and
    // the local index: Filename > Metadata > Content.
    inline int MatchKindPriority(ClassifiedMatchKind kind)
    {
        switch (kind)
        {
        case ClassifiedMatchKind::Filename: return 2;
        case ClassifiedMatchKind::Metadata: return 1;
        case ClassifiedMatchKind::Content:
        default:                            return 0;
        }
    }

    inline ClassifiedMatchKind BestMatchKind(ClassifiedMatchKind a, ClassifiedMatchKind b)
    {
        return MatchKindPriority(a) >= MatchKindPriority(b) ? a : b;
    }
}
