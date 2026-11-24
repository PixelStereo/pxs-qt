# OSCQlock: a device to test controllers on (see main.cpp). Build it with -DPXS_BUILD_EXAMPLES=ON.
add_executable(oscqlock main.cpp)
target_link_libraries(oscqlock PRIVATE pxs::oscquery pxs::params Qt6::Widgets)
set_target_properties(oscqlock PROPERTIES OUTPUT_NAME OSCQlock)
