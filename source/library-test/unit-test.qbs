import qbs

CppApplication {
    Depends
    {
        name: 'kmx-sat-lib'
    }

    name: "kmx-sat-test"
    consoleApplication: true

    files: [
    ]
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "inc_dep"
    ]
    cpp.systemIncludePaths: [
        "/usr/local/include"
    ]
    cpp.staticLibraries: [
        "libCatch2Main",
        "libCatch2",
        "/usr/lib/libCatch2Main.a",
        "/usr/lib/libCatch2.a"
    ]

    Properties {
        condition: qbs.buildVariant === "debug"
        cpp.debugInformation: true
        cpp.optimization: "none"
    }

    Properties {
        condition: qbs.buildVariant === "release"
        cpp.debugInformation: false
        cpp.optimization: "fast"
        cpp.defines: ["NDEBUG"]
        cpp.commonCompilerFlags: ["-O3", "-march=native", "-flto=auto"]
        cpp.linkerFlags: ["-flto=auto"]
    }
}
