# Proto References

本目录保存旧 proto 生成命令备忘。

当前事实：

```text
proto/caster  是源 proto。
proto/src     是已提交 C++ 生成物。
CMake 当前直接消费 proto/src，不在构建时自动生成。
```

修改 proto 时必须同步 `proto/src`、HTTP JSON、`web/src/api/types.ts`、`api/api-reference.md` 和 `api/api-contract-sync.md`，并运行：

```powershell
node tools\contract_check\check_api_contracts.mjs
```
