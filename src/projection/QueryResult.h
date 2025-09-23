#pragma once

#include "QueryResult.g.h"

namespace winrt::Microsoft::SearchPlatform::implementation
{
    struct QueryResult : QueryResultT<QueryResult>
    {
        QueryResult() = default;
        winrt::Windows::Foundation::Collections::IPropertySet _propSet{ nullptr };
        winrt::hstring _uri;

        winrt::Windows::Foundation::Collections::IPropertySet PropSet() const { return _propSet; }
        winrt::hstring Uri() const { return _uri; }
    };
}

namespace winrt::Microsoft::SearchPlatform::factory_implementation
{
    // BASIC_FACTORY(QueryResult);
}