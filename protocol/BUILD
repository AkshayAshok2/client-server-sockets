cc_library(
    name = "protocol",
    hdrs = ["protocol.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "serialization",
    srcs = ["serialization.cc"],
    hdrs = ["serialization.h"],
    deps = [":protocol", "//utils:utils"],
    visibility = ["//visibility:public"],
)
