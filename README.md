# fcitx5-oratio

一个 fcitx5 输入法的插件，可以接受自定义配置的命令，并绑定快捷键执行该命令，将命令的输出作为候选词显示。

一种用法是与语音识别服务配合使用, 以达到语音输入的目的, 比如搭配 https://github.com/StrayDragon/oratio 使用

## 使用方法

1. 安装 fcitx5 并配置
2. 安装 fcitx5-oratio
- 克隆本仓库并切换目录到项目根目录, 执行:
```bash
just build
just install
```
3. 配置插件: 重启 `fcitx5` (使用 `$ fcitx5 -rd` 或者托盘右键点击重启), 检查 `fcitx5-configtool` 中 `Addons` Tab 的 `Module` 模块里是否有 oratio 插件, 点击右边的 Configure 即可配置
4. 使用方式: 在任意可调出输入法的文本框中, 使用 `Ctrl+Shift+Alt+E` (默认配置) 呼出命令栏, 点击 Enter 执行, 等待候选词然后 Enter 可以上屏候选词

