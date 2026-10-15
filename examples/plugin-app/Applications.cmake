workpane_add_application(ExampleStudio NAME "Example studio" VERSION 1.2.0 BUILD 7 ORGANIZATION "Example" IDENTIFIER "org.example.studio" PLUGINS "${CMAKE_CURRENT_LIST_DIR}/plugins")
workpane_add_application(PlainStudio NAME "Plain studio" VERSION 3.1.0 IDENTIFIER "org.example.plain")

workpane_install_application(ExampleStudio)
workpane_install_application(PlainStudio)
