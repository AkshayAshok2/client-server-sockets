cc_library(
    name = "sha256",
    srcs = ["sha256.cc"],
    hdrs = ["sha256.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "utils",
    srcs = ["utils.cc"],
    hdrs = ["utils.h"],
    deps = [":sha256", "//protocol:protocol"],
    visibility = ["//visibility:public"],
)