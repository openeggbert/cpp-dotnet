# SPDX-License-Identifier: MIT
# Copyright (c) Robert Vokac and contributors

sharp_runtime_register_module(
    NAME ServiceModel
    TARGET sharp_runtime_service_model
    TYPE STATIC
    PUBLIC_DEPENDENCIES Core.Base Net.Http Uri Xml
    TEST_DEPENDENCIES Net.Sockets
)
