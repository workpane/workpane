#include "support/WindowsDesktop.h"

#include <gtest/gtest.h>

int main(int argc, char** argv) {
    int result = 1;

    {
        workpane::tests::WindowsDesktop desktop;
        result = desktop.run(argc, argv);
    }

    return result == 0 && ::testing::Test::HasFailure() ? 1 : result;
}
