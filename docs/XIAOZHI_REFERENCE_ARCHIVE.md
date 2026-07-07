# XiaoZhi Reference Archive

This project keeps local-only upstream XiaoZhi reference clones for behavior
matching. They are intentionally ignored by git and must not be uploaded to this
project repository.

## Local Paths

- `.local-references/xiaozhi/DOIT_AI`
- `.local-references/xiaozhi/xiaozhi-esp32-server`

The scratch copies under `tmp/` are also ignored.

## Sources

```text
DOIT_AI
  remote: https://github.com/SmartArduino/DOIT_AI.git
  branch: main
  commit: cc183ecdbf44fa315a666e73e990024ea45ac5cb
  date:   2026-03-24T16:59:14+08:00
  title:  Add command instructions

xiaozhi-esp32-server
  remote: https://github.com/xinnan-tech/xiaozhi-esp32-server.git
  branch: main
  commit: 8808b69303c01135378d35750a9b6ddb0fbba183
  date:   2026-07-07T16:25:12+08:00
  title:  Merge pull request #3265 from xinnan-tech/ha-muisc-timeout
```

## Rule

Use these clones only as local reference material for state-machine, audio, VAD,
and protocol behavior. Do not vendor their source into this repository unless we
make a deliberate branch for third-party reference code and review license/size
impact first.
