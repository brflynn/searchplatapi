#pragma once

#include "FileSearch.g.h"
#include "QueryResult.h"
#include "../api/SearchPlatAPI.h"

namespace winrt::Microsoft::SearchPlatform::implementation
{
    struct FileSearch : FileSearchT<FileSearch>
    {
        FileSearch();
        ~FileSearch();

        winrt::Windows::Foundation::Collections::IVector<winrt::Microsoft::SearchPlatform::QueryResult> Search(hstring const& query);

    private:
        std::unique_ptr<searchapi::FileSearchProvider> m_searchProvider;
    };
}

namespace winrt::Microsoft::SearchPlatform::factory_implementation
{
     BASIC_FACTORY(FileSearch);
}