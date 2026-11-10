# Original defaults / 原生默认基准

Game archives are not shipped. First-launch setup accepts a known unmodified scripts.pkg/script.pkg, or obtains it from a selected game's res/packages. It validates item definitions and copies the package to **script.pkg beside the executable**. defaults.yaml records length and SHA-256. Absence triggers setup; integrity mismatch requires a new valid baseline. These checks detect incompleteness/corruption, not the source's provenance.

分发包不携带游戏档案。首次使用必须提供原生包，或选择游戏目录取得副本。运行时基准固定为程序旁的 script.pkg，完整性记录为 defaults.yaml；不存在副本则进入初始化。

A development-only scripts.pkg and legacy manifest.yaml may exist here as read-only integration inputs. CMake never copies them automatically; CPack excludes the archive. A clean source checkout builds and runs fixture tests without game resources. Optional installation tests use the supplied game archive if this local input is absent.

此目录可保留开发者只读原生包及旧 manifest；它们不会成为自动分发的运行时基准。可选真实安装验证在没有开发副本时从指定游戏目录读取原生包。

Selected-vehicle reset reads compatible numeric/boolean defaults, retaining the active format, structure, references and mechanism. Mod-only/incompatible fields use pre-editor values. XML reset restores the whole pre-editor document. Global reset restores physical preimages/removes editor-created overrides. None replaces the current scripts archive wholesale.
