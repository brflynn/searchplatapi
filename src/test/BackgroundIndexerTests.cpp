// Copyright (C) Microsoft Corporation. All rights reserved.
#include "pch.h"
#include "../app/BackgroundIndexer.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace applocal;

namespace BackgroundIndexerTests
{
    TEST_CLASS(ShouldIndexContentTests)
    {
    public:
        TEST_METHOD(TestKnownTextExtensionIsIndexed)
        {
            IndexerOptions options;
            Assert::IsTrue(BackgroundIndexer::ShouldIndexContent(L"C:\\src\\readme.txt", options));
            Assert::IsTrue(BackgroundIndexer::ShouldIndexContent(L"C:\\src\\main.cpp", options));
            Assert::IsTrue(BackgroundIndexer::ShouldIndexContent(L"C:\\src\\notes.MD", options));
        }

        TEST_METHOD(TestUnknownExtensionIsNotIndexed)
        {
            IndexerOptions options;
            Assert::IsFalse(BackgroundIndexer::ShouldIndexContent(L"C:\\bin\\app.exe", options));
            Assert::IsFalse(BackgroundIndexer::ShouldIndexContent(L"C:\\media\\photo.jpg", options));
        }

        TEST_METHOD(TestNoExtensionIsNotIndexed)
        {
            IndexerOptions options;
            Assert::IsFalse(BackgroundIndexer::ShouldIndexContent(L"C:\\Users\\me\\Makefile", options));
        }

        TEST_METHOD(TestDotInDirectoryNameDoesNotCountAsExtension)
        {
            IndexerOptions options;
            // The only '.' is in a directory component, not the filename.
            Assert::IsFalse(BackgroundIndexer::ShouldIndexContent(L"C:\\repo.v2\\Makefile", options));
        }

        TEST_METHOD(TestTrailingDotIsNotAnExtension)
        {
            IndexerOptions options;
            Assert::IsFalse(BackgroundIndexer::ShouldIndexContent(L"C:\\src\\weirdname.", options));
        }

        TEST_METHOD(TestConfigurableExtensionListIsRespected)
        {
            IndexerOptions options;
            options.contentExtensions = { L".foo" };
            Assert::IsTrue(BackgroundIndexer::ShouldIndexContent(L"C:\\data\\thing.foo", options));
            Assert::IsFalse(BackgroundIndexer::ShouldIndexContent(L"C:\\data\\thing.txt", options));
        }
    };

    TEST_CLASS(LooksBinaryTests)
    {
    public:
        TEST_METHOD(TestTextBufferIsNotBinary)
        {
            const char text[] = "Hello, world! This is plain text.";
            Assert::IsFalse(BackgroundIndexer::LooksBinary(text, sizeof(text) - 1));
        }

        TEST_METHOD(TestNulByteMarksBufferBinary)
        {
            const char data[] = { 'A', 'B', '\0', 'C' };
            Assert::IsTrue(BackgroundIndexer::LooksBinary(data, sizeof(data)));
        }

        TEST_METHOD(TestEmptyBufferIsNotBinary)
        {
            Assert::IsFalse(BackgroundIndexer::LooksBinary(nullptr, 0));
        }
    };

    TEST_CLASS(FileTimeToInt64Tests)
    {
    public:
        TEST_METHOD(TestRoundTripsHighAndLowParts)
        {
            FILETIME ft{};
            ft.dwLowDateTime = 0x1234'5678;
            ft.dwHighDateTime = 0x0000'0001;

            int64_t value = BackgroundIndexer::FileTimeToInt64(ft);
            Assert::AreEqual(static_cast<int64_t>(0x1'1234'5678), value);
        }

        TEST_METHOD(TestZeroFileTimeIsZero)
        {
            FILETIME ft{};
            Assert::AreEqual(static_cast<int64_t>(0), BackgroundIndexer::FileTimeToInt64(ft));
        }

        TEST_METHOD(TestOrderingIsPreserved)
        {
            FILETIME earlier{};
            earlier.dwLowDateTime = 100;

            FILETIME later{};
            later.dwLowDateTime = 200;

            Assert::IsTrue(BackgroundIndexer::FileTimeToInt64(earlier) < BackgroundIndexer::FileTimeToInt64(later));
        }
    };
}
