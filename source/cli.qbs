import qbs

CppApplication {
    name: "kmx-sat"
    Depends { name: "cpp" }
    Depends { name: "kmx-sat-lib" }
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: true
    cpp.enableExceptions: true
    files: [
        "cli/kmx-sat-main.cpp",
    ]
    Group {
        fileTagsFilter: product.type
        qbs.install: true
        qbs.installDir: "bin"
    }
}
