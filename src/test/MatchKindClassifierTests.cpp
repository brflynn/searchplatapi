// Copyright (C) Microsoft Corporation. All rights reserved.
#include "pch.h"
#include "../app/MatchKindClassifier.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace applocal;

namespace MatchKindClassifierTests
{
    TEST_CLASS(ContainsCaseInsensitiveTests)
    {
    public:
        TEST_METHOD(TestExactSubstringMatches)
        {
            Assert::IsTrue(ContainsCaseInsensitive(L"Quarterly Report", L"Report"));
        }

        TEST_METHOD(TestCaseInsensitiveMatches)
        {
            Assert::IsTrue(ContainsCaseInsensitive(L"Quarterly Report", L"report"));
            Assert::IsTrue(ContainsCaseInsensitive(L"QUARTERLY REPORT", L"quarterly"));
        }

        TEST_METHOD(TestNoMatch)
        {
            Assert::IsFalse(ContainsCaseInsensitive(L"Quarterly Report", L"invoice"));
        }

        TEST_METHOD(TestEmptyNeedleOrHaystackNeverMatches)
        {
            Assert::IsFalse(ContainsCaseInsensitive(L"", L"report"));
            Assert::IsFalse(ContainsCaseInsensitive(L"Quarterly Report", L""));
            Assert::IsFalse(ContainsCaseInsensitive(L"", L""));
        }
    };

    TEST_CLASS(ClassifyIndexerMatchTests)
    {
    public:
        TEST_METHOD(TestHighRankIsAlwaysFilename)
        {
            // Rank 990 = exact filename match tier (see SearchQueryBuilder.h).
            auto kind = ClassifyIndexerMatch(990, L"anything", MetadataFields{});
            Assert::IsTrue(kind == ClassifiedMatchKind::Filename);
        }

        TEST_METHOD(TestFilenamePrefixRankIsFilename)
        {
            // Rank 900 = bottom of the filename-prefix MINMAX tier.
            auto kind = ClassifyIndexerMatch(900, L"report", MetadataFields{});
            Assert::IsTrue(kind == ClassifiedMatchKind::Filename);
        }

        TEST_METHOD(TestSubThresholdRankWithMetadataHitIsMetadata)
        {
            MetadataFields metadata;
            metadata.title = L"Quarterly Report";
            auto kind = ClassifyIndexerMatch(500, L"report", metadata);
            Assert::IsTrue(kind == ClassifiedMatchKind::Metadata);
        }

        TEST_METHOD(TestSubThresholdRankChecksAllMetadataFields)
        {
            MetadataFields authorHit;
            authorHit.author = L"Jane Doe";
            Assert::IsTrue(ClassifyIndexerMatch(100, L"jane", authorHit) == ClassifiedMatchKind::Metadata);

            MetadataFields keywordsHit;
            keywordsHit.keywords = L"budget,forecast";
            Assert::IsTrue(ClassifyIndexerMatch(100, L"forecast", keywordsHit) == ClassifiedMatchKind::Metadata);

            MetadataFields commentHit;
            commentHit.comment = L"Draft for review";
            Assert::IsTrue(ClassifyIndexerMatch(100, L"draft", commentHit) == ClassifiedMatchKind::Metadata);
        }

        TEST_METHOD(TestSubThresholdRankWithNoMetadataHitIsContent)
        {
            MetadataFields metadata;
            metadata.title = L"Unrelated title";
            auto kind = ClassifyIndexerMatch(200, L"report", metadata);
            Assert::IsTrue(kind == ClassifiedMatchKind::Content);
        }
    };

    TEST_CLASS(MatchKindMergeTests)
    {
    public:
        TEST_METHOD(TestPriorityOrdering)
        {
            Assert::IsTrue(MatchKindPriority(ClassifiedMatchKind::Filename) > MatchKindPriority(ClassifiedMatchKind::Metadata));
            Assert::IsTrue(MatchKindPriority(ClassifiedMatchKind::Metadata) > MatchKindPriority(ClassifiedMatchKind::Content));
        }

        TEST_METHOD(TestBestMatchKindPrefersFilename)
        {
            auto best = BestMatchKind(ClassifiedMatchKind::Content, ClassifiedMatchKind::Filename);
            Assert::IsTrue(best == ClassifiedMatchKind::Filename);
        }

        TEST_METHOD(TestBestMatchKindPrefersMetadataOverContent)
        {
            auto best = BestMatchKind(ClassifiedMatchKind::Content, ClassifiedMatchKind::Metadata);
            Assert::IsTrue(best == ClassifiedMatchKind::Metadata);
        }

        TEST_METHOD(TestBestMatchKindIsStableWhenEqual)
        {
            auto best = BestMatchKind(ClassifiedMatchKind::Content, ClassifiedMatchKind::Content);
            Assert::IsTrue(best == ClassifiedMatchKind::Content);
        }
    };
}
