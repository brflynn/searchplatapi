// Copyright (C) Microsoft Corporation. All rights reserved.
#include "pch.h"
#include <SearchTokenizer.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace wsearch;

namespace SearchTokenizerTests
{
    TEST_CLASS(TokenizerBasicTests)
    {
    public:
        TEST_METHOD(SingleWord)
        {
            SearchTokenizer tokenizer(L"hello");
            
            Assert::IsTrue(tokenizer.IsSingleToken());
            Assert::AreEqual(static_cast<size_t>(1), tokenizer.GetTokenCount());
            Assert::AreEqual(L"hello", tokenizer.GetTokens()[0].c_str());
        }

        TEST_METHOD(MultipleWords)
        {
            SearchTokenizer tokenizer(L"hello world");
            
            Assert::IsTrue(tokenizer.HasMultipleTokens());
            Assert::AreEqual(static_cast<size_t>(2), tokenizer.GetTokenCount());
            Assert::AreEqual(L"hello", tokenizer.GetTokens()[0].c_str());
            Assert::AreEqual(L"world", tokenizer.GetTokens()[1].c_str());
        }

        TEST_METHOD(QuotedPhrase)
        {
            SearchTokenizer tokenizer(L"\"annual report\"");
            
            Assert::IsTrue(tokenizer.IsQuoted());
            Assert::IsTrue(tokenizer.IsSingleToken());
            Assert::AreEqual(L"annual report", tokenizer.GetTokens()[0].c_str());
        }

        TEST_METHOD(EmptyString)
        {
            SearchTokenizer tokenizer(L"");
            
            Assert::IsTrue(tokenizer.IsEmpty());
            Assert::AreEqual(static_cast<size_t>(0), tokenizer.GetTokenCount());
        }

        TEST_METHOD(PunctuationFiltering)
        {
            SearchTokenizer tokenizer(L"hello, world!");
            
            Assert::AreEqual(static_cast<size_t>(2), tokenizer.GetTokenCount());
            Assert::AreEqual(L"hello", tokenizer.GetTokens()[0].c_str());
            Assert::AreEqual(L"world", tokenizer.GetTokens()[1].c_str());
        }

        TEST_METHOD(MultipleSpaces)
        {
            SearchTokenizer tokenizer(L"hello   world");
            
            Assert::AreEqual(static_cast<size_t>(2), tokenizer.GetTokenCount());
            Assert::AreEqual(L"hello", tokenizer.GetTokens()[0].c_str());
            Assert::AreEqual(L"world", tokenizer.GetTokens()[1].c_str());
        }
    };
}
