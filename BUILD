load("@com_grail_bazel_compdb//:defs.bzl", "compilation_database")
load("@com_grail_bazel_output_base_util//:defs.bzl", "OUTPUT_BASE")

# Target to generate a compile_commands.json compilation database file
compilation_database(
    name = "compdb",
    output_base = OUTPUT_BASE,
    targets = [
        "//:typesense-server",
        "//:search",
        "//:benchmark",
    ],
)

# Translation units that implement the onnxruntime/opencv/whisper-backed AI
# classes. They are compiled only when AI is enabled; when --define=ai=off they
# are dropped from the build and the mock headers provide the class definitions.
AI_ONLY_SRCS = [
    "src/text_embedder.cpp",
    "src/text_embedder_tokenizer.cpp",
    "src/image_embedder.cpp",
    "src/image_processor.cpp",
    "src/personalization_model.cpp",
    "src/aq_model.cpp",
]

filegroup(
    name = "src_files",
    srcs = glob(["src/*.cpp"], exclude = AI_ONLY_SRCS) + select({
        ":disable_ai": [],
        "//conditions:default": AI_ONLY_SRCS,
    }),
)

cc_library(
    name = "headers",
    hdrs = glob([
        "include/**/*.h",
        "include/**/*.hpp",
    ]),
    includes = ["include"],
)

config_setting(
    name = "with_cuda",
    define_values = { "use_cuda": "on" }
)

# Build without AI features (onnxruntime, opencv, whisper, sentencepiece, clip).
# Enable with: bazel build --define=ai=off //:typesense-server
config_setting(
    name = "disable_ai",
    define_values = { "ai": "off" }
)

# Dependencies that exist solely to power AI features. These pull in onnxruntime
# (which statically links opencv), whisper.cpp, sentencepiece and the CLIP
# tokenizer. They are dropped entirely when --define=ai=off.
AI_DEPS = [
    "@onnx_runtime//:onnxruntime_lib",
    "@sentencepiece",
    "@sentencepiece//:sentencepiece_headers",
    "@clip_tokenizer//:clip",
    "@whisper.cpp//:whisper",
    "@whisper.cpp//:whisper_headers",
]

cc_library(
    name = "common_deps",
    defines = [
        "NDEBUG",
    ] + select({
        ":disable_ai": [],
        "//conditions:default": ["TYPESENSE_ENABLE_AI"],
    }),
    linkopts = select({
        "@platforms//os:macos": ["-framework Foundation -framework SystemConfiguration"],
        "//conditions:default": [],
    }),
    deps = [
        ":headers",
        "@com_github_brpc_braft//:braft",
        "@com_github_brpc_brpc//:brpc",
        "@com_github_google_glog//:glog",
        "@curl",
        "@for",
        "@h2o",
        "@iconv",
        "@icu",
        "@jemalloc",
        "@kakasi",
        "@lrucache",
        "@rocksdb",
        "@s2geometry",
        "@hnsw",
        "@snowball",
        "@snowball//:snowball_headers",
        "@archive",
        # "@zip",
    ] + select({
        ":disable_ai": [],
        "//conditions:default": AI_DEPS,
    }))

cc_library(
    name = "linux_deps",
    defines = [
        "NDEBUG",
    ],
    deps = [
        "@elfutils//:libdw",
    ],
)

COPTS = [
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-Werror=return-type",
    "-O2",
    "-g",
    # WORKAROUND for a GCC 10.5.0 aarch64 -O2 code-generation bug (RTL global CSE).
    # In nlohmann::json's serializer::dump_integer, GCSE commons the address of the
    # static `digits_to_99` table across the digit-pair loop's exit path and the
    # standalone two-digit path, but the merged code reaches the shared two-digit
    # store with the table base register holding a stale (GOT-page) value on the
    # loop-exit path. As a result every positive integer with an even number of
    # digits >= 4 is serialized with its leading two bytes read from garbage memory
    # (e.g. 5000 -> "\xAA\xAA00", 1782855450 -> "p\xD082855450"). This silently
    # writes unparsable documents to the on-disk store ("Error while parsing stored
    # document." on read-back) and mangles integer fields (counts, timestamps, ids,
    # prices) in HTTP responses. amd64 is unaffected; the nlohmann source is correct.
    # Disabling RTL GCSE removes the bad commoning; tree-SSA PRE still runs, so the
    # performance impact is negligible. Durable fix: build with a newer GCC and drop
    # this flag. See typesense/search-sidecar/validation.md.
    "-fno-gcse",
]

ASAN_COPTS = [
    "-fsanitize=address",
    "-fno-omit-frame-pointer",
    "-DASAN_BUILD"
]

cc_binary(
    name = "typesense-server",
    srcs = [
        "src/main/typesense_server.cpp",
        ":src_files",
    ],
    local_defines = [
        "TYPESENSE_VERSION=\\\"$(TYPESENSE_VERSION)\\\""
    ],
    linkopts = select({
        "@platforms//os:linux": ["-static-libstdc++", "-static-libgcc", "-fuse-ld=lld"],
        "@platforms//os:macos": ["-framework Foundation", "-framework Accelerate", "-framework Metal", "-framework MetalKit"],
        "//conditions:default": [],
    }),
    copts = COPTS + select({
        "@platforms//os:linux": ["-DBACKWARD_HAS_DW=1", "-DBACKWARD_HAS_UNWIND=1"],
        "//conditions:default": [],
    }),
    deps = [":common_deps"] +  select({
        "@platforms//os:linux": [":linux_deps"],
        "//conditions:default": [],
    }),
)

cc_binary(
    name = "search",
    srcs = [
        "src/main/main.cpp",
        ":src_files",
    ],
    copts = COPTS,
    deps = [":common_deps"],
)

cc_binary(
    name = "benchmark",
    srcs = [
        "src/main/benchmark.cpp",
        ":src_files",
    ],
    copts = COPTS,
    deps = [":common_deps"],
)

filegroup(
    name = "test_src_files",
    srcs = glob(["test/*.cpp"]),
)

filegroup(
    name = "test_data_files",
    srcs = glob([
        "test/**/*.txt",
        "test/**/*.ini",
        "test/**/*.jsonl",
        "test/**/*.gz",
    ]),
)

TEST_COPTS = [
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-Werror=return-type",
    "-g",
    "-DTEST_BUILD"
]

ASAN_COPTS = [
    "-fsanitize=address",
    "-fno-omit-frame-pointer",
    "-DASAN_BUILD"
]

config_setting(
    name = "release_mode",
    define_values = { "mode": "release" }
)

config_setting(
    name = "asan_mode",
    define_values = { "mode": "asan" }
)

cc_test(
    name = "typesense-test",
    srcs = [
        ":src_files",
        ":test_src_files",
    ],
    copts = TEST_COPTS + select({
        ":release_mode": ["-O2"],
        ":asan_mode": ["-O0"] + ASAN_COPTS,
        "//conditions:default": ["-O0"]
    }),
    data = [
        ":test_data_files",
        "@libart//:data",
        "@token_offsets//file",
    ],
    deps = [
        ":common_deps",
        "@com_google_googletest//:gtest",
    ],
    defines = [
        "ROOT_DIR="
    ],
    linkopts = select({
       ":asan_mode": ["-fsanitize=address", "-fuse-ld=lld"],
       "//conditions:default": []
    }) +  select({
       "@platforms//os:linux": ["-fuse-ld=lld"],
       "//conditions:default": [],
   })
)
