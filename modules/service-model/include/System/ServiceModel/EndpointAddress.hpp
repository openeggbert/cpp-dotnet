// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
// Portions based on .NET runtime API (MIT License, Copyright .NET Foundation and Contributors)
#pragma once

#include <string>
#include <utility>

#include "System/Uri.hpp"

namespace System::ServiceModel {

    /**
     * @brief Where a service endpoint is.
     */
    class EndpointAddress {
    public:
        /**
         * @brief Constructs an address from a URI.
         *
         * @param uri The endpoint's URI.
         */
        explicit EndpointAddress(System::Uri uri) : uri_(std::move(uri)) {}

        /**
         * @brief Constructs an address from a URI string.
         *
         * @param uri The endpoint's URI.
         */
        explicit EndpointAddress(const std::string& uri) : uri_(uri) {}

        /**
         * @brief The endpoint's URI.
         *
         * @return The URI.
         */
        [[nodiscard]] const System::Uri& getUriProperty() const { return uri_; }

    private:
        System::Uri uri_;
    };

} // namespace System::ServiceModel
