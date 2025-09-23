#include "pch.h"
#include "FileSearch.h"
#include "QueryResult.h"
#include "FileSearch.g.cpp"

using namespace winrt::Windows;

namespace winrt::Microsoft::SearchPlatform::implementation
{
    // File search provider
    FileSearch::FileSearch()
    {
    }

    FileSearch::~FileSearch()
    {
        m_searchProvider.reset();
    }

    winrt::Windows::Foundation::Collections::IVector<winrt::Microsoft::SearchPlatform::QueryResult> FileSearch::Search(hstring const& query)
    {
        // Initialize the search platform API
        m_searchProvider = std::make_unique<searchapi::FileSearchProvider>();
        m_searchProvider->PrepareForSearch({}, {}); // No included or excluded scopes for now

        std::vector<searchapi::QueryResult> results = m_searchProvider->Search(query.c_str());

        auto resultVector = winrt::single_threaded_vector<winrt::Microsoft::SearchPlatform::QueryResult>();
        for (const auto& result : results)
        {
            const auto& qr = winrt::make_self<winrt::Microsoft::SearchPlatform::implementation::QueryResult>();
            qr->_propSet = result.propSet;
            qr->_uri = result.uri;
            const winrt::Microsoft::SearchPlatform::QueryResult& r = *qr;
            resultVector.Append(r);
        }
        return resultVector;
    }
}