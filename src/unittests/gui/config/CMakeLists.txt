#SPDX - FileCopyrightText : (C)2025 Deskflow Developers
#SPDX - License - Identifier : MIT

create_test(NAME ComputerTests DEPENDS gui SOURCE ComputerTests.cpp WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui")

    create_test(
        NAME LayoutClientsTests DEPENDS gui SOURCE LayoutClientsTests.cpp WORKING_DIRECTORY
        "${CMAKE_BINARY_DIR}/src/lib/gui"
    )

#ServerConfigDialog.h includes siblings from the gui directory.
        target_include_directories(LayoutClientsTests PRIVATE "${CMAKE_SOURCE_DIR}/src/lib/gui")
