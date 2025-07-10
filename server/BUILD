filegroup(
    name = "server_files",
    srcs = glob(["files/**"]),
    visibility = ["//visibility:private"],
)

cc_binary(
    name = "server",
    srcs = ["server.cc"],
    deps = [
        "//utils:utils", 
        "//protocol:protocol", 
        "//protocol:serialization"
    ],
    data = [":server_files"]
)