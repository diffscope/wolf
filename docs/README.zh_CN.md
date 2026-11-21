# WOLF：可编排的语言学框架

[English](../README.md)

[synthrt](https://github.com/diffscope/synthrt) 的语言学层，为 synthrt 加载的包增加 `linguist` 贡献类别。

synthrt 认识歌手与推理，不认识 linguist 贡献，也不需要认识：贡献类别是注册进来的，不是内置的，所以链接进程序的库可以添加自己的类别。wolf 就是这样一个库。包声明 `linguist` 贡献的方式与声明其他任何贡献相同，引用它也使用同一套语法。

这里没有程序。它是供编辑器或工具与 synthrt 一起嵌入的库。

## Linguist 贡献

包在 `desc.json` 的 `contributions.linguist` 数组中列出 linguist。每个条目形如 `{ "id": ..., "path": ... }`，指向一份 linguist 清单。条目的形式是所有类别通用的（spec 2.4 §功能贡献），本类别的规定见 [linguist-domain-contract.md](linguist-domain-contract.md) §1.1，这里不再重复。linguist 清单形如：

```json
{
  "name": "普通话",
  "interface": "org.openvpi.wolf.linguist.WolfLinguist",
  "level": 1,
  "variant": "wolf",
  "language": "cmn",
  "scheme": "pinyin",
  "exports": {
    "phonemes": [ "a", "o", "e", "i", "u", "v" ]
  },
  "configuration": {},
  "imports": [
    { "role": "linguist/g2p", "ref": ":inference/g2p" },
    { "role": "linguist/s2p", "ref": ":inference/s2p" }
  ]
}
```

`(interface, level, variant)` 三元组经由 synthrt 的解释器发现选出一个 provider。provider 为该契约解释 `exports`、`configuration`、import options、执行工厂、校验器与扩展。

`language` 与 `scheme` 是 `linguist` 类别加给每份声明的字段，在选出 provider 之前就会读取。`exports` 是 linguist 对自身的声明，`configuration` 是 provider 专用的资源。相对路径以声明文件所在目录为基准。类别、它的身份字段以及歌手侧的映射规定在 [linguist-domain-contract.md](linguist-domain-contract.md) 中。

引用 linguist 与引用其他贡献一样：

```
vendor/sample:linguist/mandarin        # 已求解的直接依赖中的模块
:linguist/mandarin                     # 发起引用的包中的模块
```

引用不带版本：它绑定的 `vendor/sample` 的版本来自发起引用的包在 `dependencies` 中为该包写的条目（spec 2.4 §结构标识符与 ModuleReference 文法），引用发起引用的包自身的模块时使用前导 `:` 的形式（spec 2.4 §结构标识符与 ModuleReference 文法）。

## 提供 linguist

实现 `wolf::LinguistProvider`，并经由 `wolf::LinguistProviderPlugin` 暴露出来。provider 负责一切与契约相关的行为，包括 import options、执行工厂、校验器与 spec 扩展：

```cpp
class MyLinguistProviderPlugin : public wolf::LinguistProviderPlugin {
public:
    srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
        create(const srt::ContribContractTriplet &triplet) override {
        // 校验请求的契约，并返回对应的 provider。
    }
};

STDC_EXPORT_PLUGIN(MyLinguistProviderPlugin)
```

随附的实现位于 `src/plugins/linguistproviders/wolflinguist`。其中的 `WolfLinguistProvider` 支持 `org.openvpi.wolf.linguist.WolfLinguist` Level 1、variant `wolf`。wolf 库注册类别并定义 provider 抽象，但它自己不创建 Wolf Level 1 的运行时对象。

```cpp
auto spec = package->contribution("linguist", "mandarin")
                 ->as<wolf::LinguistSpec>();
```

## 为什么 wolf 必须链接而不能加载

`SynthUnit` 在构造时读取一次已注册的类别列表。wolf 在 `main` 之前注册 `linguist`，所以之后构造的每个 unit 都有这个类别。插件做不到这一点：插件是*经由* unit 惰性加载的，等到插件运行它的静态初始化时，那个 unit 早已建好了类别。提供类别只能由链接进来的库完成。

只是链接还不够。会丢弃未被引用的库的链接器，例如 `--as-needed` 下的 ELF 链接器，以及对每个导入库都这样做的 MSVC 链接器，会把 wolf 从一个只用到类别、却不引用任何 wolf 符号的宿主中丢掉。这样的宿主在构造 unit 之前调用一次 `wolf::linkLinguistCategory()`。这个函数体是空的，对它的引用让库留在链接命令里。

## 版本与 ABI

当前版本为 0.2.0.0。**1.0 之前不作任何 ABI 保证。** 宿主用到的大多数类型都是公开头文件中的值类型，即 `LanguageStatus`、`LanguageEntry`、`SingerEntry`、`SingerRef` 以及 `Api/` 下的所有载荷。新增字段会改变这类类型的大小，新增虚函数会改变 `WolfPipelineExtension` 这类契约接口的虚表。混用以两个版本的头文件编译出的对象属于未定义行为，而不是链接错误，因此不会有任何诊断。

公开头文件是 `Api/`、`Linguist/`、`Session/` 与 `Support/` 下的头文件，加上根头 `wolf/wolf_global.h`。`wolf/Support` 是必须每进程唯一的两个组件，资源缓存 `wolf::ResourceCache` 与 wolf 的日志类别 `wolf::logCategory()`，由 libwolf 导出，宿主与基于 wolf 构建的 provider 都可以使用。本仓库插件共用的无状态辅助代码以隐藏可见性编入 `src/util` 下的静态库：`linguistutil` 是清单与契约值的读取、校验器与输入规则，`textutil` 是文本资源的读取与 UTF-8，`executiveutil` 是执行体基类，只有头文件。libwolf 与每个插件私有链接自己用到的库，这些库不安装，没有任何二进制导出它们的符号。在本仓库之外构建的 provider 只依赖已安装的头文件与 synthrt。

**wolf 的版本一变，宿主就必须重新构建。** 每次破坏 ABI 的改动都在同一个提交里抬升版本，所以版本号变了就表示需要重新构建。安装的 CMake 包以 `AnyNewerVersion` 兼容性写出，所以带版本的 `find_package(synthrt-wolf)` 也接受更新的版本，不会发现需要重新构建。`wolf::LinguistSession` 与 `wolf::CancelToken` 是两个以 pimpl 持有实现的公开类型，所以它们的布局在各版本间稳定。

## 构建要求

CMake 3.19 或更高版本，以及 Git，用于检出 vcpkg 及其 overlay。

包括 synthrt 在内的所有依赖都经由 vcpkg 按 `scripts/vcpkg-manifest` 中的清单安装。端口依次从两个 overlay 解析：先是本仓库的 `scripts/vcpkg-ports`，其中有 `synthrt-main` 与 `wolf-lang-packages` 两个端口，然后是 `scripts/vcpkg-sources.json` 指定的共享 overlay。

+ [synthrt](https://github.com/diffscope/synthrt)，经由本仓库的 `synthrt-main` 端口。该端口钉在 synthrt 分支 `onnxruntime-builds-uptake` 的一个提交上，这个分支基于 synthrt 的主线。共享 overlay 中的 `synthrt` 端口钉的是 refactor 线，这里不用。
+ [stdcorelib](https://github.com/SineStriker/stdcorelib)
+ [qmsetup](https://github.com/stdware/qmsetup)
+ [BLAKE3](https://github.com/BLAKE3-team/BLAKE3)，用于资源缓存的内容哈希。
+ [RE2](https://github.com/google/re2)，用于词的分类模式。随附的语言包使用 `\p{Han}` 这类 Unicode 属性类，`std::regex` 不支持。
+ [cpp-pinyin](https://github.com/wolfgitpr/cpp-pinyin)，普通话与粤语的 G2P 引擎。

只有构建测试时才需要 Boost.Test。

`multig2p-onnx`，共享的模型后端，需要 dsinfer 及其 ONNX Runtime 驱动。dsinfer 是硬依赖：wolf 为模型后端和 DiffSinger singer exports 链接它。`onnx` feature（`--x-feature=onnx`）加入 `synthrt-main[onnx]`，ONNX Runtime 本身来自共享 overlay 的 `onnxruntime-builds` 端口（Windows 上为 DirectML，其他平台为 CPU，没有 CUDA）。

`lua`，脚本化的 S2P 与 Onset 变体，需要 LuaJIT，清单把它列为普通依赖。`-DWOLF_DISABLE_LUAJIT=ON` 即使 LuaJIT 已安装也会排除它。测试桩总是声明一个 `stub-miscount` 变体，供故意违反 provider 契约的夹具使用。

## 准备环境

### VCPKG 包

依赖由 [vcpkg-workspace.cmake](https://github.com/stdware/dev-scripts/blob/main/docs/about-scripts/vcpkg-workspace.md) 安装，该脚本下载到 `scripts/` 中。它把 `scripts/vcpkg-sources.json` 指定的 vcpkg 与 overlay 检出到 `deps/`，并把依赖安装在那里。在仓库根目录执行下列命令：

```sh
curl -fsSL -o scripts/vcpkg-workspace.cmake https://raw.githubusercontent.com/stdware/dev-scripts/main/scripts/vcpkg-workspace.cmake
cmake -P scripts/vcpkg-workspace.cmake install -- --x-feature=tests
```

在 Windows 上，可以在 PowerShell 或 cmd 中这样下载脚本：

```bat
powershell -Command "Invoke-WebRequest -OutFile scripts/vcpkg-workspace.cmake https://raw.githubusercontent.com/stdware/dev-scripts/main/scripts/vcpkg-workspace.cmake"
```

`cmake -P scripts/vcpkg-workspace.cmake help` 列出其余命令，`--dry-run` 只打印命令而不执行。`scripts/vcpkg-sources.local.json` 优先于 `scripts/vcpkg-sources.json`，例如可以用它指向本地的 overlay。

清单定义了三个可选 feature，各自写在 `--` 之后，形如 `--x-feature=<名称>`：

| Feature | 作用 |
| :-- | :-- |
| `onnx` | 经由 `synthrt-main[onnx]` 构建 dsinfer 及其 ONNX Runtime 驱动，从而启用 `multig2p-onnx` 变体。 |
| `lang-packages` | 经由 `wolf-lang-packages` 端口安装已发布语言包的默认套件（`cmn`、`jpn`、`yue`、`zxx` 及它们需要的后端），供测试使用。 |
| `tests` | 加入 Boost.Test。 |

> overlay 必须与 synthrt 使用的是同一个修订，两个仓库都跟随共享 overlay 的 `main` 分支来保证这一点。`stdcorelib-plugin` 有两个修订的版本字符串同为 `0.1.0.0#1`，却拉取不同的上游 ref，CMake 辅助函数的名字也不同（`stdc_add_plugin_metadata` 与 `stdc_add_plugin_manifest`），所以与 synthrt 用的修订不同时会在配置阶段失败。

### 构建

```sh
cmake -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=deps/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_INSTALLED_DIR=deps/vcpkg/installed \
    -DVCPKG_MANIFEST_MODE=OFF \
    -DCMAKE_INSTALL_PREFIX=<dir> \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build --target all
cmake --build build --target install
```

### 测试

`-DWOLF_BUILD_TESTS=ON` 构建测试并注册到 CTest，`ctest --test-dir build` 运行它们。`src/tests/auto/plugins` 里是单个插件的单元测试。不带 LuaJIT 的构建不含 `test_LuaInterpreters`，不带 dsinfer 的构建不含 `test_MultiG2pInterpreter`、`test_MultiG2pMaxLen` 与 `test_MultiG2pBundle`（测试清单唯一的依据是注册条件，见 `src/tests/auto/CMakeLists.txt` 与 `src/tests/auto/plugins/CMakeLists.txt`；二进制与用例的数量只在一处统计，即 [Status.md](Status.md) 的验证表）。

加载真实语言包的测试从缓存变量 `WOLF_LANG_PACKAGES_SOURCE` 指定的位置读取，它指向一个放着解包后语言包的目录。该变量为空时，构建使用 `wolf-lang-packages` 端口安装的目录树（`lang-packages` feature）。当前发布版的套件版本以及每个归档的版本与文件名列在 [linguist-distribution.md](linguist-distribution.md) §5.1（那是权威表，为免漂移不在这里复述）。

七个自动测试读取的夹具包由 `scripts/make-test-fixtures.py` 生成，仓库只带生成器而不带包。把缓存变量 `WOLF_TEST_FIXTURES_SOURCE` 指向该脚本生成的目录：

```sh
python3 scripts/make-test-fixtures.py --output build/test-fixtures
cmake -B build -DWOLF_TEST_FIXTURES_SOURCE=build/test-fixtures ...
```

`--check` 把一个已有目录与脚本对比，报告每一处差异。这些包在不再入库之前，就是这样逐字节核对过的。

宿主流程测试还要读取 `scripts/make-voicebank-fixture.py` 生成的声库夹具，来源是缓存变量 `WOLF_VOICEBANK_FIXTURE_SOURCE`。缺少数据的测试以状态 77 退出，CTest 把它报告为跳过。

CI（`.github/workflows/ci.yml`）在 Linux 与 Windows 上以 `onnx` 与 `tests` feature 构建，在 Linux 上再加 `lang-packages`，以检验端口钉住的发布版。它运行声明检查的自测，以及对 `build/test-fixtures/wolf-lang-zxx`（上文 `scripts/make-test-fixtures.py` 生成的目录）的检查，运行 CTest，然后安装 wolf，针对安装好的包构建并运行 `.github/consumer`。consumer 检查 `synthrt::wolf` 目标与安装包中没有 Support 头文件，程序本身检查包中导入插件目标所在的类别目录。

### 安装布局

包安装 `include/` 下的头文件、库以及 CMake 包配置。插件安装在安装前缀下的 `lib/synthrt-wolf/plugins/<category>/<name>/`，其中 `<category>` 为 `inferenceinterpreters` 或 `linguistproviders`，构建树采用相同布局。宿主把 `<前缀>/lib/synthrt-wolf/plugins/linguistproviders` 与 `<前缀>/lib/synthrt-wolf/plugins/inferenceinterpreters` 传给 `SynthUnit::setPluginPaths`，与其他提供同类解释器的库的目录列在一起。CMake 包把每个安装的插件导出为导入目标，例如 `synthrt::wolfchain`，CMake 工程用 `$<TARGET_FILE_DIR:synthrt::wolfchain>` 取得插件所在目录，vcpkg 安装的 Debug 树也由它解析。

## 使用方法

```cmake
cmake_minimum_required(VERSION 3.19)

project(example)

find_package(synthrt-wolf CONFIG REQUIRED)
add_executable(example main.cpp)
target_link_libraries(example synthrt::wolf)
```

注册类别的正是对 wolf 的链接，所以即使一个程序只想让 linguist 贡献可用，也要直接链接 wolf，而不能指望通过传递依赖被带进来。
