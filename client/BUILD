filegroup(
    name = "client_files",
    srcs = glob(["files/**"]),
    visibility = ["//visibility:private"],
)

cc_binary(
    name = "client",
    srcs = ["client.cc"],
    deps = [
        "//utils:sha256", 
        "//utils:utils", 
        "//protocol:protocol", 
        "//protocol:serialization"
    ],
    data = [":client_files"],
)